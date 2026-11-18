#include "HttpPlugin.h"

#if defined(__APPLE__)
#include "MacSupport.hpp"  // macos/
#else
#include <windows.h>
#include <shellapi.h>
#endif

namespace RedHttpClient {

HttpPlugin::HttpPlugin()
    : handle(nullptr),
      logger(nullptr),
      ssl_options(cpr::Ssl(cpr::ssl::TLSv1_2{})),
      m_has_tls(true),
      m_has_self_signed(false) {}

HttpPlugin* HttpPlugin::get() {
  static HttpPlugin singleton;

  return &singleton;
}

void HttpPlugin::load(const RED4ext::v1::Sdk* p_sdk,
                      const RED4ext::v1::PluginHandle p_handle) {
  logger = p_sdk->logger;
  handle = p_handle;

#if defined(__APPLE__)
  // macOS / iOS (CyPort):
  // - no game state callbacks: the loader's CGameApplication hook is not available on iOS, and RedFileSystem's natives do
  //   not exist yet at this point anyway. The settings (the "RedHttpClient" storage, config.json) are loaded on the first
  //   request instead: see can_log().
  // - the development switches -no-tls and -self-signed are not read: every request is https and the system verifies the
  //   server certificate (NSURLSession, macos/cpr-apple).
  return;
#else
  Red::v1::GameState init{};
  init.OnEnter = [](Red::CGameApplication*) -> bool {
    get()->start();
    return true;
  };
  init.OnUpdate = nullptr;
  init.OnExit = nullptr;

  Red::v1::GameState shutdown{};
  shutdown.OnEnter = [](Red::CGameApplication*) -> bool {
    get()->stop();
    return true;
  };
  shutdown.OnUpdate = nullptr;
  shutdown.OnExit = nullptr;

  p_sdk->gameStates->Add(p_handle, Red::EGameStateType::Initialization, &init);
  p_sdk->gameStates->Add(p_handle, Red::EGameStateType::Shutdown, &shutdown);

  const LPWSTR command = GetCommandLineW();
  int argc;
  LPWSTR* argv = CommandLineToArgvW(command, &argc);
  for (int i = 0; i < argc; i++) {
    const std::wstring arg{argv[i]};
    if (arg == L"-no-tls") {
      m_has_tls = false;
    } else if (arg == L"-self-signed") {
      m_has_self_signed = true;
    }
  }
  LocalFree(argv);

  if (!m_has_tls) {
    logger->Warn(handle, "TLS is disabled. "
                         "This feature is only meant for local development.");
  }

  if (m_has_self_signed) {
    ssl_options = cpr::Ssl(cpr::ssl::TLSv1_2{},
      cpr::ssl::VerifyHost{false},
      cpr::ssl::VerifyPeer{false},
      cpr::ssl::VerifyStatus{false});

    logger->Warn(handle, "Self-signed certificates are allowed. "
                         "This feature is only meant for local development.");
  }
#endif
}

bool HttpPlugin::can_log() const {
#if defined(__APPLE__)
  // The first request loads the settings (see load()). Requests come from scripts, so the natives of RedFileSystem and
  // RedData are registered by then, when those plugins are installed. Without them logging stays on, as on Windows.
  std::call_once(settings_once, [this]() -> void {
    if (RedMac::TypesRegistered()) {
      const_cast<HttpPlugin*>(this)->settings.load();
    }
  });
#endif
  return settings.can_log();
}

void HttpPlugin::unload() {
  logger = nullptr;
  handle = nullptr;
}

void HttpPlugin::start() {
  settings.load();
}

void HttpPlugin::stop() {
  settings.unload();
}

cpr::SslOptions HttpPlugin::get_ssl_options() const {
  return ssl_options;
}

bool HttpPlugin::has_tls() const {
  return m_has_tls;
}

bool HttpPlugin::has_self_signed() const {
  return m_has_self_signed;
}

std::string HttpPlugin::log_http_method(const HttpMethod p_method) {
  switch (p_method) {
    case HttpMethod::GET:
      return "GET";
    case HttpMethod::POST:
      return "POST";
    case HttpMethod::PUT:
      return "PUT";
    case HttpMethod::PATCH:
      return "PATCH";
    case HttpMethod::DELETE_:
      return "DELETE";
    default:
      return "";
  }
}

void HttpPlugin::log_error(const cpr::Error& p_error) const {
  if (logger == nullptr || handle == nullptr || !can_log()) {
    return;
  }
  logger->Error(handle, "HTTP error:");
  logger->Error(handle, std::format("- code: {}", static_cast<int>(p_error.code)).c_str());
  logger->Error(handle, std::format("- message: {}", p_error.message).c_str());
}

void HttpPlugin::log_response(const cpr::Response& p_response) const {
  if (logger == nullptr || handle == nullptr || !can_log()) {
    return;
  }
  logger->Info(handle, "HTTP response:");
  logger->Info(handle, std::format("HTTP/1.1 {} {}", p_response.status_code,
                                   p_response.reason)
                         .c_str());
  for (const auto& [header, value] : p_response.header) {
    logger->Info(handle,
                 std::format("{}: {}", header, value).c_str());
  }
  if (!p_response.text.empty()) {
    logger->Info(handle, "");
    logger->Info(handle, p_response.text.c_str());
  }
}

}  // namespace RedHttpClient