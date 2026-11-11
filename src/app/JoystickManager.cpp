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

void JoystickManager::AddBinding(int id, const std::string& deviceMatch, const std::vector<int>& buttons) {
  std::vector<int> bs;
  for (int b : buttons) {
    if (b < 1 || b > 128) {
      Log("Joystick binding: button {} out of range (1-128)", b);
      return;
    }
    bs.push_back(b);
  }
  std::sort(bs.begin(), bs.end());
  bs.erase(std::unique(bs.begin(), bs.end()), bs.end());
  if (bs.empty()) return;
  bindings_.push_back({id, Utf8ToWide(deviceMatch), std::move(bs)});
  UpdateDeferred();
}

// A binding fires on release when its buttons are a real part of another binding's combination.
void JoystickManager::UpdateDeferred() {
  for (auto& b : bindings_) {
    b.deferred = false;
    for (const auto& o : bindings_)
      if (&o != &b && o.buttons.size() > b.buttons.size() &&
          std::includes(o.buttons.begin(), o.buttons.end(), b.buttons.begin(), b.buttons.end()))
        b.deferred = true;
  }
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
      for (auto& b : bindings_)
        if (b.armed && IsEqualGUID(b.armedDevice, it->guid)) b.armed = false;
      it = devices_.erase(it);
      continue;
    }
    const Device& d = *it;
    auto held = [&](int button) { return (d.cur[button - 1] & 0x80) != 0; };
    auto pressed = [&](int button) { return held(button) && !(d.prev[button - 1] & 0x80); };
    if (learning_) Learn(d);

    bool any = false;  // a binding fired on this device in this poll
    for (auto& b : bindings_) {
      if (!ContainsI(d.name, b.match)) continue;
      const bool all = std::all_of(b.buttons.begin(), b.buttons.end(), held);
      const bool now = all && std::any_of(b.buttons.begin(), b.buttons.end(), pressed);
      if (!b.deferred) {
        if (now) {
          fired.push_back(b.id);
          any = true;
        }
      } else if (now && !b.armed) {
        b.armed = true;
        b.spoiled = false;
        b.armedDevice = d.guid;
      } else if (b.armed && IsEqualGUID(b.armedDevice, d.guid) && !all) {
        b.armed = false;
        if (!b.spoiled) {
          fired.push_back(b.id);
          any = true;
        }
      }
    }
    if (any)  // e.g. 5 + 12 fired: releasing 5 must not fire "5 alone" any more
      for (auto& b : bindings_)
        if (b.armed && IsEqualGUID(b.armedDevice, d.guid)) b.spoiled = true;
    ++it;
  }
  return fired;
}

void JoystickManager::StartLearning() {
  learning_ = true;
  learnDone_ = learnHasDevice_ = false;
  learned_ = {};
}

void JoystickManager::Learn(const Device& d) {
  if (learnDone_) return;
  if (!learnHasDevice_) {
    for (int i = 0; i < 128 && !learnHasDevice_; ++i)
      if ((d.cur[i] & 0x80) && !(d.prev[i] & 0x80)) {
        learnHasDevice_ = true;
        learnDevice_ = d.guid;
        learned_.device = WideToUtf8(d.name);
      }
    if (!learnHasDevice_) return;
  }
  if (!IsEqualGUID(learnDevice_, d.guid)) return;
  bool anyHeld = false;
  for (int i = 0; i < 128; ++i) {
    if (!(d.cur[i] & 0x80)) continue;
    if (!(d.prev[i] & 0x80) || std::find(learned_.buttons.begin(), learned_.buttons.end(), i + 1) != learned_.buttons.end()) {
      if (std::find(learned_.buttons.begin(), learned_.buttons.end(), i + 1) == learned_.buttons.end())
        learned_.buttons.push_back(i + 1);
      anyHeld = true;
    }
  }
  if (!anyHeld && !learned_.buttons.empty()) {  // all released: done
    std::sort(learned_.buttons.begin(), learned_.buttons.end());
    learnDone_ = true;
  }
}

bool JoystickManager::TakeLearned(Press& out) {
  if (!learning_ || !learnDone_) return false;
  out = learned_;
  learning_ = false;
  return true;
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
