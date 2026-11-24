#include "offline.h"
#include <algorithm>
#include <fstream>
#include <random>
#include <set>

static Response ok(json j) { Response r; r.status = 200; r.body = std::move(j); return r; }
static Response fail(int status, const std::string& msg) { Response r; r.status = status; r.body = {{"error", msg}}; r.error = msg; return r; }
static std::string newId() {
  static std::mt19937_64 rng{std::random_device{}()};
  const char* hex = "0123456789abcdef"; std::string s;
  for (int i = 0; i < 36; i++) s += (i == 8 || i == 13 || i == 18 || i == 23) ? '-' : hex[rng() % 16];
  return s;
}

void Offline::load(const std::string& dataDir) {
  dataDir_ = dataDir;
  try { std::ifstream b(dataDir + "/bundle.json"); if (b) bundle_ = json::parse(b); } catch (...) { bundle_ = nullptr; }
  try { std::ifstream d(dataDir + "/offline.json"); if (d) data_ = json::parse(d); } catch (...) { data_ = json::object(); }
  if (!data_.contains("runs")) data_["runs"] = json::object();
}
void Offline::storeBundle(const json& bundle) {
  if (!bundle.contains("today")) return;
  bundle_ = bundle; std::ofstream(dataDir_ + "/bundle.json") << bundle.dump();
}
void Offline::save() { std::ofstream(dataDir_ + "/offline.json") << data_.dump(); }

