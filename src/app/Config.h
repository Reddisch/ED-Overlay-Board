#pragma once

#include "VrMath.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
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
  bool forwardClicks = true;  // trigger / pinch in VR clicks into the captured program
  bool dragScroll = true;     // forwardClicks: dragging scrolls like on a touch screen (hold still first = mouse drag)

  // type == "images"
  std::string folder;               // folder with PNG/JPG/... files (relative to config dir)
  std::vector<std::string> images;  // explicit file list (relative to config dir)
  int page = 0;

  // placement (relative to the recentered head position, meters / degrees)
  Vec3 position{0.0f, 0.0f, -1.0f};
  Vec3 rotation{0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
  float width = 0.5f;
  float opacity = 1.0f;
  bool transparent = true;          // apply opacity (switchable by toggleTransparency)
  float tint[3]{1.0f, 1.0f, 1.0f};  // filter color multiplied onto the content (dims bright pictures)
  float brightness = 1.0f;          // extra dimming factor on top of the tint
  int showEffect = 0;               // ipc::ShowEffect (0 = none)

  // RGB multiplier sent with the pixels (white = unchanged)
  void TintFactor(float out[3]) const;
  bool Tinted() const;
  // opacity baked into the texture alpha (1 = opaque)
  float EffectiveOpacity() const;
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
  std::vector<int> buttons;  // 1-based joystick button numbers (as in Windows "Game Controllers"),
                             // several = combination pressed together on that device
  std::string ButtonsText() const;  // e.g. "5 + 12"
};

// Where a window sits (and whether it is shown) in one ship type / vehicle
struct ShipPlacement {
  Vec3 position{0.0f, 0.0f, -1.0f};
  Vec3 rotation{0.0f, 0.0f, 0.0f};
  float width = 0.5f;
  bool visible = true;
};

struct AppConfig {
  std::vector<WindowConfig> windows;
  std::vector<BindingConfig> bindings;
  bool showCursor = true;
  bool haptics = true;
  float clickVolume = 0.5f;    // click sound in VR (0 = off)
  float showSoundVolume = 0.5f; // sound when a window appears / disappears, matching its effect (0 = off)
  bool showSoundOnLook = false; // also when it appears / disappears by looking at it / away (lookToShow)
  float scaleSpeed = 1.0f;     // thumbstick resize speed
  float pushPullSpeed = 0.75f; // thumbstick push/pull speed (m/s)
  float swipeSpeed = 1.0f;     // drag-to-scroll: wheel notches per ~60 pixels of dragging
  bool lookToShow = true;      // windows only appear when looking towards them
  float lookMarginDeg = 10.0f; // extra angle around a window that still counts as looking at it
  bool onlyInGame = true;         // windows (not the VR menu) only while in the game, not in the main menu
  bool headAim = false;           // VR pointer follows the view center, the hand only clicks / grabs
  float pointerSmoothing = 0.5f;  // VR pointer: 0 = raw, 1 = strongest smoothing (for hand tracking)
  float sharpness = 1.0f;      // 1 = smooth (anti-aliased), 1.5-2 = sharper but may shimmer
  float menuWidth = 0.40f;     // VR quick menu width (m)
  std::string language;        // "de", "en" or "" = Windows display language
  bool mirror = false;          // spectator window (game picture + overlay windows) for OBS
  int mirrorWidth = 1920, mirrorHeight = 1080;
  float mirrorZoom = 1.0f;      // 1 = widest view, larger = narrower (less fish-eye)
  bool perShipLayouts = true;  // remember window placement + visibility per ship type (from the game journal)
  // ship type / vehicle ("krait_mkii", "srv", "onfoot", ...) -> window name -> placement
  std::map<std::string, std::map<std::string, ShipPlacement>> shipLayouts;

  std::filesystem::path file;
  std::filesystem::path Dir() const { return file.parent_path(); }

  bool Load(const std::filesystem::path& path);
  bool Save();

 private:
  nlohmann::ordered_json raw_;  // keeps unknown top-level keys
};
