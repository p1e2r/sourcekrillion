#define CANVAS_ITY_IMPLEMENTATION
#include "canvas_ity.hpp"
#include "ocean.h"
#include "content.h"
#include <array>
#include <cstring>
#include <lunasvg.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

using canvas_ity::fill_style; using canvas_ity::stroke_style;
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float clampf(float v, float a, float b) { return std::min(b, std::max(a, v)); }
static float easeInOut(float t) { return t < .5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }
static float easeOut(float t) { return 1 - std::pow(1 - t, 3.f); }
static float seed(float n) { double v = std::sin(n * 127.1 + 311.7) * 43758.5453123; return float(v - std::floor(v)); }
static float rnd() { static std::mt19937 g{7}; return std::uniform_real_distribution<float>(0, 1)(g); }
struct RGBA { float r, g, b, a; };
static RGBA hex(const char* s) { auto h = [&](int i) { return std::stoi(std::string(s + i, 2), nullptr, 16) / 255.f; }; size_t n = std::strlen(s); return {h(1), h(3), h(5), n >= 9 ? h(7) : 1.f}; }
static uint32_t rgb(int r, int g, int b) { return (uint32_t)(r << 16 | g << 8 | b); }
static uint32_t rgbHex(const char* s) { auto c = hex(s); return rgb(int(c.r * 255 + .5f), int(c.g * 255 + .5f), int(c.b * 255 + .5f)); }

struct Stop { float d; int c[3]; };
static const Stop STOPS[] = {{0, {29, 85, 128}}, {150, {18, 58, 94}}, {350, {11, 21, 38}}, {800, {8, 16, 32}}, {1400, {5, 10, 20}}, {2600, {3, 7, 15}}, {5200, {2, 5, 11}}};
static uint32_t colorAt(float depth) {
  int n = sizeof STOPS / sizeof *STOPS, i = 0; while (i < n && STOPS[i].d <= depth) i++;
  if (i == n) return rgb(STOPS[n - 1].c[0], STOPS[n - 1].c[1], STOPS[n - 1].c[2]);
  if (i == 0) return rgb(STOPS[0].c[0], STOPS[0].c[1], STOPS[0].c[2]);
  auto& a = STOPS[i - 1]; auto& b = STOPS[i]; float t = (depth - a.d) / (b.d - a.d);
  return rgb((int)std::lround(mixf(a.c[0], b.c[0], t)), (int)std::lround(mixf(a.c[1], b.c[1], t)), (int)std::lround(mixf(a.c[2], b.c[2], t)));
}
struct Landmark { float d; const char* text; char side; };
static const Landmark LANDMARKS[] = {{60, "light begins to fade", 'L'}, {200, "most sunlight stops here", 'R'}, {332, "deepest scuba dive ever — 332m", 'R'}, {385, "sperm whales hunt below here", 'L'}, {535, "emperor penguins dive this deep", 'R'}, {660, "pressure: 67x the surface", 'L'}, {800, "siphonophore drift zone", 'R'}, {1040, "THE MIDNIGHT ZONE", 'L'}, {1240, "anglerfish territory", 'R'}, {1550, "sperm whales duel giant squid here", 'L'}, {1850, "giant squid territory", 'R'}, {2150, "colossal squid eyes: football-sized", 'L'}, {2500, "the water is 2°C and utterly black", 'R'}, {3000, "THE ABYSSAL ZONE", 'L'}, {3800, "the Titanic rests here — 3,800m", 'R'}, {4400, "cusk eels patrol the plain", 'L'}, {4900, "the abyssal plain stretches out", 'R'}, {5500, "deeper than most submarines survive", 'L'}, {6000, "THE HADAL ZONE", 'R'}, {6500, "named for Hades. fitting.", 'L'}, {7000, "the trench floor — a perfect dive ends here", 'R'}};

