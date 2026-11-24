// SourceKrillion native app: SDL3 window + OpenGL 3.3, RmlUi for the HTML/CSS-like UI,
// canvas_ity for the ocean. No web view.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"
#include "RmlUi_Include_GL3.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "app.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

std::map<std::string, std::string> g_memoryFiles;
static std::string g_assets;

// Files: "mem:" names come from memory (generated charts); game.rcss is built from game.src.rcss (px -> dp).
class Files : public Rml::FileInterface {
  struct Handle { std::string data; size_t pos = 0; };
public:
  Rml::FileHandle Open(const Rml::String& path) override {
    std::string p = path, data; auto mem = p.find("mem:");
    if (mem != std::string::npos) { auto it = g_memoryFiles.find(p.substr(mem)); if (it == g_memoryFiles.end()) return 0; data = it->second; }
    else {
      bool rcss = p.size() > 9 && p.compare(p.size() - 9, 9, "game.rcss") == 0;
      std::ifstream f(rcss ? p.substr(0, p.size() - 9) + "game.src.rcss" : p, std::ios::binary); if (!f) return 0;
      std::stringstream ss; ss << f.rdbuf(); data = ss.str();
      if (rcss) { std::stringstream in(data), out; std::string line; while (std::getline(in, line)) out << (line.rfind("@spritesheet", 0) == 0 ? line : std::regex_replace(line, std::regex(R"(([0-9.])px\b)"), "$1dp")) << "\n"; data = out.str(); }
    }
    return (Rml::FileHandle) new Handle{std::move(data)};
  }
  void Close(Rml::FileHandle f) override { delete (Handle*)f; }
  size_t Read(void* buffer, size_t size, Rml::FileHandle f) override { auto* h = (Handle*)f; size_t n = std::min(size, h->data.size() - h->pos); std::memcpy(buffer, h->data.data() + h->pos, n); h->pos += n; return n; }
  bool Seek(Rml::FileHandle f, long offset, int origin) override { auto* h = (Handle*)f; long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)h->pos : (long)h->data.size(); long p = base + offset; if (p < 0 || p > (long)h->data.size()) return false; h->pos = (size_t)p; return true; }
  size_t Tell(Rml::FileHandle f) override { return ((Handle*)f)->pos; }
  size_t Length(Rml::FileHandle f) override { return ((Handle*)f)->data.size(); }
};

// GL3 renderer that also understands PNG files (decorator images).
class Renderer : public RenderInterface_GL3 {
public:
  Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String& source) override {
    int w, h, n; unsigned char* px = stbi_load(source.c_str(), &w, &h, &n, 4); if (!px) return RenderInterface_GL3::LoadTexture(dims, source);
    for (int i = 0; i < w * h; i++) { unsigned char* p = px + i * 4; p[0] = (unsigned char)(p[0] * p[3] / 255); p[1] = (unsigned char)(p[1] * p[3] / 255); p[2] = (unsigned char)(p[2] * p[3] / 255); }
    dims = {w, h}; auto t = GenerateTexture({px, (size_t)w * h * 4}, dims); stbi_image_free(px); return t;
  }
};

static GLuint compile(GLenum type, const char* src) { GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s); return s; }
struct OceanQuad {
  GLuint program = 0, vao = 0, vbo = 0, tex = 0;
  void init() {
    const char* vs = "#version 330\nlayout(location=0) in vec2 p; out vec2 uv; void main(){ uv = vec2((p.x+1.0)/2.0, (1.0-p.y)/2.0); gl_Position = vec4(p,0,1); }";
    const char* fs = "#version 330\nin vec2 uv; uniform sampler2D t; out vec4 c; void main(){ c = vec4(texture(t, uv).rgb, 1.0); }";
    program = glCreateProgram(); glAttachShader(program, compile(GL_VERTEX_SHADER, vs)); glAttachShader(program, compile(GL_FRAGMENT_SHADER, fs)); glLinkProgram(program);
    float v[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr); glBindVertexArray(0);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  // The canvas is (w/2 x h/2) UI pixels; it covers ceil-sized area so crop to the exact UI size.
  void draw(const Ocean& o, int fbw, int fbh, float uiw, float uih) {
    glViewport(0, 0, fbw, fbh); glDisable(GL_BLEND); glUseProgram(program); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, o.pw(), o.ph(), 0, GL_RGBA, GL_UNSIGNED_BYTE, o.pixels().data());
    float sx = uiw / (o.pw() * 2.f), sy = uih / (o.ph() * 2.f);
    float v[] = {-1, -1, 1, -1, -1, 1, 1, 1}; (void)v;
    glUniform1i(glGetUniformLocation(program, "t"), 0);
    glBindVertexArray(vao);
    // scale UVs by adjusting the viewport so the canvas's extra half-pixel falls off-screen
    glViewport(0, fbh - (int)std::lround(fbh / sy), (int)std::lround(fbw / sx), (int)std::lround(fbh / sy));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glBindVertexArray(0);
  }
};

