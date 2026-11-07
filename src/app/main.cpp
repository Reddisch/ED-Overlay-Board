#include "App.h"
#include "JoystickManager.h"
#include "LayerRegistration.h"
#include "Log.h"
#include "Util.h"

#include <winrt/Windows.Foundation.h>

#include <atomic>
#include <cstdio>
#include <filesystem>

namespace {

std::atomic<bool> g_quit{false};

BOOL WINAPI OnConsoleCtrl(DWORD) {
  g_quit = true;
  return TRUE;
}

void PrintHelp() {
  std::puts(
      "EDOverlayBoard " EDOB_VERSION " - OpenXR overlay windows for Elite Dangerous\n"
      "\n"
      "Usage: EDOverlayBoard.exe [options]\n"
      "  --config <file>      use another config file (default: config.json next to the exe)\n"
      "  --install-layer      only register the OpenXR layer (done automatically on every start)\n"
      "  --uninstall-layer    remove the OpenXR layer registration\n"
      "  --joysticks          show joystick names and button numbers (for the config)\n"
      "  --help               this text\n");
}

int RunJoystickMonitor() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"EDOverlayBoardJoyMon";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance,
                              nullptr);
  JoystickManager joy;
  if (!joy.Init(hwnd)) return 1;
  joy.Monitor(g_quit);
  DestroyWindow(hwnd);
  return 0;
}

// First start: copy the example config so the user has something to edit.
std::filesystem::path PrepareDefaultConfig() {
  auto cfg = ExeDir() / "config.json";
  std::error_code ec;
  if (!std::filesystem::exists(cfg, ec)) {
    auto example = ExeDir() / "resources" / "config.example.json";
    if (std::filesystem::copy_file(example, cfg, ec))
      Log("Created {} from the example config.", cfg.string());
    else
      Log("Cannot create {}: {}", cfg.string(), ec.message());
  }
  return cfg;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // physical pixels everywhere: needed for exact client-area cropping and a sharp settings window
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  LogInit(ExeDir() / "EDOverlayBoard.log", true);

  std::filesystem::path configPath;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--help" || a == L"-h" || a == L"/?") {
      PrintHelp();
      return 0;
    }
    if (a == L"--install-layer") return LayerRegistration::Register() ? 0 : 1;
    if (a == L"--uninstall-layer") return LayerRegistration::Unregister() ? 0 : 1;
    if (a == L"--joysticks") return RunJoystickMonitor();
    if (a == L"--config" && i + 1 < argc) {
      configPath = argv[++i];
      continue;
    }
    std::fprintf(stderr, "Unknown option: %ls\n", argv[i]);
    PrintHelp();
    return 2;
  }

  // Only one instance: a second start just opens the settings of the running one.
  HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\EDOverlayBoard.App");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(L"EDOverlayBoardHidden", nullptr))
      PostMessageW(other, WM_APP + 1, 0, WM_LBUTTONDBLCLK);  // tray "double-click" -> settings
    std::puts("EDOverlayBoard is already running - opened its settings window.");
    if (single) CloseHandle(single);
    return 0;
  }

  Log("EDOverlayBoard {} starting", EDOB_VERSION);
  if (!LayerRegistration::IsRegistered()) LayerRegistration::Register();
  if (configPath.empty()) configPath = PrepareDefaultConfig();

  App app(g_quit);
  int rc = app.Run(std::filesystem::absolute(configPath));
  Log("Exit ({})", rc);
  return rc;
}
