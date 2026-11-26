// ED Overlay Board Starter - window, settings tabs and command line.
#include "Lang.h"
#include "Log.h"
#include "Starter.h"
#include "Util.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_stdlib.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr wchar_t kClass[] = L"EDOverlayBoardStarter";
constexpr int kAutoCloseSeconds = 10;

const ImVec4 kOrange{1.0f, 0.55f, 0.1f, 1.0f};
const ImVec4 kMuted{0.62f, 0.45f, 0.30f, 1.0f};
const ImVec4 kGreen{0.44f, 0.86f, 0.44f, 1.0f};
const ImVec4 kBlue{0.50f, 0.83f, 1.0f, 1.0f};
const ImVec4 kRed{1.0f, 0.36f, 0.30f, 1.0f};

// ------------------------------------------------------------------------- file dialog
enum class Pick { Program, Runtime, Folder };

bool Browse(HWND owner, Pick kind, std::string& value) {
  ComPtr<IFileOpenDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  opts |= FOS_FORCEFILESYSTEM;
  if (kind == Pick::Folder) opts |= FOS_PICKFOLDERS;
  if (kind == Pick::Program) opts |= FOS_NODEREFERENCELINKS;  // keep .lnk shortcuts as they are
  dlg->SetOptions(opts);
  std::wstring progs = T(L"Programme", L"Programs"), all = T(L"Alle Dateien", L"All files");
  std::wstring rt = T(L"OpenXR-Runtime", L"OpenXR runtime");
  COMDLG_FILTERSPEC progFilter[] = {{progs.c_str(), L"*.exe;*.bat;*.cmd;*.lnk"}, {all.c_str(), L"*.*"}};
  COMDLG_FILTERSPEC rtFilter[] = {{rt.c_str(), L"*.json"}, {all.c_str(), L"*.*"}};
  if (kind == Pick::Program) dlg->SetFileTypes(2, progFilter);
  if (kind == Pick::Runtime) dlg->SetFileTypes(2, rtFilter);
  fs::path cur = Utf8ToWide(ExpandPath(value));
  std::error_code ec;
  fs::path folder = fs::is_directory(cur, ec) ? cur : cur.parent_path();
  ComPtr<IShellItem> start;
  if (!folder.empty() && fs::is_directory(folder, ec) &&
      SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start))))
    dlg->SetFolder(start.Get());
  if (FAILED(dlg->Show(owner))) return false;
  ComPtr<IShellItem> item;
  PWSTR path = nullptr;
  if (FAILED(dlg->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return false;
  value = WideToUtf8(path);
  CoTaskMemFree(path);
  return true;
}

// Desktop shortcut "Elite - <mode>" that starts everything with this VR mode right away.
bool CreateModeShortcut(const std::string& mode, std::wstring& outPath) {
  PWSTR desk = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk))) return false;
  std::wstring name = L"Elite - " + Utf8ToWide(mode);
  for (wchar_t& c : name)
    if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
  outPath = (fs::path(desk) / (name + L".lnk")).wstring();
  CoTaskMemFree(desk);
  wchar_t exe[MAX_PATH * 2]{};
  GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
  ComPtr<IShellLinkW> link;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
  std::wstring args = L"--start --mode \"" + Utf8ToWide(mode) + L"\"";
  link->SetPath(exe);
  link->SetArguments(args.c_str());
  link->SetWorkingDirectory(fs::path(exe).parent_path().c_str());
  link->SetIconLocation(exe, 0);
  link->SetDescription(name.c_str());
  ComPtr<IPersistFile> file;
  return SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(outPath.c_str(), TRUE));
}

// ------------------------------------------------------------------------- app
class StarterApp {
 public:
  bool Init(bool autoStart, const std::string& modeOverride);
  int Run();

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  bool CreateDevice();
  void CreateTarget();
  void Frame();
  void Draw();
  void DrawStart();
  void DrawPrograms();
  void DrawModes();
  void Save() { dirty_ = true; }
  bool PathField(const char* label, std::string& value, Pick kind);
  void StatusText(const Step& s);
  std::string RuntimeName(const std::wstring& manifest) const;

