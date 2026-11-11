#include "App.h"

#include "Gui.h"
#include "ImageSource.h"
#include "Lang.h"
#include "Log.h"
#include "Util.h"
#include "WindowCaptureSource.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace std::chrono_literals;

namespace {
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr int kMaxUserWindows = ipc::kMaxWindows - 1;  // one slot is reserved for the VR menu

std::string BindingsKey(const std::vector<BindingConfig>& bs) {
  std::string k;
  for (const auto& b : bs) k += b.action + "|" + b.window + "|" + b.key + "|" + b.joystick + "|" + std::to_string(b.button) + ";";
  return k;
}
}  // namespace

App::App(std::atomic<bool>& quit) : quit_(quit) {
  menuCfg_.name = "VR-Menü";
  menuCfg_.type = "menu";
  menuCfg_.visible = false;
  menuCfg_.lookToShow = 0;
  menuCfg_.position = {0.0f, -0.15f, -0.5f};
}

App::~App() {
  settings_.reset();
  for (auto& s : slots_)
    if (s.handle) CloseHandle(s.handle);
  slots_.clear();
  vrMenu_.reset();
  if (trayCreated_) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
  }
  if (hwnd_) DestroyWindow(hwnd_);
}

int App::Run(const std::filesystem::path& configPath) {
  if (!cfg_.Load(configPath)) return 1;
  i18n::Apply(cfg_.language);
  if (!Init()) return 1;

  Log("Running. Settings: tray icon (double-click), quit via its menu. Start Elite Dangerous in VR - the windows "
      "appear there.");
  OpenSettings();
  while (!quit_) {
    // ~60 Hz, but wake up immediately for window/hotkey messages
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    Tick();
  }

  if (auto lock = shm_.Acquire(100)) shm_.Get()->appHeartbeat = 0;  // tell the layer we are gone
  SaveIfIdle(true);
  return 0;
}

bool App::Init() {
  if (!d3d_.Init(-1)) return false;
  if (!shm_.Create()) {
    Log("Cannot create shared memory (error {})", GetLastError());
    return false;
  }
  appInstance_ = static_cast<uint32_t>(GetTickCount64() ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 16));

  // Hidden window: tray icon messages + DirectInput cooperative level.
  WNDCLASSW wc{};
  wc.lpfnWndProc = HiddenWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"EDOverlayBoardHidden";
  RegisterClassW(&wc);
  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"EDOverlayBoard", WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr,
                          wc.hInstance, nullptr);
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  CreateTrayIcon();
  joysticks_.Init(hwnd_);
  RegisterBindings();

  {
    auto lock = shm_.Acquire(1000);
    ipc::State* st = shm_.Get();
    uint32_t oldRecenter = st->magic == ipc::kMagic ? st->recenterSerial : 0;
    std::memset(st, 0, sizeof(ipc::State));
    st->magic = ipc::kMagic;
    st->version = ipc::kVersion;
    st->appInstance = appInstance_;
    st->recenterSerial = recenterSerial_ = oldRecenter;
  }

  vrMenu_ = std::make_unique<VrMenu>(*this);
  RebuildSlots();
  settings_ = std::make_unique<SettingsWindow>(*this);
  return true;
}

// ---------------------------------------------------------------------------
// Slots (one per window + the VR menu)

std::unique_ptr<ContentSource> App::MakeSource(const WindowConfig& c) {
  if (c.type == "images") return std::make_unique<ImageSource>(d3d_, c, cfg_.Dir());
  if (c.type != "window") Log("Window '{}': unknown type '{}' (use \"window\" or \"images\")", c.name, c.type);
  return std::make_unique<WindowCaptureSource>(d3d_, c);
}

