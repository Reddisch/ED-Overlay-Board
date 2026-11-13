#include "OverlaySession.h"

#include "Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kMaxPointDistance = 8.0f;  // meters
constexpr float kStickDeadzone = 0.25f;
constexpr float kCursorSize = 0.012f;
constexpr double kZoneGraceSeconds = 0.2;  // pinch shortly after leaving the grab zone still grabs
constexpr double kTapSeconds = 0.35;  // hand tracking: shorter pinch = click
constexpr float kTapMove = 0.03f;

uint32_t Bgra(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return (a << 24) | (r << 16) | (g << 8) | b; }

std::vector<uint32_t> CursorPixels(int n) {
  std::vector<uint32_t> px(static_cast<size_t>(n * n));
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float dx = x - (n - 1) * 0.5f, dy = y - (n - 1) * 0.5f;
      float r = std::sqrt(dx * dx + dy * dy) / (n * 0.5f);
      uint32_t c = 0;
      if (r < 0.45f) c = Bgra(255, 255, 255, 255);
      else if (r < 0.8f) c = Bgra(255, 140, 25, 255);
      else if (r < 0.95f) c = Bgra(255, 140, 25, static_cast<uint32_t>(255 * (0.95f - r) / 0.15f));
      px[static_cast<size_t>(y * n + x)] = c;
    }
  return px;
}

}  // namespace

OverlaySession::OverlaySession(const Params& p)
    : next_(*p.next),
      instance_(p.instance),
      system_(p.system),
      session_(p.session),
      device_(p.device),
      actions_(*p.actions),
      handTrackingExt_(p.handTracking),
      colorScaleBias_(p.colorScaleBias),
      runtimeName_(p.runtimeName),
      appName_(p.appName) {
  Init();
}

OverlaySession::~OverlaySession() {
  for (auto& w : windows_) DestroySwapchain(w.swapchain);
  DestroySwapchain(cursor_);
  DestroySwapchain(frameHover_);
  DestroySwapchain(frameZone_);
  DestroySwapchain(frameGrab_);
  DestroySwapchain(frameKeys_);
  DestroySwapchain(glow_);
  input_.Destroy();
  if (localSpace_ != XR_NULL_HANDLE) next_.xrDestroySpace(localSpace_);
  if (viewSpace_ != XR_NULL_HANDLE) next_.xrDestroySpace(viewSpace_);
}

void OverlaySession::Init() {
  device_.As(&device1_);
  device_->GetImmediateContext(context_.GetAddressOf());
  UINT support = 0;
  if (FAILED(device_->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, &support)) ||
      !(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN))
    mipSrvFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM;  // filter in gamma space instead
  ComPtr<IDXGIDevice> dxgi;
  ComPtr<IDXGIAdapter> adapter;
  if (SUCCEEDED(device_.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(adapter.GetAddressOf()))) {
    DXGI_ADAPTER_DESC d{};
    adapter->GetDesc(&d);
    adapterLuid_ = d.AdapterLuid;
  }

  XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
  XrSystemHandTrackingPropertiesEXT ht{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
  if (handTrackingExt_) sp.next = &ht;
  if (XR_SUCCEEDED(next_.xrGetSystemProperties(instance_, system_, &sp))) {
    maxLayers_ = std::max<uint32_t>(sp.graphicsProperties.maxLayerCount, 1);
    Log("System: {} (max {} layers)", sp.systemName, maxLayers_);
  }
  bool handTracking = handTrackingExt_ && ht.supportsHandTracking;
  if (handTrackingExt_ && !ht.supportsHandTracking)
    Log("Runtime has XR_EXT_hand_tracking but reports supportsHandTracking = false (enable hand tracking in Virtual Desktop?)");

  XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  rs.poseInReferenceSpace.orientation.w = 1.0f;
  rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  next_.xrCreateReferenceSpace(session_, &rs, &localSpace_);
  rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  next_.xrCreateReferenceSpace(session_, &rs, &viewSpace_);

  uint32_t n = 0;
  next_.xrEnumerateSwapchainFormats(session_, 0, &n, nullptr);
  std::vector<int64_t> formats(n);
  next_.xrEnumerateSwapchainFormats(session_, n, &n, formats.data());
  for (int64_t want : {static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB), static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM)})
    if (!format_ && std::find(formats.begin(), formats.end(), want) != formats.end()) format_ = want;
  if (!format_ || localSpace_ == XR_NULL_HANDLE) {
    Log("Runtime offers no BGRA8 swapchain format / no LOCAL space - overlay disabled");
    disabled_ = true;
    return;
  }

  input_.Init(next_, session_, actions_, handTracking);

  CreateStaticSwapchain(64, 64, CursorPixels(64), cursor_);
  CreateStaticSwapchain(4, 4, std::vector<uint32_t>(16, Bgra(255, 140, 25, 70)), frameHover_);
  CreateStaticSwapchain(4, 4, std::vector<uint32_t>(16, Bgra(255, 140, 25, 190)), frameZone_);
  CreateStaticSwapchain(4, 4, std::vector<uint32_t>(16, Bgra(255, 140, 25, 255)), frameGrab_);
  CreateStaticSwapchain(4, 4, std::vector<uint32_t>(16, Bgra(60, 190, 255, 255)), frameKeys_);
  CreateStaticSwapchain(4, 4, std::vector<uint32_t>(16, Bgra(255, 205, 130, 255)), glow_);

  quads_.reserve(ipc::kMaxWindows * 5 + 4);
  colorScales_.reserve(ipc::kMaxWindows + 1);
  Log("Overlay session ready (swapchain format {})", format_);
}

// ---------------------------------------------------------------------------
// Swapchains

