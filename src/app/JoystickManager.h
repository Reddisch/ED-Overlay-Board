#pragma once

#include <Unknwn.h>
#include <dinput.h>

#include <winrt/base.h>

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

// Reads joystick/HOTAS buttons with DirectInput in background, non-exclusive mode,
// so the game still receives the same buttons (e.g. bind it to ED's own "reset HMD orientation").
//
// A binding is one button or a combination of buttons on the same device (e.g. 5 + 12). It fires
// when the last of its buttons goes down while the others are held. A binding whose buttons are
// part of a bigger combination (e.g. 5 alone and 5 + 12) fires on release instead, and only if
// the bigger combination was not used meanwhile - so a "shift" button can have its own action.
class JoystickManager {
 public:
  bool Init(HWND hwnd);
  void AddBinding(int id, const std::string& deviceMatch, const std::vector<int>& buttons);
  void ClearBindings() { bindings_.clear(); }
  std::vector<int> Poll();  // ids of bindings that fired

  // "Learn button" in the settings: collects every button held together on one device and
  // finishes when they are all released again.
  struct Press {
    std::string device;
    std::vector<int> buttons;  // 1-based, sorted
  };
  void StartLearning();
  void StopLearning() { learning_ = false; }
  bool TakeLearned(Press& out);
  std::vector<std::string> DeviceNames() const;

  // Prints all devices and every button press until `quit` is set (helper for configuration).
  void Monitor(const std::atomic<bool>& quit);

 private:
  struct Device {
    GUID guid{};
    std::wstring name;
    winrt::com_ptr<IDirectInputDevice8W> dev;
    BYTE prev[128]{};
    BYTE cur[128]{};
    bool primed = false;
  };
  struct Binding {
    int id;
    std::wstring match;
    std::vector<int> buttons;  // 1-based
    bool deferred = false;     // part of a bigger combination: fires on release
    bool armed = false;        // deferred: all buttons held, waiting for the release
    bool spoiled = false;      // deferred: another binding fired meanwhile
    GUID armedDevice{};
  };

  void Enumerate();
  bool Read(Device& d);
  void UpdateDeferred();
  void Learn(const Device& d);

  HWND hwnd_ = nullptr;
  winrt::com_ptr<IDirectInput8W> di_;
  std::vector<Device> devices_;
  std::vector<Binding> bindings_;
  std::chrono::steady_clock::time_point nextEnum_{};

  bool learning_ = false, learnDone_ = false, learnHasDevice_ = false;
  GUID learnDevice_{};
  Press learned_;
};
