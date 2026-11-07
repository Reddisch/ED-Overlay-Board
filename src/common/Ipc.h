#pragma once
// Shared-memory protocol between the companion app (EDOverlayBoard.exe) and the
// OpenXR API layer (EDOverlayBoard_Layer.dll, loaded into the game process).
//
// The app owns: window list, visibility, textures, settings, recenter requests.
// The layer owns: placements changed by the user in VR, click events, its own status.
// All access happens while holding the named mutex.

#include <windows.h>

#include <cstdint>
#include <string>

#include "VrMath.h"

namespace ipc {

constexpr uint32_t kMagic = 0x424F4445;  // "EDOB"
constexpr uint32_t kVersion = 5;
constexpr int kMaxWindows = 16;
constexpr uint64_t kHeartbeatTimeoutMs = 3000;

inline constexpr wchar_t kMappingName[] = L"Local\\EDOverlayBoard.State.v5";
inline constexpr wchar_t kMutexName[] = L"Local\\EDOverlayBoard.Mutex.v5";

// Name of a shared D3D11 texture (NT handle + keyed mutex, BGRA8). `generation` is unique
// within one app instance, so a name is never reused while the layer may still hold it.
inline std::wstring TextureName(uint32_t appInstance, uint32_t generation) {
  return L"Local\\EDOverlayBoard.Tex." + std::to_wstring(appInstance) + L"." + std::to_wstring(generation);
}

struct Placement {  // relative to the recentered head pose
  float r[3][3];
  float t[3];
  float width;  // meters

  static Placement From(const Pose& p, float width) {
    Placement pl{};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) pl.r[i][j] = p.r[i][j];
    pl.t[0] = p.t.x;
    pl.t[1] = p.t.y;
    pl.t[2] = p.t.z;
    pl.width = width;
    return pl;
  }
  Pose ToPose() const {
    Pose p;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) p.r[i][j] = r[i][j];
    p.t = {t[0], t[1], t[2]};
    return p;
  }
};

struct Window {
  char name[64];
  uint32_t active;   // slot in use
  uint32_t visible;
  uint32_t locked;
  uint32_t opaque;   // ignore texture alpha (captured windows)
  uint32_t lookToShow;  // only visible (and interactive) while looking towards it
  uint32_t interactive;   // the VR menu: stays visible while all windows are hidden
  uint32_t mouseInput;    // trigger/pinch act as a mouse (see pointer* below): VR menu, clickable programs
  uint32_t summonOnShow;  // when it becomes visible, place it in front of the user
  float headerFraction;   // mouseInput: top part (0..1) that grabs instead of clicking
  float opacity;

  // texture (app -> layer)
  uint32_t texWidth, texHeight;
  uint32_t texGeneration;   // changes whenever the shared texture is recreated
  uint32_t contentVersion;  // changes whenever the pixels change

  Placement appPlacement;  // app -> layer (config at startup)
  uint32_t appPlacementSerial;
  Placement layerPlacement;  // layer -> app (moved/resized in VR)
  uint32_t layerPlacementSerial;

  uint32_t clickSerial;  // layer -> app
  float clickX, clickY;  // [-0.5, 0.5], x right, y up

  // layer -> app, mouseInput windows only (coordinates 0..1, origin top-left)
  uint32_t pointerActive;
  float pointerX, pointerY;
  uint32_t pointerDownSerial, pointerUpSerial;  // each press / release increments
  float pointerWheel;                           // accumulated wheel steps (thumbstick)
};

struct State {
  uint32_t magic;
  uint32_t version;

  // app -> layer
  uint32_t appInstance;
  uint64_t appHeartbeat;  // GetTickCount64()
  uint32_t windowCount;
  uint32_t recenterSerial;
  uint32_t allHidden;
  uint32_t interactionEnabled;
  uint32_t showCursor;
  uint32_t haptics;
  float scaleSpeed;
  float pushPullSpeed;
  float lookMarginDeg;  // extra angle around a window that still counts as "looking at it"
  float sharpness;      // 1 = texture matched to display pixels (no shimmer), higher = sharper

  // layer -> app
  uint64_t layerHeartbeat;
  uint32_t layerPid;
  LUID layerAdapter;
  uint32_t layerHandTracking;
  uint32_t layerControllers;
  char layerRuntime[128];
  char layerApplication[128];

  Window windows[kMaxWindows];
};

class SharedState {
 public:
  ~SharedState() { Close(); }

  bool Create() {  // app side
    Close();
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), kMappingName);
    if (!mapping_) return false;
    return Map();
  }

  bool Open() {  // layer side; fails if the app never ran
    Close();
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMappingName);
    if (!mapping_) return false;
    return Map();
  }

  void Close() {
    if (state_) UnmapViewOfFile(state_);
    if (mapping_) CloseHandle(mapping_);
    if (mutex_) CloseHandle(mutex_);
    state_ = nullptr;
    mapping_ = mutex_ = nullptr;
  }

  bool Valid() const { return state_ != nullptr; }
  State* Get() const { return state_; }

  class Lock {
   public:
    Lock(HANDLE m, DWORD timeoutMs) : m_(m) {
      DWORD r = m ? WaitForSingleObject(m, timeoutMs) : WAIT_FAILED;
      locked_ = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
    }
    ~Lock() {
      if (locked_) ReleaseMutex(m_);
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    explicit operator bool() const { return locked_; }

   private:
    HANDLE m_;
    bool locked_ = false;
  };

  Lock Acquire(DWORD timeoutMs) const { return Lock(mutex_, timeoutMs); }

 private:
  bool Map() {
    state_ = static_cast<State*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(State)));
    mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!state_ || !mutex_) {
      Close();
      return false;
    }
    return true;
  }

  HANDLE mapping_ = nullptr;
  HANDLE mutex_ = nullptr;
  State* state_ = nullptr;
};

}  // namespace ipc