  HWND hwnd_ = nullptr;
  float scale_ = 1;
  ComPtr<ID3D11Device> dev_;
  ComPtr<ID3D11DeviceContext> ctx_;
  ComPtr<IDXGISwapChain> swap_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  UINT resizeW_ = 0, resizeH_ = 0;

  fs::path cfgFile_;
  StarterConfig cfg_;
  Runner runner_;
  bool dirty_ = false;
  bool autoStart_ = false;
  int selProgram_ = 0, selMode_ = 0;
  int titleLanguage_ = -1;
  std::string shortcutMsg_;
  bool closing_ = false;
  int forceTab_ = -1;  // EDOB_STARTER_TAB=0/1/2: open on that tab (screenshots for the docs/tests)
};

bool StarterApp::Init(bool autoStart, const std::string& modeOverride) {
  cfgFile_ = DataDir() / L"starter.json";
  if (!cfg_.Load(cfgFile_)) {
    i18n::Apply("");
    cfg_.Defaults();
    cfg_.Save(cfgFile_);
    Log("Created {} with detected programs", WideToUtf8(cfgFile_.wstring()));
  }
  i18n::Apply(cfg_.language);
  if (!modeOverride.empty()) {
    bool found = false;
    for (auto& m : cfg_.modes)
      if (ContainsI(Utf8ToWide(m.name), Utf8ToWide(modeOverride))) {
        cfg_.mode = m.name;  // used for this start only, not saved
        found = true;
        break;
      }
    if (!found) Log("--mode '{}' not found", modeOverride);
  }
  autoStart_ = autoStart;
  if (wchar_t tab[8]{}; GetEnvironmentVariableW(L"EDOB_STARTER_TAB", tab, 8)) forceTab_ = _wtoi(tab);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(0, kClass, L"ED Overlay Board Starter", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          760, 640, nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd_) return false;
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  scale_ = GetDpiForWindow(hwnd_) / 96.0f;
  SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(760 * scale_), static_cast<int>(640 * scale_),
               SWP_NOMOVE | SWP_NOZORDER);
  if (!CreateDevice()) return false;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 0;
  s.FrameRounding = 4;
  s.GrabRounding = 4;
  s.FramePadding = {8, 5};
  s.ItemSpacing = {10, 7};
  s.Colors[ImGuiCol_CheckMark] = kOrange;
  s.Colors[ImGuiCol_SliderGrab] = kOrange;
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
  s.ScaleAllSizes(scale_);
  wchar_t windir[MAX_PATH]{};
  GetWindowsDirectoryW(windir, MAX_PATH);
  fs::path font = fs::path(windir) / L"Fonts" / L"segoeui.ttf";
  std::error_code ec;
  if (fs::exists(font, ec))
    io.Fonts->AddFontFromFileTTF(font.string().c_str(), 17.0f * scale_);
  else
    io.Fonts->AddFontDefault();
  ImGui_ImplWin32_Init(hwnd_);
  ImGui_ImplDX11_Init(dev_.Get(), ctx_.Get());

  ShowWindow(hwnd_, SW_SHOWNORMAL);
  UpdateWindow(hwnd_);
  return true;
}

bool StarterApp::CreateDevice() {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd_;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  HRESULT hr = E_FAIL;
  for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
    hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &swap_, &dev_,
                                       nullptr, &ctx_);
    if (SUCCEEDED(hr)) break;
  }
  if (FAILED(hr)) {
    Log("D3D11 device failed: 0x{:08X}", static_cast<unsigned>(hr));
    return false;
  }
  CreateTarget();
  return true;
}

void StarterApp::CreateTarget() {
  rtv_.Reset();
  ComPtr<ID3D11Texture2D> back;
  if (SUCCEEDED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) dev_->CreateRenderTargetView(back.Get(), nullptr, &rtv_);
}

