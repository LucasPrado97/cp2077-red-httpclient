#pragma once
// macOS / iOS (CyPort) only. The part of the cpr API (libcpr 1.15, the HTTP library of the Windows build) that RedHttpClient's
// sources use, implemented on NSURLSession (CprApple.mm) instead of libcurl: no curl and no TLS library to build or ship, the
// system's TLS stack and trust store do the work, and server certificates are always verified.
// The names and argument orders are cpr's, so src/ compiles unchanged. Not a general cpr replacement.
//
// What differs from cpr/libcurl: the User-Agent is "RedHttpClient/<version>" (curl's there), a 301/302/303 answer to a POST is
// followed with GET (curl through cpr repeats the POST), a request times out after 60 seconds without data (no limit there), and
// the verification switches of cpr::Ssl are accepted and ignored (verification stays on). The plugin itself only sends https.

#include <cstdint>
#include <cstring>
#include <future>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cpr
{
enum class ErrorCode : int
{
    OK = 0,
    CONNECTION_FAILURE,
    EMPTY_RESPONSE,
    HOST_RESOLUTION_FAILURE,
    INTERNAL_ERROR,
    INVALID_URL_FORMAT,
    NETWORK_RECEIVE_ERROR,
    NETWORK_SEND_FAILURE,
    OPERATION_TIMEDOUT,
    PROXY_RESOLUTION_FAILURE,
    SSL_CONNECT_ERROR,
    SSL_LOCAL_CERTIFICATE_ERROR,
    SSL_REMOTE_CERTIFICATE_ERROR,
    SSL_CACERT_ERROR,
    GENERIC_SSL_ERROR,
    UNSUPPORTED_PROTOCOL,
    REQUEST_CANCELLED,
    TOO_MANY_REDIRECTS,
    UNKNOWN_ERROR = 1000,
};

struct Error
{
    ErrorCode code = ErrorCode::OK;
    std::string message;
};

// HTTP header names are case-insensitive.
struct CaseInsensitiveCompare
{
    bool operator()(const std::string& aLhs, const std::string& aRhs) const noexcept
    {
        return strcasecmp(aLhs.c_str(), aRhs.c_str()) < 0;
    }
};

using Header = std::map<std::string, std::string, CaseInsensitiveCompare>;

struct Response
{
    long status_code = 0;
    std::string text;
    Header header;
    std::string reason;
    Error error;
};

struct Url
{
    std::string value;

    Url() = default;
    Url(const char* aUrl) : value(aUrl ? aUrl : "") {}
    Url(std::string aUrl) : value(std::move(aUrl)) {}

    [[nodiscard]] const char* c_str() const { return value.c_str(); }
    [[nodiscard]] const std::string& str() const { return value; }
};

struct Body
{
    std::string value;

    Body() = default;
    Body(const char* aBody) : value(aBody ? aBody : "") {}
    Body(std::string aBody) : value(std::move(aBody)) {}
};

struct Pair
{
    std::string key;
    std::string value;

    Pair(std::string aKey, std::string aValue) : key(std::move(aKey)), value(std::move(aValue)) {}
};

// application/x-www-form-urlencoded fields.
struct Payload
{
    std::vector<Pair> pairs;

    Payload() = default;
    Payload(std::initializer_list<Pair> aPairs) : pairs(aPairs) {}
    template<class It>
    Payload(It aBegin, It aEnd) : pairs(aBegin, aEnd) {}
};

struct Part
{
    std::string name;
    std::string value;

    Part(std::string aName, std::string aValue) : name(std::move(aName)), value(std::move(aValue)) {}
};

// multipart/form-data text fields (RedHttpClient has no file parts).
struct Multipart
{
    std::vector<Part> parts;

    Multipart() = default;
    Multipart(const std::initializer_list<Part>& aParts) : parts(aParts) {}
};

namespace ssl
{
struct TLSv1_2 {};
struct VerifyHost { bool enabled = true; };
struct VerifyPeer { bool enabled = true; };
struct VerifyStatus { bool enabled = false; };
} // namespace ssl

// TLS 1.2 is the minimum (NSURLSession's default and the value the plugin asks for). The Verify* switches exist so
// HttpPlugin.cpp compiles; nothing reads them: the system validates the server certificate and the host name, always.
struct SslOptions
{
    bool verify_requested = true;
};

template<typename... Ts>
SslOptions Ssl(Ts&&... aOptions)
{
    SslOptions options;
    const auto apply = [&options](const auto& aOption) {
        using T = std::decay_t<decltype(aOption)>;
        if constexpr (std::is_same_v<T, ssl::VerifyHost> || std::is_same_v<T, ssl::VerifyPeer>)
        {
            options.verify_requested = options.verify_requested && aOption.enabled;
        }
    };
    (apply(aOptions), ...);
    return options;
}

namespace detail
{
struct Request
{
    std::string method;
    std::string url;
    std::string body;
    std::string content_type; // the default for this kind of body; a Content-Type header of the caller wins
    bool has_body = false;
    Header header;
};

inline void Set(Request&, const SslOptions&) {}
inline void Set(Request& aRequest, const Url& aUrl) { aRequest.url = aUrl.value; }
inline void Set(Request& aRequest, const Header& aHeader)
{
    for (const auto& [name, value] : aHeader)
    {
        aRequest.header[name] = value;
    }
}
void Set(Request& aRequest, const Body& aBody);
void Set(Request& aRequest, const Payload& aPayload);
void Set(Request& aRequest, const Multipart& aMultipart);

// Blocks the calling thread until the response is complete or the request failed. Thread-safe.
Response Perform(const Request& aRequest);

template<typename... Ts>
Request Make(const char* aMethod, Ts&&... aOptions)
{
    Request request;
    request.method = aMethod;
    (Set(request, aOptions), ...);
    return request;
}
} // namespace detail

// std::future with cpr's name; share() is what the plugin calls.
class AsyncResponse
{
public:
    AsyncResponse() = default;
    explicit AsyncResponse(std::future<Response>&& aFuture) : m_future(std::move(aFuture)) {}

    [[nodiscard]] std::shared_future<Response> share() { return m_future.share(); }
    [[nodiscard]] Response get() { return m_future.get(); }
    [[nodiscard]] bool valid() const noexcept { return m_future.valid(); }

private:
    std::future<Response> m_future;
};

namespace detail
{
// cpr runs an asynchronous request on a thread pool; here each one gets a thread that sleeps until NSURLSession is done.
inline AsyncResponse Async(Request&& aRequest)
{
    return AsyncResponse(std::async(std::launch::async, [request = std::move(aRequest)]() { return Perform(request); }));
}
} // namespace detail

template<typename... Ts> Response Get(Ts&&... aOptions) { return detail::Perform(detail::Make("GET", aOptions...)); }
template<typename... Ts> Response Post(Ts&&... aOptions) { return detail::Perform(detail::Make("POST", aOptions...)); }
template<typename... Ts> Response Put(Ts&&... aOptions) { return detail::Perform(detail::Make("PUT", aOptions...)); }
template<typename... Ts> Response Patch(Ts&&... aOptions) { return detail::Perform(detail::Make("PATCH", aOptions...)); }
template<typename... Ts> Response Delete(Ts&&... aOptions) { return detail::Perform(detail::Make("DELETE", aOptions...)); }

template<typename... Ts> AsyncResponse GetAsync(Ts&&... aOptions) { return detail::Async(detail::Make("GET", aOptions...)); }
template<typename... Ts> AsyncResponse PostAsync(Ts&&... aOptions) { return detail::Async(detail::Make("POST", aOptions...)); }
template<typename... Ts> AsyncResponse PutAsync(Ts&&... aOptions) { return detail::Async(detail::Make("PUT", aOptions...)); }
template<typename... Ts> AsyncResponse PatchAsync(Ts&&... aOptions) { return detail::Async(detail::Make("PATCH", aOptions...)); }
template<typename... Ts> AsyncResponse DeleteAsync(Ts&&... aOptions) { return detail::Async(detail::Make("DELETE", aOptions...)); }
} // namespace cpr