void App::RebuildSlots() {
  StopKeyboard("window list changed");
  lastMouseSlot_ = -1;
  for (auto& s : slots_)
    if (s.handle) CloseHandle(s.handle);
  slots_.clear();

  if (static_cast<int>(cfg_.windows.size()) > kMaxUserWindows) {
    Log("Only {} windows are supported, ignoring the rest", kMaxUserWindows);
    cfg_.windows.resize(kMaxUserWindows);
  }
  for (auto& c : cfg_.windows) {
    Slot s;
    s.cfg = &c;
    s.owned = MakeSource(c);
    s.source = s.owned.get();
    s.sourceKey = c.SourceKey();
    slots_.push_back(std::move(s));
  }
  Slot menu;
  menu.cfg = &menuCfg_;
  menu.internal = true;
  menu.source = vrMenu_.get();
  menuCfg_.width = cfg_.menuWidth;
  slots_.push_back(std::move(menu));

  auto lock = shm_.Acquire(1000);
  ipc::State* st = shm_.Get();
  st->windowCount = static_cast<uint32_t>(slots_.size());
  for (int i = 0; i < ipc::kMaxWindows; ++i) {
    auto& w = st->windows[i];
    if (i >= static_cast<int>(slots_.size())) {
      w.active = 0;
      continue;
    }
    auto& s = slots_[static_cast<size_t>(i)];
    // ignore whatever the layer reported for the previous occupant of this slot
    s.seenLayerPlacement = w.layerPlacementSerial;
    s.seenClick = w.clickSerial;
    s.seenDown = w.pointerDownSerial;
    s.seenUp = w.pointerUpSerial;
    s.seenWheel = w.pointerWheel;
    s.seenWheelX = w.pointerWheelX;
    w.active = 1;
    w.texWidth = w.texHeight = w.texGeneration = w.contentVersion = 0;
    s.pushPlacement = true;
  }
}

void App::RegisterBindings() {
  hotkeys_.UnregisterAll();
  joysticks_.ClearBindings();
  int keys = 0, buttons = 0;
  std::vector<KeyboardCapture::Hotkey> captureKeys;  // hotkeys still work while the keyboard is captured
  for (size_t i = 0; i < cfg_.bindings.size(); ++i) {
    const auto& b = cfg_.bindings[i];
    const int id = static_cast<int>(i);
    if (KeyboardCapture::Hotkey hk{0, 0, id + 1}; !b.key.empty() && HotkeyManager::Parse(b.key, hk.modifiers, hk.vk))
      captureKeys.push_back(hk);
    if (!b.key.empty() && !keyLearning_ && hotkeys_.Register(id + 1, b.key)) ++keys;
    if (b.button > 0) {
      joysticks_.AddBinding(id, b.joystick, b.button);
      ++buttons;
    }
  }
  keyboard_.SetHotkeys(std::move(captureKeys));
  Log("Bindings: {} key(s), {} joystick button(s)", keys, buttons);
}

void App::ConfigChanged(bool structural) {
  if (structural) {
    RebuildSlots();
  } else {
    for (auto& s : slots_) {
      if (s.internal) continue;
      std::string key = s.cfg->SourceKey();
      if (key != s.sourceKey) {
        s.owned = MakeSource(*s.cfg);
        s.source = s.owned.get();
        s.sourceKey = key;
      }
      s.pending = true;
    }
  }
  static std::string lastBindings;
  std::string bk = BindingsKey(cfg_.bindings);
  if (bk != lastBindings) {
    lastBindings = bk;
    RegisterBindings();
  }
  if (std::fabs(menuCfg_.width - cfg_.menuWidth) > 1e-4f) {
    menuCfg_.width = cfg_.menuWidth;
    slots_.back().pushPlacement = true;
  }
  MarkDirty();
}

void App::PlacementEdited(int window) {
  if (window >= 0 && window < static_cast<int>(slots_.size())) slots_[static_cast<size_t>(window)].pushPlacement = true;
  MarkDirty();
}

// ---------------------------------------------------------------------------
// Main loop

void App::Tick() {
  std::vector<int> fired;
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_HOTKEY) {
      fired.push_back(static_cast<int>(msg.wParam) - 1);
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  for (const auto& e : keyboard_.Drain()) {  // keyboard mode: keys taken away from the game
    if (e.binding > 0)
      fired.push_back(e.binding - 1);
    else
      ForwardKey(e);
  }
  for (int id : joysticks_.Poll()) fired.push_back(id);
  UpdateLearning();
  if (!Learning()) {
    std::sort(fired.begin(), fired.end());
    fired.erase(std::unique(fired.begin(), fired.end()), fired.end());  // same button on two identical devices
    for (int id : fired)
      if (id >= 0 && id < static_cast<int>(cfg_.bindings.size())) Execute(cfg_.bindings[static_cast<size_t>(id)]);
  }

  for (auto& s : slots_) {
    bool changed = false;
    ID3D11Texture2D* tex = s.source ? s.source->Update(changed) : nullptr;
    s.pending |= changed;
    bool shown = s.cfg->visible && (!allHidden_ || s.internal);
    if (tex && s.pending && shown) s.pending = !Publish(s, tex);
  }

  if (settings_) settings_->Frame();
  CheckKeyboard();
  SyncShared();
  SaveIfIdle(false);
}

