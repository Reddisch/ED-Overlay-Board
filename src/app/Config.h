#pragma once

#include "VrMath.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

struct WindowConfig {
  std::string name = "Fenster";
  std::string type = "window";  // "window" (capture a desktop window) | "images" (kneeboard pages)

  // type == "window"
  std::string titleContains;  // case-insensitive substring of the window title
  std::string processName;    // e.g. "EDCoPilotGUI" (case-insensitive substring of the exe name)
  bool hideTitleBar = false;  // capture only the client area (no title bar / borders)
  int trim[4]{};              // cut off left, top, right, bottom (pixels, after hideTitleBar)
  bool hasCrop = false;
  int crop[4]{};              // legacy: x, y, width, height in window pixels
  bool captureCursor = false;

  // type == "images"
  std::string folder;               // folder with PNG/JPG/... files (relative to config dir)
  std::vector<std::string> images;  // explicit file list (relative to config dir)
  int page = 0;

  // placement (relative to the recentered head position, meters / degrees)
  Vec3 position{0.0f, 0.0f, -1.0f};
  Vec3 rotation{0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
  float width = 0.5f;
  float opacity = 1.0f;
  bool visible = true;
  bool locked = false;
  int lookToShow = -1;  // -1 = use settings.lookToShow, 0 = always visible, 1 = only when looking at it

  // what makes the content source different (recreate the source when this changes)
  std::string SourceKey() const;
};

struct BindingConfig {
  std::string action;    // see kActions in Gui.cpp
  std::string window;    // target window name (toggleWindow / nextPage / prevPage)
  std::string key;       // e.g. "Ctrl+Alt+Numpad5"
  std::string joystick;  // substring of the DirectInput device name ("" = any device)
  int button = 0;        // 1-based joystick button number (as shown in Windows "Game Controllers")
};

struct AppConfig {
  std::vector<WindowConfig> windows;
  std::vector<BindingConfig> bindings;
  bool showCursor = true;
  bool haptics = true;
  float scaleSpeed = 1.0f;     // thumbstick resize speed
  float pushPullSpeed = 0.75f; // thumbstick push/pull speed (m/s)
  bool lookToShow = true;      // windows only appear when looking towards them
  float lookMarginDeg = 10.0f; // extra angle around a window that still counts as looking at it
  float sharpness = 1.0f;      // 1 = smooth (anti-aliased), 1.5-2 = sharper but may shimmer
  float menuWidth = 0.40f;     // VR quick menu width (m)

  std::filesystem::path file;
  std::filesystem::path Dir() const { return file.parent_path(); }

  bool Load(const std::filesystem::path& path);
  bool Save();

 private:
  nlohmann::ordered_json raw_;  // keeps unknown top-level keys
};
