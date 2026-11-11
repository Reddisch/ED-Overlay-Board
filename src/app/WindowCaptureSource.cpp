#include "WindowCaptureSource.h"

#include "Lang.h"
#include "Log.h"
#include "Util.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <cmath>

namespace wgc = winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using namespace std::chrono_literals;

namespace {

std::wstring ProcessFileName(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return {};
  wchar_t buf[MAX_PATH * 2]{};
  DWORD size = static_cast<DWORD>(std::size(buf));
  std::wstring result;
  if (QueryFullProcessImageNameW(proc, 0, buf, &size)) result = std::filesystem::path(buf).filename().wstring();
  CloseHandle(proc);
  return result;
}

bool IsCandidateWindow(HWND hwnd) {
  if (!IsWindowVisible(hwnd) || hwnd == GetConsoleWindow()) return false;
  BOOL cloaked = FALSE;
  DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  if (cloaked) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  return pid != GetCurrentProcessId();
}

}  // namespace

std::vector<WindowCaptureSource::WindowInfo> WindowCaptureSource::ListWindows() {
  std::vector<WindowInfo> out;
  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        auto& v = *reinterpret_cast<std::vector<WindowInfo>*>(lp);
        if (!IsCandidateWindow(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        wchar_t title[512]{};
        GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
        if (title[0] == 0) return TRUE;
        v.push_back({WideToUtf8(title), WideToUtf8(ProcessFileName(hwnd))});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  return out;
}

std::string WindowCaptureSource::Status() const {
  if (title_.empty() && process_.empty()) return T("kein Programm/Titel eingestellt", "no program/title set");
  if (session_ && hasFrame_) return T("erfasst \"", "capturing \"") + capturedTitle_ + "\"";
  if (session_)
    return T("verbunden mit \"", "connected to \"") + capturedTitle_ +
           T("\", wartet auf Bild (minimiert?)", "\", waiting for a picture (minimized?)");
  return T("wartet auf Fenster", "waiting for the window");
}

WindowCaptureSource::WindowCaptureSource(D3D& d3d, const WindowConfig& cfg)
    : d3d_(d3d),
      name_(Utf8ToWide(cfg.name)),
      title_(Utf8ToWide(cfg.titleContains)),
      process_(Utf8ToWide(cfg.processName)),
      hideTitleBar_(cfg.hideTitleBar),
      hasCrop_(cfg.hasCrop),
      captureCursor_(cfg.captureCursor) {
  std::copy(std::begin(cfg.crop), std::end(cfg.crop), crop_);
  std::copy(std::begin(cfg.trim), std::end(cfg.trim), trim_);
  if (title_.empty() && process_.empty())
    Log("Window '{}': neither 'titleContains' nor 'processName' is set - nothing to capture", cfg.name);
}

WindowCaptureSource::~WindowCaptureSource() { StopCapture(); }

HWND WindowCaptureSource::FindTarget() const {
  if (title_.empty() && process_.empty()) return nullptr;
  struct Ctx {
    const WindowCaptureSource* self;
    HWND found;
  } ctx{this, nullptr};

  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(lp);
        if (!IsCandidateWindow(hwnd)) return TRUE;
        wchar_t title[512]{};
        GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
        if (title[0] == 0) return TRUE;
        if (!c.self->title_.empty() && !ContainsI(title, c.self->title_)) return TRUE;
        if (!c.self->process_.empty() && !ContainsI(ProcessFileName(hwnd), c.self->process_)) return TRUE;
        c.found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

bool WindowCaptureSource::StartCapture(HWND hwnd) {
  try {
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::check_hresult(
        interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item_)));

    poolSize_ = item_.Size();
    pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(d3d_.WinRTDevice(),
                                                                DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize_);
    session_ = pool_.CreateCaptureSession(item_);
    try {
      session_.IsCursorCaptureEnabled(captureCursor_);
    } catch (...) {
    }
    try {
      session_.IsBorderRequired(false);  // Windows 11: no yellow capture border
    } catch (...) {
    }
    session_.StartCapture();
    hwnd_ = hwnd;
    hasFrame_ = false;

    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    capturedTitle_ = WideToUtf8(title);
    Log("Window '{}': capturing \"{}\" ({}x{})", WideToUtf8(name_), capturedTitle_, poolSize_.Width,
        poolSize_.Height);
    return true;
  } catch (const winrt::hresult_error& e) {
    Log("Window '{}': capture failed: {}", WideToUtf8(name_), WideToUtf8(e.message()));
    StopCapture();
    return false;
  }
}

void WindowCaptureSource::StopCapture() {
  try {
    if (session_) session_.Close();
    if (pool_) pool_.Close();
  } catch (...) {
  }
  session_ = nullptr;
  pool_ = nullptr;
  item_ = nullptr;
  hwnd_ = nullptr;
  hasFrame_ = false;
  pointerHwnd_ = pressHwnd_ = nullptr;
  pointerIn_ = pressed_ = dragging_ = false;
}

ID3D11Texture2D* WindowCaptureSource::Placeholder(bool& changed) {
  if (!placeholder_ || placeholderEnglish_ != i18n::English()) {
    placeholderEnglish_ = i18n::English();
    placeholderShown_ = false;
    std::wstring what = !process_.empty() ? process_ : (L"\"" + title_ + L"\"");
    placeholder_ = d3d_.CreateTextTexture(
        name_, T(L"Warte auf Fenster: ", L"Waiting for window: ") + what +
                   T(L"\n\nProgramm starten und darauf achten, dass das Fenster nicht minimiert ist. "
                     L"Zum Verschieben am Rand oder mit dem Grip-Knopf greifen.",
                     L"\n\nStart the program and make sure the window is not minimized. "
                     L"Grab this panel at its border or with the grip button to move it."));
  }
  if (!placeholderShown_) {
    placeholderShown_ = true;
    changed = true;
  }
  return placeholder_.get();
}

// Which part of the captured frame to show: optionally only the client area (no title bar),
// then trim margins, then the legacy crop rectangle.
void WindowCaptureSource::ComputeRegion(int cw, int ch, int& x0, int& y0, int& w, int& h) const {
  x0 = 0;
  y0 = 0;
  w = cw;
  h = ch;
  if (hideTitleBar_ && hwnd_) {
    // The capture covers the DWM frame bounds; the client area is offset inside it.
    RECT frame{};
    RECT client{};
    POINT origin{0, 0};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
        GetClientRect(hwnd_, &client) && ClientToScreen(hwnd_, &origin) && client.right > 0 && client.bottom > 0) {
      x0 = std::clamp(static_cast<int>(origin.x - frame.left), 0, cw - 1);
      y0 = std::clamp(static_cast<int>(origin.y - frame.top), 0, ch - 1);
      w = std::min(static_cast<int>(client.right), cw - x0);
      h = std::min(static_cast<int>(client.bottom), ch - y0);
    }
  }
  // trim: left, top, right, bottom
  int l = std::max(0, trim_[0]), t = std::max(0, trim_[1]), r = std::max(0, trim_[2]), b = std::max(0, trim_[3]);
  if (l + r < w - 8) {
    x0 += l;
    w -= l + r;
  }
  if (t + b < h - 8) {
    y0 += t;
    h -= t + b;
  }
  if (hasCrop_) {
    int cx = std::clamp(crop_[0], 0, std::max(0, w - 1)), cy = std::clamp(crop_[1], 0, std::max(0, h - 1));
    x0 += cx;
    y0 += cy;
    w = std::min(crop_[2], w - cx);
    h = std::min(crop_[3], h - cy);
  }
}

