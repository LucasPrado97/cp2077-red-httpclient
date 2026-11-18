// macOS / iOS (CyPort) only. cpr's request functions on NSURLSession. See cpr/cpr.h.
#import <Foundation/Foundation.h>

#include "cpr/cpr.h"

#include <dispatch/dispatch.h>

namespace
{
// curl_easy_escape: everything but the RFC 3986 unreserved characters is percent-encoded.
std::string UrlEncode(const std::string& aText)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(aText.size() * 3);
    for (const unsigned char c : aText)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

// The reason phrase of the status line. HTTP/2 and HTTP/3 have none and NSHTTPURLResponse does not expose HTTP/1.1's, so it is
// the standard phrase of the code (what a HTTP/1.1 server sends).
const char* ReasonPhrase(long aStatus)
{
    switch (aStatus)
    {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Content";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default: return "";
    }
}

cpr::ErrorCode MapError(NSError* aError)
{
    if (![aError.domain isEqualToString:NSURLErrorDomain])
    {
        return cpr::ErrorCode::UNKNOWN_ERROR;
    }
    switch (aError.code)
    {
    case NSURLErrorBadURL:
    case NSURLErrorUnsupportedURL:
        return cpr::ErrorCode::INVALID_URL_FORMAT;
    case NSURLErrorTimedOut:
        return cpr::ErrorCode::OPERATION_TIMEDOUT;
    case NSURLErrorCannotFindHost:
    case NSURLErrorDNSLookupFailed:
        return cpr::ErrorCode::HOST_RESOLUTION_FAILURE;
    case NSURLErrorCannotConnectToHost:
    case NSURLErrorNotConnectedToInternet:
    case NSURLErrorInternationalRoamingOff:
    case NSURLErrorDataNotAllowed:
        return cpr::ErrorCode::CONNECTION_FAILURE;
    case NSURLErrorNetworkConnectionLost:
        return cpr::ErrorCode::NETWORK_RECEIVE_ERROR;
    case NSURLErrorHTTPTooManyRedirects:
        return cpr::ErrorCode::TOO_MANY_REDIRECTS;
    case NSURLErrorSecureConnectionFailed:
        return cpr::ErrorCode::SSL_CONNECT_ERROR;
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateNotYetValid:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateUntrusted:
        return cpr::ErrorCode::SSL_REMOTE_CERTIFICATE_ERROR;
    case NSURLErrorClientCertificateRejected:
    case NSURLErrorClientCertificateRequired:
        return cpr::ErrorCode::SSL_LOCAL_CERTIFICATE_ERROR;
    case NSURLErrorAppTransportSecurityRequiresSecureConnection:
        return cpr::ErrorCode::UNSUPPORTED_PROTOCOL;
    case NSURLErrorCancelled:
        return cpr::ErrorCode::REQUEST_CANCELLED;
    case NSURLErrorZeroByteResource:
        return cpr::ErrorCode::EMPTY_RESPONSE;
    default:
        return cpr::ErrorCode::UNKNOWN_ERROR;
    }
}

// One session for the life of the process. Ephemeral: no cookie store, no cache and no credentials on disk (the Windows build
// keeps none either). No delegate: the system's default server trust evaluation applies to every connection.
NSURLSession* Session()
{
    static NSURLSession* session;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
      config.TLSMinimumSupportedProtocolVersion = tls_protocol_version_TLSv12;
      config.HTTPShouldSetCookies = NO;
      config.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
      config.HTTPCookieStorage = nil;
      config.URLCache = nil;
      config.URLCredentialStorage = nil;
      config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
      config.timeoutIntervalForRequest = 60;
      // CFNetwork would send "<process> CFNetwork/<n> Darwin/<n>" and the device's language list. The Windows build (curl)
      // sends neither, so name the plugin and no locale. A header of the request replaces these.
      config.HTTPAdditionalHeaders = @{@"User-Agent" : @CPR_APPLE_USER_AGENT, @"Accept-Language" : @"en"};
      session = [NSURLSession sessionWithConfiguration:config];
    });
    return session;
}
} // namespace

void cpr::detail::Set(Request& aRequest, const Body& aBody)
{
    // cpr hands a Body to curl as post fields; RedHttpClient always sends its own Content-Type with one (text/plain by default).
    aRequest.body = aBody.value;
    aRequest.has_body = !aBody.value.empty();
    aRequest.content_type = "application/x-www-form-urlencoded";
}

