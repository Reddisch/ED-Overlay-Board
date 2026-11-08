#pragma once

#include <cstdint>
#include <vector>

// Short "tick" played when something is clicked in VR (generated in memory, no sound file).
class ClickSound {
 public:
  ~ClickSound();
  void Play(float volume);  // 0 = off, 1 = full

 private:
  void Build(float volume);

  std::vector<uint8_t> wav_;
  float builtVolume_ = -1.0f;
};
