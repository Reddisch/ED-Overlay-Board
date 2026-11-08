#pragma once

#include "Input.h"
#include "Ipc.h"
#include "Log.h"
#include "XrIncludes.h"

#include <functional>
#include <string>
#include <vector>

// Everything the layer does for one XrSession of the game: reads the companion app's
// shared state, copies the shared window textures into OpenXR swapchains, handles
// grabbing/resizing, and appends quad layers in xrEndFrame.
class OverlaySession {
 public:
  struct Params {
    const NextDispatch* next;
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    ID3D11Device* device;
    InputActions* actions;
    bool handTracking;
    bool colorScaleBias;
    std::string runtimeName;
    std::string appName;
  };

  explicit OverlaySession(const Params& p);
  ~OverlaySession();
  OverlaySession(const OverlaySession&) = delete;
  OverlaySession& operator=(const OverlaySession&) = delete;

  XrSession Handle() const { return session_; }
  XrResult EndFrame(const XrFrameEndInfo* info);
  // The runtime moved the LOCAL space (initialization finished, system recenter, ...).
  void OnLocalSpaceChanged() {
    recenterPending_ = true;
    Log("LOCAL reference space changed by the runtime - recentering");
  }

 private:
  struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    std::vector<ID3D11Texture2D*> images;
  };

  struct Win {
    bool active = false, visible = false, locked = false, opaque = true;
    bool lookToShow = false;
    bool gazeShown = true;  // currently looked at (or lookToShow disabled)
    bool interactive = false;  // the VR menu (visible even while all windows are hidden)
    bool mouse = false;        // trigger/pinch act as a mouse (VR menu, clickable programs)
    float headerFraction = 0.0f;
    bool summonPending = false;
    bool pointerActive = false;
    float pointerX = 0, pointerY = 0;
    uint32_t pendingDown = 0, pendingUp = 0;
    float pendingWheel = 0;
    float opacity = 1.0f;
    uint32_t effect = 0;  // ipc::ShowEffect
    float anim = 0.0f;    // 0 = hidden .. 1 = fully shown (show / hide animation)
    std::string name;
    uint32_t texW = 0, texH = 0, texGen = 0, contentVersion = 0;

    Pose rel;
    float width = 0.5f;
    uint32_t appPlacementSerial = 0;
    bool placementDirty = false;

    bool clickPending = false;
    float clickX = 0, clickY = 0;

    ComPtr<ID3D11Texture2D> shared;
    ComPtr<IDXGIKeyedMutex> keyedMutex;
    uint32_t openedGen = 0;
    // Pre-filtered copy with a full mip chain: we submit the level that matches the size the
    // window has on the display, so the compositor never minifies by more than 2x (no shimmer).
    ComPtr<ID3D11Texture2D> mipTex;
    ComPtr<ID3D11ShaderResourceView> mipSrv;
    uint32_t mipW = 0, mipH = 0, mipLevels = 1;
    uint32_t mipVersion = 0;  // contentVersion currently in mipTex
    int level = 0;            // mip level shown in the swapchain
    bool swapchainDirty = true;

    Swapchain swapchain;
    uint32_t copiedVersion = 0;
    bool hasImage = false;
    bool lockedMessageShown = false;
    std::string status;  // last logged state (diagnostics)

    float Height() const {
      return mipW ? width * static_cast<float>(mipH) / static_cast<float>(mipW) : 0;
    }
  };

  struct Grab {
    int window = -1;
    Pose offset;  // window pose in pointer space
    bool primary = false;
    // for "quick pinch = click" with hand tracking
    double startTime = 0;
    Vec3 startPointer;
    Pose startRel;
    float startWidth = 0;
    Vec3 startLocal;
    bool bySelect = false;  // started with the trigger (grab zone) -> held by the trigger
    bool zone = false;      // started on the grab zone -> never a click
  };
  struct Hover {
    int window = -1;
    float distance = 0;
    Vec3 local;           // hit point in window space
    bool inside = false;  // on the window itself (not on the grab margin around it)
    bool zone = false;    // grab zone: margin around the window or the header bar of a panel
  };

  void Init();
  void SyncIpc();
  void ResetWindows();
  void UpdateHead(XrTime time);
  void UpdateGaze();
  void SummonWindows();
  bool Shown(int i) const {  // drawn and interactive this frame
    const Win& w = windows_[i];
    return w.active && w.visible && w.hasImage && (!allHidden_ || w.interactive) &&
           (w.gazeShown || grabs_[0].window == i || grabs_[1].window == i);
  }
  bool PointOnPlane(const Win& w, const Pose& ray, Vec3& local) const;
  void UpdateInteraction(float dt);
  void StartGrab(int h, const Hover& hit);
  void ReleaseGrab(int h);
  void UpdateContent();
  void UpdateAnimations(float dt);
  void UpdatePixelDensity(const XrFrameEndInfo* info);
  int ChooseMipLevel(const Win& w) const;
  bool EnsureMipTexture(Win& w, uint32_t width, uint32_t height);
  void SetStatus(int i, const std::string& status);
  void BuildLayers(const XrFrameEndInfo* info);

  Pose World(const Win& w) const { return center_ * w.rel; }
  bool Intersect(const Win& w, const Pose& ray, float margin, float& dist, Vec3& local) const;
  // Invisible border around each window that grabs instead of clicking (easier to hit than
  // the window edge, especially with a jittery hand-tracking ray).
  static float GrabMargin(const Win& w);

  bool CreateSwapchain(uint32_t w, uint32_t h, Swapchain& sc);
  void DestroySwapchain(Swapchain& sc);
  bool WriteSwapchain(Swapchain& sc, const std::function<void(ID3D11Texture2D*)>& fn);
  bool CreateStaticSwapchain(uint32_t w, uint32_t h, const std::vector<uint32_t>& bgra, Swapchain& sc);

  const NextDispatch& next_;
  XrInstance instance_;
  XrSystemId system_;
  XrSession session_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11Device1> device1_;
  ComPtr<ID3D11DeviceContext> context_;
  InputActions& actions_;
  SessionInput input_;
  bool handTrackingExt_, colorScaleBias_;
  std::string runtimeName_, appName_;
  LUID adapterLuid_{};

  bool disabled_ = false;
  int64_t format_ = 0;
  uint32_t maxLayers_ = 16;
  XrSpace localSpace_ = XR_NULL_HANDLE;
  XrSpace viewSpace_ = XR_NULL_HANDLE;

  ipc::SharedState shm_;
  uint64_t nextOpenTry_ = 0;
  bool connected_ = false;
  bool wasConnected_ = false;
  uint32_t appInstance_ = 0;
  uint32_t recenterSerial_ = 0;
  bool recenterPending_ = true;
  bool allHidden_ = false, interaction_ = true, showCursor_ = true, haptics_ = true;
  float scaleSpeed_ = 1.0f, pushPullSpeed_ = 0.75f;
  float lookMarginDeg_ = 10.0f;
  float sharpness_ = 1.0f;
  float pixelsPerRadian_ = 0.0f;  // display density at the view center, from the game's projection layer
  DXGI_FORMAT mipSrvFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

  Win windows_[ipc::kMaxWindows];
  Swapchain cursor_, frameHover_, frameZone_, frameGrab_, glow_;

  Pose center_;
  Pose head_;
  bool headValid_ = false;
  bool centered_ = false;  // first recenter with a really tracked head pose done
  int trackedFrames_ = 0;
  int validFrames_ = 0;
  Vec3 lastTrackedPos_;
  XrTime lastTime_ = 0;
  double time_ = 0;

  Grab grabs_[2];
  Hover hover_[2];
  int lastHover_[2]{-1, -1};
  bool prevGrab_[2]{}, prevSelect_[2]{};
  // Last time the ray was on a grab zone: a pinch jerks the hand ray, so a pinch that starts
  // just after leaving the zone still counts as grabbing.
  int zoneWindow_[2]{-1, -1};
  double zoneTime_[2]{-1e9, -1e9};
  int mouseCapture_[2]{-1, -1};  // mouse window holding the "mouse button" of this hand
  bool mouseByPinch_[2]{};
  float twoHandStartDist_ = 0, twoHandStartWidth_ = 0;

  std::string lastError_;
  size_t lastLayerCount_ = static_cast<size_t>(-1);

  std::vector<const XrCompositionLayerBaseHeader*> layers_;
  std::vector<XrCompositionLayerQuad> quads_;
  std::vector<XrCompositionLayerColorScaleBiasKHR> colorScales_;
};