json Offline::puzzleFor(const json& run) const { return run["puzzle"]; }
json Offline::state(const json& run) const {
  json p = run["puzzle"], prompts = json::array();
  for (auto& q : p["prompts"]) prompts.push_back({{"id", q["id"]}, {"text", q["text"]}});
  int score = 0; for (auto& r : run["results"]) score += r.value("score", 0);
  return {{"runId", run["id"]}, {"mode", run["mode"]}, {"title", run["title"]}, {"kicker", run.value("kicker", "")}, {"shareLabel", run.value("shareLabel", "")}, {"pack", run.value("pack", json(nullptr))}, {"number", run.value("number", 0)},
          {"date", p.value("date", "")}, {"dayNumber", p.value("dayNumber", 1)}, {"resetsAt", resetAt(etDate(nowMs()))}, {"prompts", prompts},
          {"round", run["round"]}, {"roundStart", run["roundStart"]}, {"pausedAt", run.value("pausedAt", json(nullptr))}, {"serverNow", nowMs()}, {"completed", run["completed"]}, {"results", run["results"]}, {"score", score}, {"offline", true}};
}
json Offline::packsView() const {
  json out = json::array();
  for (auto& pack : bundle_["packs"]) {
    json chapters = json::array(); bool unlocked = true; int done = 0;
    for (size_t i = 0; i < pack["chapters"].size(); i++) {
      std::string key = "pack:" + pack["id"].get<std::string>() + ":" + std::to_string(i);
      json best = nullptr;
      for (auto& [id, run] : data_["runs"].items()) if (run["key"] == key && run["completed"].get<bool>()) { int s = 0; for (auto& r : run["results"]) s += r["score"].get<int>(); if (best.is_null() || s > best.get<int>()) best = s; }
      chapters.push_back({{"index", i}, {"title", pack["chapters"][i]["title"]}, {"unlocked", unlocked}, {"completed", !best.is_null()}, {"best", best}});
      if (!best.is_null()) done++;
      unlocked = !best.is_null();
    }
    out.push_back({{"id", pack["id"]}, {"name", pack["name"]}, {"tagline", pack["tagline"]}, {"cta", pack["cta"]}, {"unit", pack["unit"]}, {"chapters", chapters}, {"completed", done}});
  }
  return out;
}
json Offline::startRun(const json& body) {
  std::string mode = body.value("mode", "daily"), today = etDate(nowMs());
  auto& runs = data_["runs"];
  auto find = [&](const std::string& key, bool openOnly) -> json* { for (auto& [id, r] : runs.items()) if (r["key"] == key && (!openOnly || !r["completed"].get<bool>())) return &r; return nullptr; };
  json run = {{"id", newId()}, {"mode", mode}, {"round", 0}, {"roundStart", nowMs() + PREVIEW_MS}, {"completed", false}, {"results", json::array()}, {"created", nowMs()}};
  if (mode == "pack") {
    std::string packId = body.value("pack", ""); int ch = body.value("chapter", 0);
    for (auto& pack : bundle_["packs"]) if (pack["id"] == packId && ch < (int)pack["chapters"].size()) {
      for (auto& p : packsView()) if (p["id"] == packId && !p["chapters"][ch]["unlocked"].get<bool>()) throw std::runtime_error("Finish the previous chapter first.");
      std::string key = "pack:" + packId + ":" + std::to_string(ch);
      if (auto* open = find(key, true)) return *open;
      std::string unit = pack.value("unit", "chapter"); unit[0] = (char)std::toupper(unit[0]);
      run["key"] = key; run["puzzle"] = {{"date", today}, {"dayNumber", ch + 1}, {"prompts", pack["chapters"][ch]["prompts"]}};
      run["title"] = pack["name"].get<std::string>() + " · " + unit + " " + std::to_string(ch + 1);
      run["kicker"] = upper(pack["chapters"][ch]["title"].get<std::string>());
      run["pack"] = {{"id", packId}, {"name", pack["name"]}, {"chapter", ch}, {"chapters", pack["chapters"].size()}, {"chapterTitle", pack["chapters"][ch]["title"]}};
      run["shareLabel"] = pack["name"].get<std::string>() + " " + std::to_string(ch + 1);
      return runs[run["id"].get<std::string>()] = run;
    }
    throw std::runtime_error("Pack chapter not found.");
  }
  if (mode == "unlimited") {
    if (!body.value("fresh", false)) for (auto& [id, r] : runs.items()) if (r["mode"] == "unlimited" && !r["completed"].get<bool>()) return r;
    int number = 1; for (auto& [id, r] : runs.items()) if (r["mode"] == "unlimited") number++;
    std::vector<json> pool(bundle_["pool"].begin(), bundle_["pool"].end());
    std::shuffle(pool.begin(), pool.end(), std::mt19937{std::random_device{}()});
    json prompts = json::array(); for (int i = 0; i < 7 && i < (int)pool.size(); i++) prompts.push_back(pool[i]);
    run["key"] = "unlimited:" + std::to_string(number); run["number"] = number;
    run["puzzle"] = {{"date", today}, {"dayNumber", number}, {"prompts", prompts}};
    run["title"] = "∞ Dive #" + std::to_string(number); run["kicker"] = "THE ENDLESS DIVE"; run["shareLabel"] = "∞ " + std::to_string(number);
    return runs[run["id"].get<std::string>()] = run;
  }
  if (mode == "archive") throw std::runtime_error("Connect to the internet to open the archive.");
  // daily: only the downloaded day can be played offline
  json todayPuzzle = bundle_["today"];
  if (todayPuzzle.value("date", "") != today) throw std::runtime_error("Connect to the internet to download today’s dive.");
  if (auto* existing = find("daily:" + today, false)) return *existing;
  run["mode"] = "daily"; run["key"] = "daily:" + today; run["puzzle"] = todayPuzzle;
  run["title"] = "Dive #" + std::to_string(todayPuzzle.value("dayNumber", 1)); run["kicker"] = "THE DAILY DIVE"; run["shareLabel"] = "#" + std::to_string(todayPuzzle.value("dayNumber", 1));
  return runs[run["id"].get<std::string>()] = run;
}
json Offline::logbook() const {
  std::vector<std::string> dailyDates; json list = json::array(); int best = 0, total = 0, played = 0;
  std::vector<json> runs; for (auto& [id, r] : data_["runs"].items()) if (r["completed"].get<bool>()) runs.push_back(r);
  std::sort(runs.begin(), runs.end(), [](const json& a, const json& b) { return a["created"].get<int64_t>() > b["created"].get<int64_t>(); });
  for (auto& r : runs) {
    int s = 0; json tiers = json::array(); for (auto& x : r["results"]) { s += x["score"].get<int>(); tiers.push_back(x["tier"].is_null() ? "miss" : x["tier"].get<std::string>()); }
    std::string mode = r["mode"]; std::string date = r["puzzle"].value("date", "");
    if (mode == "daily") { dailyDates.push_back(date); best = std::max(best, s); total += s; played++; }
    list.push_back({{"id", r["id"]}, {"mode", mode}, {"date", date}, {"puzzle", r["key"]}, {"number", r.value("number", 0)}, {"score", s}, {"tiers", tiers}, {"dayNumber", r["puzzle"].value("dayNumber", 1)}});
  }
  std::sort(dailyDates.begin(), dailyDates.end());
  int run = 0, bestRun = 0; std::string last;
  for (auto& d : dailyDates) { run = (!last.empty() && addDays(last, 1) == d) ? run + 1 : 1; bestRun = std::max(bestRun, run); last = d; }
  std::string today = etDate(nowMs());
  int current = (!last.empty() && (last == today || addDays(last, 1) == today)) ? run : 0;
  return {{"stats", {{"current", current}, {"bestStreak", bestRun}, {"played", played}, {"best", best}, {"average", played ? total / played : 0}}}, {"runs", list}};
}

