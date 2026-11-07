#include "Input.h"

#include "Log.h"

#include <cstring>

namespace {

struct ProfileDef {
  const char* profile;
  const char* grab;    // relative to /user/hand/<side>/
  const char* select;  // may be null
  const char* stick;   // may be null
  bool haptics;
};

constexpr ProfileDef kProfiles[] = {
    {"/interaction_profiles/oculus/touch_controller", "input/squeeze/value", "input/trigger/value", "input/thumbstick", true},
    {"/interaction_profiles/meta/touch_controller_plus", "input/squeeze/value", "input/trigger/value", "input/thumbstick", true},
    {"/interaction_profiles/valve/index_controller", "input/squeeze/value", "input/trigger/value", "input/thumbstick", true},
    {"/interaction_profiles/htc/vive_controller", "input/squeeze/click", "input/trigger/value", "input/trackpad", true},
    {"/interaction_profiles/microsoft/motion_controller", "input/squeeze/click", "input/trigger/value", "input/thumbstick", true},
    {"/interaction_profiles/hp/mixed_reality_controller", "input/squeeze/value", "input/trigger/value", "input/thumbstick", true},
    {"/interaction_profiles/bytedance/pico4_controller", "input/squeeze/value", "input/trigger/value", "input/thumbstick", true},
    {"/interaction_profiles/khr/simple_controller", "input/select/click", nullptr, nullptr, true},
};

constexpr float kOn = 0.6f, kOff = 0.4f;  // hysteresis for analog grip/trigger

}  // namespace

bool InputActions::Create(const NextDispatch& next, XrInstance instance) {
  next_ = &next;
  next.xrStringToPath(instance, "/user/hand/left", &hands_[0]);
  next.xrStringToPath(instance, "/user/hand/right", &hands_[1]);

  XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy_s(si.actionSetName, "edoverlayboard");
  strcpy_s(si.localizedActionSetName, "ED Overlay Board");
  si.priority = 0;
  if (XR_FAILED(next.xrCreateActionSet(instance, &si, &set_))) {
    Log("Input: xrCreateActionSet failed - controllers disabled");
    return false;
  }

  auto make = [&](const char* name, const char* loc, XrActionType type, XrAction& out) {
    XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(ai.actionName, name);
    strcpy_s(ai.localizedActionName, loc);
    ai.actionType = type;
    ai.countSubactionPaths = 2;
    ai.subactionPaths = hands_;
    return XR_SUCCEEDED(next.xrCreateAction(set_, &ai, &out));
  };
  bool ok = make("edob_aim", "Overlay pointer", XR_ACTION_TYPE_POSE_INPUT, aim_) &&
            make("edob_grab", "Overlay grab", XR_ACTION_TYPE_FLOAT_INPUT, grab_) &&
            make("edob_select", "Overlay click", XR_ACTION_TYPE_FLOAT_INPUT, select_) &&
            make("edob_stick", "Overlay resize", XR_ACTION_TYPE_VECTOR2F_INPUT, stick_) &&
            make("edob_haptic", "Overlay haptics", XR_ACTION_TYPE_VIBRATION_OUTPUT, haptic_);
  if (!ok) {
    Log("Input: xrCreateAction failed - controllers disabled");
    return false;
  }

  for (const auto& def : kProfiles) {
    Profile p;
    if (XR_FAILED(next.xrStringToPath(instance, def.profile, &p.path))) continue;
    for (const char* side : {"/user/hand/left/", "/user/hand/right/"}) {
      auto add = [&](XrAction a, const char* rel) {
        if (!rel) return;
        XrPath path;
        if (XR_SUCCEEDED(next.xrStringToPath(instance, (std::string(side) + rel).c_str(), &path)))
          p.bindings.push_back({a, path});
      };
      add(aim_, "input/aim/pose");
      add(grab_, def.grab);
      add(select_, def.select);
      add(stick_, def.stick);
      if (def.haptics) add(haptic_, "output/haptic");
    }
    profiles_.push_back(std::move(p));
  }
  created_ = true;
  return true;
}

const InputActions::Profile* InputActions::Find(XrPath p) const {
  for (const auto& pr : profiles_)
    if (pr.path == p) return &pr;
  return nullptr;
}

