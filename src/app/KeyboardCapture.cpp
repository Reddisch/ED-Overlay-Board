#include "KeyboardCapture.h"

#include "Log.h"

KeyboardCapture* KeyboardCapture::instance_ = nullptr;

bool KeyboardCapture::Start(DWORD passProcess) {
  if (Running()) {
    passProcess_ = passProcess;
    return true;
  }
  if (instance_) return false;  // only one capture per process (the hook procedure is static)
  passProcess_ = passProcess;
  mainThread_ = GetCurrentThreadId();
  for (int vk = 1; vk < 256; ++vk) {  // keys already held (e.g. the hotkey that started this)
    down_[vk] = systemDown_[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
  }
  {
    std::lock_guard lock(mutex_);
    queue_.clear();
  }
  instance_ = this;

  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::atomic<bool> ok{false};
  thread_ = std::thread([this, ready, &ok] {
    threadId_ = GetCurrentThreadId();
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // create the message queue before anyone posts to it
    // The hook runs on this thread: it must answer quickly, so it gets its own message loop
    // (the app's main loop is busy with capturing and copying textures).
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    ok = hook != nullptr;
    if (!hook) Log("Keyboard: SetWindowsHookEx failed (error {})", GetLastError());
    SetEvent(ready);
    if (!hook) return;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(hook);
  });
  WaitForSingleObject(ready, 2000);
  CloseHandle(ready);
  if (!ok) {
    thread_.join();
    instance_ = nullptr;
    return false;
  }
  return true;
}

void KeyboardCapture::Stop() {
  if (!thread_.joinable()) return;
  PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
  thread_.join();
  instance_ = nullptr;
  std::lock_guard lock(mutex_);
  queue_.clear();
}

void KeyboardCapture::SetHotkeys(std::vector<Hotkey> hotkeys) {
  std::lock_guard lock(mutex_);
  hotkeys_ = std::move(hotkeys);
}

std::vector<KeyboardCapture::Event> KeyboardCapture::Drain() {
  std::lock_guard lock(mutex_);
  std::vector<Event> out;
  out.swap(queue_);
  return out;
}

LRESULT CALLBACK KeyboardCapture::HookProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && instance_ && instance_->Handle(wp, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp)))
    return 1;
  return CallNextHookEx(nullptr, code, wp, lp);
}

UINT KeyboardCapture::Modifiers() const {
  UINT m = 0;
  if (down_[VK_LCONTROL] || down_[VK_RCONTROL]) m |= MOD_CONTROL;
  if (down_[VK_LMENU] || down_[VK_RMENU]) m |= MOD_ALT;
  if (down_[VK_LSHIFT] || down_[VK_RSHIFT]) m |= MOD_SHIFT;
  if (down_[VK_LWIN] || down_[VK_RWIN]) m |= MOD_WIN;
  return m;
}

bool KeyboardCapture::Handle(WPARAM wp, const KBDLLHOOKSTRUCT& k) {
  if (k.flags & LLKHF_INJECTED) return false;  // macros from voice/HOTAS tools go to the game as before
  const UINT vk = k.vkCode & 0xFF;
  const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
  down_[vk] = down;

  // The user is typing at the desktop (into the settings window or the program itself).
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  const bool pass = pid == ownProcess_ || (pid != 0 && pid == passProcess_);
  if (pass || (!down && systemDown_[vk])) {
    systemDown_[vk] = down;
    return false;
  }

  Event e;
  e.vk = vk;
  e.scan = k.scanCode;
  e.extended = (k.flags & LLKHF_EXTENDED) != 0;
  e.down = down;
  {
    std::lock_guard lock(mutex_);
    if (down) {
      const UINT mods = Modifiers();
      for (const auto& h : hotkeys_)
        if (h.vk == vk && h.modifiers == mods) e.binding = h.id;
    }
    queue_.push_back(e);
  }
  PostThreadMessageW(mainThread_, WM_NULL, 0, 0);  // wake the main loop
  return true;
}