Response Offline::handle(const std::string& method, const std::string& path, const json& body) {
  if (!hasBundle()) return fail(0, "No connection. Connect once to download dives for offline play.");
  auto& runs = data_["runs"];
  std::string p = path, query; if (auto q = p.find('?'); q != std::string::npos) { query = p.substr(q + 1); p = p.substr(0, q); }
  auto runParam = [&]() { auto k = query.find("run="); return k == std::string::npos ? std::string() : query.substr(k + 4, 36); };
  try {
    if (method == "GET") {
      if (p == "today") { json t = bundle_["today"], prompts = json::array(); for (auto& q : t["prompts"]) prompts.push_back({{"id", q["id"]}, {"text", q["text"]}}); return ok({{"date", t["date"]}, {"dayNumber", t["dayNumber"]}, {"resetsAt", resetAt(etDate(nowMs()))}, {"prompts", prompts}, {"offline", true}}); }
      if (p == "state") { auto id = runParam(); if (!runs.contains(id)) return fail(404, "Dive not found on this device."); return ok(state(runs[id])); }
      if (p == "review") {
        auto id = runParam(); if (!runs.contains(id) || !runs[id]["completed"].get<bool>()) return fail(403, "Finish your dive before opening the answer sheet.");
        json prompts = json::array();
        for (auto q : runs[id]["puzzle"]["prompts"]) { std::vector<json> as(q["answers"].begin(), q["answers"].end()); for (auto& a : as) a["score"] = tierById(a["tier"])->score; std::stable_sort(as.begin(), as.end(), [](const json& a, const json& b) { return a["score"].get<int>() > b["score"].get<int>() || (a["score"] == b["score"] && a["answer"].get<std::string>() < b["answer"].get<std::string>()); }); q["answers"] = as; prompts.push_back(q); }
        return ok({{"prompts", prompts}, {"population", 0}, {"betterThan", nullptr}, {"dist", json::array()}});
      }
      if (p == "logbook") return ok(logbook());
      if (p == "packs") return ok({{"packs", packsView()}});
      if (p == "me") return ok({{"name", ""}, {"friendCode", nullptr}, {"hasRestoreCode", false}, {"offline", true}});
      return fail(503, "You’re offline. This needs a connection.");
    }
    if (p == "start") { json r = startRun(body); save(); return ok(state(runs[r["id"].get<std::string>()])); }
    std::string id = body.value("runId", "");
    if (!runs.contains(id)) return fail(503, "You’re offline. This needs a connection.");
    json& run = runs[id];
    if (p == "pause") {
      int64_t now = nowMs();
      if (body.value("paused", false) && !run["completed"].get<bool>() && !run["roundStart"].is_null() && run["pausedAt"].is_null()) run["pausedAt"] = now;
      else if (!body.value("paused", true) && run.contains("pausedAt") && !run["pausedAt"].is_null()) { run["roundStart"] = run["roundStart"].get<int64_t>() + now - run["pausedAt"].get<int64_t>(); run["pausedAt"] = nullptr; }
      save(); return ok(state(run));
    }
    if (p == "next") { if (!run["completed"].get<bool>() && run["roundStart"].is_null()) run["roundStart"] = nowMs() + PREVIEW_MS; save(); return ok(state(run)); }
    if (p == "answer") {
      int round = run["round"]; if (run["completed"].get<bool>() || run["roundStart"].is_null() || body.value("round", -1) != round) return ok({{"state", state(run)}, {"alreadyScored", true}});
      int64_t start = run["roundStart"], now = nowMs();
      Prompt prompt = promptFromJson(run["puzzle"]["prompts"][round]);
      std::string raw = body.value("answer", ""); while (!raw.empty() && std::isspace((unsigned char)raw.back())) raw.pop_back(); while (!raw.empty() && std::isspace((unsigned char)raw.front())) raw.erase(raw.begin());
      bool timeout = body.value("timeout", false), late = now > start + ROUND_MS, timely = !late || (timeout && now <= start + ROUND_MS + 1500);
      auto match = !raw.empty() && timely ? matchAnswer(raw, prompt.answers) : std::nullopt;
      if (!match && !raw.empty() && !late && !timeout) return ok({{"rejected", true}, {"message", "“" + raw + "”: no echo. try again"}});
      if (match && match->correction && !body.value("confirm", false) && !late && !timeout && body.value("previewCorrection", true)) return ok({{"correction", match->answer.answer}, {"original", raw}});
      const Tier* t = match ? tierById(match->answer.tier) : nullptr;
      json result = {{"round", round}, {"rawAnswer", raw.empty() ? json(nullptr) : json(raw)}, {"answer", match ? json(match->answer.answer) : json(nullptr)}, {"tier", t ? json(t->id) : json(nullptr)}, {"score", t ? t->score : 0}, {"quip", match && !match->answer.quip.empty() ? json(match->answer.quip) : json(nullptr)}, {"promptText", prompt.text}};
      run["results"].push_back({{"round", round}, {"rawAnswer", result["rawAnswer"]}, {"answer", result["answer"]}, {"tier", result["tier"]}, {"score", result["score"]}});
      run["round"] = round + 1; run["roundStart"] = nullptr; if (round == ROUNDS - 1) run["completed"] = true;
      save(); return ok({{"result", result}, {"state", state(run)}});
    }
    return fail(503, "You’re offline. This needs a connection.");
  } catch (std::exception& e) { return fail(400, e.what()); }
}