// ---------------------------------------------------------------------------
// Mouse input from VR. Sent as posted window messages: the game keeps the focus and the real
// mouse cursor does not move. Works with Qt (EDCoPilot), Win32 and most WPF/WinForms programs.

bool WindowCaptureSource::ToScreen(float u, float v, POINT& screen) const {
  if (!hwnd_ || !hasFrame_ || !IsWindow(hwnd_) || region_[2] <= 0 || region_[3] <= 0) return false;
  // The capture covers the DWM frame bounds (physical pixels; this process is per-monitor DPI aware).
  RECT frame{};
  if (FAILED(DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) return false;
  screen = {frame.left + region_[0] + static_cast<LONG>(std::clamp(u, 0.0f, 1.0f) * (region_[2] - 1)),
            frame.top + region_[1] + static_cast<LONG>(std::clamp(v, 0.0f, 1.0f) * (region_[3] - 1))};
  return true;
}

HWND WindowCaptureSource::ChildAt(POINT screen) const {
  // deepest visible child window at that point (Qt/WPF UIs usually have just the top-level one)
  HWND target = hwnd_;
  for (int depth = 0; depth < 16; ++depth) {
    POINT pt = screen;
    ScreenToClient(target, &pt);
    HWND child = ChildWindowFromPointEx(target, pt, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    if (!child || child == target) break;
    target = child;
  }
  return target;
}

POINT WindowCaptureSource::ToClient(HWND target, POINT screen) {
  POINT client = screen;
  ScreenToClient(target, &client);  // physical pixels
  // DPI-unaware / system-aware targets expect their own (scaled) coordinates
  UINT windowDpi = GetDpiForWindow(target);
  UINT monitorDpi = 96, dummy = 96;
  if (HMONITOR mon = MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST))
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &monitorDpi, &dummy);
  if (windowDpi && monitorDpi && windowDpi != monitorDpi) {
    client.x = MulDiv(client.x, static_cast<int>(windowDpi), static_cast<int>(monitorDpi));
    client.y = MulDiv(client.y, static_cast<int>(windowDpi), static_cast<int>(monitorDpi));
  }
  return client;
}

