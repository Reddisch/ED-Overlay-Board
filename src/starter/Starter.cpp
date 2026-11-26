#include "Starter.h"

#include "Lang.h"
#include "Log.h"
#include "Util.h"

#include <nlohmann/json.hpp>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <fstream>
#include <set>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr wchar_t kXrKey[] = L"SOFTWARE\\Khronos\\OpenXR\\1";

std::wstring RegString(HKEY root, const wchar_t* sub, const wchar_t* name) {
  wchar_t buf[2048]{};
  DWORD size = sizeof(buf);
  if (RegGetValueW(root, sub, name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) != ERROR_SUCCESS)
    return {};
  return buf;
}

std::vector<std::wstring> RegValueNames(HKEY root, const wchar_t* sub) {
  std::vector<std::wstring> out;
  HKEY k{};
  if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[1024];
    DWORD n = static_cast<DWORD>(std::size(name));
    if (RegEnumValueW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    out.emplace_back(name, n);
  }
  RegCloseKey(k);
  return out;
}

std::wstring Normalize(std::wstring s) {
  std::erase(s, L'"');
  std::replace(s.begin(), s.end(), L'/', L'\\');
  return ToLower(s);
}

bool Exists(const fs::path& p) {
  std::error_code ec;
  return !p.empty() && fs::exists(p, ec);
}

std::string Str(const fs::path& p) { return WideToUtf8(p.wstring()); }

fs::path SteamExe() {
  std::wstring s = RegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
  std::replace(s.begin(), s.end(), L'/', L'\\');
  fs::path p = s;
  return Exists(p) ? p : fs::path{};
}

// SteamVR may live in any Steam library; OpenVR keeps its folder in openvrpaths.vrpath.
fs::path SteamVrDir() {
  wchar_t buf[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) return {};
  std::ifstream f(fs::path(buf) / L"openvr" / L"openvrpaths.vrpath");
  if (!f) return {};
  try {
    json j = json::parse(f);
    for (auto& r : j.value("runtime", json::array()))
      if (r.is_string() && Exists(Utf8ToWide(r.get<std::string>()))) return Utf8ToWide(r.get<std::string>());
  } catch (...) {
  }
  return {};
}

std::vector<std::wstring> SplitNames(const std::string& spec) {
  std::vector<std::wstring> out;
  std::wstring s = Utf8ToWide(spec), cur;
  auto flush = [&] {
    size_t a = cur.find_first_not_of(L" \t"), b = cur.find_last_not_of(L" \t");
    if (a != std::wstring::npos) out.push_back(Normalize(cur.substr(a, b - a + 1)));
    cur.clear();
  };
  for (wchar_t c : s) {
    if (c == L',' || c == L';')
      flush();
    else
      cur += c;
  }
  flush();
  return out;
}

std::wstring StripExe(std::wstring n) {
  if (n.size() > 4 && n.ends_with(L".exe")) n.resize(n.size() - 4);
  return n;
}

// Running processes, refreshed once per tick.
struct Proc {
  DWORD pid;
  std::wstring name;  // lower case, without ".exe"
};
std::vector<Proc> g_procs;

void RefreshProcesses() {
  g_procs.clear();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  PROCESSENTRY32W pe{sizeof(pe)};
  for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    g_procs.push_back({pe.th32ProcessID, StripExe(ToLower(pe.szExeFile))});
  CloseHandle(snap);
}

std::wstring ProcessPath(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return {};
  wchar_t buf[MAX_PATH * 2]{};
  DWORD n = static_cast<DWORD>(std::size(buf));
  std::wstring out = QueryFullProcessImageNameW(h, 0, buf, &n) ? Normalize(buf) : L"";
  CloseHandle(h);
  return out;
}

enum class Detect { Unknown, No, Yes };