LRESULT CALLBACK StarterApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return TRUE;
  auto* self = reinterpret_cast<StarterApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_SIZE:
      if (self && wp != SIZE_MINIMIZED) {
        self->resizeW_ = LOWORD(lp);
        self->resizeH_ = HIWORD(lp);
      }
      return 0;
    case WM_GETMINMAXINFO: {
      reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {560, 420};
      return 0;
    }
    case WM_DPICHANGED: {
      auto* r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
      return 0;
    }
    case WM_APP + 1:  // a second start brings this window to the front
      ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
      SetForegroundWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int StarterApp::Run() {
  if (autoStart_) runner_.Start(cfg_, hwnd_);
  MSG msg{};
  for (;;) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        if (dirty_) cfg_.Save(cfgFile_);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return static_cast<int>(msg.wParam);
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    runner_.Tick();
    // a minimised or background window without a running start does not need 60 frames per second
    if (IsIconic(hwnd_) || (GetForegroundWindow() != hwnd_ && !runner_.Busy()))
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
    if (!IsIconic(hwnd_)) Frame();
    if (dirty_ && !ImGui::IsAnyItemActive()) {
      cfg_.Save(cfgFile_);
      dirty_ = false;
    }
  }
}

void StarterApp::Frame() {
  if (titleLanguage_ != static_cast<int>(i18n::English())) {
    titleLanguage_ = static_cast<int>(i18n::English());
    SetWindowTextW(hwnd_, T(L"ED Overlay Board – Starter", L"ED Overlay Board – Starter"));
  }
  if (resizeW_ && resizeH_) {
    rtv_.Reset();
    swap_->ResizeBuffers(0, resizeW_, resizeH_, DXGI_FORMAT_UNKNOWN, 0);
    resizeW_ = resizeH_ = 0;
    CreateTarget();
  }
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  Draw();
  ImGui::Render();
  const float clear[4] = {0.07f, 0.07f, 0.09f, 1.0f};
  ID3D11RenderTargetView* rtv = rtv_.Get();
  ctx_->OMSetRenderTargets(1, &rtv, nullptr);
  ctx_->ClearRenderTargetView(rtv, clear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  swap_->Present(1, 0);
}

void StarterApp::Draw() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::Begin("##main", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
  ImGui::SetWindowFontScale(1.35f);
  ImGui::TextUnformatted("ELITE DANGEROUS  //  START");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  if (ImGui::BeginTabBar("tabs")) {
    auto flags = [&](int i) { return forceTab_ == i ? ImGuiTabItemFlags_SetSelected : 0; };
    if (ImGui::BeginTabItem(T("Start###start", "Start###start"), nullptr, flags(0))) {
      DrawStart();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("Programme###programs", "Programs###programs"), nullptr, flags(1))) {
      DrawPrograms();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("VR-Modi###modes", "VR modes###modes"), nullptr, flags(2))) {
      DrawModes();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    forceTab_ = -1;
  }
  ImGui::End();
}

std::string StarterApp::RuntimeName(const std::wstring& manifest) const {
  if (manifest.empty()) return T("keine", "none");
  for (auto& m : cfg_.modes)
    if (SamePath(m.runtime, manifest)) return m.name;
  return WideToUtf8(fs::path(manifest).filename().wstring());
}

void StarterApp::StatusText(const Step& s) {
  switch (s.state) {
    case StepState::Waiting: {
      double left = s.at - runner_.Elapsed();
      if (left > 0)
        ImGui::TextColored(kMuted, "%s %.0f s", T("wartet", "waiting"), std::ceil(left));
      else
        ImGui::TextColored(kMuted, "%s", T("wartet auf VR ...", "waiting for VR ..."));
      break;
    }
    case StepState::Starting:
      if (s.kind == StepKind::Runtime)
        ImGui::TextColored(kOrange, "%s", T("Admin-Abfrage ...", "admin prompt ..."));
      else
        ImGui::TextColored(kOrange, "%s (%.0f s)", T("startet ...", "starting ..."), runner_.Elapsed() - s.startedAt);
      break;
    case StepState::Ok:
      ImGui::TextColored(kGreen, "%s", s.kind == StepKind::Runtime ? T("umgestellt", "switched") : T("läuft", "running"));
      break;
    case StepState::Already:
      ImGui::TextColored(kBlue, "%s", s.kind == StepKind::Runtime ? T("war schon aktiv", "already active")
                                                                   : T("lief schon", "was running"));
      break;
    case StepState::Failed:
      ImGui::TextColored(kRed, "%s", s.error.empty() ? T("Fehler", "failed") : s.error.c_str());
      if (ImGui::IsItemHovered() && !s.error.empty()) ImGui::SetTooltip("%s", s.error.c_str());
      break;
    case StepState::Cancelled:
      ImGui::TextColored(kMuted, "%s", T("abgebrochen", "cancelled"));
      break;
  }
}