bool WindowCaptureSource::Post(HWND target, UINT msg, WPARAM wp, POINT client) {
  const LPARAM pos = MAKELPARAM(static_cast<int16_t>(client.x), static_cast<int16_t>(client.y));
  if (PostMessageW(target, msg, wp, pos)) return true;
  if (!postErrorLogged_)
    Log("Window '{}': mouse input could not be sent (error {}) - is the program running as administrator?",
        WideToUtf8(name_), GetLastError());
  postErrorLogged_ = true;
  return false;
}

// Programs that poll the real cursor (Qt, Tk, ...) get confused when a press and its release
// arrive far apart while the real mouse is somewhere else. So a tap is sent as one burst
// (move, down, up) on release, which every toolkit accepts as a click. When the pointer moves
// clearly while held it becomes either a swipe (scrolls the content like a touch screen, sent as
// mouse wheel messages) or - after holding still for a moment, or with swiping switched off - a
// real drag (down ... moves ... up) for sliders, scroll bars and text selection.
namespace {
constexpr auto kLongPress = 450ms;          // hold still this long before moving = mouse drag
constexpr float kSwipePixelsPerNotch = 60;  // at 96 dpi and speed 1: one wheel notch per 60 px
}  // namespace

void WindowCaptureSource::SetPointer(bool active, float x, float y) {
  POINT screen{};
  if (!active || !ToScreen(x, y, screen)) {
    pointerIn_ = false;
    return;
  }
  HWND target = dragging_ && IsWindow(pressHwnd_) ? pressHwnd_ : ChildAt(screen);
  const POINT client = ToClient(target, screen);
  const bool moved = client.x != pointerClient_.x || client.y != pointerClient_.y || target != pointerHwnd_;
  pointerHwnd_ = target;
  pointerClient_ = client;
  pointerScreen_ = screen;
  pointerIn_ = true;

  if (swiping_) {
    SwipeTo(screen);
    return;
  }
  if (pressed_ && !dragging_) {
    const LONG dx = screen.x - pressScreen_.x, dy = screen.y - pressScreen_.y;
    const LONG threshold = std::max<LONG>(8, region_[2] / 50);  // ~2% of the width: absorbs pinch jitter
    if (dx * dx + dy * dy > threshold * threshold && IsWindow(pressHwnd_)) {
      if (swipeEnabled_ && std::chrono::steady_clock::now() - pressTime_ < kLongPress) {
        swiping_ = true;
        swipeLast_ = pressScreen_;  // the content follows the pointer from where it was pressed
        swipeLastTime_ = std::chrono::steady_clock::now();
        swipeVel_[0] = swipeVel_[1] = 0.0f;
        Log("Window '{}': swipe from ({}, {})", WideToUtf8(name_), pressClient_.x, pressClient_.y);
        SwipeTo(screen);
        return;
      }
      dragging_ = true;
      Post(pressHwnd_, WM_MOUSEMOVE, 0, pressClient_);
      Post(pressHwnd_, WM_LBUTTONDOWN, MK_LBUTTON, pressClient_);
      Log("Window '{}': drag from ({}, {})", WideToUtf8(name_), pressClient_.x, pressClient_.y);
      pointerHwnd_ = pressHwnd_;
      pointerClient_ = ToClient(pressHwnd_, screen);
      Post(pressHwnd_, WM_MOUSEMOVE, MK_LBUTTON, pointerClient_);
      return;
    }
  }
  if (dragging_ && moved) Post(pressHwnd_, WM_MOUSEMOVE, MK_LBUTTON, client);
}