bool OverlaySession::CreateSwapchain(uint32_t w, uint32_t h, Swapchain& sc) {
  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                  XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  ci.format = format_;
  ci.sampleCount = 1;
  ci.width = w;
  ci.height = h;
  ci.faceCount = 1;
  ci.arraySize = 1;
  ci.mipCount = 1;
  if (XR_FAILED(next_.xrCreateSwapchain(session_, &ci, &sc.handle))) {
    Log("xrCreateSwapchain {}x{} failed", w, h);
    sc = {};
    return false;
  }
  uint32_t count = 0;
  next_.xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr);
  std::vector<XrSwapchainImageD3D11KHR> imgs(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
  next_.xrEnumerateSwapchainImages(sc.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
  sc.images.clear();
  for (const auto& i : imgs) sc.images.push_back(i.texture);
  sc.width = w;
  sc.height = h;
  return true;
}

void OverlaySession::DestroySwapchain(Swapchain& sc) {
  if (sc.handle != XR_NULL_HANDLE) next_.xrDestroySwapchain(sc.handle);
  sc = {};
}

bool OverlaySession::WriteSwapchain(Swapchain& sc, const std::function<void(ID3D11Texture2D*)>& fn) {
  uint32_t idx = 0;
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XrResult r = next_.xrAcquireSwapchainImage(sc.handle, &ai, &idx);
  if (XR_FAILED(r)) {
    lastError_ = std::format("xrAcquireSwapchainImage failed ({})", static_cast<int>(r));
    return false;
  }
  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = 1'000'000'000;  // 1 s
  do {
    r = next_.xrWaitSwapchainImage(sc.handle, &wi);
  } while (r == XR_TIMEOUT_EXPIRED);
  if (XR_FAILED(r)) {
    lastError_ = std::format("xrWaitSwapchainImage failed ({})", static_cast<int>(r));
    return false;
  }
  if (idx < sc.images.size() && sc.images[idx]) fn(sc.images[idx]);
  else lastError_ = std::format("swapchain image {} of {} is null", idx, sc.images.size());
  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  r = next_.xrReleaseSwapchainImage(sc.handle, &ri);
  if (XR_FAILED(r)) {
    lastError_ = std::format("xrReleaseSwapchainImage failed ({})", static_cast<int>(r));
    return false;
  }
  return true;
}

bool OverlaySession::CreateStaticSwapchain(uint32_t w, uint32_t h, const std::vector<uint32_t>& bgra, Swapchain& sc) {
  if (!CreateSwapchain(w, h, sc)) return false;
  bool ok = WriteSwapchain(sc, [&](ID3D11Texture2D* dst) {
    context_->UpdateSubresource(dst, 0, nullptr, bgra.data(), w * 4, 0);
  });
  if (!ok) Log("Static swapchain {}x{}: {}", w, h, lastError_);
  return ok;
}

void OverlaySession::SetStatus(int i, const std::string& status) {
  Win& w = windows_[i];
  if (w.status == status) return;
  w.status = status;
  Log("Window [{}] '{}': {}", i, w.name, status);
}

// ---------------------------------------------------------------------------
// Shared state with the companion app

void OverlaySession::ResetWindows() {
  for (int h = 0; h < 2; ++h) {
    grabs_[h] = {};
    mouseCapture_[h] = -1;
  }
  for (auto& w : windows_) {
    w.shared.Reset();
    w.keyedMutex.Reset();
    w.openedGen = 0;
    w.copiedVersion = 0;
    w.mipVersion = 0;
    w.appPlacementSerial = 0;
    w.placementDirty = w.clickPending = false;
  }
}

void OverlaySession::SyncIpc() {
  const uint64_t now = GetTickCount64();
  if (!shm_.Valid()) {
    connected_ = false;
    if (now < nextOpenTry_) return;
    nextOpenTry_ = now + 2000;
    if (!shm_.Open()) return;
  }
  auto lock = shm_.Acquire(1);
  if (!lock) return;  // keep last frame's state
  ipc::State* s = shm_.Get();
  if (s->magic != ipc::kMagic || s->version != ipc::kVersion) {
    connected_ = false;
    return;
  }
  connected_ = s->appHeartbeat != 0 && now - s->appHeartbeat < ipc::kHeartbeatTimeoutMs;
  if (connected_ != wasConnected_) {
    wasConnected_ = connected_;
    Log("{}", connected_ ? "Connected to the companion app" : "Companion app not running");
  }
  if (!connected_) return;

  if (s->appInstance != appInstance_) {
    appInstance_ = s->appInstance;
    ResetWindows();
  }
  if (s->recenterSerial != recenterSerial_) {
    recenterSerial_ = s->recenterSerial;
    recenterPending_ = true;
  }
  allHidden_ = s->allHidden;
  interaction_ = s->interactionEnabled;
  showCursor_ = s->showCursor;
  haptics_ = s->haptics;
  scaleSpeed_ = s->scaleSpeed;
  pushPullSpeed_ = s->pushPullSpeed;
  lookMarginDeg_ = s->lookMarginDeg;
  sharpness_ = s->sharpness > 0.1f ? s->sharpness : 1.0f;

  s->layerHeartbeat = now;
  s->layerPid = GetCurrentProcessId();
  s->layerAdapter = adapterLuid_;
  s->layerHandTracking = input_.HandTracking();
  s->layerControllers = actions_.Attached(session_);
  strncpy_s(s->layerRuntime, runtimeName_.c_str(), _TRUNCATE);
  strncpy_s(s->layerApplication, appName_.c_str(), _TRUNCATE);

  for (int i = 0; i < ipc::kMaxWindows; ++i) {
    auto& sw = s->windows[i];
    auto& w = windows_[i];
    w.active = i < static_cast<int>(s->windowCount) && sw.active;
    w.name.assign(sw.name, strnlen(sw.name, sizeof(sw.name)));
    const bool wasVisible = w.visible;
    w.visible = sw.visible;
    if (w.visible && !wasVisible && sw.summonOnShow) w.summonPending = true;
    w.locked = sw.locked;
    w.opaque = sw.opaque;
    w.lookToShow = sw.lookToShow;
    w.interactive = sw.interactive;
    w.mouse = sw.mouseInput != 0;
    w.headerFraction = sw.headerFraction;
    w.keyboardFocus = sw.keyboardFocus != 0;
    w.opacity = sw.opacity;
    w.effect = sw.showEffect < ipc::kEffectCount ? sw.showEffect : 0;
    w.texW = sw.texWidth;
    w.texH = sw.texHeight;
    w.texGen = sw.texGeneration;
    w.contentVersion = sw.contentVersion;

    bool grabbed = grabs_[0].window == i || grabs_[1].window == i;
    if (sw.appPlacementSerial != w.appPlacementSerial && !grabbed) {
      w.appPlacementSerial = sw.appPlacementSerial;
      w.rel = sw.appPlacement.ToPose();
      w.width = sw.appPlacement.width;
    }
    if (w.placementDirty) {
      sw.layerPlacement = ipc::Placement::From(w.rel, w.width);
      ++sw.layerPlacementSerial;
      w.placementDirty = false;
    }
    if (w.clickPending) {
      sw.clickX = w.clickX;
      sw.clickY = w.clickY;
      ++sw.clickSerial;
      w.clickPending = false;
    }
    if (w.mouse) {
      sw.pointerActive = w.pointerActive;
      sw.pointerX = w.pointerX;
      sw.pointerY = w.pointerY;
      sw.pointerDownSerial += w.pendingDown;
      sw.pointerUpSerial += w.pendingUp;
      sw.pointerWheel += w.pendingWheel;
      sw.pointerWheelX += w.pendingWheelX;
      w.pendingDown = w.pendingUp = 0;
      w.pendingWheel = w.pendingWheelX = 0;
    }
  }
}

void OverlaySession::SummonWindows() {
  if (!headValid_) return;
  for (auto& w : windows_) {
    if (!w.summonPending) continue;
    w.summonPending = false;
    if (w.interactive) continue;  // the VR menu is placed by UpdateHeadFollow
    // half a meter in front of the face, slightly below eye level, facing the user
    Pose yaw = head_.YawOnly();
    Vec3 pos = yaw.Apply({0.0f, -0.12f, -0.5f});
    Pose world = Pose::LookRotation(pos, (pos - head_.t).Normalized());
    w.rel = center_.Inverse() * world;
    w.placementDirty = true;
  }
}

// The VR menu cannot be moved: it stays in the middle of the view at reading distance and goes
// along with the head. Only the direction is smoothed (short lag = calm text, no jitter), the
// head position is followed directly. It always stays upright (no roll) and faces the eyes.
void OverlaySession::UpdateHeadFollow(float dt) {
  constexpr float kDistance = 0.55f;       // meters in front of the eyes
  constexpr float kFollowSeconds = 0.12f;  // smoothing time constant
  constexpr float kDeadzoneDeg = 1.0f;     // smaller head movements do not move it (sharp text)
  constexpr float kMaxPitch = 0.94f;       // |y| of the direction: ~70 deg up/down, never flips over
  for (auto& w : windows_) {
    if (!w.interactive) continue;
    if (!w.active || !w.visible) {
      w.followValid = false;  // shown again: appears right in front, no sweep from the old place
      continue;
    }
    if (!headValid_ || !centered_) continue;
    const Vec3 forward = head_.Forward().Normalized();
    if (!w.followValid) {
      w.followDir = forward;
      w.followValid = true;
    } else {
      const float angle = std::acos(std::clamp(w.followDir.Dot(forward), -1.0f, 1.0f)) * kRadToDeg;
      if (angle > kDeadzoneDeg) {
        const float a = 1.0f - std::exp(-dt / kFollowSeconds);
        w.followDir = (w.followDir + (forward - w.followDir) * a).Normalized();
      }
    }
    Vec3 dir = w.followDir;
    if (std::fabs(dir.y) > kMaxPitch) {
      const float horizontal = std::sqrt(dir.x * dir.x + dir.z * dir.z);
      const float scale = horizontal > 1e-4f ? std::sqrt(1.0f - kMaxPitch * kMaxPitch) / horizontal : 0.0f;
      dir = scale > 0.0f ? Vec3{dir.x * scale, std::copysign(kMaxPitch, dir.y), dir.z * scale} : head_.YawOnly().Forward();
    }
    const Vec3 pos = head_.t + dir * kDistance;
    w.rel = center_.Inverse() * Pose::LookRotation(pos, dir);
  }
}

// ---------------------------------------------------------------------------
// Interaction

void OverlaySession::UpdateHead(XrTime time) {
  XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
  const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
  const XrSpaceLocationFlags tracked =
      XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
  headValid_ = XR_SUCCEEDED(next_.xrLocateSpace(viewSpace_, localSpace_, time, &loc)) &&
               (loc.locationFlags & need) == need;
  if (headValid_) head_ = ToPose(loc.pose);

  // Right after session start runtimes may report a "valid" placeholder pose (e.g. VDXR:
  // 1.85 m below the origin, looking exactly forward). Only trust really tracked poses
  // that have been stable for a few frames.
  if (headValid_ && (loc.locationFlags & tracked) == tracked) {
    if (trackedFrames_ > 0 && (head_.t - lastTrackedPos_).Length() > 0.05f) trackedFrames_ = 0;  // jumped
    lastTrackedPos_ = head_.t;
    ++trackedFrames_;
  } else {
    trackedFrames_ = 0;
  }
  // Fallback for runtimes that never set the TRACKED bits: a valid pose that is no longer the
  // exact "looking straight ahead" placeholder, held for ~3 s.
  const bool placeholder = std::fabs(head_.r[0][2]) < 1e-6f && std::fabs(head_.r[1][2]) < 1e-6f;
  validFrames_ = headValid_ && !placeholder ? validFrames_ + 1 : 0;
  const bool trustworthy = trackedFrames_ >= (centered_ ? 1 : 30) || validFrames_ >= 270;

  if (recenterPending_ && trustworthy) {
    recenterPending_ = false;
    centered_ = true;
    for (int h = 0; h < 2; ++h) ReleaseGrab(h);
    center_ = head_.YawOnly();
    Log("Recentered: head at ({:.2f} {:.2f} {:.2f}), looking ({:.2f} {:.2f} {:.2f})", head_.t.x, head_.t.y, head_.t.z,
        head_.Forward().x, head_.Forward().y, head_.Forward().z);
    for (int i = 0; i < ipc::kMaxWindows; ++i)
      if (windows_[i].active) {
        Vec3 p = World(windows_[i]).t;
        Log("  window [{}] '{}' at ({:.2f} {:.2f} {:.2f}), width {:.2f} m", i, windows_[i].name, p.x, p.y, p.z,
            windows_[i].width);
      }
  }
}

void OverlaySession::UpdateGaze() {
  constexpr float kHysteresisDeg = 5.0f;  // hide a bit later than show -> no flicker at the edge
  const Vec3 forward = head_.Forward();
  for (auto& w : windows_) {
    if (!w.lookToShow) {
      w.gazeShown = true;
      continue;
    }
    if (!headValid_) continue;  // keep last state
    Vec3 to = World(w).t - head_.t;
    float dist = to.Length();
    if (dist < 0.05f) {
      w.gazeShown = true;
      continue;
    }
    float angle = std::acos(std::clamp(forward.Dot(to * (1.0f / dist)), -1.0f, 1.0f)) * kRadToDeg;
    float halfDiagonal = 0.5f * std::sqrt(w.width * w.width + w.Height() * w.Height());
    float radius = std::atan(halfDiagonal / dist) * kRadToDeg;  // angular size of the window
    float threshold = radius + lookMarginDeg_ + (w.gazeShown ? kHysteresisDeg : 0.0f);
    w.gazeShown = angle < threshold;
  }
}

void OverlaySession::PublishGaze() {
  static_assert(gaze::kMaxWindows == ipc::kMaxWindows, "one gaze entry per window slot");
  gaze_.Write([&](gaze::State& s) {
    s.count = gaze::kMaxWindows;
    for (int i = 0; i < ipc::kMaxWindows; ++i) {
      const Win& w = windows_[i];
      gaze::Entry& e = s.windows[i];
      std::memset(&e, 0, sizeof(e));
      if (!w.active) continue;
      strncpy_s(e.name, w.name.c_str(), _TRUNCATE);
      e.active = 1;
      const bool shown = connected_ && centered_ && Shown(i);
      e.shown = shown ? 1 : 0;
      // stricter than lookToShow: the view direction itself must hit the window (or its frame)
      float dist = 0;
      Vec3 local;
      e.looked = shown && headValid_ && Intersect(w, head_, GrabMargin(w), dist, local) ? 1 : 0;
    }
  });
}

bool OverlaySession::PointOnPlane(const Win& w, const Pose& ray, Vec3& local) const {
  Pose inv = World(w).Inverse();
  Vec3 o = inv.Apply(ray.t);
  Vec3 d = inv.Rotate(ray.Forward());
  if (std::fabs(d.z) < 1e-5f) return false;
  float t = -o.z / d.z;
  if (t < 0) return false;
  local = o + d * t;
  return true;
}

float OverlaySession::GrabMargin(const Win& w) { return std::clamp(w.width * 0.08f, 0.025f, 0.06f); }

bool OverlaySession::Intersect(const Win& w, const Pose& ray, float margin, float& dist, Vec3& local) const {
  Pose inv = World(w).Inverse();
  Vec3 o = inv.Apply(ray.t);
  Vec3 d = inv.Rotate(ray.Forward());
  if (std::fabs(d.z) < 1e-5f) return false;
  float t = -o.z / d.z;
  if (t < 0 || t > kMaxPointDistance) return false;
  Vec3 p = o + d * t;
  if (std::fabs(p.x) > w.width * 0.5f + margin || std::fabs(p.y) > w.Height() * 0.5f + margin) return false;
  dist = t;
  local = p;
  return true;
}

void OverlaySession::UpdateInteraction(float dt) {
  for (auto& w : windows_) w.pointerActive = false;

  for (int h = 0; h < 2; ++h) {
    const PointerState& p = input_.Pointer(h);
    const bool grabNow = p.valid && p.grab, selNow = p.valid && p.select;
    const bool grabPressed = grabNow && !prevGrab_[h], selPressed = selNow && !prevSelect_[h];
    prevGrab_[h] = grabNow;
    prevSelect_[h] = selNow;

    if (grabs_[h].window >= 0 && allHidden_ && !windows_[grabs_[h].window].interactive) ReleaseGrab(h);

    // Mouse window with the "mouse button" held: keep feeding this window until release,
    // even if the ray slides off (dragging a slider or a scrollbar).
    if (mouseCapture_[h] >= 0) {
      const int idx = mouseCapture_[h];
      Win& w = windows_[idx];
      const bool held = mouseByPinch_[h] ? grabNow : selNow;
      Vec3 local;
      if (p.valid && PointOnPlane(w, p.pose, local)) {
        w.pointerActive = true;
        w.pointerX = std::clamp(local.x / w.width + 0.5f, 0.0f, 1.0f);
        w.pointerY = std::clamp(0.5f - local.y / std::max(w.Height(), 1e-3f), 0.0f, 1.0f);
        hover_[h] = {idx, 0.0f, local, true, false};
      }
      if (!held || !p.valid || !Shown(idx) || !w.mouse) {
        ++w.pendingUp;
        mouseCapture_[h] = -1;
      }
      continue;
    }

    Hover hit;
    if (p.valid && interaction_ && grabs_[h].window < 0) {
      hit.distance = kMaxPointDistance;
      for (int i = 0; i < ipc::kMaxWindows; ++i) {
        const Win& w = windows_[i];
        if (!Shown(i)) continue;  // only windows you can see can be pointed at / grabbed
        float d;
        Vec3 local;
        if (!Intersect(w, p.pose, w.locked || w.interactive ? 0.0f : GrabMargin(w), d, local)) continue;
        const bool inside = std::fabs(local.x) <= w.width * 0.5f && std::fabs(local.y) <= w.Height() * 0.5f;
        // a hit on a window itself beats a hit on the (invisible) margin of another one
        if (hit.window < 0 || (inside && !hit.inside) || (inside == hit.inside && d < hit.distance))
          hit = {i, d, local, inside, false};
      }
      if (hit.window >= 0) {
        const Win& w = windows_[hit.window];
        const float v = 0.5f - hit.local.y / std::max(w.Height(), 1e-3f);
        hit.zone = !w.locked && !w.interactive && (!hit.inside || (w.mouse && v < w.headerFraction));
      }
    }
    hover_[h] = hit;
    if (haptics_ && hit.window >= 0 && hit.window != lastHover_[h]) input_.Haptic(h, 0.01f, 0.15f);
    lastHover_[h] = hit.window;
    if (hit.zone) {
      zoneWindow_[h] = hit.window;
      zoneTime_[h] = time_;
    }
    const bool zone = hit.window >= 0 && !windows_[hit.window].locked && !windows_[hit.window].interactive &&
                      (hit.zone || (zoneWindow_[h] == hit.window && time_ - zoneTime_[h] < kZoneGraceSeconds));

    if (grabs_[h].window >= 0) {
      const bool held = grabs_[h].bySelect ? selNow : grabNow;
      if (!held || !interaction_ || windows_[grabs_[h].window].locked) ReleaseGrab(h);
    }

    // Grab zone (margin around any window, header bar of the VR menu): grip, trigger or pinch moves.
    if (grabs_[h].window < 0 && zone && (grabPressed || selPressed)) {
      StartGrab(h, hit);
      grabs_[h].bySelect = !grabPressed;
      grabs_[h].zone = true;
      continue;
    }

    // Mouse windows (VR menu, clickable programs): trigger / pinch act as a mouse (click, drag),
    // stick = wheel, grip (controller) moves.
    if (grabs_[h].window < 0 && hit.window >= 0 && windows_[hit.window].mouse) {
      Win& w = windows_[hit.window];
      if (hit.inside) {
        w.pointerActive = true;
        w.pointerX = std::clamp(hit.local.x / w.width + 0.5f, 0.0f, 1.0f);
        w.pointerY = std::clamp(0.5f - hit.local.y / std::max(w.Height(), 1e-3f), 0.0f, 1.0f);
      }
      if (hit.inside && (selPressed || (p.isHand && grabPressed))) {
        ++w.pendingDown;
        mouseCapture_[h] = hit.window;
        mouseByPinch_[h] = !selPressed;
        if (haptics_) input_.Haptic(h, 0.01f, 0.3f);
        continue;
      }
      // stick = mouse wheel; the weaker axis is ignored so a slightly diagonal stick scrolls straight
      if (!p.isHand) {
        const float ax = std::fabs(p.stickX), ay = std::fabs(p.stickY);
        if (ay > kStickDeadzone && ay >= ax * 0.5f) w.pendingWheel += p.stickY * dt * 6.0f;
        if (ax > kStickDeadzone && ax >= ay * 0.5f) w.pendingWheelX += p.stickX * dt * 6.0f;
      }
      if (grabPressed && !p.isHand && !w.locked && !w.interactive) StartGrab(h, hit);  // the menu: never
      continue;
    }

    if (grabs_[h].window < 0 && grabPressed && hit.window >= 0) {
      Win& w = windows_[hit.window];
      if (w.locked) {
        if (!w.lockedMessageShown) Log("Window '{}' is locked (\"locked\": true in config)", w.name);
        w.lockedMessageShown = true;
        if (p.isHand) {  // locked windows can still be clicked with a pinch
          w.clickX = hit.local.x / w.width;
          w.clickY = w.Height() > 0 ? hit.local.y / w.Height() : 0;
          w.clickPending = true;
        }
      } else {
        StartGrab(h, hit);
      }
    }

    if (grabs_[h].window < 0 && selPressed && hit.window >= 0 && hit.inside) {
      Win& w = windows_[hit.window];
      w.clickX = hit.local.x / w.width;
      w.clickY = w.Height() > 0 ? hit.local.y / w.Height() : 0;
      w.clickPending = true;
    }
  }

  for (int h = 0; h < 2; ++h) {
    Grab& g = grabs_[h];
    if (g.window < 0 || !g.primary) continue;
    Win& w = windows_[g.window];
    const PointerState& p = input_.Pointer(h);
    const int o = 1 - h;
    if (grabs_[o].window == g.window) {
      float d = (p.pose.t - input_.Pointer(o).pose.t).Length();
      if (twoHandStartDist_ > 0.02f) w.width = std::clamp(twoHandStartWidth_ * d / twoHandStartDist_, 0.05f, 4.0f);
    } else {
      if (std::fabs(p.stickY) > kStickDeadzone)
        w.width = std::clamp(w.width * std::exp(p.stickY * scaleSpeed_ * dt), 0.05f, 4.0f);
      if (std::fabs(p.stickX) > kStickDeadzone)
        g.offset = Pose::Translation({0.0f, 0.0f, -p.stickX * pushPullSpeed_ * dt}) * g.offset;
    }
    w.rel = center_.Inverse() * (p.pose * g.offset);
    w.placementDirty = true;
  }
}

void OverlaySession::StartGrab(int h, const Hover& hit) {
  Win& w = windows_[hit.window];
  const PointerState& p = input_.Pointer(h);
  const int o = 1 - h;
  Grab g;
  g.window = hit.window;
  g.startTime = time_;
  g.startPointer = p.pose.t;
  g.startRel = w.rel;
  g.startWidth = w.width;
  g.startLocal = hit.local;
  if (grabs_[o].window == hit.window) {  // second hand -> two-handed scaling
    g.primary = false;
    twoHandStartDist_ = (p.pose.t - input_.Pointer(o).pose.t).Length();
    twoHandStartWidth_ = w.width;
  } else {
    g.primary = true;
    g.offset = p.pose.Inverse() * World(w);
  }
  grabs_[h] = g;
  if (haptics_) input_.Haptic(h, 0.03f, 0.5f);
}

void OverlaySession::ReleaseGrab(int h) {
  Grab g = grabs_[h];
  if (g.window < 0) return;
  grabs_[h] = {};
  Win& w = windows_[g.window];
  const int o = 1 - h;
  const PointerState& p = input_.Pointer(h);

  // Hand tracking: a short pinch without movement is a click, not a move.
  bool tap = p.isHand && g.primary && !g.zone && !w.mouse && grabs_[o].window != g.window &&
             time_ - g.startTime < kTapSeconds && (p.pose.t - g.startPointer).Length() < kTapMove;
  if (tap) {
    w.rel = g.startRel;
    w.width = g.startWidth;
    w.clickX = g.startLocal.x / w.width;
    w.clickY = w.Height() > 0 ? g.startLocal.y / w.Height() : 0;
    w.clickPending = true;
  }
  if (grabs_[o].window == g.window && g.primary) {  // hand over to the remaining hand
    grabs_[o].primary = true;
    grabs_[o].offset = input_.Pointer(o).pose.Inverse() * World(w);
  }
  w.placementDirty = true;
}

// ---------------------------------------------------------------------------
// Content

void OverlaySession::UpdateContent() {
  for (int i = 0; i < ipc::kMaxWindows; ++i) {
    Win& w = windows_[i];
    if (!w.active) {
      if (w.swapchain.handle) DestroySwapchain(w.swapchain);
      w.hasImage = false;
      continue;
    }
    if (!w.visible) {
      SetStatus(i, "hidden");
      continue;
    }
    if (w.texGen == 0 || w.texW == 0) {
      SetStatus(i, "waiting for content from the app");
      continue;
    }

    if (w.openedGen != w.texGen) {
      w.openedGen = w.texGen;
      w.shared.Reset();
      w.keyedMutex.Reset();
      HRESULT hr = device1_ ? device1_->OpenSharedResourceByName(ipc::TextureName(appInstance_, w.texGen).c_str(),
                                                                 DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                                 IID_PPV_ARGS(w.shared.GetAddressOf()))
                            : E_NOINTERFACE;
      if (FAILED(hr) || FAILED(w.shared.As(&w.keyedMutex))) {
        Log("Window '{}': cannot open shared texture (0x{:08X}) - is the game on a different GPU?", w.name,
            static_cast<uint32_t>(hr));
        w.shared.Reset();
        continue;
      }
      w.copiedVersion = 0;
    }
    if (!w.shared) continue;

    D3D11_TEXTURE2D_DESC d{};
    w.shared->GetDesc(&d);
    if (!EnsureMipTexture(w, d.Width, d.Height)) {
      SetStatus(i, std::format("cannot create mip texture {}x{}", d.Width, d.Height));
      continue;
    }

    // 1) new pixels from the app -> level 0 of our mip texture, then rebuild the mip chain
    if (w.mipVersion != w.contentVersion) {
      HRESULT hr = w.keyedMutex->AcquireSync(0, 2);
      if (hr == S_OK) {
        context_->CopySubresourceRegion(w.mipTex.Get(), 0, 0, 0, 0, w.shared.Get(), 0, nullptr);
        w.keyedMutex->ReleaseSync(0);
        if (w.mipLevels > 1) context_->GenerateMips(w.mipSrv.Get());
        w.mipVersion = w.contentVersion;
        w.swapchainDirty = true;
      } else if (!w.hasImage) {  // app is writing, try next frame
        SetStatus(i, std::format("shared texture busy (AcquireSync 0x{:08X})", static_cast<uint32_t>(hr)));
        continue;
      }
    }

    // 2) pick the mip level that matches the window's size on the display
    int level = ChooseMipLevel(w);
    if (level != w.level) {
      w.level = level;
      w.swapchainDirty = true;
    }
    const uint32_t lw = std::max(1u, w.mipW >> w.level), lh = std::max(1u, w.mipH >> w.level);
    if (!w.swapchain.handle || w.swapchain.width != lw || w.swapchain.height != lh) {
      DestroySwapchain(w.swapchain);
      if (!CreateSwapchain(lw, lh, w.swapchain)) {
        w.hasImage = false;
        SetStatus(i, std::format("cannot create swapchain {}x{}", lw, lh));
        continue;
      }
      w.swapchainDirty = true;
    }
    if (!w.swapchainDirty || w.mipVersion == 0) continue;

    // 3) chosen level -> swapchain
    lastError_.clear();
    const UINT sub = D3D11CalcSubresource(static_cast<UINT>(w.level), 0, w.mipLevels);
    bool ok = WriteSwapchain(w.swapchain, [&](ID3D11Texture2D* dst) {
      context_->CopySubresourceRegion(dst, 0, 0, 0, 0, w.mipTex.Get(), sub, nullptr);
    });
    if (ok && lastError_.empty()) {
      w.swapchainDirty = false;
      w.hasImage = true;
      SetStatus(i, std::format("showing {}x{} (source {}x{}, mip level {})", lw, lh, w.mipW, w.mipH, w.level));
    } else {
      w.hasImage = false;
      SetStatus(i, "copy failed: " + lastError_);
    }
  }
}

bool OverlaySession::EnsureMipTexture(Win& w, uint32_t width, uint32_t height) {
  if (w.mipTex && w.mipW == width && w.mipH == height) return true;
  w.mipTex.Reset();
  w.mipSrv.Reset();
  w.mipW = w.mipH = 0;

  uint32_t levels = 1;
  while ((std::max(width, height) >> levels) >= 16) ++levels;  // stop at ~16 px

  D3D11_TEXTURE2D_DESC d{};
  d.Width = width;
  d.Height = height;
  d.MipLevels = levels;
  d.ArraySize = 1;
  d.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;  // viewed as sRGB so filtering happens in linear light
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
  if (FAILED(device_->CreateTexture2D(&d, nullptr, w.mipTex.GetAddressOf()))) return false;

  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
  sv.Format = mipSrvFormat_;
  sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sv.Texture2D.MipLevels = levels;
  if (FAILED(device_->CreateShaderResourceView(w.mipTex.Get(), &sv, w.mipSrv.GetAddressOf()))) {
    w.mipTex.Reset();
    return false;
  }
  w.mipW = width;
  w.mipH = height;
  w.mipLevels = levels;
  w.mipVersion = 0;
  w.level = 0;
  w.swapchainDirty = true;
  return true;
}

void OverlaySession::UpdatePixelDensity(const XrFrameEndInfo* info) {
  for (uint32_t i = 0; i < info->layerCount; ++i) {
    const auto* l = info->layers[i];
    if (!l || l->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
    const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(l);
    if (p->viewCount == 0 || !p->views) return;
    const auto& v = p->views[0];
    // pixels per radian at the center of a tangent-space projection
    float tanSpan = std::tan(v.fov.angleRight) - std::tan(v.fov.angleLeft);
    if (tanSpan > 0.1f && v.subImage.imageRect.extent.width > 0) {
      float ppr = static_cast<float>(v.subImage.imageRect.extent.width) / tanSpan;
      if (std::fabs(ppr - pixelsPerRadian_) > 1.0f) {
        if (pixelsPerRadian_ == 0.0f)
          Log("Display density: {:.0f} px/rad ({} px wide game image)", ppr, v.subImage.imageRect.extent.width);
        pixelsPerRadian_ = ppr;
      }
    }
    return;
  }
}

namespace {
constexpr float kLineHeight = 0.004f;  // hologram: height of the glowing line (m)

float EaseOutCubic(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
float EaseOutBack(float t) {  // slight overshoot
  constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
  return 1.0f + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
}
float EffectDuration(uint32_t effect) {
  switch (effect) {
    case ipc::kEffectHologram: return 0.55f;
    case ipc::kEffectZoom: return 0.30f;
    case ipc::kEffectTv: return 0.35f;
    default: return 0.0f;
  }
}
}  // namespace

void OverlaySession::UpdateAnimations(float dt) {
  for (int i = 0; i < ipc::kMaxWindows; ++i) {
    Win& w = windows_[i];
    const float target = Shown(i) ? 1.0f : 0.0f;
    const float dur = EffectDuration(w.effect);
    if (!w.active || !w.hasImage || dur <= 0.0f) {
      w.anim = w.active && w.hasImage ? target : 0.0f;
      continue;
    }
    if (w.anim < target)
      w.anim = std::min(target, w.anim + dt / dur);
    else if (w.anim > target)
      w.anim = std::max(target, w.anim - dt / (dur * 0.7f));  // hiding is a bit faster
  }
}

int OverlaySession::ChooseMipLevel(const Win& w) const {
  if (w.mipLevels <= 1 || pixelsPerRadian_ <= 0.0f || !headValid_) return std::min(w.level, int(w.mipLevels) - 1);
  float dist = std::max((World(w).t - head_.t).Length(), 0.1f);
  float angularWidth = 2.0f * std::atan(0.5f * w.width / dist);
  float needed = std::max(16.0f, angularWidth * pixelsPerRadian_ * sharpness_);  // pixels on the display
  float ideal = std::log2(static_cast<float>(w.mipW) / needed);                  // > 0: source is larger

  const int maxLevel = static_cast<int>(w.mipLevels) - 1;
  int cur = std::min(w.level, maxLevel);
  int target = std::clamp(static_cast<int>(std::floor(ideal)), 0, maxLevel);
  if (target > cur && ideal < cur + 1.15f) target = cur;  // hysteresis: only switch when clearly needed
  if (target < cur && ideal > cur - 0.15f) target = cur;
  return target;
}

// ---------------------------------------------------------------------------
// Frame

void OverlaySession::BuildLayers(const XrFrameEndInfo* info) {
  layers_.assign(info->layers, info->layers + info->layerCount);
  quads_.clear();
  colorScales_.clear();

  auto addQuad = [&](const Swapchain& sc, const Pose& pose, float w, float h, bool blend) -> XrCompositionLayerQuad* {
    if (!sc.handle || layers_.size() >= maxLayers_ || quads_.size() == quads_.capacity()) return nullptr;
    XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = blend ? (XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                            XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)
                         : 0;
    q.space = localSpace_;
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = sc.handle;
    q.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(sc.width), static_cast<int32_t>(sc.height)}};
    q.pose = ToXr(pose);
    q.size = {w, h};
    quads_.push_back(q);
    layers_.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads_.back()));
    return &quads_.back();
  };

  for (int i = 0; i < ipc::kMaxWindows; ++i) {
    const Win& w = windows_[i];
    if (w.anim <= 0.0f || !w.active || !w.hasImage) continue;  // Shown(), or still animating out
    Pose world = World(w);
    float h = w.Height();
    if (h <= 0.0f) continue;

    // show / hide animation: visible part (crop = no distortion) or scale (squash) of the quad
    const float a = std::clamp(w.anim, 0.0f, 1.0f);
    float fx = 1.0f, fy = 1.0f;  // visible fraction (cropped)
    float sx = 1.0f, sy = 1.0f;  // extra scale of the quad (squashed / zoomed)
    bool lineOnly = false, edges = false;
    if (a < 1.0f) {
      switch (w.effect) {
        case ipc::kEffectHologram: {  // a glowing line spreads out, then the window opens from it
          constexpr float kSplit = 0.35f;
          const float line = std::min(1.0f, kLineHeight / h);
          if (a < kSplit) {
            fx = std::max(0.02f, EaseOutCubic(a / kSplit));
            fy = line;
            lineOnly = true;
          } else {
            fy = std::max(line, EaseOutCubic((a - kSplit) / (1.0f - kSplit)));
            edges = true;
          }
          break;
        }
        case ipc::kEffectZoom:
          sx = sy = std::max(0.02f, EaseOutBack(a));
          break;
        case ipc::kEffectTv:  // a bright line widens, then stretches open (squashed picture)
          sx = std::max(0.02f, EaseOutCubic(std::min(1.0f, a * 2.5f)));
          sy = std::max(0.01f, EaseOutCubic(std::clamp(a * 1.6f - 0.4f, 0.0f, 1.0f)));
          break;
        default:
          break;
      }
    }
    const float qw = w.width * fx * sx, qh = h * fy * sy;

    if (lineOnly) {  // first phase of the hologram: just the glowing line
      addQuad(glow_, world, qw, kLineHeight, true);
      continue;
    }

    // Frame behind the window = the grab margin: faint while pointing at the window, bright on
    // the grab zone, solid while held.
    bool grabbed = grabs_[0].window == i || grabs_[1].window == i;
    bool hovered = hover_[0].window == i || hover_[1].window == i;
    bool onZone = (hover_[0].window == i && hover_[0].zone) || (hover_[1].window == i && hover_[1].zone);
    if ((grabbed || hovered) && !w.locked && !w.interactive && a >= 1.0f) {  // locked windows / menu: no frame
      const float m = GrabMargin(w);
      addQuad(grabbed ? frameGrab_ : onZone ? frameZone_ : frameHover_, world * Pose::Translation({0, 0, -0.002f}),
              w.width + 2 * m, h + 2 * m, true);
    }

    if (w.keyboardFocus && a >= 1.0f) {  // PC keyboard types into this window: thin blue border
      constexpr float kBorder = 0.006f;
      addQuad(frameKeys_, world * Pose::Translation({0, 0, -0.001f}), w.width + 2 * kBorder, h + 2 * kBorder, true);
    }

    bool fade = colorScaleBias_ && w.opacity < 0.999f;
    auto* q = addQuad(w.swapchain, world, qw, qh, !w.opaque || fade);
    if (q && (fx < 1.0f || fy < 1.0f)) {  // crop to the middle part of the picture
      const int32_t W = static_cast<int32_t>(w.swapchain.width), H = static_cast<int32_t>(w.swapchain.height);
      const int32_t cw = std::clamp(static_cast<int32_t>(std::lround(W * fx)), 1, W);
      const int32_t ch = std::clamp(static_cast<int32_t>(std::lround(H * fy)), 1, H);
      q->subImage.imageRect = {{(W - cw) / 2, (H - ch) / 2}, {cw, ch}};
    }
    if (edges) {  // hologram: glowing edges travel outwards while it opens
      for (float side : {0.5f, -0.5f})
        addQuad(glow_, world * Pose::Translation({0, side * qh, 0.001f}), qw, kLineHeight * 0.6f, true);
    }
    if (q && fade && colorScales_.size() < colorScales_.capacity()) {
      XrCompositionLayerColorScaleBiasKHR cs{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
      cs.colorScale = {1, 1, 1, w.opacity};
      colorScales_.push_back(cs);
      q->next = &colorScales_.back();
    }
  }

  if (showCursor_)
    for (int h = 0; h < 2; ++h) {
      const Hover& hv = hover_[h];
      if (hv.window < 0 || grabs_[h].window >= 0) continue;
      Vec3 at = hv.local;
      at.z += 0.004f;
      addQuad(cursor_, World(windows_[hv.window]) * Pose::Translation(at), kCursorSize, kCursorSize, true);
    }
}

