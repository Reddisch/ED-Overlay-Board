#pragma once
// ED Overlay Board Starter: starts Elite Dangerous with the chosen VR mode (switches the active
// OpenXR runtime) and any number of helper programs, each with its own delay.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// A VR mode: which OpenXR runtime Elite should use and which program brings it up.
struct VrMode {
  std::string name;
  std::string runtime;    // OpenXR runtime manifest (.json); empty = leave the active runtime alone
  std::string program;    // companion program (exe or URL); empty = none
  std::string args;
  std::string processes;  // "a, b" or "folder\\file.exe": how to see that it runs; empty = program file name
  int timeout = 90;       // seconds
};

struct ProgramEntry {
  std::string name;
  bool enabled = true;
  std::string path;       // exe, .bat, .lnk or URL
  std::string args;
  std::string workDir;    // empty = folder of the program
  int delay = 0;          // seconds after "Start"
  int timeout = 60;       // seconds until "did not start"
  bool waitForVr = false; // start only once the VR mode is ready
  std::string processes;  // see VrMode::processes
  std::string windowTitle;// optional: running = a window whose title contains this text
};

struct StarterConfig {
  std::string language;   // "de", "en" or "" = Windows language
  std::string mode;       // name of the selected VR mode
  bool closeWhenDone = true;
  std::vector<VrMode> modes;
  std::vector<ProgramEntry> programs;

  bool Load(const std::filesystem::path& file);
  bool Save(const std::filesystem::path& file) const;
  void Defaults();  // detects Steam, Elite, VR runtimes and known helper programs
  const VrMode* FindMode(const std::string& name) const;
};

// ------------------------------------------------------------------------- system helpers
std::wstring ActiveOpenXrRuntime();
int SetOpenXrRuntimeMain(const std::wstring& manifest);  // runs in the elevated helper process
bool SamePath(const std::string& a, const std::wstring& b);
std::string ExpandPath(const std::string& s);  // %VARS% expanded, quotes removed

// ------------------------------------------------------------------------- start sequence
enum class StepState { Waiting, Starting, Ok, Already, Failed, Cancelled };
enum class StepKind { Runtime, VrProgram, Program };

struct Step {
  StepKind kind = StepKind::Program;
  int program = -1;   // index into StarterConfig::programs (kind == Program)
  std::string name;
  std::string path, args, workDir, processes, windowTitle;  // Runtime: path = manifest
  double at = 0;      // seconds after the start
  int timeout = 60;
  bool needsVr = false;
  StepState state = StepState::Waiting;
  std::string error;  // why it failed
  double startedAt = 0;
};

class Runner {
 public:
  ~Runner();
  void Start(const StarterConfig& cfg, HWND owner);
  void Tick();
  void Cancel();

  bool Started() const { return started_; }
  bool Busy() const;      // something is still waiting or starting
  int FailedCount() const;
  double Elapsed() const;
  double SinceDone() const;  // seconds since everything finished (0 while busy)
  bool VrBlocked() const;    // VR steps not ready yet
  const std::vector<Step>& Steps() const { return steps_; }

 private:
  double Now() const;
  void Launch(Step& s);
  void FinishElevation(Step& s);

  std::vector<Step> steps_;
  std::chrono::steady_clock::time_point t0_{};
  double doneAt_ = -1;
  bool started_ = false;
  HWND owner_ = nullptr;
  std::thread elevate_;
  std::atomic<bool> elevateDone_{false};
  std::atomic<DWORD> elevateError_{0};
};
