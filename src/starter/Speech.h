#pragma once
// Windows speech output (SAPI) for the starter: says what happens, so a start can be followed
// in the headset without looking at the window.

#include <windows.h>

#include <sapi.h>
#include <wrl/client.h>

#include <string>

class Speech {
 public:
  // Picks an installed voice for the language ("de" / "en"); falls back to the default voice.
  void SetLanguage(bool english);
  // Queued, never blocks. Does nothing while disabled or when no voice is installed.
  void Say(const std::string& utf8);
  void SetEnabled(bool on);

 private:
  bool Init();

  Microsoft::WRL::ComPtr<ISpVoice> voice_;
  bool tried_ = false;
  bool enabled_ = false;
  int language_ = -1;  // 0 = German, 1 = English, -1 = not chosen yet
};