// Pointer moved while swiping: scroll the content under the press point by the same distance.
void WindowCaptureSource::SwipeTo(POINT screen) {
  if (!IsWindow(pressHwnd_)) return;
  const auto now = std::chrono::steady_clock::now();
  const UINT dpi = std::max(96u, GetDpiForWindow(pressHwnd_));
  const float unitsPerPixel = WHEEL_DELTA * std::max(swipeSpeed_, 0.05f) / (kSwipePixelsPerNotch * dpi / 96.0f);
  // pointer down = content down = scroll up (positive wheel); pointer right = scroll left (negative)
  const float ux = -static_cast<float>(screen.x - swipeLast_.x) * unitsPerPixel;
  const float uy = static_cast<float>(screen.y - swipeLast_.y) * unitsPerPixel;
  swipeLast_ = screen;
  if (ux != 0.0f) PostWheel(pressHwnd_, pressScreen_, ux, true);
  if (uy != 0.0f) PostWheel(pressHwnd_, pressScreen_, uy, false);

  const float dt = std::max(0.005f, std::chrono::duration<float>(now - swipeLastTime_).count());
  swipeLastTime_ = now;
  swipeVel_[0] = swipeVel_[0] * 0.6f + (ux / dt) * 0.4f;
  swipeVel_[1] = swipeVel_[1] * 0.6f + (uy / dt) * 0.4f;
}

// After a swipe the content keeps scrolling for a moment and slows down (like on a phone).
void WindowCaptureSource::UpdateFling() {
  if (!flinging_) return;
  const auto now = std::chrono::steady_clock::now();
  const float dt = std::min(0.1f, std::chrono::duration<float>(now - flingLastTime_).count());
  flingLastTime_ = now;
  if (!IsWindow(flingHwnd_) || pressed_) {
    flinging_ = false;
    return;
  }
  for (int axis = 0; axis < 2; ++axis) {
    if (swipeVel_[axis] != 0.0f) PostWheel(flingHwnd_, flingScreen_, swipeVel_[axis] * dt, axis == 0);
    swipeVel_[axis] *= std::exp(-3.0f * dt);
  }
  if (std::hypot(swipeVel_[0], swipeVel_[1]) < 40.0f) flinging_ = false;
}

void WindowCaptureSource::AddButton(bool down) {
  if (down) {
    flinging_ = false;  // touching the content stops a fling
    if (pressed_ || !pointerIn_ || !IsWindow(pointerHwnd_)) return;
    pressed_ = true;
    dragging_ = swiping_ = false;
    pressHwnd_ = pointerHwnd_;
    pressClient_ = pointerClient_;
    pressScreen_ = pointerScreen_;
    pressTime_ = std::chrono::steady_clock::now();
    return;
  }
  if (!pressed_) return;
  pressed_ = false;
  if (!IsWindow(pressHwnd_)) {
    dragging_ = swiping_ = false;
    return;
  }
  if (swiping_) {
    swiping_ = false;
    const bool recent = std::chrono::steady_clock::now() - swipeLastTime_ < 100ms;
    if (recent && std::hypot(swipeVel_[0], swipeVel_[1]) > 300.0f) {
      flinging_ = true;
      flingHwnd_ = pressHwnd_;
      flingScreen_ = pressScreen_;
      flingLastTime_ = std::chrono::steady_clock::now();
    }
    return;
  }
  if (dragging_) {
    dragging_ = false;
    Post(pressHwnd_, WM_LBUTTONUP, 0, pointerHwnd_ == pressHwnd_ ? pointerClient_ : pressClient_);
    Log("Window '{}': drag to ({}, {})", WideToUtf8(name_), pointerClient_.x, pointerClient_.y);
    return;
  }
  const bool ok = Post(pressHwnd_, WM_MOUSEMOVE, 0, pressClient_) &&
                  Post(pressHwnd_, WM_LBUTTONDOWN, MK_LBUTTON, pressClient_) &&
                  Post(pressHwnd_, WM_LBUTTONUP, 0, pressClient_);
  if (ok)
    Log("Window '{}': click at ({}, {}) in window 0x{:X}", WideToUtf8(name_), pressClient_.x, pressClient_.y,
        reinterpret_cast<uintptr_t>(pressHwnd_));
}

