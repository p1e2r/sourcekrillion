// Port of public/app.mjs to C++/RmlUi. Same flow, same markup classes, same API calls.
#include "app.h"
#include <SDL3/SDL.h>
#include "charts.h"
#include "content.h"
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

extern std::map<std::string, std::string> g_memoryFiles; // served to RmlUi by the file interface (main.cpp)
static const char* WORD = "SOURCEKRILLION";
static std::string S(const json& j, const char* k, const std::string& d = "") { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : d; }
static int I(const json& j, const char* k, int d = 0) { return j.contains(k) && j[k].is_number() ? j[k].get<int>() : d; }
static std::string esc(const std::string& s) { return escapeRml(s); }
static std::string tierColor(const json& t) { auto* x = t.is_string() ? tierById(t.get<std::string>()) : nullptr; return x ? x->color : "#7d93b8"; }
static std::string tierStr(const json& t) { return t.is_string() ? t.get<std::string>() : ""; }
static std::string pad2(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

void App::init(Rml::Context* ctx, Rml::ElementDocument* doc, Net* net, Offline* off, Sound* snd, Ocean* ocean, const std::string& dataDir) {
  ctx_ = ctx; doc_ = doc; net_ = net; off_ = off; sound_ = snd; scene_ = ocean; dataDir_ = dataDir;
  doc_->AddEventListener(Rml::EventId::Click, this); doc_->AddEventListener(Rml::EventId::Keydown, this, true); doc_->AddEventListener(Rml::EventId::Change, this);
  std::string word; for (int i = 0; WORD[i]; i++) word += std::string("<span id=\"letter-") + std::to_string(i) + "\">" + WORD[i] + "</span>";
  setRml("hero-word", word);
  std::string dots; for (int i = 0; i < 7; i++) dots += "<i id=\"dot-" + std::to_string(i) + "\"></i>"; setRml("round-dots", dots);
  loadSettings(); applySettings(); sound_->init();
  bootAt_ = now_;
  // Download the offline bundle and today's dive.
  net_->get("bundle", [this](Response r) { if (r.ok()) off_->storeBundle(r.body); });
  api("GET", "today", {}, [this](Response r) {
    if (!r.ok()) { setText("boot-error", r.error); show("boot-error", true); return; }
    today_ = r.body; setText("day-number", "DIVE #" + std::to_string(I(today_, "dayNumber")));
    if (r.body.value("offline", false)) { setText("offline-note", "offline · playing from your saved dives"); show("offline-note", true); }
  });
}

// ---------- plumbing ----------
void App::api(const std::string& method, const std::string& path, const json& body, std::function<void(Response)> done, bool runScoped) {
  if (runScoped && gameOffline_) { done(off_->handle(method, path, body)); return; }
  auto handler = [this, method, path, body, done](Response r) {
    if (r.status == 0 && off_->hasBundle()) { Response o = off_->handle(method, path, body); if (o.status != 0) { done(o); return; } }
    if (r.status == 0) r.error = "No connection. Check your internet and try again.";
    done(r);
  };
  if (method == "GET") net_->get(path, handler); else net_->post(path, body, handler);
}
Rml::Element* App::el(const std::string& id) { return doc_->GetElementById(id); }
void App::setText(const std::string& id, const std::string& text) { if (auto* e = el(id)) e->SetInnerRML(esc(text)); }
void App::setRml(const std::string& id, const std::string& rml) { if (auto* e = el(id)) e->SetInnerRML(rml); }
void App::show(const std::string& id, bool on) { if (auto* e = el(id)) { if (on) { e->RemoveAttribute("hidden"); e->RemoveProperty("display"); } else { e->SetAttribute("hidden", "hidden"); e->SetProperty("display", "none"); } } }
void App::setClass(Rml::Element* e, const std::string& cls, bool on) { if (e) e->SetClass(cls, on); }
void App::bodyClass(const std::string& cls, bool on) { setClass(doc_, cls, on); }
void App::loadSettings() {
  try { std::ifstream f(dataDir_ + "/settings.json"); if (!f) return; json j = json::parse(f); settings_.lowPower = j.value("lowPower", false); settings_.colorblind = j.value("colorblind", false); settings_.corrections = j.value("corrections", true); settings_.clearRejected = j.value("clearRejected", false); settings_.shareLink = j.value("shareLink", true); settings_.muted = j.value("muted", false); settings_.buddy = j.value("buddy", "krill"); } catch (...) {}
}
void App::saveSettings() { std::ofstream(dataDir_ + "/settings.json") << json{{"lowPower", settings_.lowPower}, {"colorblind", settings_.colorblind}, {"corrections", settings_.corrections}, {"clearRejected", settings_.clearRejected}, {"shareLink", settings_.shareLink}, {"muted", settings_.muted}, {"buddy", settings_.buddy}}.dump(); }
static const std::vector<std::tuple<std::string, std::string, int>> BUDDIES = {{"krill", "The Krill", 0}, {"schooler", "Schooler", 5}, {"rare", "Rare Squid", 14}, {"deepcut", "Deep Cut Angler", 30}, {"evil", "Evil Krill", 45}};
void App::applySettings() {
  scene_->lowPower = settings_.lowPower; bodyClass("low-power", settings_.lowPower); bodyClass("colorblind", settings_.colorblind); sound_->mute(settings_.muted);
  if (auto* m = el("mute-icon")) m->SetAttribute("src", settings_.muted ? "../sprites/sound-off.svg" : "../sprites/sound-on.svg");
  if (auto* img = el("mascot-img")) img->SetAttribute("src", "../sprites/" + settings_.buddy + ".svg");
  syncBuddyGold();
}
int App::currentScore() const { int s = 0; if (game_.is_object()) for (auto& r : game_["results"]) s += I(r, "score"); return s; }

// ---------- phases ----------
void App::setPhase(const std::string& value) {
  auto* card = el("prompt-card"); bool exiting = value == "plunge" && card && !card->HasAttribute("hidden");
  setClass(card, "leaving", exiting); if (exiting) promptExitAt_ = now_ + 650;
  phase_ = value; if (value != "answering") bodyClass("timer-hot", false);
  doc_->SetAttribute("data-phase", value); scene_->setPhase(value);
  bodyClass("playing", value != "intro" && value != "done"); bodyClass("phase-done", value == "done");
  bodyClass("hud-on", value == "preview" || value == "answering" || value == "plunge" || value == "landed");
  show("intro", value == "intro"); show("prompt-card", exiting || value == "preview" || value == "answering");
  setClass(card, "arrive", value == "preview");
  show("answer-dock", value == "answering"); show("preview-label", value == "preview"); show("landing", value == "landed"); show("next-wrap", value == "landed");
  show("results", value == "done"); show("falling-answer", value == "plunge"); show("tier-ladder", value == "plunge");
}
void App::setGame(const json& state) {
  game_ = state; gameOffline_ = state.value("offline", false);
  clockAtServer_ = state.contains("serverNow") && state["serverNow"].is_number() ? state["serverNow"].get<double>() - nowMs() : 0;
  visualScore_ = currentScore(); updateHud();
}
void App::updateHud() {
  const json& results = game_.is_object() ? game_["results"] : json::array();
  setText("score", std::to_string((int)std::lround(visualScore_)));
  setText("depth", commas(std::lround(scene_->depth)) + "m");
  setText("round-label", game_.is_object() ? (game_.value("completed", false) ? "DIVE COMPLETE" : "PROMPT " + std::to_string(std::min(7, I(game_, "round") + 1)) + " OF 7") : "SURFACE");
  for (int i = 0; i < 7; i++) { auto* d = el("dot-" + std::to_string(i)); bool done = false; for (auto& r : results) if (I(r, "round") == i) done = true; if (d) { d->SetClass("done", done); d->SetClass("active", !done && game_.is_object() && I(game_, "round", -1) == i); } }
}
void App::begin(const json& options) {
  if (busy_) return; busy_ = true; show("boot-error", false); closeParty(); sound_->init();
  api("POST", "start", options, [this](Response r) {
    busy_ = false;
    if (!r.ok()) { setPhase("intro"); setText("boot-error", r.error); show("boot-error", true); return; }
    setGame(r.body);
    std::string mode = S(game_, "mode");
    static const std::map<std::string, std::pair<std::string, std::string>> copy = {{"daily", {"THE DAILY DIVE", "7 prompts · 25 seconds each · rarer answers sink deeper"}}, {"unlimited", {"THE ENDLESS DIVE", "dive after dive · rarer answers sink deeper"}}, {"archive", {"THE ARCHIVE", "every past dive · replays don’t count toward streaks"}}, {"pack", {"THEMED PACKS", "beyond the daily dive · chapter by chapter"}}};
    auto c = copy.count(mode) ? copy.at(mode) : copy.at("daily"); setText("tagline", c.first); setText("subline", c.second);
    sound_->event("begin"); scene_->targetActive = 1; review_ = nullptr;
    if (game_.value("completed", false)) { showResults(); return; }
    if (game_["roundStart"].is_null() && !game_["results"].empty()) { json last = game_["results"].back(); showLanding(last, currentScore() - I(last, "score")); }
    else showRound();
  });
}
void App::resumeDaily() {
  if (game_.is_object() && S(game_, "mode") == "daily") { if (game_.value("completed", false)) showResults(); else if (game_["roundStart"].is_null()) { json last = game_["results"].back(); showLanding(last, currentScore() - I(last, "score")); } else showRound(); }
  else begin({{"mode", "daily"}});
}
void App::showRound() {
  setPhase("preview"); syncBuddyGold(); correction_.clear(); autoSent_ = false; lastTick_ = -1;
  show("answer-status", false); setClass(el("answer-status"), "correction", false);
  int round = I(game_, "round"); setText("prompt-number", "PROMPT " + std::to_string(round + 1) + " OF 7"); setText("prompt-text", S(game_["prompts"][round], "text"));
  if (auto* in = el("answer")) { in->SetAttribute("value", ""); static_cast<Rml::ElementFormControl*>(in)->SetValue(""); in->SetAttribute("disabled", "disabled"); }
  setClass(el("submit-btn"), "disabled", true);
  float cardH = el("prompt-card") ? el("prompt-card")->GetOffsetHeight() : 180; if (cardH < 40) cardH = 180;
  scene_->setDepth(currentScore() * 10.f); scene_->roundBase = currentScore() * 10.f; scene_->roundIndex = round; scene_->round(currentScore() * 10.f, cardH); tick();
}
static std::string inputValue(Rml::Element* e) { return e ? static_cast<Rml::ElementFormControl*>(e)->GetValue() : ""; }
static std::string trim(std::string s) { while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) i++; return s.substr(i); }
void App::submit(bool timeout) {
  if (!game_.is_object() || busy_ || (phase_ != "preview" && phase_ != "answering") || paused_) return;
  std::string raw = trim(inputValue(el("answer"))); if (raw.empty() && !timeout) return;
  busy_ = true; setClass(el("submit-btn"), "disabled", true);
  json body = {{"runId", game_["runId"]}, {"round", game_["round"]}, {"answer", raw}, {"timeout", timeout}, {"confirm", !correction_.empty() && correction_ == raw}, {"previewCorrection", settings_.corrections}};
  api("POST", "answer", body, [this, raw](Response r) {
    busy_ = false;
    if (!r.ok()) { setText("answer-status", r.error); show("answer-status", true); }
    else if (r.body.value("rejected", false)) { setClass(el("answer-status"), "correction", false); setText("answer-status", S(r.body, "message")); show("answer-status", true); sound_->event("reject"); if (settings_.clearRejected) static_cast<Rml::ElementFormControl*>(el("answer"))->SetValue(""); correction_.clear(); shakeUntil_ = now_ + 350; }
    else if (r.body.contains("correction")) { correction_ = S(r.body, "correction"); setClass(el("answer-status"), "correction", true); setRml("answer-status", "“" + esc(raw) + "” → corrects to “" + esc(correction_) + "”<small>submit again to confirm · or edit</small>"); show("answer-status", true); static_cast<Rml::ElementFormControl*>(el("answer"))->SetValue(correction_); }
    else if (r.body.value("alreadyScored", false)) { setGame(r.body["state"]); if (game_.value("completed", false)) showResults(); else if (game_["roundStart"].is_null()) { json last = game_["results"].back(); showLanding(last, currentScore() - I(last, "score")); } else showRound(); }
    else { int old = currentScore(); setGame(r.body["state"]); sound_->event("submit"); if (auto* in = el("answer")) { auto b = in->GetAbsoluteOffset(); for (int i = 0; i < 12; i++) scene_->typeAt(b.x + std::rand() % std::max(1, (int)in->GetOffsetWidth()), b.y + std::rand() % 50, 1); } startPlunge(r.body["result"], old); }
    if (phase_ == "answering") { setClass(el("submit-btn"), "disabled", trim(inputValue(el("answer"))).empty()); pendingFocus_ = "answer"; }
  }, true);
}
void App::animateScore(int from, int to, bool gold) { scoreAnim_ = {true, now_, gold ? 1100.0 : 700.0, from, to, gold}; }
void App::startPlunge(const json& result, int oldScore) {
  setPhase("plunge"); std::string tier = tierStr(result["tier"]); doc_->SetAttribute("data-tier", tier.empty() ? "miss" : tier);
  visualScore_ = oldScore; updateHud(); scene_->roundIndex = I(game_, "round");
  std::string col = tierColor(result["tier"]), ans = S(result, "answer");
  auto* fall = el("falling-answer");
  setRml("falling-answer", ans.empty() ? "" : "“" + esc(upper(ans)) + "” <span style=\"font-size:18dp;opacity:.8\">▼</span>");
  if (fall) { fall->SetProperty("color", col); fall->SetProperty("box-shadow", col + " 0 0 0 3dp, " + col + " 0 0 26dp 0"); fall->SetProperty("font-effect", "glow(0dp 10dp 0dp 0dp " + col + ")"); }
  std::string ladder;
  if (!tier.empty()) for (auto& t : TIERS) ladder += std::string("<div class=\"tier-line\" id=\"line-") + t.id + "\" data-score=\"" + std::to_string(t.score) + "\"><span>" + tierImage(t.id) + upper(t.name) + " · " + std::to_string(t.score) + "</span></div>";
  setRml("tier-ladder", ladder);
  plunge_ = {true, result, oldScore, now_, dropMs(tier.empty() ? "miss" : tier), -1};
  if (!tier.empty()) sound_->event("sink"); else { sound_->event("miss"); shakeUntil_ = now_ + 500; }
}
void App::showLanding(const json& result, int oldScore) {
  setPhase("landed"); std::string tier = tierStr(result["tier"]); const Tier* t = tierById(tier); doc_->SetAttribute("data-tier", tier.empty() ? "miss" : tier);
  std::string col = t ? t->color : "#7d93b8";
  show("land-icon-wrap", t != nullptr); if (auto* ic = el("land-icon")) { ic->SetAttribute("src", "../sprites/" + iconFor(tier.empty() ? "plankton" : tier) + ".svg"); ic->SetProperty("filter", "drop-shadow(" + col + " 0 0 20dp)"); ic->SetProperty("visibility", tier == "krillion" ? "hidden" : "visible"); }
  if (auto* e = el("land-tier")) { e->SetInnerRML(t ? esc(upper(t->name)) : "NOTHING LANDED"); e->SetProperty("color", col); e->SetProperty("font-effect", "glow(0dp 14dp 0dp 0dp " + col + "), shadow(0dp 3dp #00000099)"); }
  setText("land-answer", S(result, "answer").empty() ? "—" : "“" + S(result, "answer") + "”");
  int score = I(result, "score"); setRml("land-points", "<b style=\"color:" + col + "\">+" + std::to_string(score) + " PTS</b>" + (score ? " · sink " + commas(score * 10) + "m" : ""));
  std::string quip = S(result, "quip"); if (quip.empty()) quip = t ? t->blurb : (S(result, "rawAnswer").empty() ? "the clock beat you to it." : "never made it onto the reef.");
  setText("land-quip", quip); setText("next-btn", game_.value("completed", false) ? "SURFACE ▲" : "DESCEND ▼");
  if (t) sound_->event(tier);
  scene_->target = currentScore() * 10.f; scene_->roundIndex = I(game_, "round");
  if (!scene_->fx.on) scene_->fx = {tier, 1, 0, 0, true};
  scene_->moveCamera(currentScore() * 10.f + 30, 500);
  bool gold = tier == "krillion"; visualScore_ = oldScore;
  if (gold) { celebrateUntil_ = now_ + 1250; scene_->goldLandAt = now_; goldTimer_ = now_ + 1000; pendingGold_ = {{"round", result["round"]}, {"old", oldScore}}; }
  else animateScore(oldScore, currentScore(), false);
  if (tier == "rare" || tier == "tooclever") shakeUntil_ = now_ + 500;
  if (score >= 60) { scene_->burst(col, score == 100 ? 36 : 22); if (auto* f = el("flash")) f->SetProperty("background-color", col); flashUntil_ = now_ + 600; }
  if (!gold) syncBuddyGold(); updateHud();
}
void App::next() {
  if (busy_ || !game_.is_object()) return;
  if (game_.value("completed", false)) { sound_->event("surface"); double d = std::min(9000.0, std::max(2500.0, (currentScore() * 10.0 + 30) * 3)); scene_->moveCamera(-45, d, 1); showResults(); return; }
  busy_ = true; sound_->event("begin");
  api("POST", "next", {{"runId", game_["runId"]}}, [this](Response r) { busy_ = false; if (!r.ok()) { setText("land-quip", r.error); return; } setGame(r.body); showRound(); }, true);
}
void App::syncBuddyGold() {
  bool gold = false; if (game_.is_object()) for (auto& r : game_["results"]) if (tierStr(r["tier"]) == "krillion") gold = true;
  setClass(el("mascot"), "gold", gold);
  if (settings_.buddy == "krill") if (auto* img = el("mascot-img")) img->SetAttribute("src", gold ? "../sprites/krill-gold.svg" : "../sprites/krill.svg");
}
std::string App::resultActions() {
  std::string m = S(game_, "mode");
  auto b = [](const std::string& action, const std::string& label) { return "<button class=\"quiet\" data-action=\"" + action + "\">" + label + "</button>"; };
  if (m == "unlimited") return b("unlimited-next", "NEXT DIVE ∞") + b("daily", "DAILY DIVE ↓") + b("packs", "PACKS ▤");
  if (m == "pack") { const json& p = game_["pack"]; bool more = I(p, "chapter") + 1 < I(p, "chapters"); std::string title = S(game_, "title"); auto dot = title.find("· "); std::string unit = dot == std::string::npos ? "CHAPTER" : upper(title.substr(dot + 3, title.find(' ', dot + 3) - dot - 3)); return (more ? b("pack-next", "NEXT " + unit + " →") : "") + b("packs", "ALL PACKS ▤") + b("unlimited", "UNLIMITED ∞"); }
  if (m == "archive") return b("archive", "ARCHIVE ⟲") + b("unlimited", "UNLIMITED ∞") + b("friends", "FRIENDS ☻");
  return b("unlimited", "KEEP DIVING ∞") + b("friends", "FRIENDS ☻") + b("archive", "ARCHIVE ⟲");
}
void App::showResults() {
  setPhase("done"); visualScore_ = currentScore(); syncBuddyGold(); scene_->setDepth(0); review_ = nullptr;
  api("GET", "logbook", {}, [this](Response lr) {
    if (lr.ok()) logbook_ = lr.body;
    json stats = logbook_.is_object() ? logbook_["stats"] : json{{"current", 0}, {"played", 0}, {"average", 0}, {"best", 0}};
    int score = currentScore(); const Band& band = bandFor(score);
    std::string bands; for (auto& b : BANDS) bands += std::string("<li class=\"") + (&b == &band ? "current" : "") + "\">" + tierImage(b.tier) + "<span class=\"band-range\" style=\"color:" + tierById(b.tier)->color + "\">" + std::to_string(b.min) + (b.min == 450 ? "+" : "–" + std::to_string(b.max)) + "</span><span>" + b.name + ". " + b.text + "</span></li>";
    std::string reviews; int i = 0;
    for (auto& r : game_["results"]) { std::string tier = tierStr(r["tier"]); reviews += "<li><button class=\"review-toggle\" data-review=\"" + std::to_string(i) + "\"><span class=\"review-number\">" + std::to_string(i + 1) + "</span><span class=\"review-dot\">" + tierImage(tier) + "</span><span class=\"review-question\">" + esc(S(game_["prompts"][i], "text")) + "<small>" + esc(S(r, "answer").empty() ? "—" : S(r, "answer")) + "</small></span><span class=\"review-points\" style=\"color:" + tierColor(r["tier"]) + "\">" + std::to_string(I(r, "score")) + "</span><span class=\"review-sign\" id=\"sign-" + std::to_string(i) + "\">+</span></button><div class=\"review-expanded\" id=\"review-" + std::to_string(i) + "\" hidden=\"hidden\"></div></li>"; i++; }
    std::string rating; for (int n = 1; n <= 5; n++) rating += "<button data-rating=\"" + std::to_string(n) + "\">" + std::to_string(n) + "</button>";
    std::string rml = "<div class=\"results-inner\"><div class=\"result-header\"><span class=\"result-brand\">SOURCEKRILLION</span><span>" + esc(S(game_, "title")) + " complete</span></div><div class=\"result-score\"><strong>" + std::to_string(score) + "</strong><span>-" + commas(score * 10) + "m</span></div><div class=\"distribution\" id=\"distribution\"><p>Sounding this ocean’s depths…</p></div>"
      "<div class=\"rule\"><span>DIVE LOG · DEEPER = RARER</span><div class=\"rule-line\"></div></div>" + diveChart(game_["results"]) + "<div class=\"rule\"><span>THE BEARING</span><div class=\"rule-line\"></div></div><ul class=\"band-list\">" + bands + "</ul>"
      "<div class=\"share-actions\"><button class=\"quiet\" id=\"copy-result\" data-action=\"copy-result\">Copy result</button></div><div class=\"actions\">" + resultActions() + "</div><div class=\"rule\"><span>THE CATCH · TAP A ROUND FOR EVERY ANSWER</span><div class=\"rule-line\"></div></div><ul class=\"answer-review\">" + reviews + "</ul>"
      "<div class=\"rule\"><span>LOGBOOK</span><div class=\"rule-line\"></div></div><p class=\"logbook-line\">streak " + std::to_string(I(stats, "current")) + " · played " + std::to_string(I(stats, "played")) + " · avg " + std::to_string(I(stats, "average")) + " · best " + std::to_string(I(stats, "best")) + "</p><p class=\"reset-countdown\" id=\"reset-countdown\"></p>"
      "<div class=\"inline-form\"><label>got an idea for a question? tell us what to ask</label><div class=\"row\"><input type=\"text\" id=\"suggestion\" maxlength=\"1000\" placeholder=\"name a type of cloud…\" data-enter=\"suggest\"/><button class=\"quiet\" data-action=\"suggest\">submit</button></div><p class=\"muted\" id=\"suggestion-status\"></p></div>"
      "<p class=\"muted\">how was that dive? 1 = meh · 5 = great</p><div class=\"rating\">" + rating + "</div><p class=\"muted\" id=\"rating-status\"></p>"
      "<div class=\"original-footer\"><p>one in a krillion. Open source. Open ocean.</p><div class=\"footer-nav\"><button data-action=\"faq\">FAQ</button><button data-action=\"privacy\">Privacy</button><button data-action=\"about\">About</button></div><p>Independent Krillion-inspired game. Not affiliated with krillion.io.</p></div></div>";
    setRml("results", rml); if (auto* r = el("results")) r->SetScrollTop(0);
    if (auto* p = el("distribution")) { (void)p; }
    std::string mode = S(game_, "mode"); bool ranked = mode == "daily" || mode == "archive";
    api("GET", "review?run=" + S(game_, "runId"), {}, [this, ranked, score, mode](Response rr) {
      if (phase_ != "done") return;
      if (!rr.ok()) { setRml("distribution", "<p>Comparison temporarily unavailable. Your score is saved.</p>"); return; }
      review_ = rr.body; std::vector<int> dist; if (ranked) for (auto& d : review_["dist"]) dist.push_back(d.get<int>());
      static int n = 0; std::string name = "mem:dist-" + std::to_string(++n) + ".svg"; g_memoryFiles[name] = distributionSvg(dist, score, I(review_, "population"));
      std::string axis; for (int k = 0; k < 8; k++) axis += "<span style=\"left:" + std::to_string(k * 64) + "dp\">" + std::to_string(k * 100) + "</span>";
      std::string you = "<div class=\"dist-you\" style=\"left:" + std::to_string((int)std::lround(std::clamp(score / 700.0 * 448, 12.0, 436.0))) + "dp\">YOU</div>";
      std::string caption = !ranked ? (mode == "pack" ? esc(S(game_["pack"], "name")) + " · " + esc(S(game_["pack"], "chapterTitle")) : std::string("Unlimited dive · your daily score stays unchanged.")) : review_["betterThan"].is_null() ? std::string("YOUR SCORE · MORE DIVERS NEEDED FOR COMPARISON") : "BETTER THAN " + std::to_string(I(review_, "betterThan")) + "% OF " + std::to_string(I(review_, "population")) + " OTHER DIVERS";
      setRml("distribution", "<svg src=\"" + name + "\"/><div class=\"dist-axis\">" + axis + "</div>" + you + "<p>" + caption + "</p>");
    }, true);
  }, false);
}
void App::pauseGame() { if (!game_.is_object() || (phase_ != "preview" && phase_ != "answering") || paused_) return; paused_ = true; api("POST", "pause", {{"runId", game_["runId"]}, {"paused", true}}, [this](Response r) { if (r.ok()) setGame(r.body); else paused_ = false; }, true); }
void App::resumeGame() { if (!panel_.empty() || !paused_) return; api("POST", "pause", {{"runId", game_["runId"]}, {"paused", false}}, [this](Response r) { if (r.ok()) setGame(r.body); paused_ = false; tick(); }, true); }

