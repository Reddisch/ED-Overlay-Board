#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>

// Finds out what the commander currently sits in, from the files Elite Dangerous writes to
// "Saved Games\Frontier Developments\Elite Dangerous":
//   - Journal.*.log (newest one): ship type from LoadGame / Loadout / ShipyardSwap / ShipyardNew
//   - Status.json: in SRV / fighter / on foot / taxi
//   - in game or not (journal: LoadGame = in; new journal, main menu music, Shutdown = out)
// Result: a layout key such as "krait_mkii", "srv", "fighter", "onfoot", "taxi" ("" = unknown).
class ShipTracker {
 public:
  ShipTracker();
  ~ShipTracker();

  // cheap to call every frame (checks the files about once per second); returns the current key
  const std::string& Poll();
  const std::string& Current() const { return current_; }
  bool InGame() const { return inGame_; }  // commander loaded (not main menu / loading / game closed)
  bool Available() const { return !journal_.empty(); }  // a journal was found at all
  const std::filesystem::path& Folder() const { return dir_; }

  // "krait_mkii" -> "Krait Mk II", "srv" -> "SRV", ...
  static std::string DisplayName(const std::string& key);
  // a real ship (not "", "srv", "fighter", "onfoot", "taxi")
  static bool IsShip(const std::string& key) {
    return !key.empty() && key != "srv" && key != "fighter" && key != "onfoot" && key != "taxi";
  }

 private:
  void ReadJournal();
  void ReadStatus();
  void HandleLine(const std::string& line);
  void Update();

  std::filesystem::path dir_;
  uint64_t lastCheck_ = 0;

  std::filesystem::path journal_;
  int scanCounter_ = 0;
  uint64_t journalOffset_ = 0;
  std::string partial_;  // incomplete last line

  FILETIME statusTime_{};
  std::string ship_;     // ship type from the journal, lower case
  std::string vehicle_;  // from Status.json: "srv", "fighter", "onfoot", "taxi" or "" (ship / unknown)
  std::string current_;
  bool inGame_ = false;
  bool loggedInGame_ = false;
};