// Is the program running? `processes` = names ("a, b"), or a path part like "oculus-client\client.exe"
// for generic names; empty = file name of `path`. With `title`, a visible window of those processes
// (or of any process) must contain that text.
Detect IsRunning(const std::string& processes, const std::string& path, const std::string& title) {
  auto names = SplitNames(processes);
  if (names.empty()) {
    fs::path p = Utf8ToWide(ExpandPath(path));
    if (ToLower(p.extension().wstring()) == L".exe") names.push_back(ToLower(p.stem().wstring()));
  }
  if (names.empty() && title.empty()) return Detect::Unknown;

  std::set<DWORD> pids;
  for (auto& n : names) {
    bool byPath = n.find(L'\\') != std::wstring::npos;
    std::wstring exe = StripExe(fs::path(n).filename().wstring());
    for (auto& p : g_procs) {
      if (p.name != exe) continue;
      if (byPath && ProcessPath(p.pid).find(n) == std::wstring::npos) continue;
      pids.insert(p.pid);
    }
  }
  if (title.empty()) return pids.empty() ? Detect::No : Detect::Yes;
  if (!names.empty() && pids.empty()) return Detect::No;

  struct Ctx {
    std::wstring title;
    const std::set<DWORD>* pids;
    bool any;
    bool found = false;
  } ctx{Utf8ToWide(title), &pids, names.empty()};
  EnumWindows(
      [](HWND w, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (!IsWindowVisible(w)) return TRUE;
        wchar_t t[512]{};
        GetWindowTextW(w, t, 512);
        if (!ContainsI(t, c->title)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (c->any || c->pids->count(pid)) {
          c->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found ? Detect::Yes : Detect::No;
}

std::string LastErrorText(DWORD err) {
  wchar_t* msg = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                 err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
  std::wstring s = msg ? msg : L"";
  if (msg) LocalFree(msg);
  while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L'.')) s.pop_back();
  return s.empty() ? std::to_string(err) : WideToUtf8(s);
}

}  // namespace

// ========================================================================= helpers
std::string ExpandPath(const std::string& s) {
  std::wstring w = Utf8ToWide(s);
  std::erase(w, L'"');
  wchar_t buf[4096]{};
  if (ExpandEnvironmentStringsW(w.c_str(), buf, static_cast<DWORD>(std::size(buf)))) w = buf;
  return WideToUtf8(w);
}

std::wstring ActiveOpenXrRuntime() { return RegString(HKEY_LOCAL_MACHINE, kXrKey, L"ActiveRuntime"); }

bool SamePath(const std::string& a, const std::wstring& b) {
  return !a.empty() && Normalize(Utf8ToWide(ExpandPath(a))) == Normalize(b);
}

int SetOpenXrRuntimeMain(const std::wstring& manifest) {
  if (!Exists(manifest)) return 2;
  HKEY k{};
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kXrKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k,
                      nullptr) != ERROR_SUCCESS)
    return 3;
  LSTATUS st = RegSetValueExW(k, L"ActiveRuntime", 0, REG_SZ, reinterpret_cast<const BYTE*>(manifest.c_str()),
                              static_cast<DWORD>((manifest.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  return st == ERROR_SUCCESS ? 0 : 4;
}

// ========================================================================= config
bool StarterConfig::Load(const fs::path& file) {
  std::ifstream f(file);
  if (!f) return false;
  try {
    json j = json::parse(f);
    language = j.value("language", "");
    mode = j.value("mode", "");
    closeWhenDone = j.value("closeWhenDone", true);
    modes.clear();
    for (auto& m : j.value("vrModes", json::array())) {
      VrMode v;
      v.name = m.value("name", "");
      v.runtime = m.value("runtime", "");
      v.program = m.value("program", "");
      v.args = m.value("args", "");
      v.processes = m.value("processes", "");
      v.timeout = m.value("timeout", 90);
      modes.push_back(v);
    }
    programs.clear();
    for (auto& p : j.value("programs", json::array())) {
      ProgramEntry e;
      e.name = p.value("name", "");
      e.enabled = p.value("enabled", true);
      e.path = p.value("path", "");
      e.args = p.value("args", "");
      e.workDir = p.value("workDir", "");
      e.delay = p.value("delay", 0);
      e.timeout = p.value("timeout", 60);
      e.waitForVr = p.value("waitForVr", false);
      e.processes = p.value("processes", "");
      e.windowTitle = p.value("windowTitle", "");
      programs.push_back(e);
    }
    return true;
  } catch (const std::exception& e) {
    Log("Cannot read {}: {}", Str(file), e.what());
    return false;
  }
}

bool StarterConfig::Save(const fs::path& file) const {
  json j;
  if (!language.empty()) j["language"] = language;
  j["mode"] = mode;
  j["closeWhenDone"] = closeWhenDone;
  j["vrModes"] = json::array();
  for (auto& v : modes)
    j["vrModes"].push_back({{"name", v.name}, {"runtime", v.runtime}, {"program", v.program}, {"args", v.args},
                            {"processes", v.processes}, {"timeout", v.timeout}});
  j["programs"] = json::array();
  for (auto& e : programs)
    j["programs"].push_back({{"name", e.name}, {"enabled", e.enabled}, {"path", e.path}, {"args", e.args},
                             {"workDir", e.workDir}, {"delay", e.delay}, {"timeout", e.timeout},
                             {"waitForVr", e.waitForVr}, {"processes", e.processes},
                             {"windowTitle", e.windowTitle}});
  fs::path tmp = file;
  tmp += L".tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    if (!f) return false;
    f << j.dump(2) << '\n';
    if (!f) return false;
  }
  std::error_code ec;
  fs::rename(tmp, file, ec);
  return !ec;
}

const VrMode* StarterConfig::FindMode(const std::string& name) const {
  for (auto& m : modes)
    if (m.name == name) return &m;
  return modes.empty() ? nullptr : &modes.front();
}

void StarterConfig::Defaults() {
  modes.clear();
  programs.clear();
  const fs::path steam = SteamExe();

  // ---- VR modes
  modes.push_back({T("Desktop / Runtime nicht ändern", "Desktop / keep the runtime"), "", "", "", "", 90});
  std::set<std::wstring> seen;
  auto runtimes = RegValueNames(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1\\AvailableRuntimes");
  if (fs::path svr = SteamVrDir(); !svr.empty()) runtimes.push_back((svr / L"steamxr_win64.json").wstring());
  if (auto active = ActiveOpenXrRuntime(); !active.empty()) runtimes.push_back(active);
  for (auto& r : runtimes) {
    fs::path manifest = r;
    std::wstring key = Normalize(r);
    if (!Exists(manifest) || !seen.insert(key).second) continue;
    VrMode m;
    m.runtime = Str(manifest);
    if (key.find(L"virtualdesktop") != std::wstring::npos) {
      m.name = "Virtual Desktop (VDXR)";
      fs::path exe = manifest.parent_path().parent_path() / L"VirtualDesktop.Streamer.exe";
      if (Exists(exe)) m.program = Str(exe), m.processes = "VirtualDesktop.Streamer";
    } else if (key.find(L"steamxr") != std::wstring::npos) {
      m.name = "SteamVR";
      if (!steam.empty())
        m.program = Str(steam), m.args = "-applaunch 250820";
      else
        m.program = "steam://rungameid/250820";
      m.processes = "vrserver";
    } else if (key.find(L"oculus") != std::wstring::npos) {
      m.name = "Meta Horizon Link";
      fs::path exe = manifest.parent_path().parent_path() / L"oculus-client" / L"OculusClient.exe";
      // OculusClient.exe only launches oculus-client\Client.exe and quits
      if (Exists(exe)) m.program = Str(exe), m.processes = "oculus-client\\Client.exe";
    } else {
      m.name = Str(manifest.parent_path().filename()) + " (" + Str(manifest.stem()) + ")";
    }
    modes.push_back(m);
  }
  std::sort(modes.begin() + 1, modes.end(), [](auto& a, auto& b) { return a.name < b.name; });
  mode = modes.front().name;
  for (auto& m : modes)
    if (SamePath(m.runtime, ActiveOpenXrRuntime())) mode = m.name;

  // ---- programs
  auto add = [&](std::string name, const fs::path& exe, int delay, std::string processes = "") {
    if (!Exists(exe)) return;
    ProgramEntry e;
    e.name = std::move(name);
    e.path = Str(exe);
    e.delay = delay;
    e.processes = std::move(processes);
    programs.push_back(e);
  };
  wchar_t pf[MAX_PATH]{}, pf86[MAX_PATH]{};
  GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
  GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, MAX_PATH);
  add("VoiceAttack", fs::path(pf) / L"VoiceAttack" / L"VoiceAttack.exe", 0);
  add("VoiceAttack", fs::path(pf86) / L"VoiceAttack" / L"VoiceAttack.exe", 0);
  if (!steam.empty() && programs.empty())
    add("VoiceAttack", steam.parent_path() / L"steamapps" / L"common" / L"VoiceAttack 2" / L"VoiceAttack.exe", 0);

  ProgramEntry elite;
  elite.name = "Elite Dangerous";
  elite.timeout = 240;
  elite.waitForVr = true;
  elite.processes = "EliteDangerous64, EDLaunch";
  if (!steam.empty()) {
    elite.path = Str(steam);
    elite.args = "-applaunch 359320 /VR /edo /autorun /autoquit";
  }
  programs.push_back(elite);

  add("ED Overlay Board", ExeDir() / L"EDOverlayBoard.exe", 10);
  add("EDCoPilot", L"C:\\EDCoPilot\\LaunchEDCoPilot.exe", 40, "EDCoPilot");
  if (!programs.empty() && programs.back().name == "EDCoPilot") programs.back().timeout = 90;
}

// ========================================================================= runner
Runner::~Runner() {
  if (elevate_.joinable()) elevate_.join();
}

double Runner::Now() const {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
}

void Runner::Start(const StarterConfig& cfg, HWND owner) {
  if (Busy()) return;
  if (elevate_.joinable()) elevate_.join();
  steps_.clear();
  owner_ = owner;
  const VrMode* mode = cfg.FindMode(cfg.mode);
  if (mode && !mode->runtime.empty()) {
    Step s;
    s.kind = StepKind::Runtime;
    s.name = "OpenXR -> " + mode->name;
    s.path = mode->runtime;
    s.timeout = 120;
    steps_.push_back(s);
  }
  if (mode && !mode->program.empty()) {
    Step s;
    s.kind = StepKind::VrProgram;
    s.name = mode->name;
    s.path = mode->program;
    s.args = mode->args;
    s.processes = mode->processes;
    s.timeout = mode->timeout;
    steps_.push_back(s);
  }
  for (int i = 0; i < static_cast<int>(cfg.programs.size()); ++i) {
    auto& e = cfg.programs[i];
    if (!e.enabled || e.path.empty()) continue;
    Step s;
    s.program = i;
    s.name = e.name;
    s.path = e.path;
    s.args = e.args;
    s.workDir = e.workDir;
    s.processes = e.processes;
    s.windowTitle = e.windowTitle;
    s.at = e.delay;
    s.timeout = e.timeout;
    s.needsVr = e.waitForVr;
    steps_.push_back(s);
  }
  t0_ = std::chrono::steady_clock::now();
  doneAt_ = -1;
  started_ = true;
  Log("==== Start, VR mode '{}', {} step(s)", mode ? mode->name : "-", steps_.size());
}

bool Runner::Busy() const {
  return std::any_of(steps_.begin(), steps_.end(),
                     [](auto& s) { return s.state == StepState::Waiting || s.state == StepState::Starting; });
}

int Runner::FailedCount() const {
  return static_cast<int>(std::count_if(steps_.begin(), steps_.end(), [](auto& s) { return s.state == StepState::Failed; }));
}

double Runner::Elapsed() const { return started_ ? Now() : 0; }
double Runner::SinceDone() const { return doneAt_ < 0 ? 0 : Now() - doneAt_; }

bool Runner::VrBlocked() const {
  for (auto& s : steps_)
    if (s.kind != StepKind::Program && s.state != StepState::Ok && s.state != StepState::Already) return true;
  return false;
}

void Runner::Cancel() {
  for (auto& s : steps_)
    if (s.state == StepState::Waiting || (s.state == StepState::Starting && s.kind != StepKind::Runtime))
      s.state = StepState::Cancelled;
  Log("Cancelled");
}

void Runner::Launch(Step& s) {
  s.state = StepState::Starting;
  s.startedAt = Now();
  if (s.kind == StepKind::Runtime) {
    // The elevated helper (this exe with --set-openxr-runtime) writes HKLM. The UAC prompt runs on
    // its own thread, so the window keeps drawing and nothing is started twice.
    if (elevate_.joinable()) elevate_.join();
    elevateDone_ = false;
    elevateError_ = 0;
    std::wstring manifest = Utf8ToWide(ExpandPath(s.path));
    HWND owner = owner_;
    elevate_ = std::thread([this, manifest, owner] {
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
      wchar_t exe[MAX_PATH * 2]{};
      GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
      std::wstring params = L"--set-openxr-runtime \"" + manifest + L"\"";
      SHELLEXECUTEINFOW sei{sizeof(sei)};
      sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
      sei.hwnd = owner;
      sei.lpVerb = L"runas";
      sei.lpFile = exe;
      sei.lpParameters = params.c_str();
      sei.nShow = SW_HIDE;
      DWORD err = 0;
      if (!ShellExecuteExW(&sei)) {
        err = GetLastError();
      } else if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 60000);
        DWORD code = 0;
        GetExitCodeProcess(sei.hProcess, &code);
        if (code) err = 0x20000000 | code;
        CloseHandle(sei.hProcess);
      }
      elevateError_ = err;
      elevateDone_ = true;
      CoUninitialize();
    });
    Log("{}: asking for admin rights", s.name);
    return;
  }

  std::wstring file = Utf8ToWide(ExpandPath(s.path));
  std::wstring args = Utf8ToWide(s.args);
  std::wstring dir = Utf8ToWide(ExpandPath(s.workDir));
  if (dir.empty() && Exists(file)) dir = fs::path(file).parent_path().wstring();
  SHELLEXECUTEINFOW sei{sizeof(sei)};
  sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  sei.hwnd = owner_;
  sei.lpFile = file.c_str();
  sei.lpParameters = args.empty() ? nullptr : args.c_str();
  sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
  sei.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&sei)) {
    s.state = StepState::Failed;
    s.error = LastErrorText(GetLastError());
    Log("{}: cannot start {}: {}", s.name, s.path, s.error);
    return;
  }
  Log("{}: started {} {}", s.name, s.path, s.args);
}

