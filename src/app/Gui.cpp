#include "Gui.h"

#include "App.h"
#include "LayerRegistration.h"
#include "Log.h"
#include "Util.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_stdlib.h>

#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

const ActionDef kActions[] = {
    {"recenter", "Zentrieren", false},
    {"toggleAll", "Alle Fenster ein/aus", false},
    {"toggleWindow", "Fenster ein/aus", true},
    {"nextPage", "Nächste Seite", true},
    {"prevPage", "Vorherige Seite", true},
    {"toggleInteraction", "Greifen sperren/entsperren", false},
    {"toggleMenu", "VR-Menü ein/aus", false},
    {"openSettings", "Einstellungen öffnen", false},
};
const int kActionCount = static_cast<int>(std::size(kActions));

namespace {

class ContextScope {  // makes `ctx` current for the lifetime of the object
 public:
  explicit ContextScope(ImGuiContext* ctx) : prev_(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(ctx); }
  ~ContextScope() { ImGui::SetCurrentContext(prev_); }

 private:
  ImGuiContext* prev_;
};

void LoadFont(float size) {
  ImGuiIO& io = ImGui::GetIO();
  wchar_t windir[MAX_PATH]{};
  GetWindowsDirectoryW(windir, MAX_PATH);
  auto font = std::filesystem::path(windir) / L"Fonts" / L"segoeui.ttf";
  if (std::filesystem::exists(font))
    io.Fonts->AddFontFromFileTTF(font.string().c_str(), size);
  else
    io.Fonts->AddFontDefault();
}

void ApplyStyle(float scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 0;
  s.FrameRounding = 4;
  s.GrabRounding = 4;
  s.FramePadding = {8, 5};
  s.ItemSpacing = {10, 7};
  ImVec4 orange{1.0f, 0.55f, 0.1f, 1.0f};
  s.Colors[ImGuiCol_CheckMark] = orange;
  s.Colors[ImGuiCol_SliderGrab] = orange;
  s.Colors[ImGuiCol_SliderGrabActive] = {1.0f, 0.7f, 0.3f, 1.0f};
  s.Colors[ImGuiCol_Header] = {1.0f, 0.55f, 0.1f, 0.35f};
  s.Colors[ImGuiCol_HeaderHovered] = {1.0f, 0.55f, 0.1f, 0.55f};
  s.Colors[ImGuiCol_HeaderActive] = {1.0f, 0.55f, 0.1f, 0.75f};
  s.Colors[ImGuiCol_Button] = {1.0f, 0.55f, 0.1f, 0.30f};
  s.Colors[ImGuiCol_ButtonHovered] = {1.0f, 0.55f, 0.1f, 0.60f};
  s.Colors[ImGuiCol_ButtonActive] = {1.0f, 0.55f, 0.1f, 0.85f};
  s.Colors[ImGuiCol_Tab] = {1.0f, 0.55f, 0.1f, 0.25f};
  s.Colors[ImGuiCol_TabHovered] = {1.0f, 0.55f, 0.1f, 0.60f};
  s.Colors[ImGuiCol_TabSelected] = {1.0f, 0.55f, 0.1f, 0.50f};
  s.Colors[ImGuiCol_FrameBg] = {0.20f, 0.20f, 0.24f, 1.0f};
  s.Colors[ImGuiCol_WindowBg] = {0.07f, 0.07f, 0.09f, 1.0f};
  s.ScaleAllSizes(scale);
}

void OpenFolder(const std::filesystem::path& p) {
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

int ActionIndex(const std::string& id) {
  for (int i = 0; i < kActionCount; ++i)
    if (id == kActions[i].id) return i;
  return 0;
}

constexpr ImGuiWindowFlags kFullscreen = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

}  // namespace

// =====================================================================================
// Settings window

SettingsWindow::SettingsWindow(App& app) : app_(app) {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"EDOverlayBoardSettings";
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"ED Overlay Board – Einstellungen", WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr, wc.hInstance, nullptr);
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  scale_ = GetDpiForWindow(hwnd_) / 96.0f;
  SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(1100 * scale_), static_cast<int>(760 * scale_),
               SWP_NOMOVE | SWP_NOZORDER);

  winrt::com_ptr<IDXGIDevice> dxgiDevice;
  winrt::com_ptr<IDXGIAdapter> adapter;
  winrt::com_ptr<IDXGIFactory2> factory;
  if (SUCCEEDED(app_.Graphics().Device()->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void())) &&
      SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())))
    adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void());

  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (factory) factory->CreateSwapChainForHwnd(app_.Graphics().Device(), hwnd_, &sd, nullptr, nullptr, swapchain_.put());
  CreateRenderTarget();

  IMGUI_CHECKVERSION();
  ImGuiContext* prev = ImGui::GetCurrentContext();
  ctx_ = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx_);
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ApplyStyle(scale_);
  LoadFont(17.0f * scale_);
  ImGui_ImplWin32_Init(hwnd_);
  ImGui_ImplDX11_Init(app_.Graphics().Device(), app_.Graphics().Context());
  ImGui::SetCurrentContext(prev);
}

