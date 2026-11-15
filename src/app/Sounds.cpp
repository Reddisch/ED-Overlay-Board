#include "Sounds.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr uint32_t kRate = 44100;
constexpr float kTwoPi = 6.28318531f;
constexpr size_t kMaxVoices = 12;

struct Noise {  // cheap white noise, -1..1
  uint32_t state = 0x12345678u;
  float operator()() {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
  }
};

struct LowPass {  // one-pole low-pass filter
  float y = 0.0f;
  float operator()(float x, float cutoffHz) {
    const float a = 1.0f - std::exp(-kTwoPi * cutoffHz / kRate);
    y += a * (x - y);
    return y;
  }
};

struct Osc {  // sine with a changing frequency (phase accumulator, no clicks on sweeps)
  float phase = 0.0f;
  float operator()(float hz) {
    phase += kTwoPi * hz / kRate;
    if (phase > kTwoPi) phase -= kTwoPi;
    return std::sin(phase);
  }
};

float Sweep(float from, float to, float x) { return from * std::pow(to / from, std::clamp(x, 0.0f, 1.0f)); }

// soft start (attack seconds) and a fade over the last `release` fraction of the sound
float Envelope(float t, float dur, float attack, float release) {
  float e = std::min(1.0f, t / attack);
  const float x = t / dur;
  if (x > 1.0f - release) e *= std::max(0.0f, (1.0f - x) / release);
  return e;
}

template <class Fn>
std::vector<float> Synth(float seconds, Fn&& fn) {
  const size_t n = static_cast<size_t>(seconds * kRate);
  std::vector<float> s(n);
  for (size_t i = 0; i < n; ++i) s[i] = fn(static_cast<float>(i) / kRate, seconds);
  return s;
}

std::vector<float> Click() {
  Noise noise;
  return Synth(0.045f, [&](float t, float) {
    // bright tick (fast decay) + a softer body tone + a tiny noise transient
    float s = 0.55f * std::sin(kTwoPi * 2600.0f * t) * std::exp(-t * 180.0f) +
              0.35f * std::sin(kTwoPi * 1150.0f * t) * std::exp(-t * 90.0f) +
              0.25f * noise() * std::exp(-t * 900.0f);
    return s * std::min(1.0f, t * kRate / 22.0f);  // ~0.5 ms attack, avoids a hard pop
  });
}

// no effect: a short two-tone blip, up when appearing, down when disappearing
std::vector<float> Blip(bool show) {
  return Synth(0.13f, [&](float t, float dur) {
    const bool second = t >= 0.055f;
    const float hz = (second == show) ? 990.0f : 660.0f;
    const float local = second ? t - 0.055f : t;
    return 0.32f * std::sin(kTwoPi * hz * t) * std::exp(-local * 30.0f) * Envelope(t, dur, 0.003f, 0.1f);
  });
}

// hologram: a shimmering, slightly detuned sci-fi sweep with sparkles
std::vector<float> Hologram(bool show) {
  Osc a, b, c;
  Noise noise;
  LowPass lp;
  return Synth(show ? 0.55f : 0.385f, [&](float t, float dur) {
    const float x = t / dur;
    const float hz = show ? Sweep(320.0f, 1350.0f, std::sqrt(x)) : Sweep(1350.0f, 300.0f, x * x * (3 - 2 * x));
    float s = 0.45f * a(hz) + 0.35f * b(hz * 1.0072f) + 0.12f * c(hz * 2.003f);
    s *= 0.75f + 0.25f * std::sin(kTwoPi * 27.0f * t);  // shimmer
    const float n = noise();
    const float sparkle = (n - lp(n, 5000.0f)) * 0.18f * (0.5f + 0.5f * std::sin(kTwoPi * 11.0f * t));
    return 0.30f * (s + sparkle) * Envelope(t, dur, 0.025f, 0.4f);
  });
}

// zoom: an air whoosh (filtered noise) with a pitch blip, rising in, falling out
std::vector<float> Zoom(bool show) {
  Osc o;
  Noise noise;
  LowPass lp1, lp2;
  return Synth(show ? 0.30f : 0.21f, [&](float t, float dur) {
    const float x = t / dur;
    const float cutoff = show ? Sweep(350.0f, 4500.0f, x) : Sweep(4500.0f, 300.0f, x);
    const float n = lp2(lp1(noise(), cutoff), cutoff);
    const float hz = show ? Sweep(480.0f, 1150.0f, x) : Sweep(1150.0f, 420.0f, x);
    const float peak = show ? 0.65f : 0.35f;  // loudest point of the whoosh
    const float shape = x < peak ? x / peak : (1.0f - x) / (1.0f - peak);
    return (0.9f * n + 0.16f * o(hz)) * shape * shape * Envelope(t, dur, 0.004f, 0.15f);
  });
}

