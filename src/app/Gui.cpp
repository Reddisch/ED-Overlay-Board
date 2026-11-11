#include "Gui.h"

#include "App.h"
#include "Lang.h"
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
    {"recenter", "Zentrieren", "Recenter", false},
    {"toggleAll", "Alle Fenster ein/aus", "All windows on/off", false},
    {"toggleWindow", "Fenster ein/aus", "Window on/off", true},
    {"nextPage", "Nächste Seite", "Next page", true},
    {"prevPage", "Vorherige Seite", "Previous page", true},
    {"toggleInteraction", "Greifen sperren/entsperren", "Lock/unlock grabbing", false},
    {"toggleMenu", "VR-Menü ein/aus", "VR menu on/off", false},
    {"toggleKeyboard", "PC-Tastatur ins VR-Fenster ein/aus", "PC keyboard into VR window on/off", false},
    {"openSettings", "Einstellungen öffnen", "Open settings", false},
};
const int kActionCount = static_cast<int>(std::size(kActions));
const char* ActionDef::Label() const { return T(labelDe, labelEn); }

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

// Slider Deutsch <-> English. Returns true when the language was changed.
bool LanguageSlider(App& app, const char* id, float width) {
  int lang = i18n::English() ? 1 : 0;
  ImGui::SetNextItemWidth(width);
  if (!ImGui::SliderInt(id, &lang, 0, 1, lang ? "Language: English" : "Sprache: Deutsch")) return false;
  if ((lang == 1) == i18n::English()) return false;
  app.SetLanguage(lang ? "en" : "de");
  return true;
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
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"EDOverlayBoardSettings";
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"ED Overlay Board", WS_OVERLAPPEDWINDOW,
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
  if (titleLanguage_ != static_cast<int>(i18n::English())) {
    titleLanguage_ = static_cast<int>(i18n::English());
    SetWindowTextW(hwnd_, T(L"ED Overlay Board – Einstellungen", L"ED Overlay Board – Settings"));
  }
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
        b.buttons = joy.buttons;
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
    if (ImGui::BeginTabItem(T("Fenster###windows", "Windows###windows"))) {
      DrawWindowsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("Tasten###bindings", "Bindings###bindings"))) {
      DrawBindingsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("Allgemein###general", "General###general"))) {
      DrawGeneralTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Status###status")) {
      DrawStatusTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button(app_.MenuVisible() ? T("VR-Menü ausblenden", "Hide VR menu") : T("VR-Menü einblenden", "Show VR menu")))
    app_.Do("toggleMenu");
  ImGui::SameLine();
  if (ImGui::Button(T("Zentrieren", "Recenter"))) app_.Do("recenter");
  ImGui::SameLine();
  ImGui::TextDisabled("%s", T("Änderungen wirken sofort und werden automatisch gespeichert.",
                              "Changes apply immediately and are saved automatically."));
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
    std::string label = (w.visible ? "" : T("(aus) ", "(off) ")) + w.name;
    if (ImGui::Selectable(label.c_str(), selected_ == i)) selected_ = i;
    ImGui::PopID();
  }
  ImGui::EndChild();
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (ImGui::Button(T("Neu", "New"), {half, 0}) && cfg.windows.size() < ipc::kMaxWindows - 1) {
    WindowConfig w;
    w.name = T("Fenster ", "Window ") + std::to_string(cfg.windows.size() + 1);
    w.position = {0.0f, -0.1f, -0.7f};
    cfg.windows.push_back(w);
    selected_ = static_cast<int>(cfg.windows.size()) - 1;
    app_.ConfigChanged(true);
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Löschen", "Delete"), {half, 0}) && selected_ < static_cast<int>(cfg.windows.size())) {
    cfg.windows.erase(cfg.windows.begin() + selected_);
    selected_ = std::max(0, selected_ - 1);
    app_.ConfigChanged(true);
  }
  if (ImGui::Button(T("Hoch", "Up"), {half, 0}) && selected_ > 0) {
    std::swap(cfg.windows[static_cast<size_t>(selected_)], cfg.windows[static_cast<size_t>(selected_ - 1)]);
    --selected_;
    app_.ConfigChanged(true);
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Runter", "Down"), {half, 0}) && selected_ + 1 < static_cast<int>(cfg.windows.size())) {
    std::swap(cfg.windows[static_cast<size_t>(selected_)], cfg.windows[static_cast<size_t>(selected_ + 1)]);
    ++selected_;
    app_.ConfigChanged(true);
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##props", {0, 0});
  if (selected_ >= static_cast<int>(cfg.windows.size())) {
    ImGui::TextDisabled("%s", T("Kein Fenster ausgewählt. Mit \"Neu\" ein Fenster anlegen.",
                                "No window selected. Use \"New\" to add one."));
    ImGui::EndChild();
    return;
  }
  const int i = selected_;
  WindowConfig& w = cfg.windows[static_cast<size_t>(i)];
  ImGui::PushItemWidth(-260 * s);

  ImGui::SeparatorText(T("Inhalt", "Content"));
  ImGui::InputText("Name", &w.name);
  if (ImGui::IsItemActivated()) nameBeforeEdit_ = w.name;
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    for (auto& b : cfg.bindings)
      if (b.window == nameBeforeEdit_) b.window = w.name;
    app_.ConfigChanged(false);
  }

  int type = w.type == "images" ? 1 : 0;
  if (ImGui::Combo(T("Typ###type", "Type###type"), &type,
                   T("Programmfenster spiegeln\0Bilder (Kneeboard)\0", "Mirror a program window\0Images (kneeboard)\0"))) {
    w.type = type == 1 ? "images" : "window";
    if (type == 1 && w.folder.empty()) w.folder = "klemmbrett";
    app_.ConfigChanged(false);
  }

  if (w.type == "window") {
    ImGui::InputText(T("Programm (exe)###exe", "Program (exe)###exe"), &w.processName);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SameLine();
    if (ImGui::Button(T("Auswählen...", "Choose..."))) {
      windowList_ = WindowCaptureSource::ListWindows();
      ImGui::OpenPopup("##pick");
    }
    if (ImGui::BeginPopup("##pick")) {
      ImGui::TextDisabled("%s", T("Laufende Programme (nicht minimiert):", "Running programs (not minimized):"));
      if (windowList_.empty()) ImGui::TextUnformatted(T("keine gefunden", "none found"));
      for (size_t k = 0; k < windowList_.size(); ++k) {
        const auto& info = windowList_[k];
        std::string label = info.process + "   -   " + info.title + "##" + std::to_string(k);
        if (ImGui::Selectable(label.c_str())) {
          w.processName = info.process;
          w.titleContains.clear();
          if (w.name.rfind("Fenster ", 0) == 0 || w.name.rfind("Window ", 0) == 0)
            w.name = std::filesystem::path(info.process).stem().string();
          app_.ConfigChanged(false);
        }
      }
      ImGui::EndPopup();
    }
    ImGui::InputText(T("Fenstertitel enthält###title", "Window title contains###title"), &w.titleContains);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SetItemTooltip("%s", T("Optional. Nötig, wenn das Programm mehrere Fenster hat.",
                                  "Optional. Needed when the program has several windows."));
    if (ImGui::Checkbox(T("Titelleiste und Rahmen ausblenden###notitle", "Hide title bar and border###notitle"),
                        &w.hideTitleBar))
      app_.ConfigChanged(false);
    ImGui::InputInt4(T("Ränder abschneiden (px)###trim", "Trim edges (px)###trim"), w.trim);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SetItemTooltip("%s", T("links, oben, rechts, unten - z. B. für selbst gezeichnete Titelleisten oder Menüleisten",
                                  "left, top, right, bottom - e.g. for custom-drawn title bars or menu bars"));
    if (ImGui::Checkbox(T("Mauszeiger mit aufnehmen###cursor", "Capture the mouse cursor###cursor"), &w.captureCursor))
      app_.ConfigChanged(false);
    if (ImGui::Checkbox(T("Im VR bedienbar (Maus)###clicks", "Usable in VR (mouse)###clicks"), &w.forwardClicks))
      app_.ConfigChanged(false);
    ImGui::SetItemTooltip("%s", T("Trigger bzw. Pinch = linke Maustaste (klicken, ziehen), Stick = Mausrad.\n"
                                  "Verschieben dann über Grip oder den Rand des Fensters.\n"
                                  "Elite behält den Fokus, die Maus bewegt sich nicht.",
                                  "Trigger or pinch = left mouse button (click, drag), thumbstick = mouse wheel.\n"
                                  "Move the window with grip or by its border.\n"
                                  "Elite keeps the focus, the mouse does not move."));
    if (w.forwardClicks) {
      ImGui::SameLine(0, 30 * s);
      if (ImGui::Checkbox(T("Wischen scrollt###swipe", "Swipe to scroll###swipe"), &w.dragScroll))
        app_.ConfigChanged(false);
      ImGui::SetItemTooltip("%s", T("Trigger/Pinch halten und ziehen scrollt den Inhalt (wie am Handy), auch seitlich.\n"
                                    "Loslassen in der Bewegung scrollt mit Schwung weiter.\n"
                                    "Erst ca. 0,5 s still halten, dann ziehen = normales Ziehen mit der Maus\n"
                                    "(Schieberegler, Scrollbalken, Text markieren).",
                                    "Hold trigger/pinch and drag to scroll the content (like a phone), also sideways.\n"
                                    "Letting go while moving keeps it scrolling for a moment.\n"
                                    "Hold still for about 0.5 s first, then drag = normal mouse drag\n"
                                    "(sliders, scroll bars, selecting text)."));
    }
  } else {
    ImGui::InputText(T("Bilderordner###folder", "Image folder###folder"), &w.folder);
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.ConfigChanged(false);
    ImGui::SameLine();
    if (ImGui::Button(T("Öffnen", "Open"))) OpenFolder(cfg.Dir() / std::filesystem::path(Utf8ToWide(w.folder)));
    ImGui::TextDisabled("%s", T("PNG/JPG/... ablegen. Im VR: rechte/linke Hälfte anklicken = blättern.",
                                "Put PNG/JPG/... files there. In VR: click the right/left half = turn pages."));
  }

  ImGui::SeparatorText(T("Darstellung", "Display"));
  if (ImGui::Checkbox(T("Sichtbar###visible", "Visible###visible"), &w.visible)) app_.ConfigChanged(false);
  ImGui::SameLine(0, 30 * s);
  if (ImGui::Checkbox(T("Gesperrt (nicht verschiebbar)###locked", "Locked (not movable)###locked"), &w.locked))
    app_.ConfigChanged(false);
  int look = w.lookToShow + 1;
  if (ImGui::Combo(T("Blickabhängig###look", "Gaze dependent###look"), &look,
                   T("Wie unter \"Allgemein\"\0Immer sichtbar\0Nur beim Hinsehen\0",
                     "As in \"General\"\0Always visible\0Only when looking at it\0"))) {
    w.lookToShow = look - 1;
    app_.ConfigChanged(false);
  }
  if (ImGui::SliderFloat(T("Deckkraft###opacity", "Opacity###opacity"), &w.opacity, 0.05f, 1.0f, "%.2f"))
    app_.ConfigChanged(false);
  ImGui::SetItemTooltip("%s", T("Nur wirksam, wenn die OpenXR-Runtime das unterstützt (VDXR derzeit nicht).",
                                "Only works if the OpenXR runtime supports it (VDXR currently does not)."));
  if (ImGui::Combo(T("Einblendeffekt###effect", "Show effect###effect"), &w.showEffect,
                   T("Keiner\0Hologramm\0Zoom\0Röhren-TV\0", "None\0Hologram\0Zoom\0CRT TV\0")))
    app_.ConfigChanged(false);
  ImGui::SetItemTooltip("%s", T("Animation beim Ein- und Ausblenden (auch beim Hinsehen / Wegsehen).",
                                "Animation when the window appears or disappears (also when looking at / away)."));
  if (ImGui::ColorEdit3(T("Filterfarbe###tint", "Filter color###tint"), w.tint, ImGuiColorEditFlags_NoInputs))
    app_.ConfigChanged(false);
  ImGui::SetItemTooltip("%s", T("Wird auf den Inhalt multipliziert: Weiß = unverändert, Grau = dunkler,\n"
                                "z. B. Bernstein = helle Flächen werden orange statt weiß.",
                                "Multiplied onto the content: white = unchanged, gray = darker,\n"
                                "e.g. amber = bright areas become orange instead of white."));
  ImGui::SameLine(0, 30 * s);
  if (ImGui::Button(T("Zurücksetzen###tintreset", "Reset###tintreset"))) {
    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
    w.brightness = 1.0f;
    app_.ConfigChanged(false);
  }
  {
    float pct = w.brightness * 100.0f;
    if (ImGui::SliderFloat(T("Helligkeit###bright", "Brightness###bright"), &pct, 10.0f, 100.0f, "%.0f %%")) {
      w.brightness = pct / 100.0f;
      app_.ConfigChanged(false);
    }
    ImGui::SetItemTooltip("%s", T("Dunkelt den Inhalt ab (funktioniert mit jeder Runtime, auch VDXR).",
                                  "Dims the content (works with every runtime, including VDXR)."));
  }

  ImGui::SeparatorText(T("Position (relativ zur Zentrierung)", "Position (relative to the recentered view)"));
  bool placed = false;
  placed |= ImGui::SliderFloat(T("Breite (m)###width", "Width (m)###width"), &w.width, 0.05f, 3.0f, "%.2f");
  placed |= ImGui::DragFloat3("Position (m)###pos", &w.position.x, 0.005f, -5.0f, 5.0f, "%.3f");
  ImGui::SetItemTooltip("%s", T("x = rechts, y = oben, z = hinten (negativ = vor dir)",
                                "x = right, y = up, z = back (negative = in front of you)"));
  placed |= ImGui::DragFloat3(T("Drehung (Grad)###rot", "Rotation (degrees)###rot"), &w.rotation.x, 0.5f, -180.0f,
                              180.0f, "%.1f");
  ImGui::SetItemTooltip("%s", T("Neigung, Drehung, Rollen", "pitch, yaw, roll"));
  if (ImGui::Button(T("Vor mich setzen", "Place in front of me"))) {
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
  ImGui::TextWrapped("%s", T("Tastenkombinationen gelten systemweit, auch wenn Elite im Vordergrund ist. Joystick-Tasten "
                             "werden nicht exklusiv gelesen: dieselbe Taste kann in Elite z. B. auf \"HMD zurücksetzen\" "
                             "liegen.",
                             "Key combinations work system-wide, also while Elite is in the foreground. Joystick buttons "
                             "are not read exclusively: the same button can also be bound in Elite, e.g. to \"Reset HMD\"."));
  ImGui::Spacing();

  int remove = -1;
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##bindings", 5, flags)) {
    ImGui::TableSetupColumn(T("Aktion", "Action"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn(T("Fenster", "Window"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(T("Tastatur", "Keyboard"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
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
      if (ImGui::BeginCombo("##action", kActions[a].Label())) {
        for (int k = 0; k < kActionCount; ++k)
          if (ImGui::Selectable(kActions[k].Label(), k == a)) {
            b.action = kActions[k].id;
            if (!kActions[k].needsWindow) b.window.clear();
            app_.ConfigChanged(false);
          }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      if (kActions[ActionIndex(b.action)].needsWindow) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##window", b.window.empty() ? T("(alle)", "(all)") : b.window.c_str())) {
          if (ImGui::Selectable(T("(alle)", "(all)"), b.window.empty())) {
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
        std::string label = learning ? T("Taste drücken... (Esc)", "Press a key... (Esc)")
                                     : (b.key.empty() ? T("(keine)", "(none)") : b.key);
        if (ImGui::Button((label + "##key").c_str(), {ImGui::GetContentRegionAvail().x - clearW - 6, 0}) &&
            !app_.Learning()) {
          learningBinding_ = i;
          learningJoystick_ = false;
          app_.StartKeyLearning();
        }
        ImGui::SetItemTooltip("%s", T("Klicken und dann die Tastenkombination drücken", "Click, then press the key combination"));
        ImGui::SameLine(0, 6);
        if (ImGui::Button("x##keyclr", {clearW, 0})) {
          b.key.clear();
          app_.ConfigChanged(false);
        }
      }

      ImGui::TableNextColumn();
      {
        bool learning = learningBinding_ == i && learningJoystick_;
        std::string label =
            learning ? T("Taste(n) drücken und loslassen... (Esc)", "Press and release button(s)... (Esc)")
                     : (!b.buttons.empty() ? (b.joystick.empty() ? T("beliebig", "any") : b.joystick) +
                                                 (b.buttons.size() > 1 ? T("  Tasten ", "  buttons ")
                                                                       : T("  Taste ", "  button ")) +
                                                 b.ButtonsText()
                                           : T("(keine)", "(none)"));
        if (ImGui::Button((label + "##joy").c_str(), {ImGui::GetContentRegionAvail().x - clearW - 6, 0}) &&
            !app_.Learning()) {
          learningBinding_ = i;
          learningJoystick_ = true;
          app_.StartJoystickLearning();
        }
        ImGui::SetItemTooltip("%s", T("Klicken, dann die Taste am Joystick/HOTAS drücken und loslassen.\n"
                                      "Kombination: mehrere Tasten gleichzeitig halten (z. B. 5 + 12), dann loslassen.\n"
                                      "Ist eine Taste allein UND in einer Kombination belegt, löst sie allein\n"
                                      "erst beim Loslassen aus - und nur, wenn die Kombination nicht benutzt wurde.",
                                      "Click, then press and release the button on the joystick/HOTAS.\n"
                                      "Combination: hold several buttons together (e.g. 5 + 12), then release.\n"
                                      "If a button is bound alone AND in a combination, the single binding\n"
                                      "fires on release - and only if the combination was not used."));
        ImGui::SameLine(0, 6);
        if (ImGui::Button("x##joyclr", {clearW, 0})) {
          b.buttons.clear();
          b.joystick.clear();
          app_.ConfigChanged(false);
        }
      }

      ImGui::TableNextColumn();
      if (ImGui::Button(T("Löschen", "Delete"))) remove = i;
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
  if (ImGui::Button(T("Neue Belegung", "New binding"))) {
    cfg.bindings.push_back({"recenter", "", "", "", {}});
    app_.ConfigChanged(false);
  }
  if (app_.Learning()) {
    ImGui::SameLine();
    if (ImGui::Button(T("Lernen abbrechen", "Cancel learning"))) {
      app_.StopLearning();
      learningBinding_ = -1;
    }
  }
}

void SettingsWindow::DrawGeneralTab() {
  auto& cfg = app_.Config();
  ImGui::PushItemWidth(320 * scale_);
  bool c = false;
  ImGui::SeparatorText(T("Sprache / Language", "Language / Sprache"));
  LanguageSlider(app_, "###language", 320 * scale_);
  ImGui::SeparatorText(T("Anzeige", "Display"));
  c |= ImGui::Checkbox(T("Fenster nur beim Hinsehen anzeigen (und bedienbar)###look",
                         "Show windows only when looking at them (and usable)###look"),
                       &cfg.lookToShow);
  c |= ImGui::SliderFloat(T("Blick-Rand (Grad)###margin", "Gaze margin (degrees)###margin"), &cfg.lookMarginDeg, 0.0f,
                          45.0f, "%.0f");
  ImGui::SetItemTooltip("%s", T("Größer = Fenster erscheinen früher, wenn man in ihre Richtung schaut",
                                "Larger = windows appear earlier when looking towards them"));
  c |= ImGui::SliderFloat(T("Schärfe###sharp", "Sharpness###sharp"), &cfg.sharpness, 0.5f, 2.5f, "%.2f");
  ImGui::SetItemTooltip("%s", T("1.0 = ruhiges Bild ohne Flimmern, 1.5-2.0 = schärfer, kann flimmern",
                                "1.0 = calm picture without shimmer, 1.5-2.0 = sharper, may shimmer"));
  c |= ImGui::Checkbox(T("Zeiger-Punkt anzeigen###cursor", "Show pointer dot###cursor"), &cfg.showCursor);
  ImGui::SeparatorText(T("Bedienung", "Controls"));
  c |= ImGui::Checkbox(T("Vibration (Controller)###haptics", "Vibration (controllers)###haptics"), &cfg.haptics);
  {
    float pct = cfg.clickVolume * 100.0f;
    if (ImGui::SliderFloat(T("Klickgeräusch (Lautstärke)###clickvol", "Click sound (volume)###clickvol"), &pct, 0.0f,
                           100.0f, pct < 0.5f ? T("aus", "off") : "%.0f %%")) {
      cfg.clickVolume = pct / 100.0f;
      c = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) app_.PlayClick();  // preview
  }
  c |= ImGui::SliderFloat(T("Größe ändern per Stick (Tempo)###scale", "Resize with thumbstick (speed)###scale"),
                          &cfg.scaleSpeed, 0.2f, 3.0f, "%.2f");
  c |= ImGui::SliderFloat(T("Abstand ändern per Stick (m/s)###push", "Push/pull with thumbstick (m/s)###push"),
                          &cfg.pushPullSpeed, 0.1f, 2.0f, "%.2f");
  c |= ImGui::SliderFloat(T("Wisch-Scrollen (Tempo)###swipe", "Swipe scrolling (speed)###swipe"), &cfg.swipeSpeed,
                          0.25f, 4.0f, "%.2f");
  c |= ImGui::SliderFloat(T("Breite VR-Menü (m)###menuw", "VR menu width (m)###menuw"), &cfg.menuWidth, 0.2f, 1.0f,
                          "%.2f");
  if (c) app_.ConfigChanged(false);
  ImGui::PopItemWidth();

  ImGui::SeparatorText("OpenXR-Layer");
  bool registered = LayerRegistration::IsRegistered();
  ImGui::Text("Status: %s", registered ? T("registriert (wird in OpenXR-Spiele geladen)", "registered (loaded into OpenXR games)")
                                       : T("nicht registriert", "not registered"));
  if (!registered && ImGui::Button(T("Layer registrieren", "Register layer"))) LayerRegistration::Register();
  if (registered && ImGui::Button(T("Layer entfernen", "Remove layer"))) LayerRegistration::Unregister();

  ImGui::SeparatorText(T("Dateien", "Files"));
  ImGui::TextUnformatted(cfg.file.string().c_str());
  if (ImGui::Button(T("Konfigurationsordner öffnen", "Open config folder"))) OpenFolder(cfg.Dir());
  ImGui::SameLine();
  wchar_t local[MAX_PATH]{};
  GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  if (ImGui::Button(T("Layer-Logs öffnen", "Open layer logs"))) OpenFolder(std::filesystem::path(local) / L"EDOverlayBoard");
}

void SettingsWindow::DrawStatusTab() {
  const auto& st = app_.Status();
  ImGui::SeparatorText("VR");
  if (st.connected) {
    const char* yes = T("ja", "yes");
    const char* no = T("nein", "no");
    ImGui::Text(T("Verbunden mit \"%s\"", "Connected to \"%s\""), st.application.c_str());
    ImGui::Text("Runtime: %s", st.runtime.c_str());
    ImGui::Text(T("Controller: %s    Handtracking: %s", "Controllers: %s    Hand tracking: %s"), st.controllers ? yes : no,
                st.handTracking ? yes : no);
  } else {
    ImGui::TextDisabled("%s", T("Kein Spiel verbunden. Starte Elite Dangerous im VR-Modus.",
                                "No game connected. Start Elite Dangerous in VR mode."));
  }
  ImGui::Text(T("Greifen: %s    Alle Fenster: %s", "Grabbing: %s    All windows: %s"),
              app_.InteractionEnabled() ? T("erlaubt", "allowed") : T("gesperrt", "locked"),
              app_.AllHidden() ? T("ausgeblendet", "hidden") : T("eingeblendet", "shown"));

  ImGui::SeparatorText(T("Fenster", "Windows"));
  const auto& cfg = app_.Config();
  for (int i = 0; i < static_cast<int>(cfg.windows.size()); ++i)
    ImGui::BulletText("%s: %s", cfg.windows[static_cast<size_t>(i)].name.c_str(), app_.WindowStatus(i).c_str());

  ImGui::SeparatorText("Joysticks");
  auto joys = app_.JoystickNames();
  if (joys.empty()) ImGui::TextDisabled("%s", T("keine gefunden", "none found"));
  for (const auto& j : joys) ImGui::BulletText("%s", j.c_str());

  ImGui::SeparatorText(T("Über", "About"));
  ImGui::Text("ED Overlay Board %s", EDOB_VERSION);
  ImGui::TextWrapped("%s", T("Dieses Programm wurde mit Hilfe von KI (Claude von Anthropic) erstellt.",
                             "This program was created with the help of AI (Claude by Anthropic)."));
  ImGui::TextDisabled("%s", T("Inoffizielles Fan-Werkzeug, MIT-Lizenz. Elite Dangerous ist eine Marke von Frontier Developments.",
                              "Unofficial fan tool, MIT license. Elite Dangerous is a trademark of Frontier Developments."));
  ImGui::TextDisabled(T("Daten: %s", "Data: %s"), WideToUtf8(DataDir().wstring()).c_str());
}

// =====================================================================================
// VR menu

VrMenu::VrMenu(App& app) : app_(app) {
  Resize(height_);

  ImGuiContext* prev = ImGui::GetCurrentContext();
  ctx_ = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx_);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {static_cast<float>(kWidth), static_cast<float>(height_)};
  io.MouseDrawCursor = false;
  ApplyStyle(2.0f);
  ImGui::GetStyle().FramePadding = {18, 12};
  ImGui::GetStyle().ItemSpacing = {18, 14};
  LoadFont(34.0f);
  ImGui_ImplDX11_Init(app_.Graphics().Device(), app_.Graphics().Context());
  ImGui::SetCurrentContext(prev);
}

VrMenu::~VrMenu() {
  if (ctx_) {
    ContextScope scope(ctx_);
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext(ctx_);
  }
}

bool VrMenu::Resize(uint32_t height) {
  auto& d3d = app_.Graphics();
  auto tex = d3d.CreateTexture(kWidth, height, nullptr, true);
  winrt::com_ptr<ID3D11RenderTargetView> rtv;
  if (!tex || FAILED(d3d.Device()->CreateRenderTargetView(tex.get(), nullptr, rtv.put()))) return false;
  texture_ = tex;
  rtv_ = rtv;
  if (height != height_) Log("VR menu: {}x{} px", kWidth, height);
  height_ = height;
  return true;
}

std::string VrMenu::Status() const { return T("VR-Menü", "VR menu"); }

void VrMenu::SetPointer(bool active, float x, float y) {
  ContextScope scope(ctx_);
  if (active)
    ImGui::GetIO().AddMousePosEvent(x * kWidth, y * height_);
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
  for (int pass = 0; pass < 2; ++pass) {
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    Draw();
    ImGui::Render();
    // grow / shrink with the content (more windows = taller menu) instead of scrolling
    const uint32_t want = std::max(kMinHeight, (static_cast<uint32_t>(std::ceil(neededHeight_)) + 7u) & ~7u);
    if (pass == 0 && want != height_ && Resize(want)) {
      io.DisplaySize = {static_cast<float>(kWidth), static_cast<float>(height_)};
      io.DeltaTime = 0.001f;
      continue;  // lay out again with the new size
    }
    break;
  }
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
  const float header = kHeaderHeight;
  auto* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled({0, 0}, {size.x, header}, IM_COL32(255, 140, 25, 255));
  const char* title = T("ED Overlay Board   -   hier oder am Rand greifen",
                        "ED Overlay Board   -   grab here or at the border");
  ImVec2 ts = ImGui::CalcTextSize(title);
  dl->AddText({(size.x - ts.x) * 0.5f, (header - ts.y) * 0.5f}, IM_COL32(20, 20, 24, 255), title);
  ImGui::SetCursorPosY(header + ImGui::GetStyle().ItemSpacing.y);

  ImGui::SeparatorText(T("Fenster", "Windows"));
  for (int i = 0; i < static_cast<int>(cfg.windows.size()); ++i) {
    auto& w = cfg.windows[static_cast<size_t>(i)];
    ImGui::PushID(i);
    bool v = w.visible;
    if (ImGui::Checkbox(w.name.c_str(), &v)) app_.Do("toggleWindow", w.name);
    ImGui::SameLine(size.x * 0.48f);
    bool movable = !w.locked;
    if (ImGui::Checkbox(T("verschiebbar###move", "movable###move"), &movable)) {
      w.locked = !movable;
      app_.ConfigChanged(false);
    }
    if (w.type == "window") {
      ImGui::SameLine(size.x * 0.78f);
      if (ImGui::Checkbox(T("klickbar###click", "clickable###click"), &w.forwardClicks)) app_.ConfigChanged(false);
    } else if (w.type == "images") {
      ImGui::SameLine(size.x * 0.78f);
      if (ImGui::Button(" < ")) app_.Do("prevPage", w.name);
      ImGui::SameLine();
      if (ImGui::Button(" > ")) app_.Do("nextPage", w.name);
    }
    ImGui::PopID();
  }

  if (!cfg.windows.empty()) {  // filter color / brightness of one window
    const int n = static_cast<int>(cfg.windows.size());
    tintWindow_ = std::clamp(tintWindow_, 0, n - 1);
    auto& w = cfg.windows[static_cast<size_t>(tintWindow_)];
    ImGui::SeparatorText(T("Fenster-Darstellung", "Window look"));
    if (ImGui::Button(" < ##tintprev")) tintWindow_ = (tintWindow_ + n - 1) % n;
    ImGui::SameLine();
    if (ImGui::Button(" > ##tintnext")) tintWindow_ = (tintWindow_ + 1) % n;
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
    ImGui::SameLine(size.x * 0.48f);
    struct Preset {
      const char* de;
      const char* en;
      float rgb[3];
    };
    static const Preset kPresets[] = {
        {"Neutral", "Neutral", {1.0f, 1.0f, 1.0f}},   {"Bernstein", "Amber", {1.0f, 0.62f, 0.25f}},
        {"Rot", "Red", {1.0f, 0.3f, 0.25f}},          {"Grün", "Green", {0.45f, 1.0f, 0.5f}},
        {"Blau", "Blue", {0.5f, 0.7f, 1.0f}},
    };
    // no drag & drop / tooltip: hand jitter while pinching started a color drag instead of a click
    const ImGuiColorEditFlags swFlags = ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoTooltip;
    const float sw = ImGui::GetFrameHeight();
    const float swW = (size.x * 0.52f - ImGui::GetStyle().WindowPadding.x - 4 * ImGui::GetStyle().ItemSpacing.x) / 5;
    for (int p = 0; p < static_cast<int>(std::size(kPresets)); ++p) {
      const auto& pr = kPresets[p];
      ImGui::PushID(p);
      if (p) ImGui::SameLine();
      if (ImGui::ColorButton(T(pr.de, pr.en), ImVec4(pr.rgb[0], pr.rgb[1], pr.rgb[2], 1.0f), swFlags, {swW, sw})) {
        for (int k = 0; k < 3; ++k) w.tint[k] = pr.rgb[k];
        app_.ConfigChanged(false);
      }
      ImGui::PopID();
    }
    float pct = w.brightness * 100.0f;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##bright", &pct, 10.0f, 100.0f, T("Helligkeit %.0f %%", "Brightness %.0f %%"))) {
      w.brightness = pct / 100.0f;
      app_.ConfigChanged(false);
    }
    // show effect: one button per effect, the active one highlighted
    const char* effects[] = {T("Kein Effekt", "No effect"), T("Hologramm", "Hologram"), "Zoom",
                             T("Röhren-TV", "CRT TV")};
    const float ew = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
    for (int e = 0; e < 4; ++e) {
      if (e) ImGui::SameLine();
      const bool active = w.showEffect == e;
      if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      ImGui::PushID(e);
      if (ImGui::Button(effects[e], {ew, 0}) && !active) {
        w.showEffect = e;
        app_.ConfigChanged(false);
      }
      ImGui::PopID();
      if (active) ImGui::PopStyleColor();
    }
  }

  ImGui::SeparatorText(T("Aktionen", "Actions"));
  const float third = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
  if (ImGui::Button(T("Zentrieren###recenter", "Recenter###recenter"), {third, 0})) app_.Do("recenter");
  ImGui::SameLine();
  const char* all = app_.AllHidden() ? T("Alle ein###all", "All on###all") : T("Alle aus###all", "All off###all");
  if (ImGui::Button(all, {third, 0})) app_.Do("toggleAll");
  ImGui::SameLine();
  const char* lock =
      app_.InteractionEnabled() ? T("Sperren###lock", "Lock###lock") : T("Entsperren###lock", "Unlock###lock");
  if (ImGui::Button(lock, {third, 0})) app_.Do("toggleInteraction");
  {  // PC keyboard into the program window clicked last
    const std::string kbWindow = app_.KeyboardWindow();
    std::string label = kbWindow.empty()
                            ? std::string(T("PC-Tastatur ins Fenster", "PC keyboard into window"))
                            : std::string(T("Tastatur zurück an Elite  (tippt in: ", "Keyboard back to Elite  (typing into: ")) +
                                  kbWindow + ")";
    if (!kbWindow.empty()) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.85f, 1.0f));
    if (ImGui::Button((label + "###keyboard").c_str(), {-FLT_MIN, 0})) app_.Do("toggleKeyboard");
    if (!kbWindow.empty()) ImGui::PopStyleColor();
  }

  ImGui::SeparatorText(T("Anzeige", "Display"));
  if (ImGui::Checkbox(T("Nur beim Hinsehen anzeigen###look", "Only show when looking at it###look"), &cfg.lookToShow))
    app_.ConfigChanged(false);
  const float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  ImGui::SetNextItemWidth(halfW);
  if (ImGui::SliderFloat("##sharp", &cfg.sharpness, 0.5f, 2.5f, T("Schärfe %.2f", "Sharpness %.2f")))
    app_.ConfigChanged(false);
  ImGui::SameLine();
  LanguageSlider(app_, "##language", halfW);

  ImGui::Spacing();
  if (ImGui::Button(T("Einstellungen am PC###settings", "Settings on the PC###settings"), {halfW, 0}))
    app_.OpenSettings();
  ImGui::SameLine();
  if (ImGui::Button(T("Menü schließen###close", "Close menu###close"), {halfW, 0})) app_.Do("toggleMenu");
  neededHeight_ = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().WindowPadding.y;
  ImGui::End();
}
