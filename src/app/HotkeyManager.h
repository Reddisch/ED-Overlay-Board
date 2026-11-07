#pragma once

#include <windows.h>

#include <string>
#include <vector>

// System-wide keyboard shortcuts via RegisterHotKey. WM_HOTKEY is posted to the
// thread's message queue with wParam = id.
class HotkeyManager {
 public:
  ~HotkeyManager() { UnregisterAll(); }
  bool Register(int id, const std::string& spec);
  void UnregisterAll();
  static bool Parse(const std::string& spec, UINT& modifiers, UINT& vk);
  static std::string KeyName(UINT vk);                // "" if the key is not supported
  static std::string Format(UINT modifiers, UINT vk);  // e.g. "Ctrl+Alt+Numpad5"

 private:
  std::vector<int> ids_;
};