XrResult OverlaySession::EndFrame(const XrFrameEndInfo* info) {
  if (disabled_ || !info) return next_.xrEndFrame(session_, info);
  try {
    float dt = lastTime_ ? std::clamp(static_cast<float>((info->displayTime - lastTime_) * 1e-9), 0.0f, 0.1f) : 0.011f;
    lastTime_ = info->displayTime;
    time_ += dt;

    SyncIpc();
    UpdateHead(info->displayTime);
    UpdateHeadFollow(dt);
    UpdateGaze();
    PublishGaze();
    if (centered_) SummonWindows();
    input_.Update(localSpace_, info->displayTime, head_, headValid_);

    if (!connected_ || !centered_) {  // never show windows before the first real recenter
      for (int h = 0; h < 2; ++h) {
        ReleaseGrab(h);
        hover_[h] = {};
      }
      return next_.xrEndFrame(session_, info);
    }

    UpdateInteraction(dt);
    UpdatePixelDensity(info);
    UpdateContent();
    UpdateAnimations(dt);
    BuildLayers(info);

    size_t shown = 0;  // windows only (cursor/frame quads come and go while pointing)
    for (const auto& w : windows_) shown += w.active && w.visible && w.hasImage;
    if (shown != lastLayerCount_) {
      lastLayerCount_ = shown;
      Log("Showing {} window(s) on top of {} game layer(s)", shown, info->layerCount);
    }

    XrFrameEndInfo copy = *info;
    copy.layerCount = static_cast<uint32_t>(layers_.size());
    copy.layers = layers_.data();
    XrResult r = next_.xrEndFrame(session_, &copy);
    if (XR_FAILED(r)) {
      Log("xrEndFrame with overlay layers failed ({}) - overlay disabled", static_cast<int>(r));
      disabled_ = true;
      return next_.xrEndFrame(session_, info);
    }
    return r;
  } catch (const std::exception& e) {
    Log("Exception in EndFrame: {} - overlay disabled", e.what());
  } catch (...) {
    Log("Unknown exception in EndFrame - overlay disabled");
  }
  disabled_ = true;
  return next_.xrEndFrame(session_, info);
}
