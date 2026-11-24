// Port of public/ocean.mjs + seascape.mjs. Draws the pixel ocean at half resolution
// (one canvas pixel = two UI pixels, like the web canvas) and reports where the
// UI pieces that ride on the ocean (hero, prompt card, landing, buddy) should sit.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace canvas_ity { class canvas; }
struct Image { int w = 0, h = 0; std::vector<unsigned char> rgba; };

struct OceanFx { std::string tier; float progress = 0, startDepth = 0, resultDepth = 0; bool on = false; };
struct OceanLayout { bool mobile; float rail, bottom, height, left, width, center; };

class Ocean {
public:
  Ocean(); ~Ocean();
  bool init(const std::string& assets);
  void resize(int w, int h);                      // UI pixels
  void tick(double nowMs, double dt);
  const std::vector<unsigned char>& pixels() const { return pixels_; }
  int pw() const { return pw_; } int ph() const { return ph_; }
  OceanLayout layout() const;
  float worldY(float depth) const;
  void moveCamera(float to, double duration, int ease = 0); // 0 easeInOut, 1 easeOut
  void setPhase(const std::string& phase);
  void setDepth(float v);
  void round(float depth, float cardHeight);
  void drop(float oldDepth, const std::string& tier, int score, float progress);
  void typeAt(float x, float y, int n = 2);
  void burst(const std::string& color, int n = 30);

  std::string phase = "intro";
  float depth = 0, target = 0, cameraDepth = -45, cameraTarget = -45, active = 0, targetActive = 0, roundBase = 0, roundAnchor = 210;
  double time = 0, goldLandAt = 0, now = 0;
  int roundIndex = 0;
  bool lowPower = false, reduced = false;
  OceanFx fx;
  float mx = .72f, my = .66f;
  // outputs for the UI
  float heroTop = 0, heroOpacity = 1, cardTop = 0, landingTop = 0, mascotX = 0, mascotY = 0, mascotSize = 64, introTop = 0;
private:
  struct Particle { float x, y, vx, vy, life; uint32_t color; bool bubble; float r; };
  struct Job { float from, to; double start, duration; int ease; bool on = false; } job_;
  void draw();
  void cloud(float x, float y, float s);
  void boat(float x, float y, float s);
  void fish(float x, float y, float s, int dir, bool glow);
  void bubble(float x, float y, float r, float alpha);
  void seaLife(const OceanLayout& l);
  void drawEffect(const OceanLayout& l);
  void sprite(const std::string& name, float x, float y, float w, float h);
  // fast half-resolution blits straight into the pixel buffer
  void fillRectPx(float x, float y, float w, float h, uint32_t rgb, float alpha);
  void blitLayer(const Image& img, float srcY, float srcH, float dstY, float dstH, float alpha);
  std::unique_ptr<canvas_ity::canvas> c_;
  std::vector<unsigned char> pixels_, font_;
  std::vector<Particle> particles_;
  std::map<std::string, Image> sprites_;
  Image layers_[3], trenchMirror_;
  int w_ = 0, h_ = 0, pw_ = 0, ph_ = 0;
  double dt_ = .016;
};
