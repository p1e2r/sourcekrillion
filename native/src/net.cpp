#include "net.h"
#include <fstream>
#include <thread>
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

void Net::configure(const std::string& baseUrl, const std::string& dataDir, const std::string& caFile) {
  ix::initNetSystem();
  dataDir_ = dataDir; caFile_ = caFile; setBase(baseUrl);
  std::ifstream in(dataDir_ + "/cookie.txt"); std::getline(in, cookie_);
}
void Net::setBase(const std::string& url) { baseUrl_ = url; while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back(); }
void Net::saveCookie() { std::ofstream(dataDir_ + "/cookie.txt") << cookie_; }

void Net::request(const std::string& method, const std::string& path, const std::string& body, std::function<void(Response)> done) {
  std::string url = baseUrl_ + "/api/" + path, cookie = cookie_, ca = caFile_;
  std::thread([this, method, url, body, cookie, ca, done]() {
    ix::HttpClient client;
    ix::SocketTLSOptions tls; tls.caFile = ca; client.setTLSOptions(tls);
    auto args = client.createRequest(url, method);
    args->connectTimeout = 8; args->transferTimeout = 20; args->followRedirects = true;
    args->extraHeaders["Accept"] = "application/json";
    if (!cookie.empty()) args->extraHeaders["Cookie"] = "sk_player=" + cookie;
    if (method == "POST") args->extraHeaders["Content-Type"] = "application/json";
    auto r = client.request(url, method, body, args);
    Response res; res.status = r->statusCode;
    std::string newCookie;
    for (auto& [k, v] : r->headers) {
      std::string key = k; for (auto& c : key) c = (char)std::tolower((unsigned char)c);
      if (key == "set-cookie") { auto p = v.find("sk_player="); if (p != std::string::npos) newCookie = v.substr(p + 10, 36); }
    }
    if (r->errorCode != ix::HttpErrorCode::Ok || res.status == 0) { res.status = 0; res.error = "offline"; }
    else { try { res.body = json::parse(r->body); } catch (...) { res.body = json::object(); } if (!res.ok()) res.error = res.body.value("error", "The signal is lost. Please retry."); }
    std::lock_guard<std::mutex> lock(mutex_);
    ready_.push_back([this, res, newCookie, done]() {
      online_ = res.status != 0;
      if (!newCookie.empty() && newCookie != cookie_) { cookie_ = newCookie; saveCookie(); }
      done(res);
    });
  }).detach();
}
void Net::get(const std::string& path, std::function<void(Response)> done) { request("GET", path, "", std::move(done)); }
void Net::post(const std::string& path, const json& body, std::function<void(Response)> done) { request("POST", path, body.dump(), std::move(done)); }
void Net::pump() {
  std::vector<std::function<void()>> jobs;
  { std::lock_guard<std::mutex> lock(mutex_); jobs.swap(ready_); }
  for (auto& j : jobs) j();
}

void Net::openSocket(const std::string& path, std::function<void(json)> onMessage, std::function<void()> onClose) {
  closeSocket();
  int generation = ++socketGeneration_;
  auto ws = std::make_shared<ix::WebSocket>();
  std::string url = baseUrl_; if (url.rfind("https", 0) == 0) url = "wss" + url.substr(5); else if (url.rfind("http", 0) == 0) url = "ws" + url.substr(4);
  ws->setUrl(url + path);
  ix::SocketTLSOptions tls; tls.caFile = caFile_; ws->setTLSOptions(tls);
  ix::WebSocketHttpHeaders headers; if (!cookie_.empty()) headers["Cookie"] = "sk_player=" + cookie_; ws->setExtraHeaders(headers);
  ws->disableAutomaticReconnection();
  ws->setOnMessageCallback([this, generation, onMessage, onClose](const ix::WebSocketMessagePtr& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (msg->type == ix::WebSocketMessageType::Message) { json j; try { j = json::parse(msg->str); } catch (...) { return; } ready_.push_back([this, generation, onMessage, j]() { if (generation == socketGeneration_) onMessage(j); }); }
    else if (msg->type == ix::WebSocketMessageType::Close || msg->type == ix::WebSocketMessageType::Error) ready_.push_back([this, generation, onClose]() { if (generation == socketGeneration_) { socketGeneration_++; onClose(); } });
  });
  socket_ = ws; ws->start();
}
void Net::sendSocket(const json& message) { if (socket_) socket_->send(message.dump()); }
void Net::closeSocket() { if (socket_) { socketGeneration_++; auto s = socket_; socket_.reset(); std::thread([s]() { s->stop(); }).detach(); } }
