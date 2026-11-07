#include "LayerRegistration.h"

#include "Log.h"
#include "Util.h"

#include <windows.h>

#include <string>
#include <vector>

namespace {

constexpr wchar_t kKey[] = L"Software\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
constexpr wchar_t kManifestName[] = L"EDOverlayBoard_Layer.json";

struct Entry {
  std::wstring path;
  DWORD value;
};

std::vector<Entry> ReadEntries(HKEY key) {
  std::vector<Entry> out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[2048];
    DWORD nameLen = static_cast<DWORD>(std::size(name));
    DWORD type = 0, data = 0, dataLen = sizeof(data);
    LSTATUS st = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(&data), &dataLen);
    if (st == ERROR_NO_MORE_ITEMS) break;
    if (st == ERROR_SUCCESS && type == REG_DWORD) out.push_back({name, data});
  }
  return out;
}

bool IsOurs(const std::wstring& path) {
  return ToLower(std::filesystem::path(path).filename().wstring()) == ToLower(kManifestName);
}

}  // namespace

namespace LayerRegistration {

std::filesystem::path ManifestPath() { return ExeDir() / kManifestName; }

bool IsRegistered() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  auto me = ToLower(ManifestPath().wstring());
  bool found = false;
  for (const auto& e : ReadEntries(key))
    if (ToLower(e.path) == me && e.value == 0) found = true;
  RegCloseKey(key);
  return found;
}

bool Register() {
  if (!std::filesystem::exists(ManifestPath())) {
    Log("Layer manifest missing: {}", ManifestPath().string());
    return false;
  }
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    Log("Cannot open registry key HKCU\\{}", WideToUtf8(kKey));
    return false;
  }
  auto me = ManifestPath().wstring();
  for (const auto& e : ReadEntries(key))
    if (IsOurs(e.path) && ToLower(e.path) != ToLower(me)) {
      RegDeleteValueW(key, e.path.c_str());
      Log("Removed old layer registration: {}", WideToUtf8(e.path));
    }
  DWORD zero = 0;  // 0 = enabled
  LSTATUS st = RegSetValueExW(key, me.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&zero), sizeof(zero));
  RegCloseKey(key);
  if (st != ERROR_SUCCESS) {
    Log("Cannot register OpenXR layer (error {})", st);
    return false;
  }
  Log("OpenXR layer registered: {}", WideToUtf8(me));
  return true;
}

bool Unregister() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS) return true;
  bool any = false;
  for (const auto& e : ReadEntries(key))
    if (IsOurs(e.path)) {
      RegDeleteValueW(key, e.path.c_str());
      Log("Removed layer registration: {}", WideToUtf8(e.path));
      any = true;
    }
  RegCloseKey(key);
  if (!any) Log("The OpenXR layer was not registered.");
  return true;
}

}  // namespace LayerRegistration