void App::tick() {
  auto now = [this]() { return nowMs() + clockAtServer_; };
  if (game_.is_object() && (phase_ == "preview" || phase_ == "answering") && !paused_ && game_["roundStart"].is_number()) {
    double start = game_["roundStart"].get<double>(), remaining = start + ROUND_MS - now();
    if (now() < start) { int count = std::max(1, (int)std::ceil((start - now()) / 1000)); setText("preview-label", "descending · the clock starts in " + std::to_string(count)); setText("timer-number", std::to_string(count)); if (auto* b = el("time-bar")) b->SetProperty("width", "100%"); }
    else {
      if (phase_ == "preview") { setPhase("answering"); if (auto* in = el("answer")) { in->RemoveAttribute("disabled"); pendingFocus_ = "answer"; } }
      setClass(el("submit-btn"), "disabled", busy_ || trim(inputValue(el("answer"))).empty());
      int seconds = std::max(0, (int)std::ceil(remaining / 1000)); bodyClass("timer-hot", remaining <= 3050);
      setText("timer-number", std::to_string(seconds)); if (auto* b = el("time-bar")) b->SetProperty("width", std::to_string(std::max(0.0, remaining / ROUND_MS) * 100) + "%");
      double urgency = seconds <= 3 && !scene_->reduced ? (1 - seconds / 4.0) * (.35 + std::sin(nowMs() * .012) * .2) : 0; if (auto* g = el("danger-glow")) g->SetProperty("opacity", std::to_string(urgency));
      if (seconds > 0 && seconds <= 3 && seconds != lastTick_) { sound_->event("tick"); lastTick_ = seconds; }
      if (remaining <= 0 && !autoSent_ && !busy_) { autoSent_ = true; submit(true); }
    }
  } else if (!partyOn_) if (auto* g = el("danger-glow")) g->SetProperty("opacity", "0");
  if (game_.is_object() && el("reset-countdown")) { double d = std::max(0.0, (resetAt(etDate(nowMs())) - nowMs()) / 1000.0); int h = (int)(d / 3600), m = (int)std::fmod(d, 3600) / 60, s = (int)std::fmod(d, 60); setText("reset-countdown", "next dive in " + std::to_string(h) + ":" + pad2(m) + ":" + pad2(s)); }
}