// ---------------- seascape (cached layers, port of seascape.mjs) ----------------
namespace {
float sn(float i) { double v = std::sin(i * 91.37 + 18.9) * 43758.54; return float(v - std::floor(v)); }
float step2(float v) { return std::round(v / 2) * 2; }
void color(canvas_ity::canvas& c, const char* h, canvas_ity::brush_type t = fill_style) { auto k = hex(h); c.set_color(t, k.r, k.g, k.b, k.a); }
void poly(canvas_ity::canvas& c, const std::vector<std::pair<float, float>>& pts, const char* col) { color(c, col); c.begin_path(); for (size_t i = 0; i < pts.size(); i++) i ? c.line_to(step2(pts[i].first), step2(pts[i].second)) : c.move_to(step2(pts[i].first), step2(pts[i].second)); c.close_path(); c.fill(); }
void plant(canvas_ity::canvas& c, float x, float base, float height, float phase, const char* col) {
  int count = (int)std::floor(height / 12);
  for (int i = 0; i < count; i++) {
    float y = base - i * 12, bend = std::round(std::sin(i * .36f + phase) * 3) * 2, xx = x + bend;
    color(c, col); c.fill_rectangle(xx - 2, y - 12, 4, 16);
    if (i < 2) continue;
    float leaf = (1 - (float)i / count) * 11 + 9;
    for (int dir : {-1, 1}) poly(c, {{xx, y}, {xx + dir * leaf * .3f, y - 5}, {xx + dir * leaf * .7f, y - 6}, {xx + dir * (leaf + 2), y - 17}, {xx + dir * leaf, y - 20}, {xx + dir * leaf * .45f, y - 12}, {xx, y - 5}}, col);
  }
}
void tendril(canvas_ity::canvas& c, float x, float y, float height, float phase, const char* col) { color(c, col); for (int j = 0; j < height; j += 4) { float xx = x + std::sin(j * .038f + phase) * 8 + std::sin(j * .1f) * 2; c.fill_rectangle(step2(xx), y - j, 3, 6); if (j % 28 == 0) c.fill_rectangle(step2(xx) + 2, y - j - 4, 3, 7); } }
void grain(canvas_ity::canvas& c, int width, int height, float sd, float amount) {
  for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
    float p = sn(x * 13 + y * 57 + sd);
    if (p < .06f) { c.set_color(fill_style, 88 / 255.f, 133 / 255.f, 164 / 255.f, (.012f + p * .08f) * amount); c.fill_rectangle(x, y, 1, 1); }
    else if (p > .96f) { c.set_color(fill_style, 0, 5 / 255.f, 13 / 255.f, .025f * amount); c.fill_rectangle(x, y, 1, 1); }
  }
}
void gradient(canvas_ity::canvas& c, std::vector<std::pair<float, const char*>> stops) { c.set_linear_gradient(fill_style, 0, 0, 0, 688); for (auto& [o, h] : stops) { auto k = hex(h); c.add_color_stop(fill_style, o, k.r, k.g, k.b, k.a); } c.fill_rectangle(0, 0, 384, 688); }
void top(canvas_ity::canvas& c) {
  gradient(c, {{0, "#081728"}, {.12f, "#2379a9"}, {.54f, "#194f70"}, {1, "#071322"}});
  for (auto [x, dx] : std::vector<std::pair<float, float>>{{145, -125}, {170, -88}, {250, 95}}) poly(c, {{x, 40}, {x + 8, 40}, {x + dx + 24, 430}, {x + dx, 430}}, "#8dcade17");
  grain(c, 384, 688, 1, 1);
  for (auto& p : std::vector<std::array<float, 4>>{{45, 608, 400, 1}, {365, 610, 430, 2}, {70, 595, 260, 4}, {327, 625, 250, 8}}) plant(c, p[0], p[1], p[2], p[3], "#0b2c43");
  poly(c, {{0, 365}, {38, 397}, {52, 474}, {92, 482}, {80, 541}, {109, 548}, {90, 601}, {116, 610}, {121, 688}, {0, 688}}, "#081a2c");
  poly(c, {{384, 340}, {363, 330}, {348, 370}, {322, 375}, {328, 403}, {304, 412}, {316, 435}, {353, 446}, {338, 490}, {370, 521}, {334, 560}, {327, 688}, {384, 688}}, "#081a2c");
  for (auto& p : std::vector<std::array<float, 4>>{{21, 605, 452, 1}, {8, 625, 280, 5}, {58, 624, 242, 7}, {287, 620, 260, 9}, {355, 602, 190, 3}}) plant(c, p[0], p[1], p[2], p[3], "#041323");
  for (int j = 0; j < 6; j++) tendril(c, 70 + j * 7, 612, 38 + sn(j) * 80, j, "#041323");
  poly(c, {{0, 620}, {51, 616}, {85, 630}, {143, 637}, {174, 642}, {231, 641}, {273, 635}, {317, 623}, {384, 616}, {384, 688}, {0, 688}}, "#050f1c");
  for (int i = 0; i < 45; i++) { float x = 22 + sn(i + 11) * 340, y = 220 + sn(i + 28) * 380; color(c, "#74bad638"); c.fill_rectangle(step2(x), step2(y), 2, 2); if (i % 5 == 0) { color(c, "#81bed74d", stroke_style); c.set_line_width(1); c.stroke_rectangle(step2(x), step2(y), 4, 4); } }
}
void mid(canvas_ity::canvas& c) {
  gradient(c, {{0, "#020b12"}, {.35f, "#071523"}, {.55f, "#0d2139"}, {.83f, "#081726"}, {1, "#030b14"}}); grain(c, 384, 688, 3, 1.4f);
  for (int y = 213; y < 584; y += 3) for (int x = 0; x < 384; x += 3) { float a = std::max(0.f, 1 - std::abs(y - 245) / 80.f) + std::max(0.f, 1 - std::abs(y - 510) / 65.f); if (sn(x * 19 + y) > .42f) { c.set_color(fill_style, 1 / 255.f, 8 / 255.f, 17 / 255.f, a * .4f); c.fill_rectangle(x, y, 1, 2); } }
  poly(c, {{0, 410}, {25, 447}, {28, 497}, {47, 552}, {56, 640}, {77, 688}, {0, 688}}, "#020a13");
  poly(c, {{36, 610}, {48, 418}, {61, 331}, {71, 342}, {79, 422}, {89, 480}, {101, 688}}, "#061322");
  poly(c, {{276, 688}, {278, 431}, {290, 358}, {308, 352}, {317, 452}, {326, 502}, {329, 688}}, "#061322");
  poly(c, {{384, 359}, {367, 382}, {359, 430}, {350, 474}, {348, 529}, {339, 577}, {334, 688}, {384, 688}}, "#020a13");
  for (auto& p : std::vector<std::array<float, 4>>{{12, 455, 242, 1}, {32, 553, 200, 3}, {346, 428, 255, 4}, {362, 491, 210, 8}, {100, 684, 88, 9}, {284, 688, 122, 10}}) tendril(c, p[0], p[1], p[2], p[3], "#03101b");
}
void trench(canvas_ity::canvas& c) {
  gradient(c, {{0, "#102334"}, {.36f, "#081522"}, {1, "#02060e"}}); grain(c, 384, 688, 8, 1.7f);
  for (int side = 0; side < 2; side++) for (int row = -1; row < 13; row++) for (int col = 0; col < 3; col++) {
    float k = row * 7 + col + side * 97, x = col * 27 - 22 + sn(k + 8) * 12, y = row * 58 + sn(k + 91) * 18, w = 29 + sn(k + 14) * 25, h = 39 + sn(k + 65) * 35;
    auto T = [&](std::vector<std::pair<float, float>> a) { for (auto& p : a) if (side) p.first = 384 - p.first; return a; };
    std::vector<std::pair<float, float>> pts = {{x, y + 8}, {x + w * .45f, y}, {x + w, y + 8}, {x + w + 4, y + h * .64f}, {x + w * .68f, y + h}, {x + 4, y + h - 2}, {x - 5, y + h * .52f}};
    poly(c, T(pts), "#01060d"); auto sh = pts; for (auto& p : sh) { p.first += 2; p.second += 2; } poly(c, T(sh), "#0b1927");
    poly(c, T({{x + 4, y + 11}, {x + w * .45f, y + 3}, {x + w - 3, y + 11}, {x + w * .63f, y + 17}, {x + w * .55f, y + h * .5f}, {x + 7, y + h - 8}, {x + 8, y + h * .53f}, {x - 1, y + h * .42f}}), "#172b3c");
    poly(c, T({{x + 8, y + h * .54f}, {x + w * .44f, y + h * .44f}, {x + w * .55f, y + h * .64f}, {x + w - 3, y + h * .76f}, {x + w * .65f, y + h - 2}, {x + 9, y + h - 4}}), "#0b1c2b");
    if (sn(k + 12) > .65f) for (int j = 0; j < 3; j++) { color(c, sn(k + 14) > .5f ? "#47ced9aa" : "#ed4a8ba0"); float xx = x + w * .42f + sn(j + k) * 8; c.fill_rectangle(step2(side ? 384 - xx : xx), step2(y + h * .3f + j * 4), 2, 2); }
  }
  for (int y = 50; y < 635; y += 4) for (int x = 80; x < 300; x += 4) if (sn(x + y * 21) > .7f && std::sin(y * .035f) > .45f) { color(c, "#1d3d521d"); c.fill_rectangle(x, y, 1, 1); }
}
Image paint(void (*fn)(canvas_ity::canvas&)) { canvas_ity::canvas c(384, 688); fn(c); Image img; img.w = 384; img.h = 688; img.rgba.resize(384 * 688 * 4); c.get_image_data(img.rgba.data(), 384, 688, 384 * 4, 0, 0); return img; }
}