bool App::Publish(Slot& s, ID3D11Texture2D* src) {
  D3D11_TEXTURE2D_DESC sd{};
  src->GetDesc(&sd);

  if (!s.shared || s.width != sd.Width || s.height != sd.Height) {
    if (s.handle) CloseHandle(s.handle);
    s.handle = nullptr;
    s.shared = nullptr;
    s.mutex = nullptr;

    D3D11_TEXTURE2D_DESC d{};
    d.Width = sd.Width;
    d.Height = sd.Height;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    winrt::com_ptr<ID3D11Texture2D> tex;
    if (FAILED(d3d_.Device()->CreateTexture2D(&d, nullptr, tex.put()))) {
      Log("Window '{}': cannot create shared texture {}x{}", s.cfg->name, sd.Width, sd.Height);
      return false;
    }
    const uint32_t gen = ++nextGeneration_;
    auto name = ipc::TextureName(appInstance_, gen);
    HRESULT hr = tex.as<IDXGIResource1>()->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, name.c_str(), &s.handle);
    if (FAILED(hr)) {
      Log("Window '{}': CreateSharedHandle failed: 0x{:08X}", s.cfg->name, static_cast<uint32_t>(hr));
      return false;
    }
    s.shared = tex;
    s.mutex = tex.as<IDXGIKeyedMutex>();
    s.width = sd.Width;
    s.height = sd.Height;
    s.generation = gen;
  }

  if (s.mutex->AcquireSync(0, 5) != S_OK) return false;  // the layer is copying right now; retry next tick
  // filter color (dims bright pictures); the VR menu is never tinted
  float tint[3];
  s.cfg->TintFactor(tint);
  if (s.internal || !s.cfg->Tinted() || !d3d_.CopyTinted(s.shared.get(), src, tint))
    d3d_.Context()->CopyResource(s.shared.get(), src);
  s.mutex->ReleaseSync(0);
  d3d_.Context()->Flush();
  ++s.version;
  return true;
}