// ---------- per-frame ----------
void App::update(double now, double dt) {
  now_ = now; net_->pump();
  if (!pendingFocus_.empty()) { if (auto* f = el(pendingFocus_)) { if (f->Focus()) pendingFocus_.clear(); } else pendingFocus_.clear(); }
  scene_->now = now;
  if (auto* intro = el("intro")) scene_->introTop = intro->GetAbsoluteOffset(Rml::BoxArea::Border).y / ctx_->GetDensityIndependentPixelRatio();
  float dp = ctx_->GetDensityIndependentPixelRatio();
  auto px = [dp](float v) { return std::to_string(v) + "dp"; }; (void)dp;
  // plunge animation (port of the requestAnimationFrame loop)
  if (plunge_.on) {
    float p = (float)std::min(1.0, (now - plunge_.began) / plunge_.duration), e = p < .5f ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3.f) / 2;
    std::string tier = tierStr(plunge_.result["tier"]); int score = I(plunge_.result, "score");
    scene_->drop(plunge_.oldScore * 10.f, tier, score, p);
    float sway = tier == "plankton" ? 36 * std::sin(p * 7) * (1 - .25f * p) : tier == "tooclever" ? 7 * std::sin(p * 42) : std::sin(p * (tier == "krillion" ? 24 : 16)) * (1 - p) * (tier == "krillion" ? 26 : 14);
    if (auto* f = el("falling-answer")) { auto l = scene_->layout(); f->SetProperty("left", px(scene_->layout().center + 24 + sway - f->GetOffsetWidth() / dp / 2)); f->SetProperty("top", px(l.height / 2 - 60 - f->GetOffsetHeight() / dp / 2)); f->SetProperty("transform", "rotate(" + std::to_string(sway * (tier == "plankton" ? .55f : .4f)) + "deg)"); }
    float travelled = 90 + (score * 10 - 90) * e; int i = 0;
    for (auto& t : TIERS) { if (auto* line = el(std::string("line-") + t.id)) { line->SetProperty("top", px(scene_->worldY(scene_->fx.startDepth + t.score * 10))); if (travelled >= t.score * 10 - 4 && !line->IsClassSet("active")) { line->SetClass("active", true); line->SetProperty("decorator", "none"); line->SetProperty("border-top", std::string("2dp ") + t.color); line->SetProperty("box-shadow", std::string(t.color) + " 0 0 16dp 0"); if (auto* sp = line->GetFirstChild()) { sp->SetProperty("color", t.color); sp->SetProperty("font-effect", std::string("glow(0dp 9dp 0dp 0dp ") + t.color + ")"); } if (i > plunge_.lastPassed) { sound_->event("tier"); plunge_.lastPassed = i; } } } i++; }
    if (p >= 1) { plunge_.on = false; scene_->cameraDepth = scene_->cameraTarget; showLanding(plunge_.result, plunge_.oldScore); }
  }
  if (goldTimer_ && now >= goldTimer_) { goldTimer_ = 0; if (phase_ == "landed" && I(game_["results"].back(), "round") == I(pendingGold_, "round")) { syncBuddyGold(); scene_->burst("#ffd166", 42); animateScore(I(pendingGold_, "old"), currentScore(), true); } }
  if (scoreAnim_.on) { double p = std::min(1.0, (now - scoreAnim_.start) / scoreAnim_.duration), e = 1 - std::pow(1 - p, 4); visualScore_ = scoreAnim_.from + (scoreAnim_.to - scoreAnim_.from) * e; scene_->depth = (float)visualScore_ * 10; setText("score", std::to_string((int)std::lround(visualScore_))); if (p >= 1) { scoreAnim_.on = false; visualScore_ = scoreAnim_.to; if (scoreAnim_.gold) { setClass(el("score"), "slam", true); slamUntil_ = now + 520; } } }
  if (slamUntil_ && now > slamUntil_) { slamUntil_ = 0; setClass(el("score"), "slam", false); }
  if (promptExitAt_ && now > promptExitAt_) { promptExitAt_ = 0; if (phase_ == "plunge") show("prompt-card", false); }
  scene_->tick(now, dt);
  // things that ride on the ocean
  if (auto* hero = el("hero")) { hero->SetProperty("top", px(scene_->heroTop)); hero->SetProperty("opacity", std::to_string(scene_->heroOpacity)); }
  if (phase_ == "preview" || phase_ == "answering" || phase_ == "plunge") if (auto* c = el("prompt-card")) c->SetProperty("top", px(scene_->cardTop));
  if (phase_ == "landed") if (auto* l = el("landing")) l->SetProperty("top", px(scene_->landingTop));
  if (auto* m = el("mascot")) {
    float size = scene_->mascotSize; m->SetProperty("left", px(scene_->mascotX)); m->SetProperty("top", px(scene_->mascotY)); m->SetProperty("width", px(size)); m->SetProperty("height", px(size));
    double t = now / 1000; std::string tr = "none";
    if (now < celebrateUntil_) { double k = 1 - (celebrateUntil_ - now) / 1200; tr = k < .5 ? "translateY(" + std::to_string(-60 * k) + "dp) rotate(" + std::to_string(360 * k) + "deg)" : "translateY(" + std::to_string(-60 * (1 - k)) + "dp) rotate(" + std::to_string(360 * k) + "deg)"; }
    else if (now < pokedUntil_) tr = "rotate(" + std::to_string(360 * (1 - (pokedUntil_ - now) / 900)) + "deg)";
    else if (phase_ == "plunge" && doc_->GetAttribute<Rml::String>("data-tier", "") == "plankton") tr = "translateY(" + std::to_string(2.5 - 2.5 * std::cos(t * 2.24)) + "dp) rotate(" + std::to_string(-6 + 6 * std::cos(t * 2.24)) + "deg)";
    if (auto* img = el("mascot-img")) img->SetProperty("transform", tr);
    setClass(m, "poked", now < pokedUntil_);
  }
  // hero letters float, sonar sweep, flash and shake
  if (!settings_.lowPower) for (int i = 0; WORD[i]; i++) if (auto* s = el("letter-" + std::to_string(i))) { double ph = std::fmod(now / 1000 - i * .18, 3.2) / 3.2; if (ph < 0) ph += 1; s->SetProperty("transform", "translateY(" + std::to_string(-1.5 + 1.5 * std::cos(ph * 2 * M_PI)) + "dp)"); }
  if (auto* s = el("sonar")) s->SetProperty("transform", "rotate(" + std::to_string(std::fmod(now / (doc_->IsClassSet("timer-hot") ? 700.0 : 2400.0), 1.0) * 360) + "deg)");
  if (auto* f = el("flash")) f->SetProperty("opacity", std::to_string(now < flashUntil_ ? .5 * (flashUntil_ - now) / 600 : 0));
  if (auto* g = el("game")) { if (now < shakeUntil_) { int k = (int)((shakeUntil_ - now) / 88) % 4; static const int dx[] = {2, -3, 4, -4}, dy[] = {1, 0, -2, 2}; g->SetProperty("transform", "translate(" + std::to_string(dx[k]) + "dp, " + std::to_string(dy[k]) + "dp)"); } else g->RemoveProperty("transform"); }
  if (auto* lbl = el("preview-label")) lbl->SetProperty("margin-left", px(-lbl->GetOffsetWidth() / dp / 2));
  if (auto* p = el("panel")) { p->SetProperty("margin-left", px(-p->GetOffsetWidth() / dp / 2)); p->SetProperty("margin-top", px(-p->GetOffsetHeight() / dp / 2)); }
  if (game_.is_object() && phase_ != "intro") setText("depth", commas(std::lround(scene_->depth)) + "m");
  // placeholders (RmlUi inputs have no placeholder attribute)
  if (auto* ph = el("answer-ph")) ph->SetProperty("display", inputValue(el("answer")).empty() ? "inline" : "none");
  if (auto* ph = el("party-ph")) ph->SetProperty("display", inputValue(el("party-answer")).empty() ? "inline" : "none");
  static double lastTickAt = 0; if (now - lastTickAt > 80) { lastTickAt = now; tick(); }
  // party puffer inflation
  if (partyOn_ && partyRoom_.is_object() && S(partyRoom_, "status") == "playing" && partyRoom_["round"].is_object() && partyRoom_["round"]["popped"].is_null()) {
    if (auto* img = el("puffer-img")) { double elapsed = nowMs() + partyClock_ - partyRoom_["round"]["startedAt"].get<double>(), f = std::min(1.0, elapsed / partyRoom_["round"]["maxFuseMs"].get<double>()); bool danger = elapsed > partyRoom_["round"]["minFuseMs"].get<double>() * .75; double wob = danger ? std::sin(nowMs() * .05) * (2 + f * 6) : 0; img->SetProperty("transform", "scale(" + std::to_string(.8 + f * .9) + ") rotate(" + std::to_string(wob) + "deg)"); img->SetClass("danger", danger); if (auto* g = el("danger-glow")) g->SetProperty("opacity", std::to_string(danger ? f * .6 : 0)); }
  }
  if (partyOn_ && !partyRoom_.is_object() && now - lobbyAt_ > 5000) refreshLobby();
}