int main(int argc, char** argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
  const char* base = SDL_GetBasePath();
  std::string root = base ? base : "./";
  if (!std::filesystem::exists(root + "assets") && std::filesystem::exists(root + "../Resources/assets")) root += "../Resources/"; // macOS .app bundle
  g_assets = root + "assets";
  char* pref = SDL_GetPrefPath("SourceKrillion", "SourceKrillion"); std::string dataDir = pref ? pref : "."; if (pref) SDL_free(pref);
  if (!dataDir.empty() && (dataDir.back() == '/' || dataDir.back() == '\\')) dataDir.pop_back();

  // Server address: --server URL, SOURCEKRILLION_SERVER, saved server.txt, then the default.
  std::string server = "http://localhost:3051";
  if (std::ifstream f(dataDir + "/server.txt"); f) std::getline(f, server);
  if (const char* env = SDL_getenv("SOURCEKRILLION_SERVER")) server = env;
  for (int i = 1; i + 1 < argc; i++) if (std::string(argv[i]) == "--server") { server = argv[i + 1]; std::ofstream(dataDir + "/server.txt") << server; }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
  int startW = 1280, startH = 800;
  if (const char* s = SDL_getenv("SOURCEKRILLION_SIZE")) std::sscanf(s, "%dx%d", &startW, &startH);
  SDL_Window* window = SDL_CreateWindow("SourceKrillion", startW, startH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) { SDL_Log("Window failed: %s", SDL_GetError()); return 1; }
  SDL_SetWindowMinimumSize(window, 360, 560);
  SDL_GLContext gl = SDL_GL_CreateContext(window); SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);
  Rml::String msg; if (!RmlGL3::Initialize(&msg)) { SDL_Log("OpenGL init failed: %s", msg.c_str()); return 1; }

  Renderer renderer; SystemInterface_SDL system; system.SetWindow(window); Files files;
  Rml::SetRenderInterface(&renderer); Rml::SetSystemInterface(&system); Rml::SetFileInterface(&files);
  Rml::Initialise();
  Rml::LoadFontFace(g_assets + "/fonts/VT323-Regular.ttf");
  Rml::LoadFontFace(g_assets + "/fonts/DejaVuSans.ttf", true);
  int fbw, fbh; SDL_GetWindowSizeInPixels(window, &fbw, &fbh);
  float density = SDL_GetWindowPixelDensity(window); if (density <= 0) density = 1;
  Rml::Context* ctx = Rml::CreateContext("main", {fbw, fbh}); ctx->SetDensityIndependentPixelRatio(density);
  renderer.SetViewport(fbw, fbh);
  Rml::ElementDocument* doc = ctx->LoadDocument(g_assets + "/ui/game.rml");
  if (!doc) { SDL_Log("Could not load UI"); return 1; }
  doc->Show();

  Net net; net.configure(server, dataDir, g_assets + "/cacert.pem");
  Offline offline; offline.load(dataDir);
  Sound sound; Ocean ocean; ocean.init(g_assets + "/sprites"); ocean.resize((int)std::lround(fbw / density), (int)std::lround(fbh / density));
  OceanQuad quad; quad.init();
  App app; app.setClipboard = [](const std::string& s) { SDL_SetClipboardText(s.c_str()); }; app.openUrl = [](const std::string& u) { SDL_OpenURL(u.c_str()); };
  app.init(ctx, doc, &net, &offline, &sound, &ocean, dataDir);
  SDL_StartTextInput(window);

  const char* shot = SDL_getenv("SOURCEKRILLION_SCREENSHOT"); // for visual comparison tests: "path.ppm:ms"
  std::string shotPath; double shotAt = 0; if (shot) { std::string s = shot; auto c = s.rfind(':'); shotPath = s.substr(0, c); shotAt = std::atof(s.substr(c + 1).c_str()); }
  const char* script = SDL_getenv("SOURCEKRILLION_CLICK"); // "ms:id,ms:id" clicks elements by id (tests)
  std::vector<std::pair<double, std::string>> clicks; if (script) { std::stringstream ss(script); std::string item; while (std::getline(ss, item, ',')) { auto c = item.find(':'); clicks.push_back({std::atof(item.substr(0, c).c_str()), item.substr(c + 1)}); } }

  bool running = true; Uint64 start = SDL_GetTicks(), last = SDL_GetPerformanceCounter();
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) running = false;
      if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || ev.type == SDL_EVENT_WINDOW_RESIZED || ev.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        SDL_GetWindowSizeInPixels(window, &fbw, &fbh); density = SDL_GetWindowPixelDensity(window); if (density <= 0) density = 1;
        ctx->SetDimensions({fbw, fbh}); ctx->SetDensityIndependentPixelRatio(density); renderer.SetViewport(fbw, fbh);
        ocean.resize((int)std::lround(fbw / density), (int)std::lround(fbh / density));
      }
      // SDL reports mouse positions in window points; the RmlUi context works in pixels.
      if (ev.type == SDL_EVENT_MOUSE_MOTION) { ev.motion.x *= density; ev.motion.y *= density; }
      if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) { ev.button.x *= density; ev.button.y *= density; sound.init(); }
      if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F && !(ctx->GetFocusElement() && ctx->GetFocusElement()->GetTagName() == "input")) { bool fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN; SDL_SetWindowFullscreen(window, !fs); continue; }
      RmlSDL::InputEventHandler(ctx, window, ev);
    }
    Uint64 nowc = SDL_GetPerformanceCounter(); double dt = std::min(.05, double(nowc - last) / SDL_GetPerformanceFrequency()); last = nowc;
    double now = (double)SDL_GetTicks();
    for (auto it = clicks.begin(); it != clicks.end();) { if (now - start >= it->first) { if (it->second.rfind("q=", 0) == 0) { if (auto* e = doc->QuerySelector(it->second.substr(2))) e->Click(); } else if (auto* e = doc->GetElementById(it->second)) e->Click(); else if (it->second.rfind("type=", 0) == 0) { if (auto* in = doc->GetElementById("answer")) static_cast<Rml::ElementFormControl*>(in)->SetValue(it->second.substr(5)); } else if (it->second == "enter") { Rml::Dictionary p; p["key_identifier"] = (int)Rml::Input::KI_RETURN; if (auto* in = doc->GetElementById("answer")) in->DispatchEvent(Rml::EventId::Keydown, p); } it = clicks.erase(it); } else ++it; }
    app.update(now, dt);
    ctx->Update();
    quad.draw(ocean, fbw, fbh, fbw / density, fbh / density);
    renderer.SetViewport(fbw, fbh);
    renderer.BeginFrame(); ctx->Render(); renderer.EndFrame();
    if (!shotPath.empty() && now - start >= shotAt) {
      std::vector<unsigned char> px((size_t)fbw * fbh * 3); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, fbw, fbh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      std::ofstream f(shotPath, std::ios::binary); f << "P6\n" << fbw << " " << fbh << "\n255\n"; for (int y = fbh - 1; y >= 0; y--) f.write((char*)&px[(size_t)y * fbw * 3], fbw * 3);
      running = false;
    }
    SDL_GL_SwapWindow(window);
  }
  net.closeSocket();
  Rml::Shutdown(); RmlGL3::Shutdown(); SDL_GL_DestroyContext(gl); SDL_DestroyWindow(window); SDL_Quit();
  return 0;
}