void cpr::detail::Set(Request& aRequest, const Payload& aPayload)
{
    std::string body;
    for (const auto& pair : aPayload.pairs)
    {
        if (!body.empty())
        {
            body.push_back('&');
        }
        body += UrlEncode(pair.key);
        body.push_back('=');
        body += UrlEncode(pair.value);
    }
    aRequest.body = std::move(body);
    aRequest.has_body = true;
    aRequest.content_type = "application/x-www-form-urlencoded";
}

void cpr::detail::Set(Request& aRequest, const Multipart& aMultipart)
{
    @autoreleasepool
    {
        const std::string boundary = std::string("------------------------") + [[[NSUUID UUID].UUIDString stringByReplacingOccurrencesOfString:@"-" withString:@""] UTF8String];
        std::string body;
        for (const auto& part : aMultipart.parts)
        {
            std::string name;
            for (const char c : part.name)
            {
                // A quote or a line break inside the field name would end the header early.
                if (c == '"')
                {
                    name += "%22";
                }
                else if (c == '\r')
                {
                    name += "%0D";
                }
                else if (c == '\n')
                {
                    name += "%0A";
                }
                else
                {
                    name.push_back(c);
                }
            }
            body += "--" + boundary + "\r\n";
            body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
            body += part.value + "\r\n";
        }
        body += "--" + boundary + "--\r\n";
        aRequest.body = std::move(body);
        aRequest.has_body = true;
        aRequest.content_type = "multipart/form-data; boundary=" + boundary;
    }
}

cpr::Response cpr::detail::Perform(const Request& aRequest)
{
    Response response;
    @autoreleasepool
    {
        NSString* text = [[NSString alloc] initWithBytes:aRequest.url.data() length:aRequest.url.size() encoding:NSUTF8StringEncoding];
        NSURL* url = text ? [NSURL URLWithString:text] : nil;
        if (!url || !url.scheme || !url.host)
        {
            response.error.code = ErrorCode::INVALID_URL_FORMAT;
            response.error.message = "URL is not valid: " + aRequest.url;
            return response;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        request.HTTPMethod = [NSString stringWithUTF8String:aRequest.method.c_str()];
        if (aRequest.has_body)
        {
            request.HTTPBody = [NSData dataWithBytes:aRequest.body.data() length:aRequest.body.size()];
            if (!aRequest.content_type.empty())
            {
                [request setValue:[NSString stringWithUTF8String:aRequest.content_type.c_str()] forHTTPHeaderField:@"Content-Type"];
            }
        }
        for (const auto& [name, value] : aRequest.header)
        {
            NSString* field = [[NSString alloc] initWithBytes:name.data() length:name.size() encoding:NSUTF8StringEncoding];
            NSString* content = [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
            if (field.length && content)
            {
                [request setValue:content forHTTPHeaderField:field];
            }
        }

        __block NSData* resultData = nil;
        __block NSURLResponse* resultResponse = nil;
        __block NSError* resultError = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionDataTask* task = [Session() dataTaskWithRequest:request
                                                  completionHandler:^(NSData* aData, NSURLResponse* aResponse, NSError* aError) {
                                                    resultData = aData;
                                                    resultResponse = aResponse;
                                                    resultError = aError;
                                                    dispatch_semaphore_signal(done);
                                                  }];
        [task resume];
        // The completion handler runs on the session's own queue, never on the calling thread, so waiting here cannot deadlock.
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);

        if (resultError)
        {
            response.error.code = MapError(resultError);
            const char* description = resultError.localizedDescription.UTF8String;
            response.error.message = std::string(description ? description : "request failed") + " (" +
                                     resultError.domain.UTF8String + " " + std::to_string(static_cast<long>(resultError.code)) + ")";
            return response;
        }
        if (![resultResponse isKindOfClass:[NSHTTPURLResponse class]])
        {
            response.error.code = ErrorCode::UNSUPPORTED_PROTOCOL;
            response.error.message = "the answer is not an HTTP response";
            return response;
        }
        NSHTTPURLResponse* http = (NSHTTPURLResponse*)resultResponse;
        response.status_code = static_cast<long>(http.statusCode);
        response.reason = ReasonPhrase(response.status_code);
        NSDictionary* fields = http.allHeaderFields;
        for (id key in fields)
        {
            const char* name = [[key description] UTF8String];
            const char* value = [[fields[key] description] UTF8String];
            if (name && value)
            {
                response.header[name] = value;
            }
        }
        if (resultData.length)
        {
            response.text.assign(static_cast<const char*>(resultData.bytes), resultData.length);
        }
    }
    return response;
}
