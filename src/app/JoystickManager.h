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
class JoystickManager {
 public:
  bool Init(HWND hwnd);
  void AddBinding(int id, const std::string& deviceMatch, int button);
  void ClearBindings() { bindings_.clear(); }
  std::vector<int> Poll();  // ids of bindings whose button was just pressed

  // Last button press on any device since the previous call (for "learn button" in the settings).
  struct Press {
    std::string device;
    int button = 0;  // 1-based
  };
  bool TakeLastPress(Press& out) {
    if (lastPress_.button == 0) return false;
    out = lastPress_;
    lastPress_ = {};
    return true;
  }
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
    int button;  // 1-based
  };

  void Enumerate();
  bool Read(Device& d);

  HWND hwnd_ = nullptr;
  winrt::com_ptr<IDirectInput8W> di_;
  std::vector<Device> devices_;
  std::vector<Binding> bindings_;
  Press lastPress_;
  std::chrono::steady_clock::time_point nextEnum_{};
};
