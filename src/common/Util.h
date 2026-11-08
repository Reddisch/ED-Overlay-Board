#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

inline std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

inline std::string WideToUtf8(std::wstring_view s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

inline std::wstring ToLower(std::wstring s) {
  if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
  return s;
}

inline bool ContainsI(const std::wstring& haystack, const std::wstring& needle) {
  if (needle.empty()) return true;
  return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

inline std::filesystem::path ExeDir() {
  wchar_t buf[MAX_PATH * 4]{};
  GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  return std::filesystem::path(buf).parent_path();
}

// Where config.json, the log and the image folders live: next to the exe when that folder is
// writable (portable / per-user install), otherwise %LOCALAPPDATA%\EDOverlayBoard.
inline std::filesystem::path DataDir() {
  static const std::filesystem::path dir = [] {
    const auto exe = ExeDir();
    HANDLE h = CreateFileW((exe / L".edob-write-test").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
      return exe;
    }
    wchar_t buf[MAX_PATH * 2]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
    std::filesystem::path d = (n > 0 && n < std::size(buf)) ? std::filesystem::path(buf) / L"EDOverlayBoard" : exe;
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
  }();
  return dir;
}