// ---------- events ----------
void App::ProcessEvent(Rml::Event& e) {
  Rml::Element* t = e.GetTargetElement();
  if (e.GetId() == Rml::EventId::Keydown) {
    auto key = (Rml::Input::KeyIdentifier)e.GetParameter<int>("key_identifier", 0);
    if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
      if (t && t->GetId() == "answer") submit(false);
      else if (t && t->HasAttribute("data-enter")) doAction(t->GetAttribute<Rml::String>("data-enter", ""), t);
    } else if (key == Rml::Input::KI_ESCAPE && !panel_.empty()) closePanel();
    else if (t && t->GetId() == "answer") { correction_.clear(); show("answer-status", false); }
    return;
  }
  if (e.GetId() == Rml::EventId::Change) {
    if (t && t->GetId() == "answer") { setClass(el("submit-btn"), "disabled", busy_ || trim(inputValue(t)).empty()); if (auto* in = el("answer")) { auto b = in->GetAbsoluteOffset(); scene_->typeAt(b.x / ctx_->GetDensityIndependentPixelRatio() + 14 + std::min(11.f * inputValue(t).size(), in->GetOffsetWidth() / ctx_->GetDensityIndependentPixelRatio() - 28), b.y / ctx_->GetDensityIndependentPixelRatio() + 5); } }
    return;
  }
  for (Rml::Element* n = t; n && n != doc_; n = n->GetParentNode()) {
    if (n->HasAttribute("data-action")) { doAction(n->GetAttribute<Rml::String>("data-action", ""), n); return; }
    if (n->HasAttribute("data-review")) {
      int i = n->GetAttribute<int>("data-review", 0); auto* box = el("review-" + std::to_string(i)); if (!box) return;
      bool open = !box->HasAttribute("hidden"); show("review-" + std::to_string(i), !open); setText("sign-" + std::to_string(i), open ? "+" : "−");
      if (open || !review_.is_object()) return;
      std::string own = S(game_["results"][i], "answer"), list;
      for (auto& a : review_["prompts"][i]["answers"]) list += "<li style=\"color:" + tierColor(a["tier"]) + "\"><span>" + esc(S(a, "answer")) + (S(a, "answer") == own ? "<span class=\"you\">YOU</span>" : "") + "</span><span>+" + std::to_string(I(a, "score")) + "</span></li>";
      box->SetInnerRML("<ul class=\"review-list\">" + list + "</ul>"); return;
    }
    if (n->HasAttribute("data-rating")) { int v = n->GetAttribute<int>("data-rating", 0); api("POST", "feedback", {{"kind", "rating"}, {"text", std::to_string(v)}}, [this, n](Response r) { if (r.ok()) { if (auto* p = n->GetParentNode()) for (int k = 0; k < p->GetNumChildren(); k++) p->GetChild(k)->SetClass("on", p->GetChild(k) == n); setText("rating-status", "Thanks for the feedback."); } else setText("rating-status", r.error); }); return; }
    if (n->HasAttribute("data-archive")) { std::string d = n->GetAttribute<Rml::String>("data-archive", ""); closePanel(); begin({{"mode", "archive"}, {"date", d}}); return; }
    if (n->HasAttribute("data-chapter")) { if (n->IsClassSet("locked")) return; std::string pack = n->GetAttribute<Rml::String>("data-pack", ""); int ch = n->GetAttribute<int>("data-chapter", 0); closePanel(); begin({{"mode", "pack"}, {"pack", pack}, {"chapter", ch}}); return; }
    if (n->HasAttribute("data-buddy")) { if (n->IsClassSet("locked")) return; settings_.buddy = n->GetAttribute<Rml::String>("data-buddy", "krill"); saveSettings(); applySettings(); openWardrobe(); return; }
    if (n->HasAttribute("data-setting")) {
      std::string k = n->GetAttribute<Rml::String>("data-setting", ""); bool* v = k == "lowPower" ? &settings_.lowPower : k == "colorblind" ? &settings_.colorblind : k == "corrections" ? &settings_.corrections : k == "clearRejected" ? &settings_.clearRejected : &settings_.shareLink;
      *v = !*v; n->SetClass("on", *v); saveSettings(); applySettings(); return;
    }
    if (n->HasAttribute("data-party-create")) { createRoom(n->GetAttribute<Rml::String>("data-party-create", "") == "public"); return; }
    if (n->HasAttribute("data-party-join")) { joinRoom(n->GetAttribute<Rml::String>("data-party-join", "")); return; }
    if (n->HasAttribute("data-unfriend")) { api("POST", "friends/remove", {{"code", n->GetAttribute<Rml::String>("data-unfriend", "")}}, [this](Response) { openFriends(); }); return; }
    if (n->HasAttribute("data-faq")) { int i = n->GetAttribute<int>("data-faq", 0); show("faq-" + std::to_string(i), el("faq-" + std::to_string(i)) && el("faq-" + std::to_string(i))->HasAttribute("hidden")); return; }
  }
}
void App::doAction(const std::string& a, Rml::Element* target) {
  static const std::set<std::string> panels = {"logbook", "archive", "packs", "friends", "faq", "settings", "menu", "wardrobe"};
  if (a == "close") { closePanel(); return; }
  if (!panels.count(a) && a != "make-restore" && a != "restore" && a != "save-name" && a != "add-friend" && a != "copy-code" && a != "save-server") closePanel();
  if (a == "menu") openMenu(); else if (a == "settings") openSettings(); else if (a == "wardrobe") openWardrobe(); else if (a == "logbook") openLogbook();
  else if (a == "archive") openArchive(); else if (a == "packs") openPacks(); else if (a == "friends") openFriends(); else if (a == "faq") openFaq();
  else if (a == "begin" || a == "daily") { closeParty(); resumeDaily(); }
  else if (a == "unlimited") begin({{"mode", "unlimited"}});
  else if (a == "unlimited-next") begin({{"mode", "unlimited"}, {"fresh", true}});
  else if (a == "pack-next") begin({{"mode", "pack"}, {"pack", game_["pack"]["id"]}, {"chapter", I(game_["pack"], "chapter") + 1}});
  else if (a == "party") openParty();
  else if (a == "submit") submit(false);
  else if (a == "next") next();
  else if (a == "mute") { sound_->init(); settings_.muted = !settings_.muted; saveSettings(); applySettings(); }
  else if (a == "poke") { sound_->init(); sound_->event("blub"); pokedUntil_ = now_ + 1000; }
  else if (a == "howto") { bool open = el("how-list")->HasAttribute("hidden"); show("how-list", open); setText("how-summary", open ? "▾ HOW TO PLAY" : "▸ HOW TO PLAY"); }
  else if (a == "privacy" || a == "about") { if (openUrl) openUrl(net_->base() + "/" + a); }
  else if (a == "copy-result" && game_.is_object()) {
    std::string s = "SourceKrillion " + S(game_, "shareLabel", "#" + std::to_string(I(game_, "dayNumber"))) + " 🦐\n" + std::to_string(currentScore()) + "/700 · " + std::to_string(currentScore() * 10) + "m\n";
    for (auto& r : game_["results"]) { auto* t = tierById(tierStr(r["tier"])); s += t ? t->emoji : "⬛"; }
    if (settings_.shareLink) s += "\n" + net_->base();
    if (setClipboard) setClipboard(s); setText("copy-result", "Copied ✓");
  }
  else if (a == "suggest") { std::string v = inputValue(el("suggestion")); api("POST", "feedback", {{"kind", "suggestion"}, {"text", v}}, [this](Response r) { setText("suggestion-status", r.ok() ? "Thanks — noted." : r.error); if (r.ok()) static_cast<Rml::ElementFormControl*>(el("suggestion"))->SetValue(""); }); }
  else if (a == "make-restore") api("POST", "restore-code", json::object(), [this](Response r) { if (r.ok()) renderSync(S(r.body, "code")); else setText("restore-status", r.error); });
  else if (a == "restore") api("POST", "restore", {{"code", inputValue(el("restore-input"))}}, [this](Response r) { setText("restore-status", r.ok() ? "Logbook restored on this device." : r.error); if (r.ok()) { game_ = nullptr; setPhase("intro"); } });
  else if (a == "save-name") api("POST", "me", {{"name", inputValue(el("name-input"))}}, [this](Response) { openFriends(); });
  else if (a == "add-friend") api("POST", "friends/add", {{"code", inputValue(el("friend-input"))}}, [this](Response r) { if (r.ok()) openFriends(); else setText("friend-status", r.error); });
  else if (a == "copy-code") { if (setClipboard && el("friend-code")) setClipboard(el("friend-code")->GetInnerRML()); if (target) target->SetInnerRML("copied ✓"); }
  else if (a == "save-server") { std::string v = trim(inputValue(el("server-input"))); if (v.rfind("http", 0) != 0) v = "https://" + v; net_->setBase(v); std::ofstream(dataDir_ + "/server.txt") << v; setText("server-status", "Saved. Reconnecting…"); net_->get("bundle", [this](Response r) { if (r.ok()) off_->storeBundle(r.body); setText("server-status", r.ok() ? "Connected." : "Could not reach that server."); }); api("GET", "today", {}, [this](Response r) { if (r.ok()) { today_ = r.body; setText("day-number", "DIVE #" + std::to_string(I(today_, "dayNumber"))); } }); }
  else if (a == "party-close") closeParty();
  else if (a == "party-start") net_->sendSocket({{"t", "start"}});
  else if (a == "party-reset") net_->sendSocket({{"t", "reset"}});
  else if (a == "party-join") joinRoom(inputValue(el("party-code")));
  else if (a == "party-pass") { std::string v = trim(inputValue(el("party-answer"))); if (!v.empty()) { net_->sendSocket({{"t", "answer"}, {"text", v}}); static_cast<Rml::ElementFormControl*>(el("party-answer"))->SetValue(""); } }
  else if (a == "copy-room" && partyRoom_.is_object()) { if (setClipboard) setClipboard(net_->base() + "/party?room=" + S(partyRoom_, "code")); if (target) target->SetInnerRML("copied ✓"); }
}

