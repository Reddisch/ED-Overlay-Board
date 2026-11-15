#include "ShipTracker.h"

#include "Lang.h"
#include "Log.h"
#include "Util.h"

#include <nlohmann/json.hpp>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace {

constexpr uint64_t kPollMs = 1000;
constexpr int kDirScanEvery = 5;  // look for a newer journal file every 5th poll

// display names the journal told us ("Ship_Localised"), for ships missing in the table below
std::map<std::string, std::string>& LearnedNames() {
  static std::map<std::string, std::string> names;
  return names;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool ReadAll(const std::filesystem::path& p, uint64_t offset, std::string& out, uint64_t& size) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  GetFileSizeEx(h, &sz);
  size = static_cast<uint64_t>(sz.QuadPart);
  out.clear();
  if (size > offset) {
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(offset);
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    out.resize(static_cast<size_t>(size - offset));
    DWORD got = 0;
    if (!ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr)) got = 0;
    out.resize(got);
    size = offset + got;
  }
  CloseHandle(h);
  return true;
}

}  // namespace

ShipTracker::ShipTracker() {
  PWSTR saved = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &saved)) && saved)
    dir_ = std::filesystem::path(saved) / L"Frontier Developments" / L"Elite Dangerous";
  CoTaskMemFree(saved);
  wchar_t over[MAX_PATH]{};  // for testing: EDOB_JOURNAL_DIR=<folder with Journal.*.log / Status.json>
  if (GetEnvironmentVariableW(L"EDOB_JOURNAL_DIR", over, MAX_PATH) > 0) dir_ = over;
  std::error_code ec;
  if (dir_.empty() || !std::filesystem::is_directory(dir_, ec))
    Log("Ship tracking: Elite Dangerous journal folder not found ({})", WideToUtf8(dir_.wstring()));
}

ShipTracker::~ShipTracker() = default;

const std::string& ShipTracker::Poll() {
  const uint64_t now = GetTickCount64();
  if (dir_.empty() || (lastCheck_ != 0 && now - lastCheck_ < kPollMs)) return current_;
  lastCheck_ = now;
  ReadJournal();
  ReadStatus();
  Update();
  return current_;
}

void ShipTracker::ReadJournal() {
  if (journal_.empty() || ++scanCounter_ >= kDirScanEvery) {
    scanCounter_ = 0;
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
      const auto name = it->path().filename().wstring();
      if (name.rfind(L"Journal.", 0) != 0 || it->path().extension() != L".log") continue;
      auto t = it->last_write_time(ec);
      if (ec) continue;
      if (newest.empty() || t > newestTime) {
        newest = it->path();
        newestTime = t;
      }
    }
    if (!newest.empty() && newest != journal_) {
      journal_ = newest;
      journalOffset_ = 0;
      partial_.clear();
      Log("Ship tracking: reading {}", WideToUtf8(journal_.filename().wstring()));
    }
  }
  if (journal_.empty()) return;

  std::string data;
  uint64_t size = 0;
  if (!ReadAll(journal_, journalOffset_, data, size)) return;
  if (size < journalOffset_) {  // file was replaced
    journalOffset_ = 0;
    partial_.clear();
    return;
  }
  journalOffset_ = size;
  if (data.empty()) return;
  partial_ += data;
  size_t start = 0;
  for (size_t nl; (nl = partial_.find('\n', start)) != std::string::npos; start = nl + 1)
    HandleLine(partial_.substr(start, nl - start));
  partial_.erase(0, start);
}

void ShipTracker::HandleLine(const std::string& line) {
  // cheap pre-filter: only a few events tell which ship we are in / whether we are in the game
  static const char* const kEvents[] = {"\"LoadGame\"",    "\"Loadout\"", "\"ShipyardSwap\"", "\"ShipyardNew\"",
                                        "\"Fileheader\"", "\"MainMenu\"", "\"Shutdown\""};
  if (std::none_of(std::begin(kEvents), std::end(kEvents),
                   [&](const char* e) { return line.find(e) != std::string::npos; }))
    return;
  try {
    auto j = nlohmann::json::parse(line);
    const std::string ev = j.value("event", "");
    // game start (journal part 1, a continued journal keeps the state), main menu, game closed
    if ((ev == "Fileheader" && j.value("part", 1) == 1) || ev == "Shutdown" ||
        (ev == "Music" && j.value("MusicTrack", "") == "MainMenu")) {
      inGame_ = false;
      return;
    }
    if (ev == "LoadGame") inGame_ = true;
    const char* field = ev == "LoadGame" || ev == "Loadout" ? "Ship"
                        : ev == "ShipyardSwap" || ev == "ShipyardNew" ? "ShipType"
                                                                       : nullptr;
    if (!field) return;
    const std::string ship = Lower(j.value(field, ""));
    // LoadGame on foot names the suit ("FlightSuit", "UtilitySuit_Class1"); taxis / shuttles are
    // tracked as "taxi" through Status.json. Keep the own ship in both cases.
    if (ship.empty() || ship.find("suit") != std::string::npos || ship.find("_taxi") != std::string::npos) return;
    const std::string local = j.value(std::string(field) + "_Localised", "");
    if (!local.empty() && local[0] != '$') LearnedNames()[ship] = local;
    ship_ = ship;
  } catch (...) {
  }
}