void Runner::FinishElevation(Step& s) {
  if (elevate_.joinable()) elevate_.join();
  DWORD err = elevateError_;
  if (SamePath(s.path, ActiveOpenXrRuntime())) {
    s.state = StepState::Ok;
    Log("{}: done", s.name);
    return;
  }
  s.state = StepState::Failed;
  if (err == ERROR_CANCELLED)
    s.error = T("Admin-Abfrage abgelehnt", "admin prompt declined");
  else if (err & 0x20000000)
    s.error = T("Runtime-Datei fehlt oder Registry gesperrt", "runtime file missing or registry locked");
  else if (err)
    s.error = LastErrorText(err);
  else
    s.error = T("Runtime wurde nicht umgestellt", "runtime was not changed");
  Log("{}: FAILED: {}", s.name, s.error);
}

void Runner::Tick() {
  if (!started_) return;
  RefreshProcesses();
  const double now = Now();
  for (auto& s : steps_) {
    if (s.state == StepState::Waiting) {
      if (now < s.at) continue;
      if (s.needsVr) {
        auto bad = std::find_if(steps_.begin(), steps_.end(), [](auto& o) {
          return o.kind != StepKind::Program && (o.state == StepState::Failed || o.state == StepState::Cancelled);
        });
        if (bad != steps_.end()) {
          s.state = StepState::Failed;
          s.error = std::string(T("VR-Modus nicht bereit: ", "VR mode not ready: ")) + bad->name;
          Log("{}: not started, {} failed", s.name, bad->name);
          continue;
        }
        if (VrBlocked()) continue;
      }
      bool already = s.kind == StepKind::Runtime ? SamePath(s.path, ActiveOpenXrRuntime())
                                                  : IsRunning(s.processes, s.path, s.windowTitle) == Detect::Yes;
      if (already) {
        s.state = StepState::Already;
        Log("{}: already running", s.name);
        continue;
      }
      Launch(s);
    } else if (s.state == StepState::Starting) {
      if (s.kind == StepKind::Runtime) {
        if (elevateDone_) FinishElevation(s);
        continue;
      }
      Detect d = IsRunning(s.processes, s.path, s.windowTitle);
      if (d != Detect::No) {  // Unknown (e.g. a URL): we cannot see it, count the launch as success
        s.state = StepState::Ok;
        Log("{}: running", s.name);
      } else if (now - s.startedAt > s.timeout) {
        s.state = StepState::Failed;
        s.error = std::format("{} {} s", T("nicht gefunden nach", "not found after"), s.timeout);
        Log("{}: not running after {} s", s.name, s.timeout);
      }
    }
  }
  if (!Busy() && doneAt_ < 0) {
    doneAt_ = now;
    Log("==== done, {} failed", FailedCount());
  }
}