XrResult InputActions::SuggestBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding* info) {
  const Profile* ours = created_ && info ? Find(info->interactionProfile) : nullptr;
  if (!ours) return next_->xrSuggestInteractionProfileBindings(instance, info);

  std::vector<XrActionSuggestedBinding> merged(info->suggestedBindings,
                                               info->suggestedBindings + info->countSuggestedBindings);
  merged.insert(merged.end(), ours->bindings.begin(), ours->bindings.end());
  XrInteractionProfileSuggestedBinding copy = *info;
  copy.countSuggestedBindings = static_cast<uint32_t>(merged.size());
  copy.suggestedBindings = merged.data();
  XrResult r = next_->xrSuggestInteractionProfileBindings(instance, &copy);
  if (XR_FAILED(r)) {
    Log("Input: merged bindings rejected ({}), using the game's bindings only", static_cast<int>(r));
    r = next_->xrSuggestInteractionProfileBindings(instance, info);
  }
  return r;
}

XrResult InputActions::AttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo* info) {
  if (!created_ || !info) return next_->xrAttachSessionActionSets(session, info);
  std::vector<XrActionSet> sets(info->actionSets, info->actionSets + info->countActionSets);
  sets.push_back(set_);
  XrSessionActionSetsAttachInfo copy = *info;
  copy.countActionSets = static_cast<uint32_t>(sets.size());
  copy.actionSets = sets.data();
  XrResult r = next_->xrAttachSessionActionSets(session, &copy);
  if (XR_FAILED(r)) {
    Log("Input: attaching our action set failed ({}) - controllers disabled", static_cast<int>(r));
    return next_->xrAttachSessionActionSets(session, info);
  }
  attached_ = true;
  attachedSession_ = session;
  Log("Input: controller actions attached");
  return r;
}

XrResult InputActions::SyncActions(XrSession session, const XrActionsSyncInfo* info) {
  if (!Attached(session) || !info) return next_->xrSyncActions(session, info);
  std::vector<XrActiveActionSet> sets(info->activeActionSets, info->activeActionSets + info->countActiveActionSets);
  sets.push_back({set_, XR_NULL_PATH});
  XrActionsSyncInfo copy = *info;
  copy.countActiveActionSets = static_cast<uint32_t>(sets.size());
  copy.activeActionSets = sets.data();
  XrResult r = next_->xrSyncActions(session, &copy);
  if (XR_FAILED(r)) r = next_->xrSyncActions(session, info);
  return r;
}

void InputActions::OnSessionDestroyed(XrSession session) {
  if (attachedSession_ == session) {
    attached_ = false;
    attachedSession_ = XR_NULL_HANDLE;
  }
}

// ---------------------------------------------------------------------------

void SessionInput::Init(const NextDispatch& next, XrSession session, const InputActions& actions, bool handTracking) {
  next_ = &next;
  actions_ = &actions;
  session_ = session;
  for (int h = 0; h < 2; ++h) {
    if (actions.Aim() != XR_NULL_HANDLE) {
      XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
      ci.action = actions.Aim();
      ci.subactionPath = actions.HandPath(h);
      ci.poseInActionSpace.orientation.w = 1.0f;
      next.xrCreateActionSpace(session, &ci, &aimSpaces_[h]);
    }
    if (handTracking && next.xrCreateHandTrackerEXT) {
      XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
      ci.hand = h == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
      ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
      if (XR_FAILED(next.xrCreateHandTrackerEXT(session, &ci, &handTrackers_[h]))) handTrackers_[h] = XR_NULL_HANDLE;
    }
  }
  Log("Input: hand tracking {}", HandTracking() ? "available" : "not available");
}

void SessionInput::Destroy() {
  for (int h = 0; h < 2; ++h) {
    if (aimSpaces_[h] != XR_NULL_HANDLE) next_->xrDestroySpace(aimSpaces_[h]);
    if (handTrackers_[h] != XR_NULL_HANDLE) next_->xrDestroyHandTrackerEXT(handTrackers_[h]);
    aimSpaces_[h] = XR_NULL_HANDLE;
    handTrackers_[h] = XR_NULL_HANDLE;
  }
}