Ocean::Ocean() = default;
Ocean::~Ocean() = default;
bool Ocean::init(const std::string& assets) {
  for (const char* name : {"krill", "krill-gold", "schooler", "rare", "deepcut", "tooclever"}) {
    auto doc = lunasvg::Document::loadFromFile(assets + "/" + name + ".svg"); if (!doc) continue;
    auto bmp = doc->renderToBitmap(64, 64); bmp.convertToRGBA();
    Image img; img.w = 64; img.h = 64; img.rgba.assign(bmp.data(), bmp.data() + 64 * 64 * 4); sprites_[name] = img;
  }
  std::ifstream f(assets + "/../fonts/VT323-Regular.ttf", std::ios::binary); font_.assign(std::istreambuf_iterator<char>(f), {});
  layers_[0] = paint(top); layers_[1] = paint(mid); layers_[2] = paint(trench);
  trenchMirror_ = layers_[2]; for (int y = 0; y < 688; y++) std::copy_n(&layers_[2].rgba[(687 - y) * 384 * 4], 384 * 4, &trenchMirror_.rgba[y * 384 * 4]);
  return !font_.empty();
}
OceanLayout Ocean::layout() const { bool mobile = w_ < 700; float rail = w_ >= 1200 ? 180.f : 0.f, bottom = mobile ? 50.f : 90.f; return {mobile, rail, bottom, h_ - bottom, rail, w_ - rail * 2, w_ / 2.f - 24}; }
float Ocean::worldY(float d) const { return layout().height / 2 + (d - cameraDepth) * 2; }
void Ocean::resize(int w, int h) {
  if (w == w_ && h == h_) return;
  bool had = w_ > 0; w_ = w; h_ = h; pw_ = (w + 1) / 2; ph_ = (h + 1) / 2;
  c_ = std::make_unique<canvas_ity::canvas>(pw_, ph_); pixels_.assign((size_t)pw_ * ph_ * 4, 0);
  if (had && (phase == "preview" || phase == "answering")) { auto l = layout(); cameraDepth = cameraTarget = roundBase + (l.height / 2 - roundAnchor) / 2; job_.on = false; }
}
void Ocean::moveCamera(float to, double duration, int ease) { cameraTarget = to; job_ = {cameraDepth, to, now, reduced ? 1.0 : duration, ease, true}; }
void Ocean::setPhase(const std::string& p) { phase = p; targetActive = (p == "intro" || p == "done") ? 0.f : 1.f; if (p == "intro") moveCamera(-45, 700); }
void Ocean::setDepth(float v) { target = v; if (phase != "plunge" && phase != "surface") { depth = v; if (phase == "landed") moveCamera(v + 30, 500); } }
void Ocean::round(float d, float cardHeight) { target = d; depth = d; fx.on = false; roundBase = d; auto l = layout(); float dockTop = l.height - 80; roundAnchor = std::max(8.f, std::min(l.mobile ? 175.f : 210.f, (dockTop - cardHeight) / 2)); moveCamera(d + (l.height / 2 - roundAnchor) / 2, 2200); }
void Ocean::drop(float oldDepth, const std::string& tier, int score, float progress) {
  job_.on = false; if (!fx.on || fx.startDepth != oldDepth) dropFrom_ = cameraDepth; fx = {tier, progress, oldDepth, oldDepth + score * 10.f, true};
  float eased = tier == "plankton" ? (1 - std::cos(float(M_PI) * progress)) / 2 : easeInOut(progress);
  cameraDepth = mixf(dropFrom_, oldDepth + score * 10.f + 30, eased); cameraTarget = oldDepth + score * 10.f + 30;
  depth = tier == "krillion" ? oldDepth : mixf(oldDepth, oldDepth + score * 10.f, eased); target = oldDepth + score * 10.f;
}
void Ocean::typeAt(float x, float y, int n) { if (reduced) return; for (int i = 0; i < n; i++) particles_.push_back({x + (rnd() - .5f) * 8, y + rnd() * 10, (rnd() - .5f) * 8, -35 - rnd() * 15, 1, 0xb2dce8, true, 1 + rnd() * 2}); }
void Ocean::burst(const std::string& col, int n) { if (reduced) return; auto l = layout(); uint32_t c = rgbHex(col.c_str()); for (int i = 0; i < (lowPower ? n / 3 : n); i++) particles_.push_back({l.center, l.height / 2 - 60, (rnd() - .5f) * 220, (rnd() - .5f) * 240 - 40, 1, c, false, 0}); }
void Ocean::tick(double nowMs, double dt) {
  now = nowMs; dt_ = dt; time += reduced ? 0 : dt;
  if (job_.on) { float t = (float)clampf(float((now - job_.start) / job_.duration), 0, 1); cameraDepth = mixf(job_.from, job_.to, job_.ease ? easeOut(t) : easeInOut(t)); if (t >= 1) job_.on = false; }
  active = mixf(active, targetActive, 1 - std::exp(-3.6f * (float)dt));
  for (auto& p : particles_) { p.x += p.vx * (float)dt; p.y += p.vy * (float)dt; if (!p.bubble) p.vy += 90 * (float)dt; p.life -= (float)dt * .65f; }
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](auto& p) { return p.life <= 0; }), particles_.end());
  if (w_ > 0) draw();
}