// CRT TV: power-on = pop + thump + static + a faint line whine;
// power-off = the classic falling "zip" with a crackle and a last pop
std::vector<float> Tv(bool show) {
  Osc o, whine;
  Noise noise;
  LowPass lp;
  if (show)
    return Synth(0.35f, [&](float t, float dur) {
      const float n = noise();
      float s = 0.55f * n * std::exp(-t * 600.0f) +                      // pop
                0.45f * std::sin(kTwoPi * 58.0f * t) * std::exp(-t * 18.0f) +  // thump of the tube
                0.28f * lp(n, 3500.0f) * std::exp(-t * 9.0f) +             // static
                0.035f * whine(7800.0f) * std::min(1.0f, t * 12.0f);       // line whine
      return 0.6f * s * Envelope(t, dur, 0.001f, 0.25f);
    });
  return Synth(0.245f, [&](float t, float dur) {
    const float x = t / dur;
    const float n = noise();
    float s = 0.4f * o(Sweep(1900.0f, 70.0f, x)) +           // zip
              0.25f * lp(n, 3000.0f) * std::exp(-t * 25.0f);  // crackle
    if (x > 0.9f) s += 0.4f * n * std::exp(-(t - 0.9f * dur) * 500.0f);  // last pop
    return 0.6f * s * Envelope(t, dur, 0.001f, 0.08f);
  });
}
}  // namespace

Sounds::~Sounds() {
  for (auto& v : voices_) Close(*v);
}

void Sounds::Close(Voice& v) {
  if (!v.out) return;
  waveOutReset(v.out);
  waveOutUnprepareHeader(v.out, &v.hdr, sizeof(v.hdr));
  waveOutClose(v.out);
  v.out = nullptr;
}

const std::vector<float>& Sounds::Samples(Id id) {
  auto& s = samples_[id];
  if (s.empty()) {
    switch (id) {
      case kClick: s = Click(); break;
      case kShowNone: s = Blip(true); break;
      case kHideNone: s = Blip(false); break;
      case kShowHologram: s = Hologram(true); break;
      case kHideHologram: s = Hologram(false); break;
      case kShowZoom: s = Zoom(true); break;
      case kHideZoom: s = Zoom(false); break;
      case kShowTv: s = Tv(true); break;
      case kHideTv: s = Tv(false); break;
      default: break;
    }
  }
  return s;
}

void Sounds::Update() {
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                               [](const std::unique_ptr<Voice>& v) {
                                 if (!(v->hdr.dwFlags & WHDR_DONE)) return false;
                                 Close(*v);
                                 return true;
                               }),
                voices_.end());
}

void Sounds::Play(Id id, float volume) {
  volume = std::clamp(volume, 0.0f, 1.0f);
  if (volume <= 0.001f || id < 0 || id >= kCount) return;
  Update();
  if (voices_.size() >= kMaxVoices) {  // far too many at once: drop the oldest
    Close(*voices_.front());
    voices_.erase(voices_.begin());
  }

  const auto& src = Samples(id);
  auto v = std::make_unique<Voice>();
  v->pcm.resize(src.size());
  for (size_t i = 0; i < src.size(); ++i)
    v->pcm[i] = static_cast<int16_t>(std::clamp(src[i] * volume, -1.0f, 1.0f) * 32000.0f);

  WAVEFORMATEX fmt{};
  fmt.wFormatTag = WAVE_FORMAT_PCM;
  fmt.nChannels = 1;
  fmt.nSamplesPerSec = kRate;
  fmt.wBitsPerSample = 16;
  fmt.nBlockAlign = 2;
  fmt.nAvgBytesPerSec = kRate * 2;
  if (waveOutOpen(&v->out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    v->out = nullptr;
    return;
  }
  v->hdr.lpData = reinterpret_cast<LPSTR>(v->pcm.data());
  v->hdr.dwBufferLength = static_cast<DWORD>(v->pcm.size() * sizeof(int16_t));
  if (waveOutPrepareHeader(v->out, &v->hdr, sizeof(v->hdr)) != MMSYSERR_NOERROR ||
      waveOutWrite(v->out, &v->hdr, sizeof(v->hdr)) != MMSYSERR_NOERROR) {
    Close(*v);
    return;
  }
  voices_.push_back(std::move(v));
}

void Sounds::PlayEffect(int effect, bool show, float volume) {
  static constexpr Id kShow[] = {kShowNone, kShowHologram, kShowZoom, kShowTv};
  static constexpr Id kHide[] = {kHideNone, kHideHologram, kHideZoom, kHideTv};
  const int e = std::clamp(effect, 0, 3);
  Play(show ? kShow[e] : kHide[e], volume);
}
