#pragma once

#include "XrIncludes.h"

#include <set>
#include <vector>

// Per-hand pointer for one frame (controller aim pose or a ray derived from hand tracking).
struct PointerState {
  bool valid = false;
  bool isHand = false;   // true = hand tracking (pinch to grab, quick pinch to click)
  Pose pose;             // ray origin + direction (-Z) in the layer's LOCAL space
  bool grab = false;     // grip / pinch
  bool select = false;   // trigger (controllers only)
  float stickX = 0.0f, stickY = 0.0f;
};

// Instance level: our own action set, merged into the game's bindings.
// We only add bindings to interaction profiles the game itself suggests, so the
// runtime never picks a different controller profile than the game expects.
class InputActions {
 public:
  bool Create(const NextDispatch& next, XrInstance instance);
  XrResult SuggestBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding* info);
  XrResult AttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo* info);
  XrResult SyncActions(XrSession session, const XrActionsSyncInfo* info);
  void OnSessionDestroyed(XrSession session);

  bool Attached(XrSession session) const { return attached_ && attachedSession_ == session; }
  XrAction Aim() const { return aim_; }
  XrAction Grab() const { return grab_; }
  XrAction Select() const { return select_; }
  XrAction Stick() const { return stick_; }
  XrAction Haptic() const { return haptic_; }
  XrPath HandPath(int h) const { return hands_[h]; }

 private:
  struct Profile {
    XrPath path = XR_NULL_PATH;
    std::vector<XrActionSuggestedBinding> bindings;
  };
  const Profile* Find(XrPath p) const;

  const NextDispatch* next_ = nullptr;
  bool created_ = false;
  XrActionSet set_ = XR_NULL_HANDLE;
  XrAction aim_ = XR_NULL_HANDLE, grab_ = XR_NULL_HANDLE, select_ = XR_NULL_HANDLE, stick_ = XR_NULL_HANDLE,
           haptic_ = XR_NULL_HANDLE;
  XrPath hands_[2]{};
  std::vector<Profile> profiles_;
  bool attached_ = false;
  XrSession attachedSession_ = XR_NULL_HANDLE;
};

// "One Euro" low-pass filter: strong smoothing while (almost) still, little lag while moving fast.
struct OneEuro {
  float minCutoff = 1.0f;  // Hz while still
  float beta = 0.0f;       // how much faster movement raises the cutoff
  bool init = false;
  float x = 0.0f, dx = 0.0f;
  float Filter(float value, float dt);
};

// Hand tracking ray: filtered, and steadied while pinching so a click lands where you aimed.
struct HandRay {
  OneEuro dir[3], org[3];
  XrTime last = 0;
  static constexpr int kHistory = 16;  // ring buffer of recent filtered rays (~180 ms at 90 Hz)
  struct Sample { XrTime time; Vec3 origin, dir; } history[kHistory]{};
  int head = 0, count = 0;
  bool wasPinched = false, locked = false;
  Vec3 lockOrigin, lockDir;
  float blend = 1.0f;  // 0 = lock ray, 1 = live ray (after the lock broke: eases back to live)
  void Reset() { *this = HandRay{}; }
};

// Session level: action spaces + hand trackers, produces PointerStates every frame.
class SessionInput {
 public:
  void Init(const NextDispatch& next, XrSession session, const InputActions& actions, bool handTracking);
  void Destroy();
  void Update(XrSpace base, XrTime time, const Pose& head, bool headValid);
  const PointerState& Pointer(int h) const { return pointers_[h]; }
  void Haptic(int h, float seconds, float amplitude);
  bool HandTracking() const { return handTrackers_[0] != XR_NULL_HANDLE; }

 private:
  bool UpdateController(int h, XrSpace base, XrTime time, PointerState& p);
  bool UpdateHand(int h, XrSpace base, XrTime time, const Pose& head, PointerState& p);

  const NextDispatch* next_ = nullptr;
  const InputActions* actions_ = nullptr;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace aimSpaces_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrHandTrackerEXT handTrackers_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
  PointerState pointers_[2];
  bool pinch_[2]{};
  HandRay rays_[2];
  bool grabLatch_[2]{}, selectLatch_[2]{};
};
