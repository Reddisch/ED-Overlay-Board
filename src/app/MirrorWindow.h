#pragma once

#include "D3D.h"

#include <dxgi1_2.h>
#include <windows.h>

#include <cstdint>
#include <string>

// Desktop window showing the spectator mirror rendered by the layer in the game: the game's
// picture with the overlay windows on top. Meant to be captured by OBS ("Window Capture").
// The swapchain has the size of the mirror picture; Windows stretches it to the window, whose
// aspect ratio is kept.
class MirrorWindow {
 public:
  explicit MirrorWindow(D3D& d3d);
  ~MirrorWindow();
  MirrorWindow(const MirrorWindow&) = delete;
  MirrorWindow& operator=(const MirrorWindow&) = delete;

  struct Source {
    bool connected = false;  // game with the layer running
    uint32_t pid = 0, generation = 0, width = 0, height = 0, frame = 0;
  };
  void Update(bool enabled, const Source& src);
  bool TakeClosed();  // true once after the user closed the window
  bool Showing() const { return showing_; }  // a picture from the game is on screen

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  void Create();
  bool EnsureSwapchain(uint32_t w, uint32_t h);
  void ShowBlank();

  D3D& d3d_;
  HWND hwnd_ = nullptr;
  winrt::com_ptr<IDXGISwapChain1> swapchain_;
  uint32_t bufW_ = 0, bufH_ = 0;
  float aspect_ = 16.0f / 9.0f;

  winrt::com_ptr<ID3D11Texture2D> shared_;
  winrt::com_ptr<IDXGIKeyedMutex> mutex_;
  uint32_t openedPid_ = 0, openedGen_ = 0;
  uint32_t taken_ = 0;
  bool showing_ = false, blank_ = false;
  bool closed_ = false;
  bool visible_ = false;
  uint64_t lastOpenTry_ = 0;
};
