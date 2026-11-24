#include "charts.h"
#include "content.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

std::string iconFor(const std::string& tier) { return tier == "krillion" ? "krill" : tier; }
std::string tierImage(const std::string& tier, const std::string& cls) {
  if (tier.empty() || !tierById(tier)) return "<span class=\"miss-icon\"></span>";
  return "<svg class=\"tier-img " + cls + "\" src=\"../sprites/" + iconFor(tier) + ".svg\"/>";
}
float dropMs(const std::string& tier) {
  if (tier == "plankton" || tier == "rare") return 3400; if (tier == "tooclever" || tier == "schooler") return 3000;
  if (tier == "deepcut") return 4300; if (tier == "krillion") return 4600; return 900;
}
std::string diveChart(const nlohmann::json& results) {
  static const std::vector<std::pair<std::string, int>> depth = {{"plankton", 8}, {"tooclever", 18}, {"schooler", 36}, {"rare", 60}, {"deepcut", 82}, {"krillion", 97}};
  std::string out = "<div class=\"dive-chart\">";
  for (int n : {0, 25, 50, 75, 100}) out += "<div class=\"chart-grid\" style=\"top:" + std::to_string(n * 240 / 100) + "px\"><span>" + (n == 0 ? std::string("0m") : "−" + std::to_string(n)) + "</span></div>";
  out += "<div class=\"dive-columns\">";
  for (size_t i = 0; i < results.size(); i++) {
    const auto& r = results[i]; std::string tier = r["tier"].is_null() ? "" : r["tier"].get<std::string>();
    int d = 3; for (auto& [k, v] : depth) if (k == tier) d = v;
    const Tier* t = tierById(tier); std::string col = t ? t->color : "#43556b";
    int px = d * 240 / 100;
    out += "<div class=\"dive-column\"><div class=\"dive-stem\" style=\"height:" + std::to_string(std::max(0, px - 8)) + "px;decorator:vertical-gradient(#00000000 " + col + ")\"></div><div class=\"dive-dot\" style=\"top:" + std::to_string(px - 9) + "px;filter:drop-shadow(" + col + " 0 0 4.75px)\">" + tierImage(tier) + "</div><span>" + std::to_string(i + 1) + "</span></div>";
  }
  return out + "</div></div>";
}
std::string distributionSvg(const std::vector<int>& dist, int score, int population) {
  std::vector<int> bins = dist.empty() ? std::vector<int>(71, 0) : dist;
  const float width = 560, top = 18, baseline = 140; std::vector<float> sm(bins.size());
  for (size_t i = 0; i < bins.size(); i++) { float s = 0; for (size_t j = 0; j < bins.size(); j++) s += bins[j] * std::exp(-.5f * std::pow((float(i) - float(j)) / 2.2f, 2.f)); sm[i] = s; }
  float mx = std::max(1.f, *std::max_element(sm.begin(), sm.end()));
  std::string path; char b[256];
  std::vector<std::pair<float, float>> pts;
  for (size_t i = 0; i < sm.size(); i++) { float x = i * width / (bins.size() - 1), y = baseline - sm[i] / mx * (baseline - top); pts.push_back({x, y}); std::snprintf(b, sizeof b, "%s%.1f,%.1f ", i ? "L" : "M", x, y); path += b; }
  float x = score / 700.f * width; int idx = std::clamp((int)std::lround(score / 700.f * (pts.size() - 1)), 0, (int)pts.size() - 1); float y = pts[idx].second;
  std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 560 174\" width=\"560\" height=\"174\"><path d=\"" + path + "L560,140 L0,140 Z\" fill=\"#4de3ff\" fill-opacity=\"0.145\"/><path d=\"" + path + "\" fill=\"none\" stroke=\"#4de3ff\" stroke-width=\"2.4\"/><path d=\"M0 140H560\" stroke=\"#7d93b8\" stroke-opacity=\"0.33\"/>";
  for (int i = 0; i < 8; i++) { std::snprintf(b, sizeof b, "<path d=\"M%d 140v6\" stroke=\"#7d93b8\" stroke-opacity=\"0.33\"/>", i * 80); s += b; }
  std::snprintf(b, sizeof b, "<path d=\"M%.1f 140V%.1f\" stroke=\"#ff5d8f\" stroke-width=\"2\"/><circle cx=\"%.1f\" cy=\"%.1f\" r=\"4\" fill=\"#ff5d8f\"/>", x, y, x, y); s += b;
  (void)population;
  return s + "</svg>";
}