bool SessionInput::UpdateController(int h, XrSpace base, XrTime time, PointerState& p) {
  if (!actions_->Attached(session_) || aimSpaces_[h] == XR_NULL_HANDLE) return false;
  XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
  gi.subactionPath = actions_->HandPath(h);

  gi.action = actions_->Aim();
  XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
  if (XR_FAILED(next_->xrGetActionStatePose(session_, &gi, &ps)) || !ps.isActive) return false;

  XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(next_->xrLocateSpace(aimSpaces_[h], base, time, &loc))) return false;
  const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
  if ((loc.locationFlags & need) != need) return false;
  p.pose = ToPose(loc.pose);

  XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
  gi.action = actions_->Grab();
  float grab = XR_SUCCEEDED(next_->xrGetActionStateFloat(session_, &gi, &fs)) && fs.isActive ? fs.currentState : 0.0f;
  gi.action = actions_->Select();
  fs = {XR_TYPE_ACTION_STATE_FLOAT};
  float sel = XR_SUCCEEDED(next_->xrGetActionStateFloat(session_, &gi, &fs)) && fs.isActive ? fs.currentState : 0.0f;
  grabLatch_[h] = grabLatch_[h] ? grab > kOff : grab > kOn;
  selectLatch_[h] = selectLatch_[h] ? sel > kOff : sel > kOn;
  p.grab = grabLatch_[h];
  p.select = selectLatch_[h];

  gi.action = actions_->Stick();
  XrActionStateVector2f vs{XR_TYPE_ACTION_STATE_VECTOR2F};
  if (XR_SUCCEEDED(next_->xrGetActionStateVector2f(session_, &gi, &vs)) && vs.isActive) {
    p.stickX = vs.currentState.x;
    p.stickY = vs.currentState.y;
  }
  return true;
}

bool SessionInput::UpdateHand(int h, XrSpace base, XrTime time, const Pose& head, PointerState& p) {
  if (handTrackers_[h] == XR_NULL_HANDLE) return false;
  XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT]{};
  XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
  locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
  locs.jointLocations = joints;
  XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
  li.baseSpace = base;
  li.time = time;
  if (XR_FAILED(next_->xrLocateHandJointsEXT(handTrackers_[h], &li, &locs)) || !locs.isActive) return false;

  auto valid = [&](XrHandJointEXT j) { return (joints[j].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0; };
  if (!valid(XR_HAND_JOINT_INDEX_PROXIMAL_EXT) || !valid(XR_HAND_JOINT_INDEX_TIP_EXT) ||
      !valid(XR_HAND_JOINT_THUMB_TIP_EXT))
    return false;
  auto pos = [&](XrHandJointEXT j) {
    const auto& v = joints[j].pose.position;
    return Vec3{v.x, v.y, v.z};
  };

  // Ray from an estimated shoulder through the index knuckle: stable while pinching.
  const float side = h == 0 ? -1.0f : 1.0f;
  Pose yaw = head.YawOnly();
  Vec3 shoulder = yaw.Apply({side * 0.17f, -0.25f, 0.05f});
  Vec3 knuckle = pos(XR_HAND_JOINT_INDEX_PROXIMAL_EXT);
  p.pose = Pose::LookRotation(knuckle, (knuckle - shoulder).Normalized());

  float pinchDist = (pos(XR_HAND_JOINT_THUMB_TIP_EXT) - pos(XR_HAND_JOINT_INDEX_TIP_EXT)).Length();
  pinch_[h] = pinch_[h] ? pinchDist < 0.035f : pinchDist < 0.018f;
  p.isHand = true;
  p.grab = pinch_[h];
  return true;
}

void SessionInput::Update(XrSpace base, XrTime time, const Pose& head, bool headValid) {
  for (int h = 0; h < 2; ++h) {
    PointerState p;
    if (UpdateController(h, base, time, p)) {
      p.valid = true;
    } else if (headValid && UpdateHand(h, base, time, head, p)) {
      p.valid = true;
    } else {
      p = {};
      pinch_[h] = grabLatch_[h] = selectLatch_[h] = false;
    }
    pointers_[h] = p;
  }
}

void SessionInput::Haptic(int h, float seconds, float amplitude) {
  if (!actions_ || !actions_->Attached(session_) || pointers_[h].isHand) return;
  XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
  hi.action = actions_->Haptic();
  hi.subactionPath = actions_->HandPath(h);
  XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
  v.duration = static_cast<XrDuration>(seconds * 1e9);
  v.frequency = XR_FREQUENCY_UNSPECIFIED;
  v.amplitude = amplitude;
  next_->xrApplyHapticFeedback(session_, &hi, reinterpret_cast<const XrHapticBaseHeader*>(&v));
}
