// Offline mode: answers the same API calls as the server, using the last downloaded bundle.
// The bundle (today's full answer lists, the Unlimited pool and the packs) is refreshed
// whenever the app is online; without a connection the app keeps playing from it.
#pragma once
#include "net.h"
#include "content.h"
#include <map>

class Offline {
public:
  void load(const std::string& dataDir);
  bool hasBundle() const { return !bundle_.is_null(); }
  void storeBundle(const json& bundle);
  std::string bundleDate() const { return hasBundle() ? bundle_["today"].value("date", "") : ""; }
  Response handle(const std::string& method, const std::string& path, const json& body);
private:
  json state(const json& run) const;
  json puzzleFor(const json& run) const;
  json startRun(const json& body);
  json packsView() const;
  json logbook() const;
  void save();
  std::string dataDir_;
  json bundle_, data_ = json::object();
};