void WindowCaptureSource::PostWheel(HWND target, POINT screen, float units, bool horizontal) {
  float& accum = horizontal ? wheelAccumX_ : wheelAccum_;
  accum += units;
  const int delta = static_cast<int>(accum);
  if (delta == 0) return;
  accum -= static_cast<float>(delta);
  // the wheel messages carry screen coordinates (in the target's DPI view)
  POINT pt = screen;
  PhysicalToLogicalPointForPerMonitorDPI(target, &pt);
  const WPARAM wp = MAKEWPARAM(dragging_ ? MK_LBUTTON : 0, static_cast<WORD>(static_cast<int16_t>(delta)));
  PostMessageW(target, horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL, wp,
               MAKELPARAM(static_cast<int16_t>(pt.x), static_cast<int16_t>(pt.y)));
  const auto now = std::chrono::steady_clock::now();
  if (now - lastWheelLog_ > std::chrono::seconds(1)) {
    lastWheelLog_ = now;
    Log("Window '{}': mouse wheel{} ({} units) in window 0x{:X}", WideToUtf8(name_), horizontal ? " (horizontal)" : "",
        delta, reinterpret_cast<uintptr_t>(target));
  }
}

void WindowCaptureSource::AddWheel(float steps) {
  if (!pointerIn_ || !IsWindow(pointerHwnd_)) return;
  flinging_ = false;
  PostWheel(pointerHwnd_, pointerScreen_, steps * WHEEL_DELTA, false);
}

void WindowCaptureSource::AddWheelX(float steps) {
  if (!pointerIn_ || !IsWindow(pointerHwnd_)) return;
  flinging_ = false;
  PostWheel(pointerHwnd_, pointerScreen_, steps * WHEEL_DELTA, true);
}

// PC keyboard mode: the window that has the keyboard focus inside the program (even while it is
// in the background), else the part that was clicked last in VR, else the main window.
HWND WindowCaptureSource::KeyboardTarget() const {
  if (!hwnd_ || !IsWindow(hwnd_)) return nullptr;
  auto inside = [&](HWND h) { return h && IsWindow(h) && (h == hwnd_ || IsChild(hwnd_, h)); };
  GUITHREADINFO gi{sizeof(gi)};
  if (GetGUIThreadInfo(GetWindowThreadProcessId(hwnd_, nullptr), &gi) && inside(gi.hwndFocus)) return gi.hwndFocus;
  if (inside(pressHwnd_)) return pressHwnd_;
  return hwnd_;
}

ID3D11Texture2D* WindowCaptureSource::Update(bool& changed) {
  changed = false;
  UpdateFling();

  if (session_ && !IsWindow(hwnd_)) {
    Log("Window '{}': target window closed", WideToUtf8(name_));
    StopCapture();
  }

  if (!session_) {
    auto now = std::chrono::steady_clock::now();
    if (now >= nextSearch_) {
      nextSearch_ = now + 2s;
      if (HWND h = FindTarget()) StartCapture(h);
    }
  }

  if (session_) {
    try {
      wgc::Direct3D11CaptureFrame frame{nullptr};
      while (auto next = pool_.TryGetNextFrame()) frame = next;  // only the newest frame matters

      if (frame) {
        auto size = frame.ContentSize();
        auto access =
            frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> src;
        winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), src.put_void()));
        D3D11_TEXTURE2D_DESC sd{};
        src->GetDesc(&sd);

        int cw = std::min<int>(size.Width, static_cast<int>(sd.Width));
        int ch = std::min<int>(size.Height, static_cast<int>(sd.Height));
        int x0 = 0, y0 = 0, w = cw, h = ch;
        ComputeRegion(cw, ch, x0, y0, w, h);
        region_[0] = x0;
        region_[1] = y0;
        region_[2] = w;
        region_[3] = h;

        if (w > 0 && h > 0) {
          D3D11_TEXTURE2D_DESC td{};
          if (texture_) texture_->GetDesc(&td);
          if (!texture_ || td.Width != static_cast<UINT>(w) || td.Height != static_cast<UINT>(h))
            texture_ = d3d_.CreateTexture(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
          if (texture_) {
            D3D11_BOX box{static_cast<UINT>(x0), static_cast<UINT>(y0), 0, static_cast<UINT>(x0 + w),
                          static_cast<UINT>(y0 + h), 1};
            d3d_.Context()->CopySubresourceRegion(texture_.get(), 0, 0, 0, 0, src.get(), 0, &box);
            hasFrame_ = true;
            placeholderShown_ = false;
            changed = true;
          }
        }

        if (size.Width != poolSize_.Width || size.Height != poolSize_.Height) {
          poolSize_ = size;
          pool_.Recreate(d3d_.WinRTDevice(), DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize_);
        }
      }
    } catch (const winrt::hresult_error& e) {
      Log("Window '{}': capture error: {}", WideToUtf8(name_), WideToUtf8(e.message()));
      StopCapture();
    }
  }

  if (hasFrame_ && texture_) return texture_.get();
  return Placeholder(changed);
}
