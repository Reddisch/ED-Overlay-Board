#pragma once

#include <Unknwn.h>
#include <windows.h>

#include <d3d11_1.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include "VrMath.h"

using Microsoft::WRL::ComPtr;

inline Pose ToPose(const XrPosef& p) {
  return Pose::FromQuat({p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                        {p.position.x, p.position.y, p.position.z});
}

inline XrPosef ToXr(const Pose& p) {
  Quat q = p.ToQuat();
  XrPosef out{};
  out.orientation = {q.x, q.y, q.z, q.w};
  out.position = {p.t.x, p.t.y, p.t.z};
  return out;
}

// Functions of the next layer / the runtime that the layer calls itself.
#define EDOB_XR_CORE_FUNCS(X)                                                                              \
  X(xrDestroyInstance) X(xrGetInstanceProperties) X(xrGetSystemProperties) X(xrCreateSession)              \
  X(xrDestroySession) X(xrEndFrame) X(xrPollEvent) X(xrCreateReferenceSpace) X(xrCreateActionSpace) X(xrDestroySpace)     \
  X(xrLocateSpace) X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain)               \
  X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage)                         \
  X(xrReleaseSwapchainImage) X(xrStringToPath) X(xrCreateActionSet) X(xrCreateAction)                      \
  X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrSyncActions)                     \
  X(xrGetActionStateFloat) X(xrGetActionStateVector2f) X(xrGetActionStatePose) X(xrApplyHapticFeedback)

#define EDOB_XR_HAND_FUNCS(X) X(xrCreateHandTrackerEXT) X(xrDestroyHandTrackerEXT) X(xrLocateHandJointsEXT)

struct NextDispatch {
  PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
#define EDOB_DECL(name) PFN_##name name = nullptr;
  EDOB_XR_CORE_FUNCS(EDOB_DECL)
  EDOB_XR_HAND_FUNCS(EDOB_DECL)
#undef EDOB_DECL

  void Load(XrInstance instance, bool handTracking) {
#define EDOB_LOAD(name) xrGetInstanceProcAddr(instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&name));
    EDOB_XR_CORE_FUNCS(EDOB_LOAD)
    if (handTracking) {
      EDOB_XR_HAND_FUNCS(EDOB_LOAD)
    }
#undef EDOB_LOAD
  }
};
