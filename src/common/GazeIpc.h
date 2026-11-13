#pragma once
// Read-only gaze information for other programs, written by the layer once per frame.
//
// Companion tools that are mirrored into VR (e.g. a route planner that can be driven with a
// joystick) can find out whether the user is currently looking at their window, and react to
// input only then. Independent of the app <-> layer protocol in Ipc.h (no version coupling).
//
// Readers open the mapping read-only. A write is bracketed by two increments of `sequence`:
// an odd value means a write is in progress; if the value changed during a read, read again.
// `heartbeat` is GetTickCount64() of the last write; older than ~1 s = the game is not running
// (or no longer renders), treat every window as not looked at.

#include <windows.h>

#include <cstdint>

namespace gaze {

constexpr uint32_t kMagic = 0x5A41474F;  // "OGAZ"
constexpr uint32_t kVersion = 1;
constexpr int kMaxWindows = 16;

inline constexpr wchar_t kMappingName[] = L"Local\\EDOverlayBoard.Gaze.v1";

struct Entry {        // 80 bytes
  char name[64];      // window name from config.json (UTF-8, zero-terminated)
  uint32_t active;    // slot in use
  uint32_t shown;     // drawn this frame (visible, not hidden by lookToShow / "hide all")
  uint32_t looked;    // shown and the view direction (head forward) hits the window or its frame
  uint32_t reserved;
};

struct State {             // offsets: 0, 4, 8, 12, 16, 24
  uint32_t magic;
  uint32_t version;
  volatile LONG sequence;  // odd while writing
  uint32_t count;          // number of entries (kMaxWindows)
  uint64_t heartbeat;      // GetTickCount64() of the last write, 0 = layer gone
  Entry windows[kMaxWindows];
};
static_assert(sizeof(Entry) == 80, "gaze::Entry layout is part of the public format");
static_assert(sizeof(State) == 24 + 80 * kMaxWindows, "gaze::State layout is part of the public format");

class Publisher {
 public:
  Publisher() = default;
  Publisher(const Publisher&) = delete;
  Publisher& operator=(const Publisher&) = delete;
  ~Publisher() { Close(); }

  // fn(State&) fills the entries; magic, version, sequence and heartbeat are handled here.
  template <class Fn>
  void Write(Fn&& fn) {
    if (!state_ && !Open()) return;
    InterlockedIncrement(&state_->sequence);  // odd: writing (full barrier)
    fn(*state_);
    state_->heartbeat = GetTickCount64();
    InterlockedIncrement(&state_->sequence);  // even: consistent again
  }

 private:
  bool Open() {
    if (failed_) return false;  // don't retry every frame
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), kMappingName);
    if (mapping_) state_ = static_cast<State*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(State)));
    if (!state_) {
      Close();
      failed_ = true;
      return false;
    }
    state_->magic = kMagic;
    state_->version = kVersion;
    return true;
  }

  void Close() {
    if (state_) {
      state_->heartbeat = 0;  // readers that still hold the mapping see "gone" at once
      UnmapViewOfFile(state_);
    }
    if (mapping_) CloseHandle(mapping_);
    state_ = nullptr;
    mapping_ = nullptr;
  }

  HANDLE mapping_ = nullptr;
  State* state_ = nullptr;
  bool failed_ = false;
};

}  // namespace gaze