void StarterApp::DrawStart() {
  const bool busy = runner_.Busy();
  ImGui::Spacing();
  ImGui::BeginDisabled(busy);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(T("VR-Modus", "VR mode"));
  ImGui::SameLine(140 * scale_);
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##mode", cfg_.mode.c_str())) {
    for (auto& m : cfg_.modes)
      if (ImGui::Selectable(m.name.c_str(), m.name == cfg_.mode)) {
        cfg_.mode = m.name;
        Save();
      }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  ImGui::TextColored(kMuted, "%s: %s", T("Aktive OpenXR-Runtime", "Active OpenXR runtime"),
                     RuntimeName(ActiveOpenXrRuntime()).c_str());
  ImGui::Spacing();

  const auto& steps = runner_.Steps();
  auto stepFor = [&](StepKind kind, int program) -> const Step* {
    for (auto& s : steps)
      if (s.kind == kind && (kind != StepKind::Program || s.program == program)) return &s;
    return nullptr;
  };
  const float footer = ImGui::GetFrameHeightWithSpacing() * 2.6f;
  if (ImGui::BeginTable("run", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                        ImVec2(0, -footer))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(T("Programm", "Program"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(T("nach", "after"), ImGuiTableColumnFlags_WidthFixed, 60 * scale_);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    // the VR mode's own steps first (runtime switch, companion program)
    for (auto& s : steps) {
      if (s.kind == StepKind::Program) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kOrange, "  %s", s.name.c_str());
      ImGui::TableNextColumn();
      ImGui::TableNextColumn();
      StatusText(s);
    }
    for (int i = 0; i < static_cast<int>(cfg_.programs.size()); ++i) {
      auto& e = cfg_.programs[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(busy);
      if (ImGui::Checkbox(e.name.empty() ? "?" : e.name.c_str(), &e.enabled)) Save();
      ImGui::EndDisabled();
      if (e.path.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", T("Kein Pfad - im Reiter 'Programme' eintragen", "No path - set it in the 'Programs' tab"));
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kMuted, e.waitForVr ? "VR+%d s" : "%d s", e.delay);
      if (ImGui::IsItemHovered() && e.waitForVr)
        ImGui::SetTooltip("%s", T("Startet erst, wenn der VR-Modus bereit ist", "Starts once the VR mode is ready"));
      ImGui::TableNextColumn();
      if (const Step* s = stepFor(StepKind::Program, i))
        StatusText(*s);
      else if (e.enabled && e.path.empty())
        ImGui::TextColored(kRed, "%s", T("kein Pfad", "no path"));
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  // ---- footer
  ImGui::Spacing();
  const ImVec2 big(200 * scale_, ImGui::GetFrameHeight() * 1.4f);
  if (busy) {
    if (ImGui::Button(T("Abbrechen", "Cancel"), big)) runner_.Cancel();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "%s %.0f s", T("seit Start", "since start"), runner_.Elapsed());
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.55f, 0.1f, 0.65f));
    if (ImGui::Button(T("STARTEN", "START"), big)) runner_.Start(cfg_, hwnd_);
    ImGui::PopStyleColor();
    if (runner_.Started()) {
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      const int failed = runner_.FailedCount();
      if (failed) {
        ImGui::TextColored(kRed, "%s: %d %s", T("Fertig", "Done"), failed, T("Fehler", "failed"));
      } else if (cfg_.closeWhenDone) {
        const double left = kAutoCloseSeconds - runner_.SinceDone();
        ImGui::TextColored(kGreen, "%s %.0f s", T("Alles läuft. o7  Schließt in", "All running. o7  Closing in"),
                           std::ceil(std::max(0.0, left)));
        if (left <= 0 && !closing_) {
          closing_ = true;
          PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
      } else {
        ImGui::TextColored(kGreen, "%s", T("Alles läuft. o7", "All running. o7"));
      }
    }
  }
  if (ImGui::Checkbox(T("Fenster schließen, wenn alles läuft", "Close the window when everything runs"),
                      &cfg_.closeWhenDone))
    Save();
  ImGui::SameLine();
  int lang = i18n::English() ? 1 : 0;
  ImGui::SetNextItemWidth(220 * scale_);
  if (ImGui::SliderInt("##lang", &lang, 0, 1, lang ? "Language: English" : "Sprache: Deutsch") &&
      (lang == 1) != i18n::English()) {
    cfg_.language = lang ? "en" : "de";
    i18n::Apply(cfg_.language);
    Save();
  }
}

bool StarterApp::PathField(const char* label, std::string& value, Pick kind) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(170 * scale_);
  const float btn = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(-btn - ImGui::GetStyle().ItemSpacing.x);
  bool changed = ImGui::InputText("##v", &value);
  ImGui::SameLine();
  if (ImGui::Button("...")) changed |= Browse(hwnd_, kind, value);
  ImGui::PopID();
  return changed;
}

namespace {
bool TextField(const char* label, std::string& value, float indent, const char* hint = nullptr) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(indent);
  ImGui::SetNextItemWidth(-1);
  bool changed = hint ? ImGui::InputTextWithHint("##v", hint, &value) : ImGui::InputText("##v", &value);
  ImGui::PopID();
  return changed;
}

bool IntField(const char* label, int& value, float indent, int lo, int hi) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(indent);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
  bool changed = ImGui::InputInt("##v", &value, 1, 10);
  value = std::clamp(value, lo, hi);
  ImGui::PopID();
  return changed;
}

