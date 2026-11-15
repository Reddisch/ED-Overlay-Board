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

// Reading side (also usable as an example for other programs).
class Reader {
 public:
  Reader() = default;
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  ~Reader() { Close(); }

  // Consistent copy of the state; false if the layer is not running (or stopped rendering).
  bool Read(State& out) {
    if (!state_) {
      const uint64_t now = GetTickCount64();
      if (now - lastTry_ < 1000) return false;  // try to open at most once a second
      lastTry_ = now;
      mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
      if (mapping_) state_ = static_cast<const State*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(State)));
      if (!state_) {
        Close();
        return false;
      }
    }
    if (state_->magic != kMagic || state_->version != kVersion) return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
      const LONG before = state_->sequence;
      MemoryBarrier();
      if (before & 1) continue;  // being written right now
      out = *const_cast<const State*>(state_);
      MemoryBarrier();
      if (state_->sequence == before) return out.heartbeat != 0 && GetTickCount64() - out.heartbeat < 1000;
    }
    return false;
  }

 private:
  void Close() {
    if (state_) UnmapViewOfFile(state_);
    if (mapping_) CloseHandle(mapping_);
    state_ = nullptr;
    mapping_ = nullptr;
  }

  HANDLE mapping_ = nullptr;
  const State* state_ = nullptr;
  uint64_t lastTry_ = 0;
};

}  // namespace gaze