// ---- fast blits (UI pixel coordinates in, half-res pixels out), browser-style sRGB blending ----
void Ocean::fillRectPx(float x, float y, float w, float h, uint32_t col, float alpha) {
  int x0 = std::max(0, (int)std::lround(x / 2)), x1 = std::min(pw_, (int)std::lround((x + w) / 2)), y0 = std::max(0, (int)std::lround(y / 2)), y1 = std::min(ph_, (int)std::lround((y + h) / 2));
  if (x1 <= x0 && w > 0 && x / 2 < pw_ && x >= 0) x1 = std::min(pw_, x0 + 1);
  if (y1 <= y0 && h > 0 && y / 2 < ph_ && y >= 0) y1 = std::min(ph_, y0 + 1);
  unsigned r = col >> 16 & 255, g = col >> 8 & 255, b = col & 255; int a = (int)std::lround(clampf(alpha, 0, 1) * 256);
  for (int yy = y0; yy < y1; yy++) { unsigned char* p = &pixels_[((size_t)yy * pw_ + x0) * 4]; for (int xx = x0; xx < x1; xx++, p += 4) { p[0] = (unsigned char)((p[0] * (256 - a) + r * a) >> 8); p[1] = (unsigned char)((p[1] * (256 - a) + g * a) >> 8); p[2] = (unsigned char)((p[2] * (256 - a) + b * a) >> 8); p[3] = 255; } }
}
void Ocean::blitLayer(const Image& img, float srcY, float srcH, float dstY, float dstH, float alpha) {
  int y0 = std::max(0, (int)std::lround(dstY / 2)), y1 = std::min(ph_, (int)std::lround((dstY + dstH) / 2)); int a = (int)std::lround(clampf(alpha, 0, 1) * 256); if (a <= 0) return;
  for (int yy = y0; yy < y1; yy++) {
    int sy = std::clamp((int)(srcY + (yy * 2 - dstY) / dstH * srcH), 0, img.h - 1);
    const unsigned char* row = &img.rgba[(size_t)sy * img.w * 4]; unsigned char* p = &pixels_[(size_t)yy * pw_ * 4];
    for (int xx = 0; xx < pw_; xx++, p += 4) { int sx = std::min(img.w - 1, xx * img.w / pw_); const unsigned char* s = row + sx * 4; int sa = s[3] * a >> 8; p[0] = (unsigned char)((p[0] * (255 - sa) + s[0] * sa) / 255); p[1] = (unsigned char)((p[1] * (255 - sa) + s[1] * sa) / 255); p[2] = (unsigned char)((p[2] * (255 - sa) + s[2] * sa) / 255); }
  }
}
void Ocean::cloud(float x, float y, float s) {
  static const int blocks[][4] = {{0, 4, 5, 2}, {2, 3, 7, 3}, {4, 1, 5, 5}, {6, 0, 4, 5}, {9, 2, 4, 4}, {12, 3, 5, 2}, {15, 4, 5, 1}, {1, 5, 6, 1}, {8, 5, 5, 1}}, shade[][4] = {{1, 5, 4, 1}, {5, 4, 2, 1}, {8, 5, 4, 1}, {13, 4, 3, 1}};
  for (auto& b : blocks) fillRectPx(std::round(x + b[0] * s), std::round(y + b[1] * s), b[2] * s, b[3] * s, 0xeaf6fa, 1);
  for (auto& b : shade) fillRectPx(x + b[0] * s, y + b[1] * s, b[2] * s, b[3] * s, 0xc8e0e8, 1);
}
void Ocean::boat(float x, float y, float s) {
  x = std::round(x); y = std::round(y);
  static const int hull[][4] = {{0, -5, 2, 3}, {1, -6, 3, 5}, {3, -4, 23, 3}, {4, -1, 20, 1}, {5, 0, 18, 1}, {23, -5, 4, 2}, {26, -7, 4, 1}, {25, -6, 3, 2}, {10, -10, 10, 1}, {11, -9, 2, 5}, {17, -9, 2, 5}, {14, -20, 1, 11}}, flag[][4] = {{9, -17, 5, 3}, {10, -19, 4, 2}, {8, -16, 6, 2}, {7, -15, 3, 1}};
  for (auto& b : hull) fillRectPx(x + b[0] * s, y + b[1] * s, b[2] * s, b[3] * s, 0x061e2c, 1);
  for (auto& b : flag) fillRectPx(x + b[0] * s, y + b[1] * s, b[2] * s, b[3] * s, 0xff5682, 1);
  fillRectPx(x + 8 * s, y - 15 * s, 6 * s, s, 0xe6386a, 1); fillRectPx(x + 13 * s, y - 9 * s, s, 3 * s, 0x9cc9d8, 1); fillRectPx(x + 16 * s, y - 9 * s, s, 3 * s, 0x9cc9d8, 1);
}
void Ocean::fish(float x, float y, float s, int dir, bool glow) {
  auto& c = *c_; c.save(); c.translate(x, y); c.scale(dir * s, s); color(c, glow ? "#0b515e" : "#031522");
  c.save(); c.scale(8, 3.5f); c.begin_path(); c.arc(0, 0, 1, 0, 2 * float(M_PI)); c.restore(); c.fill();
  c.begin_path(); c.move_to(-7, 0); c.line_to(-14, -4); c.line_to(-14, 4); c.close_path(); c.fill();
  if (glow) { color(c, "#52cdd6"); c.fill_rectangle(4, -1, 1, 1); } c.restore();
}
void Ocean::bubble(float x, float y, float r, float alpha) {
  auto& c = *c_; c.set_color(stroke_style, 173 / 255.f, 218 / 255.f, 238 / 255.f, alpha); c.set_line_width(1); c.begin_path(); c.arc(x, y, r, 0, 2 * float(M_PI)); c.stroke();
  if (r > 4) { c.set_color(stroke_style, 224 / 255.f, 247 / 255.f, 1, alpha * .7f); c.begin_path(); c.arc(x - 1, y - 1, r - 2, float(M_PI), float(M_PI) * 1.5f); c.stroke(); }
}
void Ocean::sprite(const std::string& name, float x, float y, float w, float h) { auto it = sprites_.find(name); if (it != sprites_.end()) c_->draw_image(it->second.rgba.data(), 64, 64, 64 * 4, x, y, w, h); }

