#pragma once

#include "ContentSource.h"
#include "D3D.h"
#include "WindowCaptureSource.h"

#include <dxgi1_2.h>
#include <windows.h>

#include <chrono>
#include <string>
#include <vector>

struct ImGuiContext;
class App;

struct ActionDef {
  const char* id;
  const char* label;
  bool needsWindow;
};
extern const ActionDef kActions[];
extern const int kActionCount;

// Desktop settings window (Dear ImGui on D3D11). Closing it only hides it.
class SettingsWindow {
 public:
  explicit SettingsWindow(App& app);
  ~SettingsWindow();
  void Show();
  bool Visible() const;
  void Frame();  // renders one frame when visible

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  void CreateRenderTarget();
  void Draw();
  void DrawWindowsTab();
  void DrawBindingsTab();
  void DrawGeneralTab();
  void DrawStatusTab();

  App& app_;
  HWND hwnd_ = nullptr;
  winrt::com_ptr<IDXGISwapChain1> swapchain_;
  winrt::com_ptr<ID3D11RenderTargetView> rtv_;
  ImGuiContext* ctx_ = nullptr;
  float scale_ = 1.0f;
  UINT resizeW_ = 0, resizeH_ = 0;

  int selected_ = 0;
  int learningBinding_ = -1;
  bool learningJoystick_ = false;
  std::string nameBeforeEdit_;
  std::vector<WindowCaptureSource::WindowInfo> windowList_;
};

// VR quick menu: rendered off-screen with Dear ImGui and shown in VR like any other window.
// The layer sends pointer/click/wheel events for it through shared memory.
class VrMenu : public ContentSource {
 public:
  static constexpr uint32_t kWidth = 1000;
  static constexpr uint32_t kHeight = 820;
  static constexpr float kHeaderFraction = 0.10f;  // hand tracking grabs here instead of clicking

  explicit VrMenu(App& app);
  ~VrMenu() override;

  ID3D11Texture2D* Update(bool& changed) override;
  std::string Status() const override { return "VR-Menü"; }

  void SetPointer(bool active, float x, float y);
  void AddButton(bool down);
  void AddWheel(float steps);

 private:
  void Draw();

  App& app_;
  ImGuiContext* ctx_ = nullptr;
  winrt::com_ptr<ID3D11Texture2D> texture_;
  winrt::com_ptr<ID3D11RenderTargetView> rtv_;
  std::chrono::steady_clock::time_point lastRender_{};
  bool inputPending_ = true;
  bool pointerWasActive_ = false;
};