void ShipTracker::ReadStatus() {
  const auto file = dir_ / L"Status.json";
  WIN32_FILE_ATTRIBUTE_DATA a{};
  if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &a)) return;
  if (CompareFileTime(&a.ftLastWriteTime, &statusTime_) == 0) return;
  std::string data;
  uint64_t size = 0;
  if (!ReadAll(file, 0, data, size) || data.empty()) return;  // being rewritten: try again next time
  try {
    auto j = nlohmann::json::parse(data);
    statusTime_ = a.ftLastWriteTime;
    const uint64_t flags = j.value("Flags", uint64_t{0});
    const uint64_t flags2 = j.value("Flags2", uint64_t{0});
    if (flags2 & (1u << 1))
      vehicle_ = "taxi";
    else if (flags2 & (1u << 0))
      vehicle_ = "onfoot";
    else if (flags & (1u << 26))
      vehicle_ = "srv";
    else if (flags & (1u << 25))
      vehicle_ = "fighter";
    else
      vehicle_.clear();  // in the own ship, or in the main menu (keep the last ship)
  } catch (...) {
  }
}

void ShipTracker::Update() {
  if (inGame_ != loggedInGame_) {
    loggedInGame_ = inGame_;
    Log("Ship tracking: {}", inGame_ ? "in the game" : "not in the game (main menu / loading / closed)");
  }
  std::string key = vehicle_.empty() ? ship_ : vehicle_;
  if (key == current_) return;
  current_ = key;
  Log("Ship tracking: now in '{}'", current_);
}

std::string ShipTracker::DisplayName(const std::string& key) {
  static const std::map<std::string, std::string> kNames = {
      {"srv", "SRV"},
      {"fighter", "Fighter"},
      {"taxi", "Taxi"},
      {"adder", "Adder"},
      {"anaconda", "Anaconda"},
      {"asp", "Asp Explorer"},
      {"asp_scout", "Asp Scout"},
      {"belugaliner", "Beluga Liner"},
      {"cobramkiii", "Cobra Mk III"},
      {"cobramkiv", "Cobra Mk IV"},
      {"cobramkv", "Cobra Mk V"},
      {"corsair", "Corsair"},
      {"cutter", "Imperial Cutter"},
      {"diamondback", "Diamondback Scout"},
      {"diamondbackxl", "Diamondback Explorer"},
      {"dolphin", "Dolphin"},
      {"eagle", "Eagle"},
      {"empire_courier", "Imperial Courier"},
      {"empire_eagle", "Imperial Eagle"},
      {"empire_trader", "Imperial Clipper"},
      {"explorer_nx", "Caspian Explorer"},
      {"federation_corvette", "Federal Corvette"},
      {"federation_dropship", "Federal Dropship"},
      {"federation_dropship_mkii", "Federal Assault Ship"},
      {"federation_gunship", "Federal Gunship"},
      {"ferdelance", "Fer-de-Lance"},
      {"hauler", "Hauler"},
      {"independant_trader", "Keelback"},
      {"krait_light", "Krait Phantom"},
      {"krait_mkii", "Krait Mk II"},
      {"smallcombat01_nx", "Kestrel Mk II"},
      {"mamba", "Mamba"},
      {"mandalay", "Mandalay"},
      {"orca", "Orca"},
      {"panthermkii", "Panther Clipper Mk II"},
      {"python", "Python"},
      {"python_nx", "Python Mk II"},
      {"sidewinder", "Sidewinder"},
      {"type6", "Type-6 Transporter"},
      {"type7", "Type-7 Transporter"},
      {"type8", "Type-8 Transporter"},
      {"type9", "Type-9 Heavy"},
      {"type9_military", "Type-10 Defender"},
      {"typex", "Alliance Chieftain"},
      {"typex_2", "Alliance Crusader"},
      {"typex_3", "Alliance Challenger"},
      {"viper", "Viper Mk III"},
      {"viper_mkiv", "Viper Mk IV"},
      {"vulture", "Vulture"},
  };
  if (key == "onfoot") return T("Zu Fuß", "On foot");
  if (auto it = kNames.find(key); it != kNames.end()) return it->second;
  if (auto it = LearnedNames().find(key); it != LearnedNames().end()) return it->second;
  // unknown (new) ship: "some_ship" -> "Some Ship"
  std::string s = key;
  bool up = true;
  for (char& c : s) {
    if (c == '_') {
      c = ' ';
      up = true;
    } else if (up) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      up = false;
    }
  }
  return s;
}
