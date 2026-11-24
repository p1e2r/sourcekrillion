#include "content.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>

const std::vector<Tier> TIERS = {
  {"plankton", "Plankton", 10, "🫧", "#8fa3c4", "The answer everyone blurts out."},
  {"tooclever", "Too Clever", 15, "🤡", "#ff9f43", "The obscure answer the whole school thought of."},
  {"schooler", "Schooler", 30, "🐟", "#4de3ff", "Solid — swims with the school."},
  {"rare", "Rare", 60, "🦑", "#ff5252", "An uncommon answer. Nice pull."},
  {"deepcut", "Deep Cut", 85, "🏮", "#9d7bff", "True obscurity. Few go this deep."},
  {"krillion", "One in a Krillion", 100, "🌟", "#ffd166", "The designated gem. Trench floor."},
};
const Tier* tierById(const std::string& id) { for (auto& t : TIERS) if (id == t.id) return &t; return nullptr; }
const std::vector<Band> BANDS = {
  {0, 150, "plankton", "Plankton", "Plenty of ocean left down there."},
  {151, 250, "schooler", "Schooler", "Swimming with the school."},
  {251, 350, "rare", "Rare", "Below the thermocline. Nice."},
  {351, 449, "deepcut", "Deep cut", "Seriously deep dive."},
  {450, 700, "krillion", "One in a krillion", "Trench dweller. Absurd."},
};
const Band& bandFor(int score) { for (int i = (int)BANDS.size() - 1; i >= 0; i--) if (score >= BANDS[i].min) return BANDS[i]; return BANDS[0]; }

// Decode UTF-8, fold accents to ASCII the way NFKD + stripping combining marks does for Latin text.
static void foldCodepoint(uint32_t c, std::string& out) {
  static const std::map<uint32_t, const char*> fold = {
    {0xC0,"a"},{0xC1,"a"},{0xC2,"a"},{0xC3,"a"},{0xC4,"a"},{0xC5,"a"},{0xC7,"c"},{0xC8,"e"},{0xC9,"e"},{0xCA,"e"},{0xCB,"e"},{0xCC,"i"},{0xCD,"i"},{0xCE,"i"},{0xCF,"i"},{0xD1,"n"},{0xD2,"o"},{0xD3,"o"},{0xD4,"o"},{0xD5,"o"},{0xD6,"o"},{0xD9,"u"},{0xDA,"u"},{0xDB,"u"},{0xDC,"u"},{0xDD,"y"},
    {0xE0,"a"},{0xE1,"a"},{0xE2,"a"},{0xE3,"a"},{0xE4,"a"},{0xE5,"a"},{0xE7,"c"},{0xE8,"e"},{0xE9,"e"},{0xEA,"e"},{0xEB,"e"},{0xEC,"i"},{0xED,"i"},{0xEE,"i"},{0xEF,"i"},{0xF1,"n"},{0xF2,"o"},{0xF3,"o"},{0xF4,"o"},{0xF5,"o"},{0xF6,"o"},{0xF9,"u"},{0xFA,"u"},{0xFB,"u"},{0xFC,"u"},{0xFD,"y"},{0xFF,"y"},
    {0x100,"a"},{0x101,"a"},{0x102,"a"},{0x103,"a"},{0x104,"a"},{0x105,"a"},{0x106,"c"},{0x107,"c"},{0x10C,"c"},{0x10D,"c"},{0x10E,"d"},{0x10F,"d"},{0x112,"e"},{0x113,"e"},{0x118,"e"},{0x119,"e"},{0x11A,"e"},{0x11B,"e"},{0x11E,"g"},{0x11F,"g"},{0x12B,"i"},{0x130,"i"},{0x141,"l"},{0x142,"l"},{0x143,"n"},{0x144,"n"},{0x147,"n"},{0x148,"n"},{0x14C,"o"},{0x14D,"o"},{0x150,"o"},{0x151,"o"},{0x158,"r"},{0x159,"r"},{0x15A,"s"},{0x15B,"s"},{0x15E,"s"},{0x15F,"s"},{0x160,"s"},{0x161,"s"},{0x162,"t"},{0x163,"t"},{0x164,"t"},{0x165,"t"},{0x16B,"u"},{0x16E,"u"},{0x16F,"u"},{0x170,"u"},{0x171,"u"},{0x178,"y"},{0x179,"z"},{0x17A,"z"},{0x17B,"z"},{0x17C,"z"},{0x17D,"z"},{0x17E,"z"},
  };
  if (c < 0x80) { out += (char)c; return; }
  auto it = fold.find(c); if (it != fold.end()) out += it->second;
}
std::string normalizeAnswer(const std::string& value) {
  std::string folded;
  for (size_t i = 0; i < value.size();) {
    unsigned char b = value[i]; uint32_t c; int len;
    if (b < 0x80) { c = b; len = 1; } else if ((b >> 5) == 6) { c = b & 31; len = 2; } else if ((b >> 4) == 14) { c = b & 15; len = 3; } else { c = b & 7; len = 4; }
    for (int k = 1; k < len && i + k < value.size(); k++) c = (c << 6) | (value[i + k] & 63);
    i += len; foldCodepoint(c, folded);
  }
  std::string lower; for (char ch : folded) lower += (char)std::tolower((unsigned char)ch);
  std::string out;
  for (size_t i = 0; i < lower.size(); i++) {
    char ch = lower[i];
    if (ch == '&') out += "and";
    else if (ch == '+' && i + 1 < lower.size() && lower[i + 1] == '+') { out += "plusplus"; i++; }
    else if (ch == '#') out += "sharp";
    else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) out += ch;
  }
  return out;
}
static int distance(const std::string& a, const std::string& b, int limit) {
  if (std::abs((int)a.size() - (int)b.size()) > limit) return limit + 1;
  std::vector<int> row(b.size() + 1); for (size_t i = 0; i <= b.size(); i++) row[i] = (int)i;
  for (size_t i = 1; i <= a.size(); i++) {
    std::vector<int> next(b.size() + 1); next[0] = (int)i; int best = next[0];
    for (size_t j = 1; j <= b.size(); j++) { next[j] = std::min({next[j - 1] + 1, row[j] + 1, row[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)}); best = std::min(best, next[j]); }
    if (best > limit) return limit + 1;
    row = next;
  }
  return row[b.size()];
}
std::optional<Match> matchAnswer(const std::string& input, const std::vector<Answer>& answers) {
  std::string key = normalizeAnswer(input);
  if (key.empty() || key.size() > 256) return std::nullopt;
  struct Entry { std::string key; const Answer* a; };
  std::vector<Entry> entries;
  for (auto& a : answers) { entries.push_back({normalizeAnswer(a.answer), &a}); for (auto& al : a.aliases) entries.push_back({normalizeAnswer(al), &a}); }
  for (auto& e : entries) if (e.key == key) return Match{*e.a, key != normalizeAnswer(e.a->answer)};
  int allowance = key.size() < 4 ? 0 : key.size() < 8 ? 1 : 2;
  std::vector<std::pair<int, const Answer*>> cands;
  for (auto& e : entries) { int d = distance(key, e.key, allowance); if (d <= allowance) cands.push_back({d, e.a}); }
  std::stable_sort(cands.begin(), cands.end(), [](auto& x, auto& y) { return x.first < y.first; });
  if (!cands.empty() && (cands.size() == 1 || cands[0].first < cands[1].first || cands[0].second->answer == cands[1].second->answer)) return Match{*cands[0].second, true};
  return std::nullopt;
}
Prompt promptFromJson(const nlohmann::json& j) {
  Prompt p; p.id = j.value("id", ""); p.text = j.value("text", "");
  if (j.contains("answers")) for (auto& a : j["answers"]) {
    Answer x; x.answer = a.value("answer", ""); x.tier = a.value("tier", "plankton"); x.quip = a.value("quip", "");
    if (a.contains("aliases")) for (auto& al : a["aliases"]) x.aliases.push_back(al.get<std::string>());
    p.answers.push_back(x);
  }
  return p;
}