// list on the left with add / remove / up / down; returns the selected index (or -1)
template <class Item, class NewFn>
int ListPane(const char* id, std::vector<Item>& items, int& sel, float width, bool& changed, NewFn makeNew) {
  ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_Borders);
  const float buttons = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
  ImGui::BeginChild("##list", ImVec2(0, -buttons));
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    ImGui::PushID(i);
    const std::string& n = items[i].name;
    if (ImGui::Selectable(n.empty() ? "?" : n.c_str(), sel == i)) sel = i;
    ImGui::PopID();
  }
  ImGui::EndChild();
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  if (ImGui::Button(T("Neu", "New"), ImVec2(half, 0))) {
    items.push_back(makeNew());
    sel = static_cast<int>(items.size()) - 1;
    changed = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(sel < 0 || sel >= static_cast<int>(items.size()));
  if (ImGui::Button(T("Löschen", "Delete"), ImVec2(half, 0))) {
    items.erase(items.begin() + sel);
    sel = std::min(sel, static_cast<int>(items.size()) - 1);
    changed = true;
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(sel <= 0);
  if (ImGui::Button(T("Nach oben", "Up"), ImVec2(half, 0))) {
    std::swap(items[sel], items[sel - 1]);
    --sel;
    changed = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(sel < 0 || sel + 1 >= static_cast<int>(items.size()));
  if (ImGui::Button(T("Nach unten", "Down"), ImVec2(half, 0))) {
    std::swap(items[sel], items[sel + 1]);
    ++sel;
    changed = true;
  }
  ImGui::EndDisabled();
  ImGui::EndChild();
  if (sel >= static_cast<int>(items.size())) sel = static_cast<int>(items.size()) - 1;
  return sel;
}

void Hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}
}  // namespace

