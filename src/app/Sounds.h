#pragma once

#include <windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <memory>
#include <vector>

// Short sounds generated in memory (no sound files): the click in VR and one sound per show
// effect for appearing / disappearing windows. Played through waveOut, one voice per sound, so
// several sounds can overlap (a click and a window appearing, several windows at once).
class Sounds {
 public:
  enum Id {
    kClick,
    kShowNone, kHideNone,
    kShowHologram, kHideHologram,
    kShowZoom, kHideZoom,
    kShowTv, kHideTv,
    kCount
  };

  ~Sounds();
  void Play(Id id, float volume);                       // volume 0 = off, 1 = full
  void PlayEffect(int effect, bool show, float volume);  // effect = ipc::ShowEffect
  void Update();                                         // frees voices that have finished

 private:
  struct Voice {
    HWAVEOUT out = nullptr;
    WAVEHDR hdr{};
    std::vector<int16_t> pcm;
  };
  static void Close(Voice& v);
  const std::vector<float>& Samples(Id id);

  std::vector<std::unique_ptr<Voice>> voices_;
  std::vector<float> samples_[kCount];
};
