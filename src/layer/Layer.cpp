// OpenXR API layer entry points.
// The loader calls xrNegotiateLoaderApiLayerInterface, then our xrCreateApiLayerInstance;
// afterwards every OpenXR call of the game goes through Hook_xrGetInstanceProcAddr.

#include "Input.h"
#include "Log.h"
#include "OverlaySession.h"
#include "XrIncludes.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kLayerName = "XR_APILAYER_EDOB_overlay_board";

struct Global {
  std::mutex mutex;
  XrInstance instance = XR_NULL_HANDLE;
  NextDispatch next;
  bool handTrackingExt = false;
  bool colorScaleBiasExt = false;
  std::string runtimeName;
  std::string appName;
  InputActions actions;
  std::unique_ptr<OverlaySession> session;
};
Global* g = nullptr;

std::filesystem::path LogPath() {
  wchar_t buf[MAX_PATH]{};
  GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  return std::filesystem::path(buf) / L"EDOverlayBoard" / (L"layer-" + std::filesystem::path(exe).stem().wstring() + L".log");
}

bool HasExtension(const std::vector<XrExtensionProperties>& exts, const char* name) {
  for (const auto& e : exts)
    if (std::string_view(e.extensionName) == name) return true;
  return false;
}

// ---------------------------------------------------------------------------
// Hooks

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrGetInstanceProcAddr(XrInstance instance, const char* name,
                                                          PFN_xrVoidFunction* function);

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrDestroyInstance(XrInstance instance) {
  PFN_xrDestroyInstance next = g->next.xrDestroyInstance;
  {
    std::lock_guard lock(g->mutex);
    if (instance == g->instance) {
      g->session.reset();
      g->instance = XR_NULL_HANDLE;
    }
  }
  return next(instance);
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo,
                                                    XrSession* session) {
  XrResult r = g->next.xrCreateSession(instance, createInfo, session);
  if (XR_FAILED(r) || instance != g->instance || !createInfo) return r;

  const XrGraphicsBindingD3D11KHR* d3d11 = nullptr;
  for (auto* p = static_cast<const XrBaseInStructure*>(createInfo->next); p; p = p->next)
    if (p->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) d3d11 = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(p);
  if (!d3d11 || !d3d11->device) {
    Log("Session uses a graphics API other than D3D11 - overlay disabled for this game");
    return r;
  }

  std::lock_guard lock(g->mutex);
  try {
    OverlaySession::Params p{&g->next,        g->instance,        createInfo->systemId, *session,
                             d3d11->device,   &g->actions,        g->handTrackingExt,   g->colorScaleBiasExt,
                             g->runtimeName,  g->appName};
    g->session = std::make_unique<OverlaySession>(p);
  } catch (...) {
    Log("Failed to set up the overlay session");
    g->session.reset();
  }
  return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrDestroySession(XrSession session) {
  {
    std::lock_guard lock(g->mutex);
    if (g->session && g->session->Handle() == session) g->session.reset();
    g->actions.OnSessionDestroyed(session);
  }
  return g->next.xrDestroySession(session);
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
  OverlaySession* s = g->session.get();
  if (s && s->Handle() == session) return s->EndFrame(info);
  return g->next.xrEndFrame(session, info);
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrPollEvent(XrInstance instance, XrEventDataBuffer* event) {
  XrResult r = g->next.xrPollEvent(instance, event);
  if (r == XR_SUCCESS && event && event->type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
    auto* e = reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(event);
    OverlaySession* s = g->session.get();
    if (s && s->Handle() == e->session && e->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
      s->OnLocalSpaceChanged();
  }
  return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrSuggestInteractionProfileBindings(
    XrInstance instance, const XrInteractionProfileSuggestedBinding* info) {
  if (instance != g->instance) return g->next.xrSuggestInteractionProfileBindings(instance, info);
  return g->actions.SuggestBindings(instance, info);
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrAttachSessionActionSets(XrSession session,
                                                              const XrSessionActionSetsAttachInfo* info) {
  return g->actions.AttachSessionActionSets(session, info);
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrSyncActions(XrSession session, const XrActionsSyncInfo* info) {
  return g->actions.SyncActions(session, info);
}

XRAPI_ATTR XrResult XRAPI_CALL Hook_xrGetInstanceProcAddr(XrInstance instance, const char* name,
                                                          PFN_xrVoidFunction* function) {
  if (!g || !g->next.xrGetInstanceProcAddr) return XR_ERROR_HANDLE_INVALID;
  std::string_view n(name ? name : "");
#define EDOB_HOOK(fn)                                             \
  if (n == #fn) {                                                 \
    *function = reinterpret_cast<PFN_xrVoidFunction>(Hook_##fn);  \
    return XR_SUCCESS;                                            \
  }
  if (instance != XR_NULL_HANDLE) {
    EDOB_HOOK(xrGetInstanceProcAddr)
    EDOB_HOOK(xrDestroyInstance)
    EDOB_HOOK(xrCreateSession)
    EDOB_HOOK(xrDestroySession)
    EDOB_HOOK(xrEndFrame)
    EDOB_HOOK(xrPollEvent)
    EDOB_HOOK(xrSuggestInteractionProfileBindings)
    EDOB_HOOK(xrAttachSessionActionSets)
    EDOB_HOOK(xrSyncActions)
  }
#undef EDOB_HOOK
  return g->next.xrGetInstanceProcAddr(instance, name, function);
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info,
                                                              const XrApiLayerCreateInfo* layerInfo,
                                                              XrInstance* instance) {
  if (!layerInfo || !layerInfo->nextInfo || !info) return XR_ERROR_INITIALIZATION_FAILED;
  if (!g) g = new Global();  // intentionally leaked: may still be referenced during process shutdown
  LogInit(LogPath(), false);

  PFN_xrGetInstanceProcAddr nextGipa = layerInfo->nextInfo->nextGetInstanceProcAddr;
  Log("ED Overlay Board layer {} loaded into \"{}\"", EDOB_VERSION, info->applicationInfo.applicationName);

  // Enable optional extensions the runtime supports and the game did not ask for.
  std::vector<XrExtensionProperties> available;
  PFN_xrEnumerateInstanceExtensionProperties enumExt = nullptr;
  if (XR_SUCCEEDED(nextGipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
                            reinterpret_cast<PFN_xrVoidFunction*>(&enumExt))) &&
      enumExt) {
    uint32_t n = 0;
    if (XR_SUCCEEDED(enumExt(nullptr, 0, &n, nullptr))) {
      available.resize(n, {XR_TYPE_EXTENSION_PROPERTIES});
      enumExt(nullptr, n, &n, available.data());
    }
  }
  std::vector<const char*> exts(info->enabledExtensionNames, info->enabledExtensionNames + info->enabledExtensionCount);
  auto enable = [&](const char* name) {
    for (const char* e : exts)
      if (std::string_view(e) == name) return true;
    if (!HasExtension(available, name)) return false;
    exts.push_back(name);
    return true;
  };
  bool handTracking = enable(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
  bool colorScale = enable(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);

  XrInstanceCreateInfo ci = *info;
  ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
  ci.enabledExtensionNames = exts.data();
  XrApiLayerCreateInfo li = *layerInfo;
  li.nextInfo = layerInfo->nextInfo->next;

  XrResult r = layerInfo->nextInfo->nextCreateApiLayerInstance(&ci, &li, instance);
  if (XR_FAILED(r) && ci.enabledExtensionCount != info->enabledExtensionCount) {
    Log("Instance creation with extra extensions failed ({}), retrying without", static_cast<int>(r));
    handTracking = colorScale = false;
    r = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &li, instance);
  }
  if (XR_FAILED(r)) return r;

  std::lock_guard lock(g->mutex);
  g->instance = *instance;
  g->next = {};
  g->next.xrGetInstanceProcAddr = nextGipa;
  g->next.Load(*instance, handTracking);
  g->handTrackingExt = handTracking && g->next.xrCreateHandTrackerEXT && g->next.xrLocateHandJointsEXT;
  g->colorScaleBiasExt = colorScale;
  g->appName = info->applicationInfo.applicationName;

  XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_SUCCEEDED(g->next.xrGetInstanceProperties(*instance, &props))) g->runtimeName = props.runtimeName;
  Log("Runtime: {} (hand tracking ext: {}, color scale ext: {})", g->runtimeName, g->handTrackingExt, colorScale);

  g->actions = InputActions();
  g->actions.Create(g->next, *instance);
  return r;
}

}  // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* request) {
  if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
      request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)
    return XR_ERROR_INITIALIZATION_FAILED;
  if (layerName && std::string_view(layerName) != kLayerName) return XR_ERROR_INITIALIZATION_FAILED;
  if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
      loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
    return XR_ERROR_INITIALIZATION_FAILED;

  request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
  request->layerApiVersion = std::min<XrVersion>(XR_CURRENT_API_VERSION, loaderInfo->maxApiVersion);
  request->getInstanceProcAddr = Hook_xrGetInstanceProcAddr;
  request->createApiLayerInstance = Layer_xrCreateApiLayerInstance;
  return XR_SUCCESS;
}