void StarterApp::DrawPrograms() {
  if (runner_.Busy()) {
    Hint(T("Während des Starts gesperrt.", "Locked while starting."));
    return;
  }
  bool changed = false;
  const int sel = ListPane("##programs", cfg_.programs, selProgram_, 220 * scale_, changed, [] {
    ProgramEntry e;
    e.name = T("Neues Programm", "New program");
    return e;
  });
  ImGui::SameLine();
  ImGui::BeginChild("##edit", ImVec2(0, 0));
  if (sel >= 0) {
    auto& e = cfg_.programs[sel];
    const float ind = 170 * scale_;
    changed |= TextField(T("Name", "Name"), e.name, ind);
    changed |= ImGui::Checkbox(T("Beim Start mit starten", "Start it"), &e.enabled);
    ImGui::Separator();
    std::string before = e.path;
    if (PathField(T("Programm", "Program"), e.path, Pick::Program)) {
      changed = true;
      if ((e.name.empty() || e.name == T("Neues Programm", "New program")) && !e.path.empty())
        e.name = WideToUtf8(fs::path(Utf8ToWide(e.path)).stem().wstring());
    }
    changed |= TextField(T("Argumente", "Arguments"), e.args, ind);
    changed |= PathField(T("Arbeitsordner", "Working folder"), e.workDir, Pick::Folder);
    Hint(T("Leer = Ordner des Programms. Statt eines Programms geht auch eine URL wie steam://rungameid/359320.",
           "Empty = folder of the program. A URL like steam://rungameid/359320 works too."));
    ImGui::Separator();
    changed |= IntField(T("Start nach (s)", "Start after (s)"), e.delay, ind, 0, 3600);
    changed |= ImGui::Checkbox(T("Erst starten, wenn der VR-Modus bereit ist", "Start only once the VR mode is ready"),
                               &e.waitForVr);
    Hint(T("Für Elite: so startet das Spiel erst, wenn die OpenXR-Runtime umgestellt ist und das VR-Programm läuft.",
           "For Elite: the game starts only after the OpenXR runtime was switched and the VR program runs."));
    changed |= IntField(T("Zeitlimit (s)", "Timeout (s)"), e.timeout, ind, 5, 3600);
    ImGui::Separator();
    ImGui::TextUnformatted(T("Woran erkennt man, dass es läuft?", "How to see that it runs?"));
    changed |= TextField(T("Prozessname(n)", "Process name(s)"), e.processes, ind,
                         T("leer = Name der Programmdatei", "empty = name of the program file"));
    changed |= TextField(T("Fenstertitel enthält", "Window title contains"), e.windowTitle, ind,
                         T("optional", "optional"));
    Hint(T("Mehrere Namen mit Komma trennen. Nötig, wenn das Programm über einen Starter läuft "
           "(z.B. Elite über steam.exe: EliteDangerous64, EDLaunch). Bei allgemeinen Namen hilft ein "
           "Ordnerteil, z.B. oculus-client\\Client.exe. Was schon läuft, wird nicht doppelt gestartet.",
           "Separate several names with commas. Needed when a launcher starts the program (e.g. Elite via "
           "steam.exe: EliteDangerous64, EDLaunch). For generic names add a folder part, e.g. "
           "oculus-client\\Client.exe. Programs already running are not started twice."));
  }
  ImGui::EndChild();
  if (changed) Save();
}