void App::SyncShared() {
  auto lock = shm_.Acquire(5);
  if (!lock) return;
  ipc::State* st = shm_.Get();
  const uint64_t now = GetTickCount64();

  st->appHeartbeat = now;
  st->recenterSerial = recenterSerial_;
  st->allHidden = allHidden_;
  st->interactionEnabled = interactionEnabled_;
  st->showCursor = cfg_.showCursor;
  st->haptics = cfg_.haptics;
  st->scaleSpeed = cfg_.scaleSpeed;
  st->pushPullSpeed = cfg_.pushPullSpeed;
  st->lookMarginDeg = std::clamp(cfg_.lookMarginDeg, 0.0f, 90.0f);
  st->sharpness = std::clamp(cfg_.sharpness, 0.5f, 4.0f);
  st->windowCount = static_cast<uint32_t>(slots_.size());

  for (size_t i = 0; i < slots_.size(); ++i) {
    auto& s = slots_[i];
    auto& w = st->windows[i];
    const WindowConfig& c = *s.cfg;
    strncpy_s(w.name, c.name.c_str(), _TRUNCATE);
    w.active = 1;
    w.visible = c.visible;
    w.locked = c.locked;
    w.opaque = c.type != "images";
    w.lookToShow = s.internal ? 0 : (c.lookToShow < 0 ? cfg_.lookToShow : c.lookToShow == 1);
    w.interactive = s.internal;
    // VR menu and clickable program windows: trigger/pinch act as a mouse (click, drag, wheel)
    const bool mouse = s.internal || (c.type == "window" && c.forwardClicks);
    w.mouseInput = mouse;
    w.keyboardFocus = static_cast<int>(i) == keyboardSlot_;
    if (s.source && !s.internal) s.source->SetSwipe(c.dragScroll, std::clamp(cfg_.swipeSpeed, 0.05f, 10.0f));
    if (s.mouse && !mouse && s.source) {  // switched off while in use: release the button
      s.source->SetPointer(false, 0.0f, 0.0f);
      s.source->AddButton(false);
    }
    s.mouse = mouse;
    w.summonOnShow = s.internal;
    w.headerFraction = s.internal ? vrMenu_->HeaderFraction() : 0.0f;
    w.opacity = std::clamp(c.opacity, 0.05f, 1.0f);
    w.showEffect = s.internal ? 0u : static_cast<uint32_t>(std::clamp(c.showEffect, 0, int(ipc::kEffectCount) - 1));
    w.texWidth = s.width;
    w.texHeight = s.height;
    w.texGeneration = s.generation;
    w.contentVersion = s.version;

    if (s.pushPlacement) {  // config / settings window -> layer
      w.appPlacement = ipc::Placement::From(Pose::FromEulerDeg(c.position, c.rotation), c.width);
      w.appPlacementSerial = ++placementSerial_;
      s.pushPlacement = false;
    }
    if (w.layerPlacementSerial != s.seenLayerPlacement) {  // moved/resized in VR
      s.seenLayerPlacement = w.layerPlacementSerial;
      // also the start placement for the next game session
      w.appPlacement = w.layerPlacement;
      Pose p = w.layerPlacement.ToPose();
      s.cfg->position = p.t;
      s.cfg->rotation = p.EulerDeg();
      s.cfg->width = w.layerPlacement.width;
      if (s.internal) cfg_.menuWidth = s.cfg->width;
      MarkDirty();
    }
    if (w.clickSerial != s.seenClick) {
      s.seenClick = w.clickSerial;
      if (!s.internal && !mouse) {
        clickSound_.Play(cfg_.clickVolume);
        s.source->OnClick(w.clickX, w.clickY);
        MarkDirty();
      }
    }
    if (mouse && s.source) {  // pointer events
      s.source->SetPointer(w.pointerActive != 0, w.pointerX, w.pointerY);
      uint32_t downs = std::min(w.pointerDownSerial - s.seenDown, 8u);
      uint32_t ups = std::min(w.pointerUpSerial - s.seenUp, 8u);
      s.seenDown = w.pointerDownSerial;
      s.seenUp = w.pointerUpSerial;
      if (downs > 0) clickSound_.Play(cfg_.clickVolume);
      if (downs > 0 && !s.internal) {  // the keyboard follows the program window clicked last
        lastMouseSlot_ = static_cast<int>(i);
        if (keyboardSlot_ >= 0 && keyboardSlot_ != lastMouseSlot_) {
          StopKeyboard("other window clicked");
          StartKeyboard();
        }
      }
      for (uint32_t k = 0; k < std::max(downs, ups); ++k) {
        if (k < downs) s.source->AddButton(true);
        if (k < ups) s.source->AddButton(false);
      }
      float wheel = w.pointerWheel - s.seenWheel;
      s.seenWheel = w.pointerWheel;
      if (std::fabs(wheel) > 1e-3f) s.source->AddWheel(wheel);
      float wheelX = w.pointerWheelX - s.seenWheelX;
      s.seenWheelX = w.pointerWheelX;
      if (std::fabs(wheelX) > 1e-3f) s.source->AddWheelX(wheelX);
    }
  }
  for (size_t i = slots_.size(); i < ipc::kMaxWindows; ++i) st->windows[i].active = 0;

  bool connected = st->layerHeartbeat != 0 && now - st->layerHeartbeat < ipc::kHeartbeatTimeoutMs;
  if (connected != vrStatus_.connected) {
    if (connected)
      Log("VR connected: \"{}\" on runtime \"{}\"", st->layerApplication, st->layerRuntime);
    else
      Log("VR disconnected.");
    if (!connected) StopKeyboard("VR disconnected");
  }
  vrStatus_.connected = connected;
  vrStatus_.application = st->layerApplication;
  vrStatus_.runtime = st->layerRuntime;
  vrStatus_.controllers = st->layerControllers != 0;
  vrStatus_.handTracking = st->layerHandTracking != 0;
}

