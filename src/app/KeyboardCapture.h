#pragma once

#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

// "PC keyboard into a VR window": while running, a low-level keyboard hook takes the keys away
// from the game (Elite keeps the focus but no longer sees them) and queues them for the app,
// which forwards them to the program shown in VR as window messages.
//
// Keys are not taken when:
//  - they are injected by software (VoiceAttack, EDCoPilot, AutoHotkey ... keep working),
//  - the foreground window belongs to this app or to `passProcess` (typing at the desktop),
//  - it is the release of a key that was already held when the capture started.
// The app's own hotkeys (RegisterHotKey) would not fire for taken keys, so they are matched
// here and reported as `binding` events.
class KeyboardCapture {
 public:
  struct Event {
    UINT vk = 0;
    UINT scan = 0;
    bool extended = false;
    bool down = false;
    int binding = 0;  // > 0: this key combination is the app hotkey with this id (key not forwarded)
  };
  struct Hotkey {
    UINT modifiers = 0;  // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk = 0;
    int id = 0;  // > 0
  };

  ~KeyboardCapture() { Stop(); }
  bool Start(DWORD passProcess);
  void Stop();
  bool Running() const { return thread_.joinable(); }
  void SetPassProcess(DWORD pid) { passProcess_ = pid; }
  void SetHotkeys(std::vector<Hotkey> hotkeys);
  std::vector<Event> Drain();

 private:
  static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp);
  bool Handle(WPARAM wp, const KBDLLHOOKSTRUCT& k);  // true = take the key away from the system
  UINT Modifiers() const;

  static KeyboardCapture* instance_;
  std::thread thread_;
  DWORD threadId_ = 0;
  std::atomic<DWORD> passProcess_{0};
  DWORD ownProcess_ = GetCurrentProcessId();
  DWORD mainThread_ = 0;

  // hook thread only
  bool down_[256]{};        // physical state as seen by the hook
  bool systemDown_[256]{};  // keys the system saw going down (their release must reach it too)

  std::mutex mutex_;
  std::vector<Event> queue_;
  std::vector<Hotkey> hotkeys_;
};
