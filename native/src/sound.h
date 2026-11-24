// Port of public/sound.mjs: the same cue table, synthesized per sample into an SDL audio stream.
#pragma once
#include <mutex>
#include <string>
#include <vector>

struct Cue { bool noise = false; float from = 0, to = 0, duration = .2f, volume = .15f, delay = 0, attack = -1, lowpass = 0, sweep = 0, highpass = 0; bool triangle = false; };

class Sound {
public:
  bool init();
  void mute(bool value) { muted_ = value; }
  bool muted() const { return muted_; }
  void event(const std::string& name);
  void mix(float* out, int frames); // audio thread
private:
  struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0; void set(bool high, float freq, float rate); float run(float x); };
  struct Voice { Cue cue; double t = 0; double phase = 0; Biquad lp, hp; bool hasLp = false; uint32_t seed = 1; };
  void play(const Cue& cue);
  std::mutex mutex_;
  std::vector<Voice> voices_;
  float gain_ = .75f;
  bool muted_ = false, ready_ = false;
  int rate_ = 48000;
};