// ---------------------------------------------------------------------------
// Actions

void App::Do(const std::string& action, const std::string& window) {
  BindingConfig b;
  b.action = action;
  b.window = window;
  Execute(b);
}

void App::Execute(const BindingConfig& b) {
  auto forTargets = [&](auto&& fn) {
    for (auto& s : slots_)
      if (!s.internal && (b.window.empty() || s.cfg->name == b.window)) fn(s);
  };

  if (b.action == "recenter") {
    ++recenterSerial_;
    Log("Recenter.");
  } else if (b.action == "toggleAll") {
    allHidden_ = !allHidden_;
    for (auto& s : slots_) s.pending = true;
    Log("{}", allHidden_ ? "All windows hidden." : "All windows shown.");
  } else if (b.action == "toggleWindow") {
    forTargets([&](Slot& s) {
      s.cfg->visible = !s.cfg->visible;
      s.pending = true;
      Log("Window '{}' {}", s.cfg->name, s.cfg->visible ? "shown" : "hidden");
    });
    MarkDirty();
  } else if (b.action == "nextPage") {
    forTargets([](Slot& s) { s.source->NextPage(); });
    MarkDirty();
  } else if (b.action == "prevPage") {
    forTargets([](Slot& s) { s.source->PrevPage(); });
    MarkDirty();
  } else if (b.action == "toggleInteraction") {
    interactionEnabled_ = !interactionEnabled_;
    Log("{}", interactionEnabled_ ? "Interaction enabled." : "Interaction disabled (windows locked).");
  } else if (b.action == "toggleMenu") {
    menuCfg_.visible = !menuCfg_.visible;
    slots_.back().pending = true;
  } else if (b.action == "toggleKeyboard") {
    if (keyboardSlot_ >= 0)
      StopKeyboard("switched off");
    else
      StartKeyboard();
    clickSound_.Play(cfg_.clickVolume);
  } else if (b.action == "openSettings") {
    OpenSettings();
  } else {
    Log("Unknown action '{}'", b.action);
  }
}

bool App::MenuVisible() const { return menuCfg_.visible; }

// ---------------------------------------------------------------------------
// PC keyboard into a VR window. Elite keeps the focus; the keys are taken away from it by a
// low-level hook and posted to the program as WM_KEYDOWN / WM_CHAR / WM_KEYUP.

std::string App::KeyboardWindow() const {
  return keyboardSlot_ >= 0 && keyboardSlot_ < static_cast<int>(slots_.size())
             ? slots_[static_cast<size_t>(keyboardSlot_)].cfg->name
             : std::string{};
}

void App::StartKeyboard() {
  // target: the program window clicked last in VR, else the first visible clickable one
  auto usable = [&](int i) {
    if (i < 0 || i >= static_cast<int>(slots_.size())) return false;
    const Slot& s = slots_[static_cast<size_t>(i)];
    return !s.internal && s.mouse && s.cfg->visible && !allHidden_ && s.source && s.source->KeyboardTarget();
  };
  int slot = usable(lastMouseSlot_) ? lastMouseSlot_ : -1;
  for (int i = 0; slot < 0 && i < static_cast<int>(slots_.size()); ++i)
    if (usable(i)) slot = i;
  if (slot < 0) {
    Log("Keyboard: no visible program window with mouse input to type into");
    return;
  }
  HWND target = slots_[static_cast<size_t>(slot)].source->KeyboardTarget();
  DWORD pid = 0;
  GetWindowThreadProcessId(target, &pid);
  if (!keyboard_.Start(pid)) return;
  keyboardSlot_ = slot;
  keyTarget_ = target;
  std::memset(keyState_, 0, sizeof(keyState_));
  for (int vk : {VK_CAPITAL, VK_NUMLOCK, VK_SCROLL}) keyState_[vk] = GetKeyState(vk) & 1;
  Log("Keyboard: typing into '{}' (window 0x{:X})", slots_[static_cast<size_t>(slot)].cfg->name,
      reinterpret_cast<uintptr_t>(target));
}

