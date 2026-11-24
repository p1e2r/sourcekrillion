// Talks to a SourceKrillion server. Requests run on background threads; callbacks are
// delivered on the main thread through pump(). The player cookie is persisted on disk.
#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ix { class WebSocket; }
using json = nlohmann::json;

struct Response { int status = 0; json body; std::string error; bool ok() const { return status >= 200 && status < 300; } };

class Net {
public:
  void configure(const std::string& baseUrl, const std::string& dataDir, const std::string& caFile);
  std::string base() const { return baseUrl_; }
  void setBase(const std::string& url);
  void get(const std::string& path, std::function<void(Response)> done);
  void post(const std::string& path, const json& body, std::function<void(Response)> done);
  void pump();                       // run finished callbacks on the calling (main) thread
  bool online() const { return online_; }
  // Party room socket
  void openSocket(const std::string& path, std::function<void(json)> onMessage, std::function<void()> onClose);
  void sendSocket(const json& message);
  void closeSocket();
private:
  void request(const std::string& method, const std::string& path, const std::string& body, std::function<void(Response)> done);
  void saveCookie();
  std::string baseUrl_, dataDir_, caFile_, cookie_;
  std::mutex mutex_;
  std::vector<std::function<void()>> ready_;
  bool online_ = true;
  std::shared_ptr<ix::WebSocket> socket_;
  int socketGeneration_ = 0;
};