SettingsWindow::~SettingsWindow() {
  if (ctx_) {
    ContextScope scope(ctx_);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(ctx_);
  }
  if (hwnd_) DestroyWindow(hwnd_);
}

void SettingsWindow::CreateRenderTarget() {
  rtv_ = nullptr;
  if (!swapchain_) return;
  winrt::com_ptr<ID3D11Texture2D> back;
  swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void());
  if (back) app_.Graphics().Device()->CreateRenderTargetView(back.get(), nullptr, rtv_.put());
}

void SettingsWindow::Show() {
  ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(hwnd_);
}

bool SettingsWindow::Visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_); }

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->ctx_) {
    ContextScope scope(self->ctx_);
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return TRUE;
  }
  switch (msg) {
    case WM_SIZE:
      if (self && wp != SIZE_MINIMIZED) {
        self->resizeW_ = LOWORD(lp);
        self->resizeH_ = HIWORD(lp);
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      mmi->ptMinTrackSize = {700, 450};
      return 0;
    }
    case WM_DPICHANGED: {
      auto* r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
      return 0;
    }
    case WM_CLOSE:
      ShowWindow(hwnd, SW_HIDE);  // keep running in the tray
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void SettingsWindow::Frame() {
  if (!Visible() || !swapchain_) return;
  if (resizeW_ && resizeH_) {
    rtv_ = nullptr;
    swapchain_->ResizeBuffers(0, resizeW_, resizeH_, DXGI_FORMAT_UNKNOWN, 0);
    resizeW_ = resizeH_ = 0;
    CreateRenderTarget();
  }
  ContextScope scope(ctx_);
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  Draw();
  ImGui::Render();
  auto* ctx = app_.Graphics().Context();
  const float clear[4] = {0.07f, 0.07f, 0.09f, 1.0f};
  ID3D11RenderTargetView* rtv = rtv_.get();
  ctx->OMSetRenderTargets(1, &rtv, nullptr);
  ctx->ClearRenderTargetView(rtv, clear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  ctx->OMSetRenderTargets(0, nullptr, nullptr);
  swapchain_->Present(0, 0);
}

void SettingsWindow::Draw() {
  // finish a pending "learn key/button"
  std::string key;
  JoystickManager::Press joy;
  bool cancelled = false;
  if (learningBinding_ >= 0 && app_.TakeLearned(key, joy, cancelled)) {
    auto& binds = app_.Config().bindings;
    if (!cancelled && learningBinding_ < static_cast<int>(binds.size())) {
      auto& b = binds[static_cast<size_t>(learningBinding_)];
      if (learningJoystick_) {
        b.joystick = joy.device;
        b.button = joy.button;
      } else {
        b.key = key;
      }
      app_.ConfigChanged(false);
    }
    learningBinding_ = -1;
  }

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##settings", nullptr, kFullscreen);

  const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
  ImGui::BeginChild("##tabs", {0, -footer});
  if (ImGui::BeginTabBar("##tabbar")) {
    if (ImGui::BeginTabItem("Fenster")) {
      DrawWindowsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Tasten")) {
      DrawBindingsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Allgemein")) {
      DrawGeneralTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Status")) {
      DrawStatusTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button(app_.MenuVisible() ? "VR-Menü ausblenden" : "VR-Menü einblenden")) app_.Do("toggleMenu");
  ImGui::SameLine();
  if (ImGui::Button("Zentrieren")) app_.Do("recenter");
  ImGui::SameLine();
  ImGui::TextDisabled("Änderungen wirken sofort und werden automatisch gespeichert.");
  ImGui::End();
}

void SettingsWindow::DrawWindowsTab() {
  auto& cfg = app_.Config();
  const float s = scale_;

  ImGui::BeginChild("##left", {230 * s, 0});
  ImGui::BeginChild("##list", {0, -(ImGui::GetFrameHeightWithSpacing() * 2 + 4)}, ImGuiChildFlags_Borders);
  for (int i = 0; i < static_cast<int>(cfg.windows.size()); ++i) {
    ImGui::PushID(i);
    const auto& w = cfg.windows[static_cast<size_t>(i)];
    std::string label = (w.visible ? "" : "(aus) ") + w.name;
    if (ImGui::Selectable(label.c_str(), selected_ == i)) selected_ = i;
    ImGui::PopID();
  }
  ImGui::EndChild();
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (ImGui::Button("Neu", {half, 0}) && cfg.windows.size() < ipc::kMaxWindows - 1) {
    WindowConfig w;
    w.name = "Fenster " + std::to_string(cfg.windows.size() + 1);
    w.position = {0.0f, -0.1f, -0.7f};
    cfg.windows.push_back(w);
    selected_ = static_cast<int>(cfg.windows.size()) - 1;
    app_.ConfigChanged(true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Löschen", {half, 0}) && selected_ < static_cast<int>(cfg.windows.size())) {
    cfg.windows.erase(cfg.windows.begin() + selected_);
    selected_ = std::max(0, selected_ - 1);
    app_.ConfigChanged(true);
  }
  if (ImGui::Button("Hoch", {half, 0}) && selected_ > 0) {
    std::swap(cfg.windows[static_cast<size_t>(selected_)], cfg.windows[static_cast<size_t>(selected_ - 1)]);
    --selected_;
    app_.ConfigChanged(true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Runter", {half, 0}) && selected_ + 1 < static_cast<int>(cfg.windows.size())) {
    std::swap(cfg.windows[static_cast<size_t>(selected_)], cfg.windows[static_cast<size_t>(selected_ + 1)]);
    ++selected_;
    app_.ConfigChanged(true);
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##props", {0, 0});
  if (selected_ >= static_cast<int>(cfg.windows.size())) {
    ImGui::TextDisabled("Kein Fenster ausgewählt. Mit \"Neu\" ein Fenster anlegen.");
    ImGui::EndChild();
    return;
  }
  const int i = selected_;
  WindowConfig& w = cfg.windows[static_cast<size_t>(i)];
  ImGui::PushItemWidth(-260 * s);

  ImGui::SeparatorText("Inhalt");
  ImGui::InputText("Name", &w.name);
  if (ImGui::IsItemActivated()) nameBeforeEdit_ = w.name;
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    for (auto& b : cfg.bindings)
      if (b.window == nameBeforeEdit_) b.window = w.name;
    app_.ConfigChanged(false);
  }

  int type = w.type == "images" ? 1 : 0;
  if (ImGui::Combo("Typ", &type, "Programmfenster spiegeln\0Bilder (Kneeboard)\0")) {
    w.type = type == 1 ? "images" : "window";
    if (type == 1 && w.folder.empty()) w.folder = "kneeboard";
    app_.ConfigChanged(false);
  }

  if (w.type == "window") {
    ImGui::InputText("Programm (exe)", &w.processName);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SameLine();
    if (ImGui::Button("Auswählen...")) {
      windowList_ = WindowCaptureSource::ListWindows();
      ImGui::OpenPopup("##pick");
    }
    if (ImGui::BeginPopup("##pick")) {
      ImGui::TextDisabled("Laufende Programme (nicht minimiert):");
      if (windowList_.empty()) ImGui::Text("keine gefunden");
      for (size_t k = 0; k < windowList_.size(); ++k) {
        const auto& info = windowList_[k];
        std::string label = info.process + "   -   " + info.title + "##" + std::to_string(k);
        if (ImGui::Selectable(label.c_str())) {
          w.processName = info.process;
          w.titleContains.clear();
          if (w.name.rfind("Fenster ", 0) == 0) w.name = std::filesystem::path(info.process).stem().string();
          app_.ConfigChanged(false);
        }
      }
      ImGui::EndPopup();
    }
    ImGui::InputText("Fenstertitel enthält", &w.titleContains);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SetItemTooltip("Optional. Nötig, wenn das Programm mehrere Fenster hat.");
    if (ImGui::Checkbox("Titelleiste und Rahmen ausblenden", &w.hideTitleBar)) app_.ConfigChanged(false);
    ImGui::InputInt4("Ränder abschneiden (px)", w.trim);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SetItemTooltip("links, oben, rechts, unten - z. B. für selbst gezeichnete Titelleisten oder Menüleisten");
    if (ImGui::Checkbox("Mauszeiger mit aufnehmen", &w.captureCursor)) app_.ConfigChanged(false);
    if (ImGui::Checkbox("Im VR klickbar", &w.forwardClicks)) app_.ConfigChanged(false);
    ImGui::SetItemTooltip(
        "Trigger (Controller) bzw. kurzer Pinch (Hand-Tracking) klickt in das Programm.\n"
        "Elite behält den Fokus, die Maus bewegt sich nicht.");
  } else {
    ImGui::InputText("Bilderordner", &w.folder);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SameLine();
    if (ImGui::Button("Öffnen")) OpenFolder(cfg.Dir() / std::filesystem::path(Utf8ToWide(w.folder)));
    ImGui::TextDisabled("PNG/JPG/... ablegen. Im VR: rechte/linke Hälfte anklicken = blättern.");
  }

  ImGui::SeparatorText("Darstellung");
  if (ImGui::Checkbox("Sichtbar", &w.visible)) app_.ConfigChanged(false);
  ImGui::SameLine(0, 30 * s);
  if (ImGui::Checkbox("Gesperrt (nicht verschiebbar)", &w.locked)) app_.ConfigChanged(false);
  int look = w.lookToShow + 1;
  if (ImGui::Combo("Blickabhängig", &look, "Wie unter \"Allgemein\"\0Immer sichtbar\0Nur beim Hinsehen\0")) {
    w.lookToShow = look - 1;
    app_.ConfigChanged(false);
  }
  if (ImGui::SliderFloat("Deckkraft", &w.opacity, 0.05f, 1.0f, "%.2f")) app_.ConfigChanged(false);
  ImGui::SetItemTooltip("Nur wirksam, wenn die OpenXR-Runtime das unterstützt (VDXR derzeit nicht).");

  ImGui::SeparatorText("Position (relativ zur Zentrierung)");
  bool placed = false;
  placed |= ImGui::SliderFloat("Breite (m)", &w.width, 0.05f, 3.0f, "%.2f");
  placed |= ImGui::DragFloat3("Position (m)", &w.position.x, 0.005f, -5.0f, 5.0f, "%.3f");
  ImGui::SetItemTooltip("x = rechts, y = oben, z = hinten (negativ = vor dir)");
  placed |= ImGui::DragFloat3("Drehung (Grad)", &w.rotation.x, 0.5f, -180.0f, 180.0f, "%.1f");
  ImGui::SetItemTooltip("Neigung, Drehung, Rollen");
  if (ImGui::Button("Vor mich setzen")) {
    w.position = {0.0f, -0.1f, -0.7f};
    w.rotation = {0.0f, 0.0f, 0.0f};
    placed = true;
  }
  if (placed) app_.PlacementEdited(i);

  ImGui::SeparatorText("Status");
  ImGui::TextWrapped("%s", app_.WindowStatus(i).c_str());
  ImGui::PopItemWidth();
  ImGui::EndChild();
}

void SettingsWindow::DrawBindingsTab() {
  auto& cfg = app_.Config();
  ImGui::TextWrapped(
      "Tastenkombinationen gelten systemweit, auch wenn Elite im Vordergrund ist. Joystick-Tasten werden nicht "
      "exklusiv gelesen: dieselbe Taste kann in Elite z. B. auf \"HMD zurücksetzen\" liegen.");
  ImGui::Spacing();

  int remove = -1;
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##bindings", 5, flags)) {
    ImGui::TableSetupColumn("Aktion", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Fenster", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Tastatur", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Joystick", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70 * scale_);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(cfg.bindings.size()); ++i) {
      auto& b = cfg.bindings[static_cast<size_t>(i)];
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      int a = ActionIndex(b.action);
      if (ImGui::BeginCombo("##action", kActions[a].label)) {
        for (int k = 0; k < kActionCount; ++k)
          if (ImGui::Selectable(kActions[k].label, k == a)) {
            b.action = kActions[k].id;
            if (!kActions[k].needsWindow) b.window.clear();
            app_.ConfigChanged(false);
          }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      if (kActions[ActionIndex(b.action)].needsWindow) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##window", b.window.empty() ? "(alle)" : b.window.c_str())) {
          if (ImGui::Selectable("(alle)", b.window.empty())) {
            b.window.clear();
            app_.ConfigChanged(false);
          }
          for (const auto& w : cfg.windows)
            if (ImGui::Selectable(w.name.c_str(), w.name == b.window)) {
              b.window = w.name;
              app_.ConfigChanged(false);
            }
          ImGui::EndCombo();
        }
      } else {
        ImGui::TextDisabled("-");
      }

      const float clearW = ImGui::GetFrameHeight();
      ImGui::TableNextColumn();
      {
        bool learning = learningBinding_ == i && !learningJoystick_;
        std::string label = learning ? "Taste drücken... (Esc)" : (b.key.empty() ? "(keine)" : b.key);
        if (ImGui::Button((label + "##key").c_str(), {ImGui::GetContentRegionAvail().x - clearW - 6, 0}) &&
            !app_.Learning()) {
          learningBinding_ = i;
          learningJoystick_ = false;
          app_.StartKeyLearning();
        }
        ImGui::SetItemTooltip("Klicken und dann die Tastenkombination drücken");
        ImGui::SameLine(0, 6);
        if (ImGui::Button("x##keyclr", {clearW, 0})) {
          b.key.clear();
          app_.ConfigChanged(false);
        }
      }

      ImGui::TableNextColumn();
      {
        bool learning = learningBinding_ == i && learningJoystick_;
        std::string label = learning ? "Joystick-Taste drücken... (Esc)"
                                     : (b.button > 0 ? (b.joystick.empty() ? "beliebig" : b.joystick) + "  Taste " +
                                                           std::to_string(b.button)
                                                     : "(keine)");
        if (ImGui::Button((label + "##joy").c_str(), {ImGui::GetContentRegionAvail().x - clearW - 6, 0}) &&
            !app_.Learning()) {
          learningBinding_ = i;
          learningJoystick_ = true;
          app_.StartJoystickLearning();
        }
        ImGui::SetItemTooltip("Klicken und dann die Taste am Joystick/HOTAS drücken");
        ImGui::SameLine(0, 6);
        if (ImGui::Button("x##joyclr", {clearW, 0})) {
          b.button = 0;
          b.joystick.clear();
          app_.ConfigChanged(false);
        }
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("Löschen")) remove = i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (remove >= 0) {
    if (learningBinding_ == remove) {
      app_.StopLearning();
      learningBinding_ = -1;
    }
    cfg.bindings.erase(cfg.bindings.begin() + remove);
    app_.ConfigChanged(false);
  }
  if (ImGui::Button("Neue Belegung")) {
    cfg.bindings.push_back({"recenter", "", "", "", 0});
    app_.ConfigChanged(false);
  }
  if (app_.Learning()) {
    ImGui::SameLine();
    if (ImGui::Button("Lernen abbrechen")) {
      app_.StopLearning();
      learningBinding_ = -1;
    }
  }
}

void SettingsWindow::DrawGeneralTab() {
  auto& cfg = app_.Config();
  ImGui::PushItemWidth(320 * scale_);
  bool c = false;
  ImGui::SeparatorText("Anzeige");
  c |= ImGui::Checkbox("Fenster nur beim Hinsehen anzeigen (und bedienbar)", &cfg.lookToShow);
  c |= ImGui::SliderFloat("Blick-Rand (Grad)", &cfg.lookMarginDeg, 0.0f, 45.0f, "%.0f");
  ImGui::SetItemTooltip("Größer = Fenster erscheinen früher, wenn man in ihre Richtung schaut");
  c |= ImGui::SliderFloat("Schärfe", &cfg.sharpness, 0.5f, 2.5f, "%.2f");
  ImGui::SetItemTooltip("1.0 = ruhiges Bild ohne Flimmern, 1.5-2.0 = schärfer, kann flimmern");
  c |= ImGui::Checkbox("Zeiger-Punkt anzeigen", &cfg.showCursor);
  ImGui::SeparatorText("Bedienung");
  c |= ImGui::Checkbox("Vibration (Controller)", &cfg.haptics);
  c |= ImGui::SliderFloat("Größe ändern per Stick (Tempo)", &cfg.scaleSpeed, 0.2f, 3.0f, "%.2f");
  c |= ImGui::SliderFloat("Abstand ändern per Stick (m/s)", &cfg.pushPullSpeed, 0.1f, 2.0f, "%.2f");
  c |= ImGui::SliderFloat("Breite VR-Menü (m)", &cfg.menuWidth, 0.2f, 1.0f, "%.2f");
  if (c) app_.ConfigChanged(false);
  ImGui::PopItemWidth();

  ImGui::SeparatorText("OpenXR-Layer");
  bool registered = LayerRegistration::IsRegistered();
  ImGui::Text("Status: %s", registered ? "registriert (wird in OpenXR-Spiele geladen)" : "nicht registriert");
  if (!registered && ImGui::Button("Layer registrieren")) LayerRegistration::Register();
  if (registered && ImGui::Button("Layer entfernen")) LayerRegistration::Unregister();

  ImGui::SeparatorText("Dateien");
  ImGui::TextUnformatted(cfg.file.string().c_str());
  if (ImGui::Button("Konfigurationsordner öffnen")) OpenFolder(cfg.Dir());
  ImGui::SameLine();
  wchar_t local[MAX_PATH]{};
  GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  if (ImGui::Button("Layer-Logs öffnen")) OpenFolder(std::filesystem::path(local) / L"EDOverlayBoard");
}

void SettingsWindow::DrawStatusTab() {
  const auto& st = app_.Status();
  ImGui::SeparatorText("VR");
  if (st.connected) {
    ImGui::Text("Verbunden mit \"%s\"", st.application.c_str());
    ImGui::Text("Runtime: %s", st.runtime.c_str());
    ImGui::Text("Controller: %s    Handtracking: %s", st.controllers ? "ja" : "nein", st.handTracking ? "ja" : "nein");
  } else {
    ImGui::TextDisabled("Kein Spiel verbunden. Starte Elite Dangerous im VR-Modus.");
  }
  ImGui::Text("Greifen: %s    Alle Fenster: %s", app_.InteractionEnabled() ? "erlaubt" : "gesperrt",
              app_.AllHidden() ? "ausgeblendet" : "eingeblendet");

  ImGui::SeparatorText("Fenster");
  const auto& cfg = app_.Config();
  for (int i = 0; i < static_cast<int>(cfg.windows.size()); ++i)
    ImGui::BulletText("%s: %s", cfg.windows[static_cast<size_t>(i)].name.c_str(), app_.WindowStatus(i).c_str());

  ImGui::SeparatorText("Joysticks");
  auto joys = app_.JoystickNames();
  if (joys.empty()) ImGui::TextDisabled("keine gefunden");
  for (const auto& j : joys) ImGui::BulletText("%s", j.c_str());
}

// =====================================================================================
// VR menu

VrMenu::VrMenu(App& app) : app_(app) {
  auto& d3d = app_.Graphics();
  texture_ = d3d.CreateTexture(kWidth, kHeight, nullptr, true);
  if (texture_) d3d.Device()->CreateRenderTargetView(texture_.get(), nullptr, rtv_.put());

  ImGuiContext* prev = ImGui::GetCurrentContext();
  ctx_ = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx_);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {static_cast<float>(kWidth), static_cast<float>(kHeight)};
  io.MouseDrawCursor = false;
  ApplyStyle(2.0f);
  ImGui::GetStyle().FramePadding = {18, 12};
  ImGui::GetStyle().ItemSpacing = {18, 14};
  LoadFont(34.0f);
  ImGui_ImplDX11_Init(d3d.Device(), d3d.Context());
  ImGui::SetCurrentContext(prev);
}

VrMenu::~VrMenu() {
  if (ctx_) {
    ContextScope scope(ctx_);
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext(ctx_);
  }
}

void VrMenu::SetPointer(bool active, float x, float y) {
  ContextScope scope(ctx_);
  if (active)
    ImGui::GetIO().AddMousePosEvent(x * kWidth, y * kHeight);
  else if (pointerWasActive_)
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  if (active || pointerWasActive_) inputPending_ = true;
  pointerWasActive_ = active;
}

void VrMenu::AddButton(bool down) {
  ContextScope scope(ctx_);
  ImGui::GetIO().AddMouseButtonEvent(0, down);
  inputPending_ = true;
}

void VrMenu::AddWheel(float steps) {
  ContextScope scope(ctx_);
  ImGui::GetIO().AddMouseWheelEvent(0.0f, steps);
  inputPending_ = true;
}

ID3D11Texture2D* VrMenu::Update(bool& changed) {
  changed = false;
  if (!texture_ || !rtv_) return texture_.get();
  auto now = std::chrono::steady_clock::now();
  const bool first = lastRender_.time_since_epoch().count() == 0;
  if (!first && !app_.MenuVisible()) return texture_.get();
  float dt = first ? 1.0f / 30 : std::chrono::duration<float>(now - lastRender_).count();
  if (!first && !inputPending_ && dt < 0.1f) return texture_.get();  // idle: 10 fps is enough
  if (!first && dt < 1.0f / 45) return texture_.get();
  lastRender_ = now;
  inputPending_ = false;

  ContextScope scope(ctx_);
  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = std::clamp(dt, 0.001f, 0.25f);
  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();
  Draw();
  ImGui::Render();
  auto* ctx = app_.Graphics().Context();
  const float clear[4] = {0.07f, 0.07f, 0.09f, 1.0f};
  ID3D11RenderTargetView* rtv = rtv_.get();
  ctx->OMSetRenderTargets(1, &rtv, nullptr);
  ctx->ClearRenderTargetView(rtv, clear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  ctx->OMSetRenderTargets(0, nullptr, nullptr);
  changed = true;
  return texture_.get();
}

void VrMenu::Draw() {
  auto& cfg = app_.Config();
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(size);
  ImGui::Begin("##vrmenu", nullptr, kFullscreen | ImGuiWindowFlags_NoScrollWithMouse);

  // header = grab zone for hand tracking
  const float header = size.y * kHeaderFraction;
  auto* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled({0, 0}, {size.x, header}, IM_COL32(255, 140, 25, 255));
  const char* title = "ED Overlay Board   -   hier oder am Rand greifen";
  ImVec2 ts = ImGui::CalcTextSize(title);
  dl->AddText({(size.x - ts.x) * 0.5f, (header - ts.y) * 0.5f}, IM_COL32(20, 20, 24, 255), title);
  ImGui::SetCursorPosY(header + ImGui::GetStyle().ItemSpacing.y);

  ImGui::SeparatorText("Fenster");
  for (int i = 0; i < static_cast<int>(cfg.windows.size()); ++i) {
    auto& w = cfg.windows[static_cast<size_t>(i)];
    ImGui::PushID(i);
    bool v = w.visible;
    if (ImGui::Checkbox(w.name.c_str(), &v)) app_.Do("toggleWindow", w.name);
    ImGui::SameLine(size.x * 0.48f);
    bool movable = !w.locked;
    if (ImGui::Checkbox("verschiebbar", &movable)) {
      w.locked = !movable;
      app_.ConfigChanged(false);
    }
    if (w.type == "window") {
      ImGui::SameLine(size.x * 0.78f);
      if (ImGui::Checkbox("klickbar", &w.forwardClicks)) app_.ConfigChanged(false);
    } else if (w.type == "images") {
      ImGui::SameLine(size.x * 0.78f);
      if (ImGui::Button(" < ")) app_.Do("prevPage", w.name);
      ImGui::SameLine();
      if (ImGui::Button(" > ")) app_.Do("nextPage", w.name);
    }
    ImGui::PopID();
  }

  ImGui::SeparatorText("Aktionen");
  const float third = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
  if (ImGui::Button("Zentrieren", {third, 0})) app_.Do("recenter");
  ImGui::SameLine();
  if (ImGui::Button(app_.AllHidden() ? "Alle ein" : "Alle aus", {third, 0})) app_.Do("toggleAll");
  ImGui::SameLine();
  if (ImGui::Button(app_.InteractionEnabled() ? "Sperren" : "Entsperren", {third, 0})) app_.Do("toggleInteraction");

  ImGui::SeparatorText("Anzeige");
  if (ImGui::Checkbox("Nur beim Hinsehen anzeigen", &cfg.lookToShow)) app_.ConfigChanged(false);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderFloat("##sharp", &cfg.sharpness, 0.5f, 2.5f, "Schärfe %.2f")) app_.ConfigChanged(false);

  ImGui::Spacing();
  const float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  if (ImGui::Button("Einstellungen am PC", {halfW, 0})) app_.OpenSettings();
  ImGui::SameLine();
  if (ImGui::Button("Menü schließen", {halfW, 0})) app_.Do("toggleMenu");
  ImGui::End();
}