// ---------- panels ----------
void App::openPanel(const std::string& rml, bool wide) {
  std::string close = "<button class=\"close\" data-action=\"close\">×</button>";
  setRml("panel", close + rml); setClass(el("panel"), "wide", wide); show("layer", true);
  bool wasEmpty = panel_.empty(); panel_ = "open"; if (wasEmpty) pauseGame();
}
void App::closePanel() { if (panel_.empty()) return; panel_.clear(); show("layer", false); resumeGame(); }
void App::openMenu() {
  auto b = [](const char* a, const char* l) { return std::string("<button data-action=\"") + a + "\">" + l + "</button>"; };
  openPanel(std::string("<p class=\"eyebrow\">SOURCEKRILLION</p><h2>Menu</h2><div class=\"rule\"><span>DIVE IN</span><div class=\"rule-line\"></div></div><div class=\"menu-nav\">") + b("daily", "↓ Daily dive") + b("unlimited", "∞ Unlimited") + b("party", "◉ Puffer Party") + b("archive", "⟲ Archive") + b("packs", "▤ Themed packs") + "</div><div class=\"rule\"><span>YOU</span><div class=\"rule-line\"></div></div><div class=\"menu-nav\">" + b("logbook", "☰ My SourceKrillion") + b("friends", "☻ Friends") + b("settings", "⚙ Settings") + b("faq", "? FAQ") + "</div><p class=\"panel-signoff\">one in a krillion.</p>");
}
void App::openSettings() {
  struct Row { const char* key; const char* title; const char* help; bool on; };
  Row rows[] = {{"lowPower", "Low-power mode", "Lighter graphics and fewer background animations.", settings_.lowPower}, {"colorblind", "Colorblind mode", "Brighter text and more separated tier colors.", settings_.colorblind}, {"corrections", "Answer corrections", "Preview corrected answers before scoring.", settings_.corrections}, {"clearRejected", "Clear rejected entries", "Empty the answer box after a failed attempt.", settings_.clearRejected}, {"shareLink", "Share link", "Add a link to shared results.", settings_.shareLink}};
  std::string rml = "<h2>Settings</h2>"; for (auto& r : rows) rml += std::string("<div class=\"setting") + (r.on ? " on" : "") + "\" data-setting=\"" + r.key + "\"><span><strong>" + r.title + "</strong><small>" + r.help + "</small></span><span class=\"toggle\"><i></i></span></div>";
  rml += "<div class=\"rule\"><span>YOUR LOGBOOK</span><div class=\"rule-line\"></div></div><button class=\"quiet\" data-action=\"logbook\">My dives →</button><p class=\"muted\">Saved on the server. Use a restore code to bring it to another device.</p><div class=\"inline-form\"><label>server address</label><div class=\"row\"><input type=\"text\" id=\"server-input\" value=\"" + esc(net_->base()) + "\" data-enter=\"save-server\"/><button class=\"quiet\" data-action=\"save-server\">save</button></div><p class=\"muted\" id=\"server-status\"></p></div>";
  openPanel(rml);
}
void App::openWardrobe() {
  api("GET", "logbook", {}, [this](Response r) {
    int best = r.ok() ? I(r.body["stats"], "bestStreak") : 0; std::string list;
    for (auto& [id, name, streak] : BUDDIES) list += "<button data-buddy=\"" + id + "\" class=\"" + (settings_.buddy == id ? "on" : "") + (best < streak ? " locked disabled" : "") + "\"><svg src=\"../sprites/" + id + ".svg\"/><span>" + name + "</span><small>" + (best >= streak ? std::string("Earned") : std::to_string(streak) + "-day streak") + "</small></button>";
    openPanel("<h2>Dive buddy</h2><div class=\"buddy-list\">" + list + "</div><p class=\"muted\">Earn buddies with your best daily streak.</p>");
  });
}
void App::openLogbook() {
  openPanel("<p class=\"eyebrow\">YOUR DIVER’S LOGBOOK</p><h2>My SourceKrillion</h2><p class=\"muted\">Your dives. One history. Every device.</p><div id=\"logbook-content\"><p class=\"muted\">Opening your logbook…</p></div><div class=\"rule\"><span>EVERY DEVICE</span><div class=\"rule-line\"></div></div><div id=\"sync-content\"></div><p class=\"muted\">Streaks count consecutive daily dives, with each new day starting at midnight ET. Archive replays don’t extend your streak.</p>");
  api("GET", "logbook", {}, [this](Response r) {
    if (!r.ok()) { setText("logbook-content", r.error); return; }
    logbook_ = r.body; auto& s = logbook_["stats"]; std::string list; int n = 0;
    for (auto& run : logbook_["runs"]) { if (n++ >= 40) break; std::string m = S(run, "mode"), label = m == "daily" ? "Dive #" + std::to_string(I(run, "dayNumber")) : m == "archive" ? "Archive #" + std::to_string(I(run, "dayNumber")) : m == "unlimited" ? "∞ Dive #" + std::to_string(I(run, "number")) : "Pack"; list += "<li><span>" + label + " <small>" + esc(S(run, "date")) + "</small></span><span>" + std::to_string(I(run, "score")) + " pts · " + commas(I(run, "score") * 10) + "m</span></li>"; }
    if (list.empty()) list = "<li>Your next dive starts your logbook.</li>";
    setRml("logbook-content", "<div class=\"stats-grid\"><div><strong>" + std::to_string(I(s, "current")) + "</strong><span>Current streak</span></div><div><strong>" + std::to_string(I(s, "bestStreak")) + "</strong><span>Best streak</span></div><div><strong>" + std::to_string(I(s, "played")) + "</strong><span>Daily dives</span></div><div><strong>" + std::to_string(I(s, "best")) + "</strong><span>Best daily score</span></div></div><ul class=\"history-list\">" + list + "</ul>");
    renderSync("");
  });
}
void App::renderSync(const std::string& code) {
  api("GET", "me", {}, [this, code](Response r) {
    bool has = r.ok() && r.body.value("hasRestoreCode", false);
    std::string rml = code.empty() ? "<p class=\"muted\">" + std::string(has ? "You have a restore code. Make a new one if you lost it (the old one stops working)." : "Make a restore code to open this logbook on another device.") + "</p>" : "<p class=\"restore-code\">" + esc(code) + "</p><p class=\"muted\">Write this down. Enter it on another device to open this logbook there. Making a new code replaces this one.</p>";
    rml += std::string("<button class=\"quiet\" data-action=\"make-restore\">") + (has || !code.empty() ? "New restore code" : "Make a restore code") + "</button><div class=\"inline-form\"><label>have a code from another device?</label><div class=\"row\"><input type=\"text\" id=\"restore-input\" maxlength=\"16\" placeholder=\"XXXX-XXXX-XXXX\" data-enter=\"restore\"/><button class=\"quiet\" data-action=\"restore\">restore</button></div><p class=\"muted\" id=\"restore-status\"></p></div>";
    setRml("sync-content", rml);
  });
}
void App::openArchive() {
  openPanel("<p class=\"eyebrow\">EVERY DIVE SO FAR</p><h2>Archive</h2><p class=\"muted\">Replay any past daily dive. Archive dives don’t extend your streak.</p><ul class=\"archive-list\" id=\"archive-list\"><li class=\"muted\">Opening the archive…</li></ul>", true);
  api("GET", "archive", {}, [this](Response r) {
    if (!r.ok()) { setRml("archive-list", "<li class=\"error\">" + esc(r.error) + "</li>"); return; }
    std::string list; static const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}, *mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (auto& d : r.body["days"]) { std::string date = S(d, "date"); int y = 0, m = 1, dd = 1; std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &dd); int w = (int)((daysFromCivil(y, m, dd) % 7 + 11) % 7); std::string pretty = std::string(wd[w]) + ", " + mo[m - 1] + " " + std::to_string(dd) + ", " + std::to_string(y); bool done = d.value("completed", false); list += "<li><button class=\"archive-day\" data-archive=\"" + date + "\"><span><strong>Dive #" + std::to_string(I(d, "dayNumber")) + "</strong><small>" + pretty + "</small></span><span class=\"go" + (done ? " done" : "") + "\">" + (done ? std::to_string(I(d, "score")) + " pts" : d.value("played", false) ? "resume →" : "dive →") + "</span></button></li>"; }
    setRml("archive-list", list.empty() ? "<li class=\"muted\">The archive opens tomorrow. Today’s dive is the first.</li>" : list);
  });
}
void App::openPacks() {
  openPanel("<p class=\"eyebrow\">THEMED PACKS · BEYOND THE DAILY DIVE</p><h2>Themed packs</h2><p class=\"muted\">Free · chapters unlock in order</p><div id=\"packs-list\"><p class=\"muted\">Loading packs…</p></div>", true);
  api("GET", "packs", {}, [this](Response r) {
    if (!r.ok()) { setRml("packs-list", "<p class=\"error\">" + esc(r.error) + "</p>"); return; }
    std::string rml;
    for (auto& p : r.body["packs"]) {
      std::string chapters; for (auto& c : p["chapters"]) { bool un = c.value("unlocked", false), done = c.value("completed", false); chapters += "<li><button class=\"chapter-row" + std::string(un ? "" : " locked") + "\" data-pack=\"" + S(p, "id") + "\" data-chapter=\"" + std::to_string(I(c, "index")) + "\"><span>" + std::to_string(I(c, "index") + 1) + ". " + esc(S(c, "title")) + "</span><span class=\"go\">" + (done ? std::to_string(I(c, "best")) + " pts" : un ? "dive →" : "locked") + "</span></button></li>"; }
      int total = (int)p["chapters"].size(); rml += "<div class=\"pack-card\"><p class=\"eyebrow\">" + esc(upper(S(p, "name"))) + "</p><p class=\"pack-tag\">" + std::to_string(total) + " " + esc(S(p, "unit")) + "s · " + esc(S(p, "tagline")) + "</p><div class=\"pack-meter\"><i style=\"width:" + std::to_string(total ? I(p, "completed") * 100 / total : 0) + "%\"></i></div><ul class=\"chapter-list\">" + chapters + "</ul></div>";
    }
    setRml("packs-list", rml);
  });
}
void App::openFriends() {
  if (panel_.empty() || !el("friends-content")) openPanel("<p class=\"eyebrow\">BETTER WITH COMPANY</p><h2>Friends</h2><div id=\"friends-content\"><p class=\"muted\">Opening friends…</p></div>", true);
  auto render = [this](Response r) {
    if (!r.ok()) { setRml("friends-content", "<p class=\"error\">" + esc(r.error) + "</p>"); return; }
    const json& v = r.body; std::string people; int i = 0;
    for (auto& p : v["people"]) { i++; bool you = p.value("you", false); std::string tiers; if (p["tiers"].is_array()) for (auto& t : p["tiers"]) tiers += tierById(tierStr(t)) ? "<svg src=\"../sprites/" + iconFor(tierStr(t)) + ".svg\"/>" : "<span class=\"miss-icon\"></span>"; people += std::string("<li class=\"") + (you ? "you" : "") + "\"><span class=\"rank\">" + (p["score"].is_null() ? "·" : std::to_string(i)) + "</span><span class=\"who\">" + esc(S(p, "name")) + (you ? "<span class=\"you-tag\">YOU</span>" : "") + "<small>" + (I(p, "streak") ? std::to_string(I(p, "streak")) + "-day streak" : std::string("no streak")) + " · " + esc(S(p, "code")) + "</small></span><span class=\"tiers\">" + tiers + "</span><span class=\"pts\">" + (p["score"].is_null() ? (p.value("played", false) ? "done" : "—") : std::to_string(I(p, "score"))) + "</span>" + (you ? "" : "<button class=\"unfriend\" data-unfriend=\"" + esc(S(p, "code")) + "\">×</button>") + "</li>"; }
    setRml("friends-content", "<div class=\"friend-code\"><span class=\"muted\">your friend code</span><strong id=\"friend-code\">" + esc(S(v["me"], "friendCode")) + "</strong><button class=\"quiet\" data-action=\"copy-code\">copy</button></div><div class=\"inline-form\"><label>your diver name</label><div class=\"row\"><input type=\"text\" id=\"name-input\" maxlength=\"18\" placeholder=\"name yourself…\" value=\"" + esc(S(v["me"], "name")) + "\" data-enter=\"save-name\"/><button class=\"quiet\" data-action=\"save-name\">save</button></div></div><div class=\"inline-form\"><label>add a friend by code</label><div class=\"row\"><input type=\"text\" id=\"friend-input\" maxlength=\"8\" placeholder=\"ABC123\" data-enter=\"add-friend\"/><button class=\"quiet\" data-action=\"add-friend\">add</button></div><p class=\"muted\" id=\"friend-status\"></p></div><div class=\"rule\"><span>TODAY · DIVE #" + std::to_string(I(v, "dayNumber")) + "</span><div class=\"rule-line\"></div></div>" + (v.value("revealed", false) ? "" : "<p class=\"muted\">Finish today’s dive to see your friends’ scores.</p>") + "<ul class=\"leaderboard\">" + people + "</ul>" + (v["people"].size() < 2 ? "<p class=\"muted\">Share your code. When a friend adds you, you both appear here.</p>" : ""));
  };
  api("GET", "friends", {}, [this, render](Response r) { if (r.ok() && r.body["me"].is_null()) api("POST", "me", {{"name", ""}}, [this, render](Response) { api("GET", "friends", {}, render); }); else render(r); });
}
void App::openFaq() {
  static const std::pair<const char*, const char*> faq[] = {{"how does scoring work?", "Every prompt has a prepared list of accepted answers, each placed in a rarity tier: Plankton 10, Too Clever 15, Schooler 30, Rare 60, Deep Cut 85 and One in a Krillion 100. Each point sinks you ten metres. Seven perfect answers reach 7,000 metres."}, {"where do the daily prompts come from?", "Each day the server asks a free AI model for seven new categories with full answer lists in a strict format, checks them, and stores them. If that fails, the day comes from the built-in classic pool. Once a day has been played it never changes."}, {"what is too clever?", "The “obscure” answer that everybody thinks only they will say. It scores a little more than Plankton."}, {"what are unlimited, archive and packs?", "Unlimited draws seven fresh prompts from the pool, dive after dive. The archive replays past daily dives. Themed packs are chapters of seven prompts around one topic; finishing a chapter unlocks the next."}, {"how does puffer party work?", "Everyone in a room gets the same category. Whoever holds the puffer must name a new, valid answer to pass it on. It pops at a random moment; whoever is holding it is out. Last one floating wins."}, {"how do i move my logbook to another device?", "Open My SourceKrillion, make a restore code, and enter it on the other device. Anyone with the code can open your logbook, so keep it private."}, {"does the app work offline?", "Yes. Whenever it is online it saves today’s dive, the Unlimited pool and the packs. Without a connection it plays from those; friends, the archive and Puffer Party need a connection."}, {"is this the official krillion?", "No. SourceKrillion is an independent, open-source game inspired by Krillion. It is not affiliated with krillion.io."}};
  std::string rml = "<p class=\"eyebrow\">SOURCEKRILLION</p><h2>FAQ</h2>"; int i = 0;
  for (auto& [q, a] : faq) { rml += "<div class=\"faq-item\"><div class=\"q\" data-faq=\"" + std::to_string(i) + "\">▸ " + q + "</div><p id=\"faq-" + std::to_string(i) + "\" hidden=\"hidden\">" + esc(a) + "</p></div>"; i++; }
  openPanel(rml, true);
}

