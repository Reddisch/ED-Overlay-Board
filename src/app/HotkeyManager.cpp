#include "HotkeyManager.h"

#include "Log.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string Trim(const std::string& s) {
  auto b = s.find_first_not_of(" \t");
  auto e = s.find_last_not_of(" \t");
  return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
}

const std::map<std::string, UINT>& KeyNames() {
  static const std::map<std::string, UINT> names = [] {
    std::map<std::string, UINT> m = {
        {"space", VK_SPACE},     {"enter", VK_RETURN},       {"tab", VK_TAB},         {"escape", VK_ESCAPE},
        {"backspace", VK_BACK},  {"insert", VK_INSERT},      {"delete", VK_DELETE},   {"home", VK_HOME},
        {"end", VK_END},         {"pageup", VK_PRIOR},       {"pagedown", VK_NEXT},   {"up", VK_UP},
        {"down", VK_DOWN},       {"left", VK_LEFT},          {"right", VK_RIGHT},     {"pause", VK_PAUSE},
        {"scrolllock", VK_SCROLL}, {"numpadadd", VK_ADD},    {"numpadsubtract", VK_SUBTRACT},
        {"numpadmultiply", VK_MULTIPLY}, {"numpaddivide", VK_DIVIDE}, {"numpaddecimal", VK_DECIMAL},
    };
    for (char c = 'a'; c <= 'z'; ++c) m[std::string(1, c)] = static_cast<UINT>(std::toupper(c));
    for (char c = '0'; c <= '9'; ++c) m[std::string(1, c)] = static_cast<UINT>(c);
    for (int i = 0; i <= 9; ++i) m["numpad" + std::to_string(i)] = VK_NUMPAD0 + i;
    for (int i = 1; i <= 24; ++i) m["f" + std::to_string(i)] = VK_F1 + i - 1;
    return m;
  }();
  return names;
}

}  // namespace

bool HotkeyManager::Parse(const std::string& spec, UINT& modifiers, UINT& vk) {
  modifiers = 0;
  vk = 0;
  std::stringstream ss(spec);
  std::string part;
  while (std::getline(ss, part, '+')) {
    auto p = Lower(Trim(part));
    if (p == "ctrl" || p == "control") modifiers |= MOD_CONTROL;
    else if (p == "alt") modifiers |= MOD_ALT;
    else if (p == "shift") modifiers |= MOD_SHIFT;
    else if (p == "win") modifiers |= MOD_WIN;
    else {
      auto it = KeyNames().find(p);
      if (it == KeyNames().end() || vk != 0) return false;
      vk = it->second;
    }
  }
  return vk != 0;
}

std::string HotkeyManager::KeyName(UINT vk) {
  static const std::map<UINT, std::string> pretty = [] {
    std::map<UINT, std::string> m = {
        {VK_SPACE, "Space"},   {VK_RETURN, "Enter"},     {VK_TAB, "Tab"},         {VK_ESCAPE, "Escape"},
        {VK_BACK, "Backspace"}, {VK_INSERT, "Insert"},   {VK_DELETE, "Delete"},   {VK_HOME, "Home"},
        {VK_END, "End"},       {VK_PRIOR, "PageUp"},     {VK_NEXT, "PageDown"},   {VK_UP, "Up"},
        {VK_DOWN, "Down"},     {VK_LEFT, "Left"},        {VK_RIGHT, "Right"},     {VK_PAUSE, "Pause"},
        {VK_SCROLL, "ScrollLock"}, {VK_ADD, "NumpadAdd"}, {VK_SUBTRACT, "NumpadSubtract"},
        {VK_MULTIPLY, "NumpadMultiply"}, {VK_DIVIDE, "NumpadDivide"}, {VK_DECIMAL, "NumpadDecimal"},
    };
    for (char c = 'A'; c <= 'Z'; ++c) m[static_cast<UINT>(c)] = std::string(1, c);
    for (char c = '0'; c <= '9'; ++c) m[static_cast<UINT>(c)] = std::string(1, c);
    for (int i = 0; i <= 9; ++i) m[VK_NUMPAD0 + i] = "Numpad" + std::to_string(i);
    for (int i = 1; i <= 24; ++i) m[VK_F1 + i - 1] = "F" + std::to_string(i);
    return m;
  }();
  auto it = pretty.find(vk);
  return it == pretty.end() ? std::string{} : it->second;
}

std::string HotkeyManager::Format(UINT modifiers, UINT vk) {
  std::string name = KeyName(vk);
  if (name.empty()) return {};
  std::string s;
  if (modifiers & MOD_CONTROL) s += "Ctrl+";
  if (modifiers & MOD_ALT) s += "Alt+";
  if (modifiers & MOD_SHIFT) s += "Shift+";
  if (modifiers & MOD_WIN) s += "Win+";
  return s + name;
}

bool HotkeyManager::Register(int id, const std::string& spec) {
  UINT mods = 0, vk = 0;
  if (!Parse(spec, mods, vk)) {
    Log("Hotkey: cannot parse \"{}\"", spec);
    return false;
  }
  if (!RegisterHotKey(nullptr, id, mods | MOD_NOREPEAT, vk)) {
    Log("Hotkey: \"{}\" is already in use by another program", spec);
    return false;
  }
  ids_.push_back(id);
  return true;
}

void HotkeyManager::UnregisterAll() {
  for (int id : ids_) UnregisterHotKey(nullptr, id);
  ids_.clear();
}
