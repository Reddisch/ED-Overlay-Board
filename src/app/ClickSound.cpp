#include "ClickSound.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
template <typename T>
void Put(std::vector<uint8_t>& v, T value) {
  const auto* p = reinterpret_cast<const uint8_t*>(&value);
  v.insert(v.end(), p, p + sizeof(T));
}
}  // namespace

ClickSound::~ClickSound() {
  if (!wav_.empty()) PlaySoundW(nullptr, nullptr, 0);  // stop before the buffer goes away
}

void ClickSound::Build(float volume) {
  constexpr uint32_t kRate = 44100;
  constexpr uint32_t kSamples = kRate * 45 / 1000;  // 45 ms
  std::vector<int16_t> pcm(kSamples);
  uint32_t noise = 0x12345678u;
  for (uint32_t i = 0; i < kSamples; ++i) {
    const float t = static_cast<float>(i) / kRate;
    noise = noise * 1664525u + 1013904223u;
    const float n = (static_cast<float>(noise >> 8) / 8388608.0f - 1.0f);
    // bright tick (fast decay) + a softer body tone + a tiny noise transient
    float s = 0.55f * std::sin(2.0f * 3.14159265f * 2600.0f * t) * std::exp(-t * 180.0f) +
              0.35f * std::sin(2.0f * 3.14159265f * 1150.0f * t) * std::exp(-t * 90.0f) +
              0.25f * n * std::exp(-t * 900.0f);
    const float attack = std::min(1.0f, i / 22.0f);  // ~0.5 ms, avoids a hard pop
    s *= attack * volume;
    pcm[i] = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32000.0f);
  }

  const uint32_t dataBytes = kSamples * 2;
  wav_.clear();
  wav_.reserve(44 + dataBytes);
  wav_.insert(wav_.end(), {'R', 'I', 'F', 'F'});
  Put<uint32_t>(wav_, 36 + dataBytes);
  wav_.insert(wav_.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  Put<uint32_t>(wav_, 16);
  Put<uint16_t>(wav_, 1);  // PCM
  Put<uint16_t>(wav_, 1);  // mono
  Put<uint32_t>(wav_, kRate);
  Put<uint32_t>(wav_, kRate * 2);
  Put<uint16_t>(wav_, 2);
  Put<uint16_t>(wav_, 16);
  wav_.insert(wav_.end(), {'d', 'a', 't', 'a'});
  Put<uint32_t>(wav_, dataBytes);
  const auto* bytes = reinterpret_cast<const uint8_t*>(pcm.data());
  wav_.insert(wav_.end(), bytes, bytes + dataBytes);
  builtVolume_ = volume;
}

void ClickSound::Play(float volume) {
  volume = std::clamp(volume, 0.0f, 1.0f);
  if (volume <= 0.001f) return;
  if (std::fabs(volume - builtVolume_) > 0.001f) {
    if (!wav_.empty()) PlaySoundW(nullptr, nullptr, 0);  // the old buffer may still be playing
    Build(volume);
  }
  PlaySoundW(reinterpret_cast<LPCWSTR>(wav_.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
