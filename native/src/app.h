#pragma once
#include "net.h"
#include "offline.h"
#include "ocean.h"
#include "sound.h"
#include <RmlUi/Core.h>
#include <functional>
#include <map>

struct Settings { bool lowPower = false, colorblind = false, corrections = true, clearRejected = false, shareLink = true, muted = false; std::string buddy = "krill", serverUrl; };

class App : public Rml::EventListener {
public:
  void init(Rml::Context* ctx, Rml::ElementDocument* doc, Net* net, Offline* off, Sound* snd, Ocean* ocean, const std::string& dataDir);
  void update(double nowMs, double dt);
  void ProcessEvent(Rml::Event& e) override;
  std::function<void(const std::string&)> setClipboard, openUrl;
private:
  // plumbing
  void api(const std::string& method, const std::string& path, const json& body, std::function<void(Response)> done, bool runScoped = false);
  Rml::Element* el(const std::string& id);
  void setText(const std::string& id, const std::string& text);
  void setRml(const std::string& id, const std::string& rml);
  void show(const std::string& id, bool on);
  void setClass(Rml::Element* e, const std::string& cls, bool on);
  void bodyClass(const std::string& cls, bool on);
  void saveSettings(); void loadSettings(); void applySettings();
  // game flow
  void setPhase(const std::string& phase);
  void setGame(const json& state);
  void updateHud();
  void begin(const json& options);
  void resumeDaily();
  void showRound();
  void submit(bool timeout);
  void startPlunge(const json& result, int oldScore);
  void showLanding(const json& result, int oldScore);
  void next();
  void showResults();
  void tick();
  void animateScore(int from, int to, bool gold);
  void syncBuddyGold();
  void pauseGame(); void resumeGame();
  int currentScore() const;
  // panels
  void openPanel(const std::string& rml, bool wide = false);
  void closePanel();
  void openMenu(); void openSettings(); void openWardrobe(); void openLogbook(); void renderSync(const std::string& code);
  void openArchive(); void openPacks(); void openFriends(); void openFaq();
  void doAction(const std::string& action, Rml::Element* target);
  std::string resultActions();
  // party
  void openParty(const std::string& code = "");
  void closeParty();
  void renderLobby(const json& rooms);
  void refreshLobby();
  void createRoom(bool isPublic);
  void joinRoom(std::string code);
  void onRoom(const json& room);
  void renderRoom();

  Rml::Context* ctx_ = nullptr; Rml::ElementDocument* doc_ = nullptr; Net* net_ = nullptr; Offline* off_ = nullptr; Sound* sound_ = nullptr; Ocean* scene_ = nullptr;
  std::string dataDir_;
  Settings settings_;
  json today_, game_, review_, logbook_, partyRoom_;
  std::string phase_ = "intro", correction_, panel_, lastPop_;
  bool busy_ = false, paused_ = false, autoSent_ = false, gameOffline_ = false, partyOn_ = false;
  int lastTick_ = -1;
  double now_ = 0, clockAtServer_ = 0, promptExitAt_ = 0, partyClock_ = 0, lobbyAt_ = 0, bootAt_ = 0;
  // animation state
  struct { bool on = false; json result; int oldScore = 0; double began = 0, duration = 0; int lastPassed = -1; } plunge_;
  struct { bool on = false; double start = 0, duration = 0; int from = 0, to = 0; bool gold = false; } scoreAnim_;
  double visualScore_ = 0, goldTimer_ = 0, flashUntil_ = 0, shakeUntil_ = 0, pokedUntil_ = 0, celebrateUntil_ = 0, slamUntil_ = 0;
  json pendingGold_;
  std::map<std::string, double> later_;
  std::string pendingFocus_;
};