// ---- dates (America/New_York: UTC-5, DST second Sunday of March to first Sunday of November) ----
int64_t daysFromCivil(int y, unsigned m, unsigned d) { y -= m <= 2; const int era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = (unsigned)(y - era * 400); const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + (int64_t)doe - 719468; }
static void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) { z += 719468; const int64_t era = (z >= 0 ? z : z - 146096) / 146097; const unsigned doe = (unsigned)(z - era * 146097); const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; y = (int)(yoe + era * 400); const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); const unsigned mp = (5 * doy + 2) / 153; d = doy - (153 * mp + 2) / 5 + 1; m = mp + (mp < 10 ? 3 : -9); y += m <= 2; }
static int weekday(int64_t days) { return (int)((days % 7 + 11) % 7); } // 0 = Sunday
static bool nyDst(int64_t utcMs) {
  int y; unsigned m, d; civilFromDays(utcMs / 86400000, y, m, d);
  int64_t mar1 = daysFromCivil(y, 3, 1), nov1 = daysFromCivil(y, 11, 1);
  int64_t start = mar1 + (7 - weekday(mar1)) % 7 + 7, end = nov1 + (7 - weekday(nov1)) % 7; // 2nd Sun Mar, 1st Sun Nov
  int64_t startMs = start * 86400000 + 7 * 3600000LL, endMs = end * 86400000 + 6 * 3600000LL;   // 2am local
  return utcMs >= startMs && utcMs < endMs;
}
static std::string ymd(int64_t days) { int y; unsigned m, d; civilFromDays(days, y, m, d); char b[16]; std::snprintf(b, sizeof b, "%04d-%02u-%02u", y, m, d); return b; }
std::string etDate(int64_t ms) { int64_t local = ms - (nyDst(ms) ? 4 : 5) * 3600000LL; return ymd((local >= 0 ? local : local - 86399999) / 86400000); }
static int64_t daysOf(const std::string& date) { int y = 1970; unsigned m = 1, d = 1; std::sscanf(date.c_str(), "%d-%u-%u", &y, &m, &d); return daysFromCivil(y, m, d); }
int64_t resetAt(const std::string& date) { int64_t next = daysOf(date) + 1; int64_t probe = next * 86400000 + 4 * 3600000LL; return etDate(probe) == ymd(next) ? probe : next * 86400000 + 5 * 3600000LL; }
std::string addDays(const std::string& date, int days) { return ymd(daysOf(date) + days); }
int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string upper(std::string s) { for (auto& c : s) c = (char)std::toupper((unsigned char)c); return s; }
std::string commas(long long v) { std::string s = std::to_string(v < 0 ? -v : v), out; int n = 0; for (int i = (int)s.size() - 1; i >= 0; i--) { out.insert(out.begin(), s[i]); if (++n % 3 == 0 && i) out.insert(out.begin(), ','); } return (v < 0 ? "-" : "") + out; }
std::string escapeRml(const std::string& s) { std::string o; for (char c : s) { switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; case '\'': o += "&#39;"; break; default: o += c; } } return o; }
