#include "sound.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>

static Cue tone(float from, float to, float dur, float vol, float delay = 0, bool tri = false, float lowpass = 0, float attack = -1) { Cue c; c.from = from; c.to = to; c.duration = dur; c.volume = vol; c.delay = delay; c.triangle = tri; c.lowpass = lowpass; c.attack = attack; return c; }
static Cue noise(float dur, float lowpass, float vol, float sweep = 0, float delay = 0, float highpass = 0) { Cue c; c.noise = true; c.duration = dur; c.lowpass = lowpass; c.volume = vol; c.sweep = sweep; c.delay = delay; c.highpass = highpass; return c; }
static const std::map<std::string, std::vector<Cue>>& cues() {
  static const std::map<std::string, std::vector<Cue>> table = {
    {"begin", {noise(1.1f, 2000, .15f, 240), tone(110, 48, 1.2f, .32f), tone(784, 0, .5f, .12f, 0, true, 3000), tone(523, 0, .75f, .12f, .22f, true, 3000)}},
    {"submit", {tone(1150, 0, .16f, .16f, 0, false, 0, .003f)}},
    {"reject", {noise(.22f, 420, .2f), tone(150, 62, .36f, .22f, 0, false, 700)}},
    {"tier", {tone(780, 1560, .07f, .13f, 0, true, 2500)}},
    {"sink", {tone(1150, 0, .7f, .1f, 0, false, 0, .01f), noise(1.3f, 1400, .16f, 180), tone(300, 60, 1.3f, .1f)}},
    {"small", {tone(140, 70, .25f, .28f, 0, false, 600), noise(.14f, 500, .1f)}},
    {"big", {tone(80, 45, .6f, .38f), noise(.7f, 900, .18f, 200, .02f), tone(523, 0, .4f, .09f, .15f, true)}},
    {"krillion", {tone(60, 40, 1.2f, .34f), tone(784, 0, .7f, .15f, 0, true, 3000), tone(988, 0, .7f, .15f, .13f, true, 3000), tone(1175, 0, .7f, .15f, .26f, true, 3000), tone(1568, 0, .7f, .15f, .39f, true, 3000), noise(1.4f, 2500, .05f, 0, .3f, 1200)}},
    {"miss", {tone(220, 52, .7f, .24f), noise(.5f, 400, .14f, 0, .05f)}},
    {"tick", {tone(880, 0, .07f, .16f), tone(880, 0, .05f, .06f, .12f)}},
    {"surface", {noise(1.6f, 300, .16f, 3000), tone(80, 600, 1.5f, .08f)}},
  };
  return table;
}

void Sound::Biquad::set(bool high, float freq, float rate) {
  const float q = .7071f, w = 2 * float(M_PI) * std::min(freq, rate * .45f) / rate, alpha = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + alpha;
  if (high) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; } else { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; }
  a1 = -2 * c / a0; a2 = (1 - alpha) / a0;
}
float Sound::Biquad::run(float x) { float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }

static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int) {
  auto* s = static_cast<Sound*>(user);
  std::vector<float> buf(additional / sizeof(float));
  s->mix(buf.data(), (int)buf.size());
  SDL_PutAudioStreamData(stream, buf.data(), additional);
}
bool Sound::init() {
  if (ready_) return true;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
  SDL_AudioSpec spec{SDL_AUDIO_F32, 1, rate_};
  SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
  if (!stream) return false;
  SDL_ResumeAudioStreamDevice(stream); ready_ = true; return true;
}
void Sound::play(const Cue& cue) {
  Voice v; v.cue = cue; if (v.cue.attack < 0) v.cue.attack = cue.noise ? .003f : .005f;
  v.t = -cue.delay; v.seed = (uint32_t)std::rand() | 1;
  v.hasLp = cue.lowpass > 0 || cue.noise; v.lp.set(false, cue.lowpass > 0 ? cue.lowpass : 1200, (float)rate_);
  if (cue.highpass > 0) v.hp.set(true, cue.highpass, (float)rate_);
  std::lock_guard<std::mutex> lock(mutex_); voices_.push_back(v);
}
void Sound::event(const std::string& name) {
  if (!ready_ || muted_) return;
  static const std::map<std::string, std::string> aliases = {{"plankton", "small"}, {"tooclever", "small"}, {"schooler", "big"}, {"rare", "big"}, {"deepcut", "big"}, {"descend", "begin"}};
  if (name == "blub") { float var = .85f + (std::rand() % 1000) / 1000.f * .45f; play(tone(260 * var, 720 * var, .13f, .22f, 0, false, 2200)); play(noise(.05f, 1800, .05f, 0, 0, 700)); return; }
  auto key = aliases.count(name) ? aliases.at(name) : name;
  auto it = cues().find(key); if (it == cues().end()) return;
  for (auto& c : it->second) play(c);
}
void Sound::mix(float* out, int frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  const double dt = 1.0 / rate_;
  float target = muted_ ? 0.f : .75f;
  for (int i = 0; i < frames; i++) out[i] = 0;
  for (auto& v : voices_) {
    const Cue& c = v.cue; const double end = c.duration + c.attack;
    for (int i = 0; i < frames; i++, v.t += dt) {
      if (v.t < 0 || v.t > end + .1) continue;
      double env = v.t < c.attack ? c.volume * v.t / c.attack : c.volume * std::pow(.0001 / c.volume, (v.t - c.attack) / c.duration);
      float sample;
      if (c.noise) { v.seed ^= v.seed << 13; v.seed ^= v.seed >> 17; v.seed ^= v.seed << 5; sample = (v.seed / 4294967295.f) * 2 - 1; }
      else {
        double f = c.to > 0 ? c.from * std::pow(c.to / c.from, std::min(1.0, v.t / c.duration)) : c.from;
        v.phase += f * dt; v.phase -= std::floor(v.phase);
        sample = c.triangle ? float(4 * std::fabs(v.phase - .5) - 1) : (float)std::sin(2 * M_PI * v.phase);
      }
      if (v.hasLp) {
        if (c.sweep > 0 && (i & 31) == 0) { float base = c.lowpass > 0 ? c.lowpass : 1200; v.lp.set(false, base * std::pow(c.sweep / base, (float)std::min(1.0, v.t / c.duration)), (float)rate_); }
        sample = v.lp.run(sample);
      }
      if (c.highpass > 0) sample = v.hp.run(sample);
      out[i] += float(sample * env);
    }
  }
  for (int i = 0; i < frames; i++) { gain_ += (target - gain_) * .0008f; out[i] = std::clamp(out[i] * gain_, -1.f, 1.f); }
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.t > v.cue.duration + v.cue.attack + .1; }), voices_.end());
}