void StarterApp::DrawModes() {
  if (runner_.Busy()) {
    Hint(T("Während des Starts gesperrt.", "Locked while starting."));
    return;
  }
  bool changed = false;
  std::string oldName = selMode_ >= 0 && selMode_ < static_cast<int>(cfg_.modes.size()) ? cfg_.modes[selMode_].name : "";
  const int sel = ListPane("##modes", cfg_.modes, selMode_, 220 * scale_, changed, [] {
    VrMode m;
    m.name = T("Neuer VR-Modus", "New VR mode");
    return m;
  });
  ImGui::SameLine();
  ImGui::BeginChild("##edit", ImVec2(0, 0));
  if (sel >= 0) {
    auto& m = cfg_.modes[sel];
    const float ind = 170 * scale_;
    if (TextField(T("Name", "Name"), m.name, ind)) {
      changed = true;
      if (cfg_.mode == oldName) cfg_.mode = m.name;
    }
    ImGui::Separator();
    changed |= PathField(T("OpenXR-Runtime", "OpenXR runtime"), m.runtime, Pick::Runtime);
    Hint(T("Die .json-Datei der Runtime. Leer = die aktive Runtime nicht ändern. Zum Umstellen fragt Windows "
           "einmal nach Admin-Rechten (nur wenn eine andere Runtime aktiv ist).",
           "The runtime's .json file. Empty = keep the active runtime. To switch, Windows asks once for admin "
           "rights (only when another runtime is active)."));
    ImGui::Separator();
    changed |= PathField(T("VR-Programm", "VR program"), m.program, Pick::Program);
    changed |= TextField(T("Argumente", "Arguments"), m.args, ind);
    changed |= TextField(T("Prozessname(n)", "Process name(s)"), m.processes, ind,
                         T("leer = Name der Programmdatei", "empty = name of the program file"));
    changed |= IntField(T("Zeitlimit (s)", "Timeout (s)"), m.timeout, ind, 5, 3600);
    Hint(T("Wird vor allen anderen Programmen gestartet, z.B. Virtual Desktop Streamer, SteamVR oder die "
           "Meta-Horizon-Link-App. Ob die Brille wirklich verbunden ist, kann der Starter nicht sehen.",
           "Started before all other programs, e.g. Virtual Desktop Streamer, SteamVR or the Meta Horizon Link "
           "app. Whether the headset is really connected cannot be seen."));
    ImGui::Spacing();
    if (ImGui::Button(T("Desktop-Verknüpfung für diesen Modus", "Desktop shortcut for this mode"))) {
      std::wstring path;
      shortcutMsg_ = CreateModeShortcut(m.name, path)
                         ? std::string(T("Erstellt: ", "Created: ")) + WideToUtf8(fs::path(path).filename().wstring())
                         : std::string(T("Verknüpfung konnte nicht erstellt werden", "Could not create the shortcut"));
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", T("Startet mit einem Doppelklick sofort alles mit diesem VR-Modus (--start --mode).",
                                "Double-click starts everything with this VR mode right away (--start --mode)."));
    if (!shortcutMsg_.empty()) {
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kGreen, "%s", shortcutMsg_.c_str());
    }
    if (ImGui::Button(T("Alle VR-Modi neu erkennen", "Detect all VR modes again"))) {
      StarterConfig fresh;
      fresh.Defaults();
      cfg_.modes = fresh.modes;
      if (!cfg_.FindMode(cfg_.mode) || cfg_.FindMode(cfg_.mode)->name != cfg_.mode) cfg_.mode = fresh.mode;
      selMode_ = 0;
      changed = true;
    }
  }
  ImGui::EndChild();
  if (changed) Save();
}

void PrintHelp() {
  std::puts(
      "EDOverlayBoardStarter " EDOB_VERSION " - starts Elite Dangerous, the VR runtime and helper programs\n"
      "\n"
      "Usage: EDOverlayBoardStarter.exe [options]\n"
      "  --start              start right away (same as pressing START)\n"
      "  --mode <name>        use this VR mode (part of its name, e.g. \"SteamVR\", \"Virtual\", \"Link\")\n"
      "  --set-openxr-runtime <file.json>\n"
      "                       set the active OpenXR runtime (needs admin rights; used internally)\n"
      "  --help               this text\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
  }
  bool start = false;
  std::string mode;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--help" || a == L"-h" || a == L"/?") {
      PrintHelp();
      return 0;
    }
    if (a == L"--set-openxr-runtime" && i + 1 < argc) return SetOpenXrRuntimeMain(argv[++i]);
    if (a == L"--start") {
      start = true;
      continue;
    }
    if (a == L"--mode" && i + 1 < argc) {
      mode = WideToUtf8(argv[++i]);
      continue;
    }
    std::fprintf(stderr, "Unknown option: %ls\n", argv[i]);
    PrintHelp();
    return 2;
  }

  HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\EDOverlayBoard.Starter");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kClass, nullptr)) PostMessageW(other, WM_APP + 1, 0, 0);
    if (single) CloseHandle(single);
    return 0;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  LogInit(DataDir() / L"EDOverlayBoardStarter.log", false);
  Log("EDOverlayBoardStarter {} starting", EDOB_VERSION);

  StarterApp app;
  if (!app.Init(start, mode)) return 1;
  int rc = app.Run();
  Log("Exit ({})", rc);
  CoUninitialize();
  return rc;
}
