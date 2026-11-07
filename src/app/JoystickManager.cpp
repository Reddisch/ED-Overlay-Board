#include "JoystickManager.h"

#include "Log.h"
#include "Util.h"

#include <algorithm>
#include <thread>

using namespace std::chrono_literals;

bool JoystickManager::Init(HWND hwnd) {
  hwnd_ = hwnd;
  HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, di_.put_void(),
                                  nullptr);
  if (FAILED(hr)) {
    Log("DirectInput8Create failed: 0x{:08X}", static_cast<uint32_t>(hr));
    return false;
  }
  Enumerate();
  return true;
}

void JoystickManager::AddBinding(int id, const std::string& deviceMatch, int button) {
  if (button < 1 || button > 128) {
    Log("Joystick binding: button {} out of range (1-128)", button);
    return;
  }
  bindings_.push_back({id, Utf8ToWide(deviceMatch), button});
}

void JoystickManager::Enumerate() {
  if (!di_) return;
  nextEnum_ = std::chrono::steady_clock::now() + 5s;

  std::vector<DIDEVICEINSTANCEW> found;
  di_->EnumDevices(
      DI8DEVCLASS_GAMECTRL,
      [](LPCDIDEVICEINSTANCEW inst, LPVOID ctx) -> BOOL {
        static_cast<std::vector<DIDEVICEINSTANCEW>*>(ctx)->push_back(*inst);
        return DIENUM_CONTINUE;
      },
      &found, DIEDFL_ATTACHEDONLY);

  for (const auto& inst : found) {
    bool known = std::any_of(devices_.begin(), devices_.end(),
                             [&](const Device& d) { return IsEqualGUID(d.guid, inst.guidInstance); });
    if (known) continue;

    Device d;
    d.guid = inst.guidInstance;
    d.name = inst.tszProductName;
    if (FAILED(di_->CreateDevice(inst.guidInstance, d.dev.put(), nullptr))) continue;
    if (FAILED(d.dev->SetDataFormat(&c_dfDIJoystick2))) continue;
    d.dev->SetCooperativeLevel(hwnd_, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    d.dev->Acquire();
    Log("Joystick: found \"{}\"", WideToUtf8(d.name));
    devices_.push_back(std::move(d));
  }
}

bool JoystickManager::Read(Device& d) {
  d.dev->Poll();
  DIJOYSTATE2 st{};
  HRESULT hr = d.dev->GetDeviceState(sizeof(st), &st);
  if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
    d.dev->Acquire();
    return true;  // keep device, try again next frame
  }
  if (FAILED(hr)) return false;  // unplugged
  std::copy(std::begin(d.cur), std::end(d.cur), d.prev);
  std::copy(std::begin(st.rgbButtons), std::end(st.rgbButtons), d.cur);
  if (!d.primed) {  // don't fire for buttons that are already held at startup
    std::copy(std::begin(d.cur), std::end(d.cur), d.prev);
    d.primed = true;
  }
  return true;
}

std::vector<int> JoystickManager::Poll() {
  std::vector<int> fired;
  if (!di_) return fired;
  if (std::chrono::steady_clock::now() >= nextEnum_) Enumerate();

  for (auto it = devices_.begin(); it != devices_.end();) {
    if (!Read(*it)) {
      Log("Joystick: lost \"{}\"", WideToUtf8(it->name));
      it = devices_.erase(it);
      continue;
    }
    for (int i = 0; i < 128; ++i)
      if ((it->cur[i] & 0x80) && !(it->prev[i] & 0x80)) lastPress_ = {WideToUtf8(it->name), i + 1};
    for (const auto& b : bindings_) {
      int i = b.button - 1;
      if ((it->cur[i] & 0x80) && !(it->prev[i] & 0x80) && ContainsI(it->name, b.match)) fired.push_back(b.id);
    }
    ++it;
  }
  return fired;
}

std::vector<std::string> JoystickManager::DeviceNames() const {
  std::vector<std::string> out;
  for (const auto& d : devices_) out.push_back(WideToUtf8(d.name));
  return out;
}

void JoystickManager::Monitor(const std::atomic<bool>& quit) {
  Log("Joystick monitor: press buttons on your devices, Ctrl+C to quit.");
  if (devices_.empty()) Log("No DirectInput game controllers found.");
  for (const auto& d : devices_) Log("  device: \"{}\"", WideToUtf8(d.name));
  while (!quit) {
    if (std::chrono::steady_clock::now() >= nextEnum_) Enumerate();
    for (auto& d : devices_) {
      if (!Read(d)) continue;
      for (int i = 0; i < 128; ++i)
        if ((d.cur[i] & 0x80) && !(d.prev[i] & 0x80))
          Log("  \"{}\"  button {}   ->  {{ \"joystick\": \"{}\", \"button\": {} }}", WideToUtf8(d.name), i + 1,
              WideToUtf8(d.name), i + 1);
    }
    std::this_thread::sleep_for(15ms);
  }
}
