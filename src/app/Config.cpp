#include "Config.h"

#include "Log.h"

#include <cmath>
#include <fstream>

using json = nlohmann::ordered_json;

namespace {

Vec3 ReadVec3(const json& j, const char* key, Vec3 def) {
  if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3) return def;
  return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>()};
}

double Round(float v, double step) {
  const double inv = std::round(1.0 / step);  // divide by an integer so 0.239 stays 0.239
  return std::round(v * inv) / inv;
}

json Vec3Json(const Vec3& v, double step) { return json::array({Round(v.x, step), Round(v.y, step), Round(v.z, step)}); }

}  // namespace

std::string WindowConfig::SourceKey() const {
  std::string k = type + "|" + processName + "|" + titleContains + "|" + (hideTitleBar ? "1" : "0") + "|" +
                  (captureCursor ? "1" : "0") + "|" + folder;
  for (int v : trim) k += "|" + std::to_string(v);
  if (hasCrop)
    for (int v : crop) k += "|c" + std::to_string(v);
  for (const auto& i : images) k += "|" + i;
  return k;
}

bool AppConfig::Load(const std::filesystem::path& path) {
  file = path;
  std::ifstream in(path);
  if (!in) {
    Log("Config: cannot open {}", path.string());
    return false;
  }
  try {
    raw_ = json::parse(in, nullptr, /*allow_exceptions*/ true, /*ignore_comments*/ true);
  } catch (const std::exception& e) {
    Log("Config: parse error in {}: {}", path.string(), e.what());
    return false;
  }

  try {
    if (raw_.contains("settings")) {
      const auto& s = raw_["settings"];
      showCursor = s.value("showCursor", showCursor);
      haptics = s.value("haptics", haptics);
      scaleSpeed = s.value("scaleSpeed", scaleSpeed);
      pushPullSpeed = s.value("pushPullSpeed", pushPullSpeed);
      lookToShow = s.value("lookToShow", lookToShow);
      lookMarginDeg = s.value("lookMarginDeg", lookMarginDeg);
      sharpness = s.value("sharpness", sharpness);
      menuWidth = s.value("menuWidth", menuWidth);
    }

    windows.clear();
    for (const auto& w : raw_.value("windows", json::array())) {
      WindowConfig c;
      c.name = w.value("name", c.name);
      c.type = w.value("type", c.type);
      c.titleContains = w.value("titleContains", "");
      c.processName = w.value("processName", "");
      c.hideTitleBar = w.value("hideTitleBar", false);
      if (w.contains("trim") && w["trim"].is_array() && w["trim"].size() == 4)
        for (int i = 0; i < 4; ++i) c.trim[i] = w["trim"][i].get<int>();
      c.captureCursor = w.value("captureCursor", false);
      c.forwardClicks = w.value("forwardClicks", true);
      if (w.contains("crop") && w["crop"].is_array() && w["crop"].size() == 4) {
        c.hasCrop = true;
        for (int i = 0; i < 4; ++i) c.crop[i] = w["crop"][i].get<int>();
      }
      c.folder = w.value("folder", "");
      for (const auto& img : w.value("images", json::array())) c.images.push_back(img.get<std::string>());
      c.page = w.value("page", 0);
      c.position = ReadVec3(w, "position", c.position);
      c.rotation = ReadVec3(w, "rotation", c.rotation);
      c.width = w.value("width", c.width);
      c.opacity = w.value("opacity", c.opacity);
      c.visible = w.value("visible", c.visible);
      c.locked = w.value("locked", c.locked);
      if (w.contains("lookToShow")) c.lookToShow = w["lookToShow"].get<bool>() ? 1 : 0;
      windows.push_back(std::move(c));
    }

    bindings.clear();
    for (const auto& b : raw_.value("bindings", json::array())) {
      BindingConfig c;
      c.action = b.value("action", "");
      c.window = b.value("window", "");
      c.key = b.value("key", "");
      c.joystick = b.value("joystick", "");
      c.button = b.value("button", 0);
      bindings.push_back(std::move(c));
    }
  } catch (const std::exception& e) {
    Log("Config: invalid value in {}: {}", path.string(), e.what());
    return false;
  }

  Log("Config: loaded {} window(s), {} binding(s) from {}", windows.size(), bindings.size(), path.string());
  return true;
}

bool AppConfig::Save() {
  json s = json::object();
  s["showCursor"] = showCursor;
  s["haptics"] = haptics;
  s["scaleSpeed"] = Round(scaleSpeed, 0.01);
  s["pushPullSpeed"] = Round(pushPullSpeed, 0.01);
  s["lookToShow"] = lookToShow;
  s["lookMarginDeg"] = Round(lookMarginDeg, 0.1);
  s["sharpness"] = Round(sharpness, 0.01);
  s["menuWidth"] = Round(menuWidth, 0.001);

  json ws = json::array();
  for (const auto& c : windows) {
    json w = json::object();
    w["name"] = c.name;
    w["type"] = c.type;
    if (c.type == "window") {
      if (!c.processName.empty()) w["processName"] = c.processName;
      if (!c.titleContains.empty()) w["titleContains"] = c.titleContains;
      w["hideTitleBar"] = c.hideTitleBar;
      if (c.trim[0] || c.trim[1] || c.trim[2] || c.trim[3])
        w["trim"] = json::array({c.trim[0], c.trim[1], c.trim[2], c.trim[3]});
      if (c.hasCrop) w["crop"] = json::array({c.crop[0], c.crop[1], c.crop[2], c.crop[3]});
      if (c.captureCursor) w["captureCursor"] = true;
      if (!c.forwardClicks) w["forwardClicks"] = false;
    } else {
      if (!c.folder.empty()) w["folder"] = c.folder;
      if (!c.images.empty()) w["images"] = c.images;
      w["page"] = c.page;
    }
    w["position"] = Vec3Json(c.position, 0.001);
    w["rotation"] = Vec3Json(c.rotation, 0.1);
    w["width"] = Round(c.width, 0.001);
    if (c.opacity < 0.999f) w["opacity"] = Round(c.opacity, 0.01);
    w["visible"] = c.visible;
    if (c.locked) w["locked"] = true;
    if (c.lookToShow >= 0) w["lookToShow"] = c.lookToShow == 1;
    ws.push_back(std::move(w));
  }

  json bs = json::array();
  for (const auto& b : bindings) {
    json j = json::object();
    j["action"] = b.action;
    if (!b.window.empty()) j["window"] = b.window;
    if (!b.key.empty()) j["key"] = b.key;
    if (b.button > 0) {
      j["joystick"] = b.joystick;
      j["button"] = b.button;
    }
    bs.push_back(std::move(j));
  }

  raw_["settings"] = std::move(s);
  raw_["windows"] = std::move(ws);
  raw_["bindings"] = std::move(bs);

  auto tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      Log("Config: cannot write {}", tmp.string());
      return false;
    }
    out << raw_.dump(2) << "\n";
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  if (ec) {
    Log("Config: cannot replace {}: {}", file.string(), ec.message());
    return false;
  }
  return true;
}
