#pragma once

#include "Config.h"
#include "ContentSource.h"
#include "D3D.h"
#include "HotkeyManager.h"
#include "Ipc.h"
#include "JoystickManager.h"

#include <dxgi1_2.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class SettingsWindow;
class VrMenu;

// Companion app: produces the window contents, handles hotkeys/joysticks, the config,
// the desktop settings window and the VR quick menu. The OpenXR layer inside the game
// picks everything up through shared memory.
class App {
 public:
  explicit App(std::atomic<bool>& quit);
  ~App();
  int Run(const std::filesystem::path& configPath);

  // ---- API for the settings window and the VR menu --------------------------------
  AppConfig& Config() { return cfg_; }
  D3D& Graphics() { return d3d_; }
  void ConfigChanged(bool structural);  // apply immediately, save shortly after
  void PlacementEdited(int window);     // position/rotation/width typed in the settings
  void Do(const std::string& action, const std::string& window = {});
  std::string WindowStatus(int window) const;

  struct VrStatus {
    bool connected = false;
    std::string application, runtime;
    bool controllers = false, handTracking = false;
  };
  const VrStatus& Status() const { return vrStatus_; }
  bool InteractionEnabled() const { return interactionEnabled_; }
  bool AllHidden() const { return allHidden_; }
  bool MenuVisible() const;

  void StartKeyLearning();
  void StartJoystickLearning();
  void StopLearning();
  bool Learning() const { return keyLearning_ || joyLearning_; }
  // returns true once when something was learned (key: spec, joystick: device+button) or cancelled
  bool TakeLearned(std::string& key, JoystickManager::Press& joy, bool& cancelled);
  std::vector<std::string> JoystickNames() const { return joysticks_.DeviceNames(); }

  void OpenSettings();
  void Quit() { quit_ = true; }

 private:
  struct Slot {
    WindowConfig* cfg = nullptr;
    bool internal = false;  // the VR menu
    std::unique_ptr<ContentSource> owned;
    ContentSource* source = nullptr;
    std::string sourceKey;
    winrt::com_ptr<ID3D11Texture2D> shared;
    winrt::com_ptr<IDXGIKeyedMutex> mutex;
    HANDLE handle = nullptr;
    uint32_t width = 0, height = 0;
    uint32_t generation = 0;
    uint32_t version = 0;
    bool pending = true;
    bool pushPlacement = true;
    uint32_t seenLayerPlacement = 0, seenClick = 0, seenDown = 0, seenUp = 0;
    float seenWheel = 0.0f;
  };

  bool Init();
  void Tick();
  void Execute(const BindingConfig& b);
  void RegisterBindings();
  void RebuildSlots();
  std::unique_ptr<ContentSource> MakeSource(const WindowConfig& c);
  bool Publish(Slot& s, ID3D11Texture2D* src);
  void SyncShared();
  void UpdateLearning();
  void MarkDirty();
  void SaveIfIdle(bool force);
  void CreateTrayIcon();
  void ShowTrayMenu();
  static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  std::atomic<bool>& quit_;
  AppConfig cfg_;
  WindowConfig menuCfg_;
  D3D d3d_;
  HotkeyManager hotkeys_;
  JoystickManager joysticks_;
  ipc::SharedState shm_;
  HWND hwnd_ = nullptr;
  bool trayCreated_ = false;
  std::vector<Slot> slots_;
  std::unique_ptr<SettingsWindow> settings_;
  std::unique_ptr<VrMenu> vrMenu_;

  uint32_t appInstance_ = 0;
  uint32_t nextGeneration_ = 0;
  uint32_t placementSerial_ = 0;
  uint32_t recenterSerial_ = 0;
  bool allHidden_ = false;
  bool interactionEnabled_ = true;
  VrStatus vrStatus_;

  bool keyLearning_ = false, joyLearning_ = false;
  bool learnedReady_ = false, learnedCancelled_ = false;
  std::string learnedKey_;
  JoystickManager::Press learnedJoy_;
  bool keysDownAtStart_[256]{};

  bool dirty_ = false;
  std::chrono::steady_clock::time_point dirtySince_{};
};
