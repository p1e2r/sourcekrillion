// Game rules shared with the web version (port of public/rules.mjs).
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

struct Tier { const char* id; const char* name; int score; const char* emoji; const char* color; const char* blurb; };
extern const std::vector<Tier> TIERS;
const Tier* tierById(const std::string& id);
struct Band { int min, max; const char* tier; const char* name; const char* text; };
extern const std::vector<Band> BANDS;
const Band& bandFor(int score);

constexpr int ROUND_MS = 25000, PREVIEW_MS = 3000, ROUNDS = 7;

struct Answer { std::string answer, tier, quip; std::vector<std::string> aliases; };
struct Prompt { std::string id, text; std::vector<Answer> answers; };
struct Match { Answer answer; bool correction = false; };

std::string normalizeAnswer(const std::string& value);
std::optional<Match> matchAnswer(const std::string& input, const std::vector<Answer>& answers);
Prompt promptFromJson(const nlohmann::json& j);

std::string etDate(int64_t nowMs);          // New York calendar date, YYYY-MM-DD
int64_t resetAt(const std::string& date);   // next New York midnight, ms since epoch
int64_t nowMs();
std::string addDays(const std::string& date, int days);
int64_t daysFromCivil(int y, unsigned m, unsigned d);
std::string upper(std::string s);
std::string commas(long long v);            // 1234 -> "1,234"
std::string escapeRml(const std::string& s);