void Ocean::draw() {
  auto& c = *c_; const float w = (float)w_, h = (float)h_, t = (float)time; auto l = layout();
  const float sea = l.height / 2 - cameraDepth * 2;
  auto wy = [&](float d) { return l.height / 2 + (d - cameraDepth) * 2; };
  // ---- stage 1: fast fills ----
  fillRectPx(0, 0, w, h, 0x050a14, 1);
  if (sea > 0) {
    int y1 = std::min(ph_, (int)std::lround((sea + 8) / 2));
    for (int yy = 0; yy < y1; yy++) { float yc = yy * 2.f, f = clampf((yc - (sea - 620)) / 620, 0, 1); float r, g, b; if (f < .6f) { float k = f / .6f; r = mixf(0x81, 0xa8, k); g = mixf(0xcb, 0xdc, k); b = mixf(0xe8, 0xef, k); } else { float k = (f - .6f) / .4f; r = mixf(0xa8, 0xf1, k); g = mixf(0xdc, 0xf9, k); b = mixf(0xef, 0xff, k); } fillRectPx(0, yc, w, 2, rgb((int)r, (int)g, (int)b), 1); }
    if (l.mobile) { float sx = w * .79f, sy = sea - 151; fillRectPx(sx, sy, 38, 38, 0xffdf85, 1); fillRectPx(sx - 4, sy + 8, 46, 22, 0xffeaa7, 1); cloud(40, sea - 147, 6.5f); cloud(210, sea - 140, 3.5f); cloud(142, sea - 105, 4); cloud(-25, sea - 100, 3); cloud(335, sea - 110, 5); }
    else { float sy = sea - 600, sx = w * .84f, sunY = sy + 80; fillRectPx(sx, sunY, 128, 128, 0xffdf85, 1); fillRectPx(sx - 8, sunY + 12, 144, 104, 0xffeaa7, 1); cloud(w * .1f, sy + 20, 24); cloud(w * .54f, sy + 40, 13); cloud(w * .36f, sy + 205, 17); cloud(-120, sy + 215, 12); cloud(w * .85f, sy + 230, 16); }
    boat(w * .25f, sea + 3, w / 288);
  }
  for (float y = std::max(0.f, std::floor(sea) - 3); y < h; y += 6) fillRectPx(0, y, w, 7, colorAt(std::max(0.f, cameraDepth + (y - l.height / 2) / 2)), 1);
  if (sea > -10 && sea < h + 10) for (int xx = 0; xx < pw_; xx++) { float x = xx * 2.f, top = sea + std::sin(x * .011f + t * .7f) * 3 + std::sin(x * .024f - t * .3f) * 2; fillRectPx(x, top, 2, sea + 14 - top, 0x1d5580, 1); }
  struct Band { int index; float start, end, alpha; };
  for (Band b : {Band{0, 40, 470, .45f}, Band{1, 480, 2620, .5f}, Band{2, 2950, 7060, .55f}}) {
    float topY = wy(b.start), bottom = wy(b.end); if (topY > h || bottom < 0) continue; float scale = w / 384, tileH = 688 * scale;
    for (float y = std::max(0.f, topY); y < std::min(h, bottom);) {
      float sourceY = std::fmod(std::fmod(y - topY, tileH) + tileH, tileH), strip = std::min({8.f, h - y, bottom - y, tileH - sourceY});
      float fade = std::max(0.f, std::min({1.f, (y + strip / 2 - topY) / 150, (bottom - y - strip / 2) / 150}));
      bool mirrored = b.index == 2 && ((int)std::floor((y - topY) / tileH)) % 2 == 1;
      blitLayer(mirrored ? trenchMirror_ : layers_[b.index], sourceY / scale, strip / scale, y, strip + .5f, b.alpha * fade); y += strip;
    }
  }
  for (int i = 0; i < (lowPower ? 65 : 220); i++) { float x = seed(i + 91) * w, yy = std::fmod(std::fmod(seed(i + 331) * h + t * (1 + seed(i) * 4) + cameraDepth * .35f, h) + h, h); if (yy < sea) continue; float s = seed(i + 20) > .8f ? 2.f : 1.f; fillRectPx(x, yy, s, s, 0xbee0f8, .08f + seed(i + 2) * .26f); }
  // ---- stage 2: vector drawing ----
  c.put_image_data(pixels_.data(), pw_, ph_, pw_ * 4, 0, 0);
  c.set_transform(.5f, 0, 0, .5f, 0, 0);
  for (int i = 0; i < (l.mobile ? 14 : 26); i++) { float d = i < (l.mobile ? 5 : 10) ? 75 + seed(i + 7) * 45 : cameraDepth + (seed(i + 7) - .5f) * 400, yy = wy(d); if (yy < sea + 20 || yy < -20 || yy > h + 20) continue; int dir = i % 2 ? 1 : -1; float x = std::fmod(std::fmod(seed(i + 32) * w + t * (4 + seed(i + 40) * 13) * dir, w + 60) + w + 60, w + 60) - 30; fish(x, yy, .6f + seed(i + 9) * .8f, dir, cameraDepth > 600 && i % 7 == 0); }
  seaLife(l);
  if (cameraDepth > 3200) { float fy = wy(7000); color(c, "#09192b"); c.begin_path(); c.move_to(0, fy); for (float x = 0; x <= w + 20; x += 12) c.line_to(x, fy + std::sin(x * .014f) * 30 + std::sin(x * .045f) * 9); c.line_to(w, h); c.line_to(0, h); c.fill(); }
  float rulerX = w - l.rail - 14; c.set_line_width(1.5f); color(c, "#7d93b88c", stroke_style); c.set_font(font_.data(), (int)font_.size(), 13); c.text_align = canvas_ity::rightward;
  c.begin_path(); c.move_to(rulerX, std::max(0.f, sea)); c.line_to(rulerX, l.height); c.stroke();
  float minD = std::max(0.f, std::floor((cameraDepth - l.height / 4) / 10) * 10), maxD = cameraDepth + l.height / 4;
  for (float d = minD; d <= maxD; d += 10) { float yy = wy(d); if (yy < 0 || yy > l.height) continue; int di = (int)d; float size = di % 100 == 0 ? 16.f : di % 50 == 0 ? 10.f : 5.f; color(c, "#7d93b88c", stroke_style); c.begin_path(); c.move_to(rulerX - size, yy); c.line_to(rulerX, yy); c.stroke(); if (di % 100 == 0 && (!roundIndex || std::abs(yy - wy(target)) > 14)) { color(c, "#7d93b8e6"); std::string s = di == 0 ? "0m" : "-" + commas(di) + "m"; c.fill_text(s.c_str(), rulerX - size - 5, yy + 4); } }
  if (roundIndex) { float yy = wy(target); if (yy > 0 && yy < l.height) { color(c, "#ff5d8f"); c.begin_path(); c.move_to(rulerX + 4, yy); c.line_to(rulerX + 12, yy - 5); c.line_to(rulerX + 12, yy + 5); c.fill(); color(c, "#ff5d8f", stroke_style); c.begin_path(); c.move_to(rulerX - 20, yy); c.line_to(rulerX + 4, yy); c.stroke(); c.fill_text("YOU", rulerX - 24, yy + 4); } }
  c.set_font(font_.data(), (int)font_.size(), l.mobile ? 13.f : 17.f); color(c, phase == "plunge" ? "#7d93b8d9" : "#7d93b81a");
  if (phase != "intro" && phase != "done") for (auto& m : LANDMARKS) { float yy = wy(m.d); if (yy < 90 || yy > l.height - 100) continue; c.text_align = m.side == 'L' ? canvas_ity::leftward : canvas_ity::rightward; std::string s = m.side == 'L' ? std::string("── ") + m.text : std::string(m.text) + " ──"; c.fill_text(s.c_str(), m.side == 'L' ? l.left + l.width * .05f : w - l.rail - 96, yy); }
  c.text_align = canvas_ity::leftward;
  if (fx.on && (phase == "plunge" || (phase == "landed" && fx.tier == "krillion"))) drawEffect(l);
  for (auto& p : particles_) { c.set_global_alpha(p.life); if (p.bubble) bubble(p.x, p.y, p.r ? p.r : 2, p.life * .65f); else { c.set_color(fill_style, (p.color >> 16 & 255) / 255.f, (p.color >> 8 & 255) / 255.f, (p.color & 255) / 255.f, 1); c.fill_rectangle(p.x, p.y, 3, 4); } } c.set_global_alpha(1);
  float fadeTop = phase == "intro" ? introTop - 14 : l.height - 85, fadeEnd = phase == "intro" ? fadeTop + 85 : l.height + 40;
  c.set_linear_gradient(fill_style, 0, fadeTop, 0, fadeEnd); c.add_color_stop(fill_style, 0, 5 / 255.f, 10 / 255.f, 20 / 255.f, 0); c.add_color_stop(fill_style, .35f, 5 / 255.f, 10 / 255.f, 20 / 255.f, 0xde / 255.f); c.add_color_stop(fill_style, 1, 5 / 255.f, 10 / 255.f, 20 / 255.f, 1); c.fill_rectangle(0, fadeTop, w, h - fadeTop);
  for (int i = 0; i < (lowPower ? 3 : 10); i++) { float x = seed(i + 741) * w + std::sin(t * .3f + i) * 10, yy = h - std::fmod(t * 16 + seed(i + 94) * h, h * .9f); if (yy > sea) bubble(x, yy, 2.f + i % 3, .38f); }
  c.get_image_data(pixels_.data(), pw_, ph_, pw_ * 4, 0, 0);
  // ---- outputs for the UI layer ----
  heroTop = sea - 336; heroOpacity = (phase == "intro" || phase == "done") ? 1.f : phase == "preview" ? clampf((sea - 175) / 360, 0, 1) : 0.f;
  cardTop = wy(roundBase); landingTop = wy(target) - 124;
  float tx, ty;
  if (phase == "intro") { tx = l.left + l.width * .72f; ty = sea + 105; } else if (phase == "done") { tx = l.left + l.width * .82f; ty = l.height * .16f; }
  else if (phase == "landed" && fx.on && fx.tier == "krillion") { tx = l.center; ty = wy(target) - 80; } else if (phase == "landed") { tx = l.left + l.width * .16f; ty = l.height * .28f; }
  else if (phase == "plunge") { tx = std::min(l.center + 95, w - l.rail - 44); ty = l.height / 2 - 74; } else { tx = l.left + 48; ty = l.height - 168; }
  float follow = 1 - std::exp(-2.6f * (float)dt_); mx = mixf(mx * w, tx, follow) / w; my = mixf(my * h, ty, follow) / h;
  mascotSize = phase == "landed" && fx.on && fx.tier == "krillion" ? (l.mobile ? 66.f : 88.f) : 64.f; mascotX = mx * w - mascotSize / 2; mascotY = my * h - mascotSize / 2;
}
void Ocean::seaLife(const OceanLayout& l) {
  auto& c = *c_; float t = (float)time, w = (float)w_, h = (float)h_;
  float y = worldY(800); if (y > -100 && y < h + 100) for (int i = 0; i < 22; i++) { c.set_color(fill_style, 77 / 255.f, 227 / 255.f, 1, .22f + .18f * std::sin(t * 3 + i)); c.begin_path(); c.arc(w * (.12f + i * .034f), y + std::sin(t * .7f + i * .55f) * 22, 2.2f, 0, 2 * float(M_PI)); c.fill(); }
  float eye = worldY(980); if (eye > -100 && eye < h + 100) for (int i = 0; i < 6; i++) { float x = std::fmod(t * 14 + i * 110, w + 200) - 100, yy = eye + std::sin(t + i * 2) * 20 + i * 8; fish(x, yy, .6f, -1, false); c.set_color(fill_style, 232 / 255.f, 241 / 255.f, 1, .25f + .25f * std::sin(t * 5 + i)); c.fill_rectangle(x - 1, yy - 1, 2, 2); }
  float wreck = worldY(3800); if (wreck > -160 && wreck < h + 160) { c.save(); c.translate(w * .5f, wreck); c.rotate(.06f); color(c, "#040912df"); c.fill_rectangle(-108, -8, 137, 22); c.begin_path(); c.move_to(-108, -8); c.line_to(-132, 14); c.line_to(-108, 14); c.fill(); for (int i = 0; i < 3; i++) c.fill_rectangle(-92 + i * 34, -22, 8, 14); c.fill_rectangle(58, -4, 60, 18); c.restore(); }
  float spark = worldY(4400); if (spark > -80 && spark < h + 80) for (int i = 0; i < 8; i++) { c.set_color(fill_style, 1, 93 / 255.f, 143 / 255.f, .15f + .2f * std::sin(t * 4 + i * 3)); c.fill_rectangle(w * (.05f + i * .13f) + std::sin(t * .8f + i * 2) * 14, spark + std::sin(t * 1.3f + i) * 18 + (i % 3) * 10, 2, 2); }
  (void)l;
}
void Ocean::drawEffect(const OceanLayout& l) {
  auto& c = *c_; float t = (float)time, p = fx.progress, x = l.center; bool landed = phase == "landed"; float y = l.height / 2 - 60; const std::string& tier = fx.tier;
  float scatter = landed ? clampf(float((now - (goldLandAt ? goldLandAt : now) - 1000) / 1500), 0, 1) : 0;
  if (tier == "deepcut") { c.set_radial_gradient(fill_style, x, y, 66, x, y, 330); c.add_color_stop(fill_style, 0, 2 / 255.f, 4 / 255.f, 10 / 255.f, 0); c.add_color_stop(fill_style, .55f, 2 / 255.f, 4 / 255.f, 10 / 255.f, .72f); c.add_color_stop(fill_style, 1, 2 / 255.f, 4 / 255.f, 10 / 255.f, .96f); c.fill_rectangle(0, 0, (float)w_, (float)h_); }
  if (tier == "rare") for (int i = 0; i < 9; i++) { float xx = x + std::sin(i * 4 + t) * 55, yy = y + std::cos(i * 3 + t) * 35, r = 30 + seed(i) * 50; c.set_radial_gradient(fill_style, xx, yy, 0, xx, yy, r); c.add_color_stop(fill_style, 0, 60 / 255.f, 4 / 255.f, 14 / 255.f, .816f); c.add_color_stop(fill_style, 1, 20 / 255.f, 0, 6 / 255.f, 0); c.fill_rectangle(xx - r, yy - r, r * 2, r * 2); }
  if (tier == "tooclever") for (int i = 0; i < 11; i++) { int dir = i % 2 ? 1 : -1; float pr = clampf(p * 2 - i * .07f, 0, 1), xx = mixf(dir > 0 ? -70.f : w_ + 70.f, dir > 0 ? w_ + 70.f : -70.f, pr), yy = l.height * .42f + (seed(i + 8) - .5f) * 260; c.save(); c.translate(xx, yy); c.scale((float)-dir, 1); c.rotate(std::sin(t + i) * .2f); sprite("tooclever", -27, -27, 54, 54); c.restore(); }
  if (!landed) { c.set_radial_gradient(fill_style, x, y - 32, 6, x, y - 32, 80); c.add_color_stop(fill_style, 0, 77 / 255.f, 227 / 255.f, 1, .14f); c.add_color_stop(fill_style, 1, 77 / 255.f, 227 / 255.f, 1, 0); c.fill_rectangle(x - 80, y - 112, 160, 160); }
  if (tier == "schooler") for (auto [dx, dy] : std::vector<std::pair<float, float>>{{-64, -26}, {-88, 10}, {64, -18}, {88, 18}, {-40, 34}, {44, 38}}) { c.save(); c.translate(x + dx, y + dy + std::sin(t * 4 + dx) * 6); c.scale(dx > 0 ? -1.f : 1.f, 1); sprite("schooler", -16, -16, 32, 32); c.restore(); }
  if (tier == "krillion") for (int i = 0; i < 18; i++) { float angle = t * (i % 2 ? 1 : -1) + i * 2.4f, r = (30 + seed(i) * 48) * (landed ? .62f : 1) * (1 + scatter * 7), xx = x + std::cos(angle) * r * 1.12f, yy = y + std::sin(angle) * r * .62f, size = 15 + seed(i + 3) * 13; c.save(); c.set_global_alpha(std::max(0.f, .92f - scatter * 1.15f)); c.translate(xx, yy); c.scale(-std::sin(angle) * (i % 2 ? 1 : -1) > 0 ? -1.f : 1.f, 1); sprite("krill", -size / 2, -size / 2, size, size); c.restore(); }
  c.set_global_alpha(1);
  for (int i = 0; i < (landed ? 24 : 18); i++) { float yy = y - std::fmod(seed(i + 62) * 180 + t * (landed ? 38 : 24), 180.f), xx = x + std::sin(i * 3 + t) * 30; bubble(xx, yy, 1.5f + seed(i) * 3, landed ? .35f : .5f); }
}
