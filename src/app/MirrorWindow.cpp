#include "MirrorWindow.h"

#include "Ipc.h"
#include "Log.h"

#include <d3d11_1.h>

#include <algorithm>

namespace {
constexpr wchar_t kClass[] = L"EDOverlayBoardMirror";
// fixed title (OBS remembers the window by title, class and program)
constexpr wchar_t kTitle[] = L"ED Overlay Board - Mirror";
}  // namespace

MirrorWindow::MirrorWindow(D3D& d3d) : d3d_(d3d) {}

MirrorWindow::~MirrorWindow() {
  swapchain_ = nullptr;
  if (hwnd_) DestroyWindow(hwnd_);
}

void MirrorWindow::Create() {
  if (hwnd_) return;
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  // client area 1280 x 720 physical pixels
  RECT r{0, 0, 1280, 720};
  AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
  hwnd_ = CreateWindowExW(0, kClass, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                          r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

bool MirrorWindow::EnsureSwapchain(uint32_t w, uint32_t h) {
  if (swapchain_ && bufW_ == w && bufH_ == h) return true;
  if (swapchain_) {
    if (FAILED(swapchain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) swapchain_ = nullptr;
  }
  if (!swapchain_) {
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::com_ptr<IDXGIFactory2> factory;
    if (SUCCEEDED(d3d_.Device()->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void())) &&
        SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())))
      adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void());
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // same as the mirror texture: plain copy
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!factory || FAILED(factory->CreateSwapChainForHwnd(d3d_.Device(), hwnd_, &sd, nullptr, nullptr, swapchain_.put()))) {
      Log("Mirror window: cannot create swapchain {}x{}", w, h);
      swapchain_ = nullptr;
      return false;
    }
  }
  bufW_ = w;
  bufH_ = h;
  return true;
}

void MirrorWindow::ShowBlank() {  // no picture from the game: dark grey
  if (blank_ || !EnsureSwapchain(bufW_ ? bufW_ : 1280, bufH_ ? bufH_ : 720)) return;
  winrt::com_ptr<ID3D11Texture2D> back;
  winrt::com_ptr<ID3D11RenderTargetView> rtv;
  swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void());
  if (back && SUCCEEDED(d3d_.Device()->CreateRenderTargetView(back.get(), nullptr, rtv.put()))) {
    const float clear[4] = {0.07f, 0.07f, 0.09f, 1.0f};
    d3d_.Context()->ClearRenderTargetView(rtv.get(), clear);
  }
  swapchain_->Present(0, 0);
  blank_ = true;
  showing_ = false;
}

void MirrorWindow::Update(bool enabled, const Source& src) {
  if (!enabled) {
    if (visible_) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
    shared_ = nullptr;
    mutex_ = nullptr;
    openedGen_ = openedPid_ = 0;
    showing_ = false;
    blank_ = false;
    return;
  }
  Create();
  if (!visible_) {
    visible_ = true;
    closed_ = false;
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    blank_ = false;
  }

  if (!src.connected || src.generation == 0) {
    shared_ = nullptr;
    mutex_ = nullptr;
    openedGen_ = openedPid_ = 0;
    ShowBlank();
    return;
  }

  if (src.generation != openedGen_ || src.pid != openedPid_) {
    const uint64_t now = GetTickCount64();
    if (now - lastOpenTry_ < 500) return;
    lastOpenTry_ = now;
    shared_ = nullptr;
    mutex_ = nullptr;
    winrt::com_ptr<ID3D11Device1> device1;
    HRESULT hr = d3d_.Device()->QueryInterface(__uuidof(ID3D11Device1), device1.put_void());
    if (SUCCEEDED(hr))
      hr = device1->OpenSharedResourceByName(ipc::MirrorTextureName(src.pid, src.generation).c_str(),
                                             DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                             __uuidof(ID3D11Texture2D), shared_.put_void());
    if (SUCCEEDED(hr)) mutex_ = shared_.try_as<IDXGIKeyedMutex>();
    if (FAILED(hr) || !mutex_) {
      Log("Mirror window: cannot open the mirror texture of the game (0x{:08X}) - game on a different GPU?",
          static_cast<uint32_t>(hr));
      shared_ = nullptr;
      mutex_ = nullptr;
      openedGen_ = src.generation;  // do not retry this one every tick
      openedPid_ = src.pid;
      ShowBlank();
      return;
    }
    openedGen_ = src.generation;
    openedPid_ = src.pid;
    D3D11_TEXTURE2D_DESC d{};
    shared_->GetDesc(&d);
    aspect_ = static_cast<float>(d.Width) / static_cast<float>(std::max(1u, d.Height));
    Log("Mirror window: showing {}x{} from the game", d.Width, d.Height);
  }
  if (!shared_ || src.frame == taken_) return;

  D3D11_TEXTURE2D_DESC d{};
  shared_->GetDesc(&d);
  if (!EnsureSwapchain(d.Width, d.Height)) return;
  if (mutex_->AcquireSync(0, 0) != S_OK) return;  // the layer is rendering: next tick
  winrt::com_ptr<ID3D11Texture2D> back;
  swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void());
  if (back) d3d_.Context()->CopyResource(back.get(), shared_.get());
  mutex_->ReleaseSync(0);
  d3d_.Context()->Flush();
  swapchain_->Present(0, 0);
  taken_ = src.frame;
  showing_ = true;
  blank_ = false;
}

bool MirrorWindow::TakeClosed() {
  if (!closed_) return false;
  closed_ = false;
  return true;
}

LRESULT CALLBACK MirrorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_SIZING: {  // keep the aspect ratio of the picture
      if (!self) break;
      auto* r = reinterpret_cast<RECT*>(lp);
      RECT frame{0, 0, 0, 0};
      AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);
      const int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
      const int cw = std::max(160, static_cast<int>(r->right - r->left) - fw);
      const int ch = std::max(90, static_cast<int>(r->bottom - r->top) - fh);
      if (wp == WMSZ_TOP || wp == WMSZ_BOTTOM) {
        const int w = static_cast<int>(ch * self->aspect_ + 0.5f) + fw;
        r->right = r->left + w;
      } else {
        const int h = static_cast<int>(cw / self->aspect_ + 0.5f) + fh;
        if (wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT || wp == WMSZ_TOP)
          r->top = r->bottom - h;
        else
          r->bottom = r->top + h;
      }
      return TRUE;
    }
    case WM_ERASEBKGND:
      return 1;  // the swapchain covers everything
    case WM_CLOSE:
      if (self) {
        ShowWindow(hwnd, SW_HIDE);
        self->visible_ = false;
        self->closed_ = true;
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