// ---------- puffer party ----------
void App::openParty(const std::string& code) {
  closePanel(); if (game_.is_object() && (phase_ == "preview" || phase_ == "answering")) pauseGame();
  partyOn_ = true; bodyClass("party-on", true); show("party", true); if (phase_ == "done") setPhase("intro");
  if (!code.empty()) { joinRoom(code); return; }
  partyRoom_ = nullptr; renderLobby(nullptr); refreshLobby();
}
void App::closeParty() { if (!partyOn_) return; partyOn_ = false; net_->closeSocket(); partyRoom_ = nullptr; show("party", false); bodyClass("party-on", false); }
void App::renderLobby(const json& rooms) {
  std::string name; try { std::ifstream f(dataDir_ + "/party-name.txt"); std::getline(f, name); } catch (...) {}
  std::string list = rooms.is_null() ? "<li class=\"muted\">Loading rooms…</li>" : "";
  if (rooms.is_object()) {
    if (!rooms.value("enabled", false)) list = "<li class=\"muted\">Party rooms are off on this server.</li>";
    else if (rooms["rooms"].empty()) list = "<li class=\"muted\">No public rooms right now. Open one!</li>";
    else for (auto& r : rooms["rooms"]) list += "<li><button class=\"room-row\" data-party-join=\"" + S(r, "code") + "\"><span><strong>" + S(r, "code") + "</strong><small>" + (S(r, "status") == "playing" ? esc(S(r, "prompt", "in play")) : std::string("waiting in the lobby")) + "</small></span><span class=\"go\">" + std::to_string(I(r, "players")) + "/" + std::to_string(I(r, "max")) + (S(r, "status") == "playing" ? " · watching" : " · join →") + "</span></button></li>";
  }
  setRml("party", "<div class=\"party-sheet\"><button class=\"close party-close\" data-action=\"party-close\">×</button><div class=\"party-head\"><span class=\"open-label\">SOURCEKRILLION</span><p class=\"party-title\">PUFFER PARTY</p><p class=\"tagline\">pass the puffer · don’t let it pop on you</p></div><svg class=\"party-puffer idle\" src=\"../sprites/puffer.svg\"/><p class=\"muted party-blurb\">Name an answer and pass the puffer before it pops. Play with friends or join a public room. Last one floating wins.</p><div class=\"inline-form\"><label>your diver name</label><div class=\"row\"><input type=\"text\" id=\"party-name\" maxlength=\"18\" placeholder=\"name yourself…\" value=\"" + esc(name) + "\"/></div></div><div class=\"party-actions\"><button class=\"begin-btn small\" data-party-create=\"public\">OPEN A PUBLIC ROOM</button><button class=\"quiet\" data-party-create=\"private\">PRIVATE ROOM</button></div><div class=\"inline-form\"><label>join with a room code</label><div class=\"row\"><input type=\"text\" id=\"party-code\" maxlength=\"4\" placeholder=\"ABCD\" data-enter=\"party-join\"/><button class=\"quiet\" data-action=\"party-join\">join</button></div><p class=\"error\" id=\"party-error\"></p></div><div class=\"rule\"><span>PUBLIC ROOMS</span><div class=\"rule-line\"></div></div><ul class=\"room-list\" id=\"room-list\">" + list + "</ul></div>");
}
void App::refreshLobby() { lobbyAt_ = now_; api("GET", "party/rooms", {}, [this](Response r) { if (!partyOn_ || partyRoom_.is_object()) return; if (!r.ok()) { setRml("room-list", "<li class=\"muted\">" + esc(r.error) + "</li>"); return; } std::string keepName = inputValue(el("party-name")); renderLobby(r.body); if (auto* n = el("party-name")) static_cast<Rml::ElementFormControl*>(n)->SetValue(keepName); }); }
void App::createRoom(bool isPublic) { std::string name = inputValue(el("party-name")); std::ofstream(dataDir_ + "/party-name.txt") << name; api("POST", "me", {{"name", name}}, [this, isPublic](Response) { api("POST", "party/create", {{"public", isPublic}}, [this](Response r) { if (r.ok()) joinRoom(S(r.body, "code")); else setText("party-error", r.error); }); }); }
void App::joinRoom(std::string code) {
  std::string clean; for (char c : code) if (std::isalpha((unsigned char)c)) clean += (char)std::toupper((unsigned char)c); code = clean.substr(0, 4);
  if (code.size() != 4) { setText("party-error", "Room codes have four letters."); return; }
  std::string name; if (el("party-name")) { name = inputValue(el("party-name")); std::ofstream(dataDir_ + "/party-name.txt") << name; } else { std::ifstream f(dataDir_ + "/party-name.txt"); std::getline(f, name); }
  api("POST", "me", {{"name", name}}, [this, code, name](Response r) {
    if (!r.ok()) { setText("party-error", r.error); return; }
    std::string enc; for (char c : name) { if (std::isalnum((unsigned char)c)) enc += c; else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", (unsigned char)c); enc += b; } }
    lastPop_.clear();
    net_->openSocket("/api/party/ws?room=" + code + "&name=" + enc, [this](json msg) {
      std::string t = S(msg, "t");
      if (t == "state") onRoom(msg["room"]);
      else if (t == "answer") { if (!msg.value("ok", false)) { sound_->event("reject"); setText("party-status", S(msg, "message")); show("party-status", true); } else sound_->event("submit"); }
      else if (t == "error") { setText(el("party-status") ? "party-status" : "party-error", S(msg, "message")); show("party-status", true); }
    }, [this]() { bool wasIn = partyRoom_.is_object(); partyRoom_ = nullptr; if (!partyOn_) return; renderLobby(nullptr); refreshLobby(); setText("party-error", wasIn ? "Disconnected from the room." : "That room does not exist (or it is full)."); });
  });
}
void App::onRoom(const json& room) {
  json prev = partyRoom_; partyRoom_ = room; partyClock_ = room["serverNow"].get<double>() - nowMs();
  const json& r = room["round"];
  if (r.is_object() && !r["popped"].is_null() && S(r, "popped") != lastPop_) { lastPop_ = S(r, "popped"); sound_->event("miss"); shakeUntil_ = now_ + 500; }
  if (prev.is_object() && prev["round"].is_object() && r.is_object() && S(prev["round"], "holder") != S(r, "holder") && r["popped"].is_null()) sound_->event("tier");
  if (S(room, "status") == "over" && (!prev.is_object() || S(prev, "status") != "over")) sound_->event(S(room, "winner") == S(room, "you") ? "krillion" : "surface");
  if (S(room, "status") == "playing" && (!prev.is_object() || S(prev, "status") != "playing")) sound_->event("begin");
  renderRoom();
}
void App::renderRoom() {
  const json& room = partyRoom_; std::string me = S(room, "you"); bool host = S(room, "host") == me; const json& r = room["round"];
  bool playing = S(room, "status") == "playing", holding = r.is_object() && r["popped"].is_null() && S(r, "holder") == me && playing;
  auto nameOf = [&](const std::string& id) { for (auto& p : room["players"]) if (S(p, "id") == id) return S(p, "name"); return std::string("someone"); };
  std::string draft = inputValue(el("party-answer"));
  std::string players; int connected = 0;
  for (auto& p : room["players"]) { bool alive = p.value("alive", true); if (p.value("connected", true)) connected++; std::string cls = std::string(alive ? "" : "out") + (r.is_object() && r["popped"].is_null() && S(r, "holder") == S(p, "id") && playing ? " holding" : "") + (S(p, "id") == me ? " me" : ""); players += "<div class=\"" + cls + "\"><svg src=\"../sprites/" + (alive ? "puffer" : "puffer-angry") + ".svg\"/><span>" + esc(S(p, "name")) + (S(p, "id") == S(room, "host") ? " ★" : "") + (p.value("connected", true) ? "" : " (away)") + "</span><small>" + (alive ? std::to_string(I(p, "answers")) + " passed" : std::string("popped")) + "</small></div>"; }
  std::string rml = "<div class=\"party-sheet room\"><button class=\"close party-close\" data-action=\"party-close\">×</button><div class=\"party-room-head\"><span class=\"eyebrow\">ROOM " + S(room, "code") + (room.value("public", false) ? " · PUBLIC" : " · PRIVATE") + "</span><button class=\"text-button\" data-action=\"copy-room\">copy invite link</button></div><div class=\"party-players\">" + players + "</div>";
  if (S(room, "status") == "lobby") rml += "<div class=\"party-wait\"><svg class=\"party-puffer idle\" src=\"../sprites/puffer.svg\"/><p class=\"tagline\">" + std::string(room["players"].size() < 2 ? "waiting for divers…" : host ? "everyone in? start the round" : "waiting for the host…") + "</p>" + (host ? std::string("<button class=\"begin-btn small") + (connected < 2 ? " disabled" : "") + "\" data-action=\"party-start\">▼ START ▼</button>" : "") + "<p class=\"muted\">Share the code <span style=\"color:#ffd166\">" + S(room, "code") + "</span> · up to 8 divers</p></div>";
  if (r.is_object() && S(room, "status") != "lobby") {
    std::string hint = !r["popped"].is_null() ? "POP! " + esc(nameOf(S(r, "popped"))) + " is out" : holding ? std::string("you have the puffer · name one, quick") : esc(nameOf(S(r, "holder"))) + " has the puffer";
    std::string used; int n = (int)r["used"].size(); for (int i = std::max(0, n - 14); i < n; i++) { auto& u = r["used"][i]; used += "<div style=\"color:" + tierColor(u["tier"]) + ";box-shadow:" + tierColor(u["tier"]) + " 0 0 0 2dp\">" + esc(S(u, "answer")) + "<small>" + esc(nameOf(S(u, "by"))) + "</small></div>"; }
    rml += "<div class=\"prompt-card party-prompt\"><p class=\"eyebrow\">ROUND " + std::to_string(I(room, "roundNumber")) + "</p><h2>" + esc(S(r, "prompt")) + "</h2><p class=\"hint\">" + hint + "</p></div><div class=\"party-stage\"><svg id=\"puffer-img\" class=\"party-puffer live\" src=\"../sprites/" + (r["popped"].is_null() ? "puffer" : "puffer-angry") + ".svg\"/><p class=\"holder-name\">" + (r["popped"].is_null() ? esc(nameOf(S(r, "holder"))) : std::string("POP!")) + "</p></div><div class=\"used-answers\">" + used + "</div>";
  }
  if (S(room, "status") == "over") rml += "<div class=\"party-over\"><p class=\"eyebrow\">LAST ONE FLOATING</p><h2>" + (room["winner"].is_null() ? std::string("nobody") : esc(nameOf(S(room, "winner"))) + (S(room, "winner") == me ? " (you!)" : "")) + "</h2>" + (host ? "<button class=\"begin-btn small\" data-action=\"party-reset\">PLAY AGAIN</button>" : "<p class=\"muted\">waiting for the host to play again…</p>") + "</div>";
  rml += "</div>";
  if (playing) rml += std::string("<div class=\"answer-dock party-dock") + (holding ? "" : " waiting") + "\"><div class=\"answer-status\" id=\"party-status\" hidden=\"hidden\"></div><div id=\"party-form\"><div class=\"input-wrap\"><input type=\"text\" id=\"party-answer\" maxlength=\"120\" data-enter=\"party-pass\"" + (holding ? "" : " disabled=\"disabled\"") + "/><span class=\"placeholder\" id=\"party-ph\">" + (holding ? "type one answer…" : "wait for the puffer…") + "</span><div class=\"time-bar\" style=\"width:100%\"></div></div><button id=\"party-pass\" class=\"" + (holding ? "" : "disabled") + "\" data-action=\"party-pass\">PASS</button></div></div>";
  setRml("party", rml);
  if (holding) if (auto* in = el("party-answer")) { static_cast<Rml::ElementFormControl*>(in)->SetValue(draft); pendingFocus_ = "party-answer"; }
}
