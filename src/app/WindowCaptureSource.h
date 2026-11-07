#pragma once

#include "Config.h"
#include "ContentSource.h"
#include "D3D.h"

#include <winrt/Windows.Graphics.Capture.h>

#include <chrono>
#include <string>
#include <vector>

// Mirrors a desktop window (EDCoPilot, EDDiscovery, EDMC, a browser, ...) using
// Windows.Graphics.Capture. The window may be behind the game; it must not be minimized.
class WindowCaptureSource : public ContentSource {
 public:
  struct WindowInfo {
    std::string title;
    std::string process;  // exe file name
  };
  static std::vector<WindowInfo> ListWindows();  // visible top-level windows of other programs

  WindowCaptureSource(D3D& d3d, const WindowConfig& cfg);
  ~WindowCaptureSource() override;

  ID3D11Texture2D* Update(bool& changed) override;
  std::string Status() const override;
  // Mouse input from VR (trigger / pinch = left button, thumbstick = wheel) -> the captured
  // program, as posted window messages: the game keeps the focus, the real cursor does not move.
  void SetPointer(bool active, float x, float y) override;
  void AddButton(bool down) override;
  void AddWheel(float steps) override;

 private:
  HWND FindTarget() const;
  bool StartCapture(HWND hwnd);
  void StopCapture();
  ID3D11Texture2D* Placeholder(bool& changed);
  void ComputeRegion(int contentW, int contentH, int& x0, int& y0, int& w, int& h) const;
  bool ToScreen(float u, float v, POINT& screen) const;  // u, v: 0..1 in the shown region
  HWND ChildAt(POINT screen) const;
  static POINT ToClient(HWND target, POINT screen);
  bool Post(HWND target, UINT msg, WPARAM wp, POINT client);

  D3D& d3d_;
  std::wstring name_, title_, process_;
  bool hideTitleBar_ = false;
  int trim_[4]{};
  bool hasCrop_ = false;
  int crop_[4]{};
  bool captureCursor_ = false;

  HWND hwnd_ = nullptr;
  std::string capturedTitle_;
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
  winrt::Windows::Graphics::SizeInt32 poolSize_{};

  winrt::com_ptr<ID3D11Texture2D> texture_;      // our private copy
  winrt::com_ptr<ID3D11Texture2D> placeholder_;  // "waiting for window" panel
  bool hasFrame_ = false;
  bool placeholderShown_ = false;
  bool placeholderEnglish_ = false;
  int region_[4]{};  // part of the captured frame that is shown: x, y, width, height (frame pixels)

  // mouse input state
  HWND pointerHwnd_ = nullptr;  // (child) window under the VR pointer
  POINT pointerClient_{}, pointerScreen_{};
  bool pointerIn_ = false;
  bool pressed_ = false;   // trigger / pinch held
  bool dragging_ = false;  // moved while held: real drag (button down sent)
  HWND pressHwnd_ = nullptr;
  POINT pressClient_{}, pressScreen_{};
  float wheelAccum_ = 0.0f;  // in WHEEL_DELTA units
  std::chrono::steady_clock::time_point lastWheelLog_{};
  bool postErrorLogged_ = false;
  std::chrono::steady_clock::time_point nextSearch_{};
};
