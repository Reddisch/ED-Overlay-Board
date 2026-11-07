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
  // Trigger / pinch on the overlay -> left click into the captured window (posted messages:
  // the game keeps the focus and the real mouse cursor does not move).
  void OnClick(float x, float y) override;

 private:
  HWND FindTarget() const;
  bool StartCapture(HWND hwnd);
  void StopCapture();
  ID3D11Texture2D* Placeholder(bool& changed);
  void ComputeRegion(int contentW, int contentH, int& x0, int& y0, int& w, int& h) const;

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
  int region_[4]{};  // part of the captured frame that is shown: x, y, width, height (frame pixels)
  std::chrono::steady_clock::time_point nextSearch_{};
};