void App::StopKeyboard(const char* reason) {
  if (keyboardSlot_ < 0) return;
  keyboard_.Stop();
  if (IsWindow(keyTarget_))  // release whatever is still held, so no modifier gets stuck in the program
    for (int vk = 1; vk < 256; ++vk)
      if ((keyState_[vk] & 0x80) && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU)
        ForwardKey({static_cast<UINT>(vk), MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC), false, false, 0});
  Log("Keyboard: back to the game ({})", reason);
  keyboardSlot_ = -1;
  keyTarget_ = nullptr;
}

void App::CheckKeyboard() {
  if (keyboardSlot_ < 0) return;
  const Slot& s = slots_[static_cast<size_t>(keyboardSlot_)];
  HWND target = s.cfg->visible && !allHidden_ && s.mouse && s.source ? s.source->KeyboardTarget() : nullptr;
  if (!target) {
    StopKeyboard("window hidden or closed");
    return;
  }
  keyTarget_ = target;  // follows the focus inside the program (e.g. a text field clicked in VR)
  DWORD pid = 0;
  GetWindowThreadProcessId(target, &pid);
  keyboard_.SetPassProcess(pid);
}

void App::ForwardKey(const KeyboardCapture::Event& e) {
  if (!IsWindow(keyTarget_) || e.vk == 0 || e.vk > 255) return;
  const bool wasDown = (keyState_[e.vk] & 0x80) != 0;
  if (!e.down && !wasDown) return;  // release of a key we never forwarded (e.g. a hotkey)
  if (e.down) {
    if (!wasDown && (e.vk == VK_CAPITAL || e.vk == VK_NUMLOCK || e.vk == VK_SCROLL)) keyState_[e.vk] ^= 1;
    keyState_[e.vk] |= 0x80;
  } else {
    keyState_[e.vk] &= 0x7F;
  }
  auto both = [&](int generic, int l, int r) { keyState_[generic] = (keyState_[l] | keyState_[r]) & 0x80; };
  both(VK_SHIFT, VK_LSHIFT, VK_RSHIFT);
  both(VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
  both(VK_MENU, VK_LMENU, VK_RMENU);

  const bool alt = (keyState_[VK_MENU] & 0x80) != 0, ctrl = (keyState_[VK_CONTROL] & 0x80) != 0;
  const bool sys = alt && !ctrl;  // Alt+key = system key; Ctrl+Alt is AltGr
  // programs expect the generic VK in WM_KEYDOWN (VK_SHIFT, not VK_LSHIFT)
  UINT vk = e.vk;
  if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
  if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
  if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
  LPARAM lp = 1 | (static_cast<LPARAM>(e.scan & 0xFF) << 16) | (e.extended ? (1 << 24) : 0) | (alt ? (1 << 29) : 0);
  if (e.down && wasDown) lp |= 1 << 30;  // auto repeat
  if (!e.down) lp |= (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);  // was down, going up
  const bool sysUp = !e.down && (sys || vk == VK_MENU);
  const UINT msg = e.down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sysUp ? WM_SYSKEYUP : WM_KEYUP);
  PostMessageW(keyTarget_, msg, vk, lp);

  if (e.down) {  // what TranslateMessage would do: characters with the program's keyboard layout
    wchar_t chars[8]{};
    HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(keyTarget_, nullptr));
    const int n = ToUnicodeEx(e.vk, e.scan, keyState_, chars, 8, 0, layout);
    for (int k = 0; k < n; ++k) PostMessageW(keyTarget_, sys ? WM_SYSCHAR : WM_CHAR, chars[k], lp);
  }
}

std::string App::WindowStatus(int window) const {
  if (window < 0 || window >= static_cast<int>(slots_.size()) || !slots_[static_cast<size_t>(window)].source) return {};
  const auto& s = slots_[static_cast<size_t>(window)];
  std::string st = s.source->Status();
  if (!s.cfg->visible) st += T(" (ausgeblendet)", " (hidden)");
  return st;
}

void App::OpenSettings() {
  if (settings_) settings_->Show();
}

// ---------------------------------------------------------------------------
// "Learn" a key combination / joystick button for the settings window

