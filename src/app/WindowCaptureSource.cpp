#include "WindowCaptureSource.h"

#include "Log.h"
#include "Util.h"

#include <dwmapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>

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
  if (title_.empty() && process_.empty()) return "kein Programm/Titel eingestellt";
  if (session_ && hasFrame_) return "erfasst \"" + capturedTitle_ + "\"";
  if (session_) return "verbunden mit \"" + capturedTitle_ + "\", wartet auf Bild (minimiert?)";
  return "wartet auf Fenster";
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
}

ID3D11Texture2D* WindowCaptureSource::Placeholder(bool& changed) {
  if (!placeholder_) {
    std::wstring what = !process_.empty() ? process_ : (L"\"" + title_ + L"\"");
    placeholder_ = d3d_.CreateTextTexture(
        name_, L"Waiting for window: " + what +
                   L"\n\nStart the program and make sure the window is not minimized. "
                   L"Grab this panel with the grip button to move it.");
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

ID3D11Texture2D* WindowCaptureSource::Update(bool& changed) {
  changed = false;

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