void App::StartKeyLearning() {
  StopLearning();
  keyLearning_ = true;
  learnedReady_ = learnedCancelled_ = false;
  hotkeys_.UnregisterAll();  // otherwise our own hotkeys would swallow the combination
  for (int vk = 1; vk < 256; ++vk) keysDownAtStart_[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void App::StartJoystickLearning() {
  StopLearning();
  joyLearning_ = true;
  learnedReady_ = learnedCancelled_ = false;
  JoystickManager::Press ignore;
  joysticks_.TakeLastPress(ignore);
  for (int vk = 1; vk < 256; ++vk) keysDownAtStart_[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void App::StopLearning() {
  bool wasKey = keyLearning_;
  keyLearning_ = joyLearning_ = false;
  if (wasKey) RegisterBindings();
}

bool App::TakeLearned(std::string& key, JoystickManager::Press& joy, bool& cancelled) {
  if (!learnedReady_) return false;
  key = learnedKey_;
  joy = learnedJoy_;
  cancelled = learnedCancelled_;
  learnedReady_ = learnedCancelled_ = false;
  return true;
}

void App::UpdateLearning() {
  if (!keyLearning_ && !joyLearning_) return;
  auto finish = [&](bool cancelled) {
    learnedCancelled_ = cancelled;
    learnedReady_ = true;
    StopLearning();
  };

  for (int vk = 1; vk < 256; ++vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (!down) {
      keysDownAtStart_[vk] = false;
      continue;
    }
    if (keysDownAtStart_[vk]) continue;  // was already held when learning started
    if (vk == VK_ESCAPE) {
      finish(true);
      return;
    }
    if (!keyLearning_) continue;
    switch (vk) {
      case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL:
      case VK_RCONTROL: case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN:
        continue;
    }
    UINT mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
    std::string spec = HotkeyManager::Format(mods, static_cast<UINT>(vk));
    if (spec.empty()) continue;  // unsupported key (mouse buttons, media keys, ...)
    learnedKey_ = spec;
    finish(false);
    return;
  }

  if (joyLearning_ && joysticks_.TakeLastPress(learnedJoy_)) finish(false);
}

// ---------------------------------------------------------------------------
// Tray icon

void App::CreateTrayIcon() {
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = kTrayMessage;
  nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(nid.szTip, T(L"ED Overlay Board (Doppelklick = Einstellungen)", L"ED Overlay Board (double-click = settings)"));
  trayCreated_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void App::SetLanguage(const std::string& language) {
  cfg_.language = language;
  i18n::Apply(language);
  if (trayCreated_) {  // tray tooltip
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    wcscpy_s(nid.szTip, T(L"ED Overlay Board (Doppelklick = Einstellungen)", L"ED Overlay Board (double-click = settings)"));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }
  ConfigChanged(false);
}

void App::ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, T(L"Einstellungen", L"Settings"));
  AppendMenuW(menu, MF_STRING, 2,
              MenuVisible() ? T(L"VR-Menü ausblenden", L"Hide VR menu") : T(L"VR-Menü einblenden", L"Show VR menu"));
  AppendMenuW(menu, MF_STRING, 3, T(L"Zentrieren", L"Recenter"));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 9, T(L"Beenden", L"Quit"));
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
  DestroyMenu(menu);
  switch (cmd) {
    case 1: OpenSettings(); break;
    case 2: Do("toggleMenu"); break;
    case 3: Do("recenter"); break;
    case 9: Quit(); break;
  }
}

LRESULT CALLBACK App::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && msg == kTrayMessage) {
    switch (LOWORD(lp)) {
      case WM_LBUTTONDBLCLK: self->OpenSettings(); break;
      case WM_RBUTTONUP:
      case WM_CONTEXTMENU: self->ShowTrayMenu(); break;
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------

void App::MarkDirty() {
  dirty_ = true;
  dirtySince_ = std::chrono::steady_clock::now();
}

void App::SaveIfIdle(bool force) {
  if (!dirty_) return;
  if (!force && std::chrono::steady_clock::now() - dirtySince_ < 1500ms) return;
  for (auto& s : slots_)
    if (!s.internal) s.cfg->page = s.source->Page();
  if (cfg_.Save()) Log("Settings saved.");
  dirty_ = false;
}
