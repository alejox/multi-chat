// Red en macOS con NSURLSession (HTTPS y WebSocket del sistema, macOS 10.15+).

#include "net.hpp"

#import <Foundation/Foundation.h>

#include <atomic>

namespace net {

    const char *const kUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                                   "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

    namespace {

        constexpr NSUInteger kMaxBody = 16 * 1024 * 1024;

        NSURLSession *makeSession(NSTimeInterval timeout)
        {
            NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
            config.HTTPAdditionalHeaders = @{@"User-Agent": @(kUserAgent)};
            config.timeoutIntervalForRequest = timeout;
            config.HTTPCookieStorage = nil;
            config.URLCache = nil;
            return [NSURLSession sessionWithConfiguration:config];
        }

        NSURLSession *httpSession()
        {
            static NSURLSession *session = makeSession(15);
            return session;
        }

        // En un WebSocket la espera de mensajes no tiene límite (Twitch hace PING cada 5 min).
        NSURLSession *socketSession()
        {
            static NSURLSession *session = makeSession(7 * 24 * 3600);
            return session;
        }

        NSMutableURLRequest *makeRequest(const std::string &url, const Headers &headers, NSTimeInterval timeout,
                                         std::string &error)
        {
            NSURL *u = [NSURL URLWithString:@(url.c_str())];
            if (!u) {
                error = "URL no válida: " + url;
                return nil;
            }
            NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:u
                                                                   cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                               timeoutInterval:timeout];
            for (const auto &[name, value] : headers)
                [request setValue:@(value.c_str()) forHTTPHeaderField:@(name.c_str())];
            return request;
        }

        std::string describe(NSError *error)
        {
            const char *text = error.localizedDescription.UTF8String;
            return text ? text : "error de red";
        }

        class MacWebSocket : public WebSocket
        {
              public:
            explicit MacWebSocket(NSURLSessionWebSocketTask *task) : task_(task) {}
            ~MacWebSocket() override
            {
                close();
            }

            Result receive(std::string &message) override
            {
                if (closed_)
                    return Result::Closed;
                __block bool ok = false;
                __block std::string text;
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                [task_ receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *msg, NSError *error) {
                    if (!error && msg) {
                        ok = true;
                        if (msg.type == NSURLSessionWebSocketMessageTypeString) {
                            const char *utf8 = msg.string.UTF8String;
                            text = utf8 ? utf8 : "";
                        } else {
                            text.assign(static_cast<const char *>(msg.data.bytes), msg.data.length);
                        }
                    }
                    dispatch_semaphore_signal(done);
                }];
                dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
                if (!ok || closed_)
                    return Result::Closed;
                message = std::move(text);
                return Result::Message;
            }

            bool send(const std::string &text) override
            {
                if (closed_)
                    return false;
                NSURLSessionWebSocketMessage *msg =
                    [[NSURLSessionWebSocketMessage alloc] initWithString:@(text.c_str())];
                [task_ sendMessage:msg completionHandler:^(NSError *) {
                }];
                return true;
            }

            void close() override
            {
                // Cancelar la tarea completa el receive pendiente con un error.
                if (!closed_.exchange(true))
                    [task_ cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
            }

              private:
            NSURLSessionWebSocketTask *task_;
            std::atomic<bool> closed_ {false};
        };

    }  // namespace

    Response request(const std::string &method, const std::string &url, const Headers &headers, const std::string &body,
                     int timeoutMs)
    {
        Response res;
        @autoreleasepool {
            NSMutableURLRequest *req = makeRequest(url, headers, timeoutMs / 1000.0, res.error);
            if (!req)
                return res;
            req.HTTPMethod = @(method.c_str());
            if (!body.empty())
                req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];

            __block Response out;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            NSURLSessionDataTask *task = [httpSession()
                dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                    if (error) {
                        out.error = describe(error);
                    } else if (data.length > kMaxBody) {
                        out.error = "respuesta demasiado grande";
                    } else {
                        out.status = (int) ((NSHTTPURLResponse *) response).statusCode;
                        out.body.assign(static_cast<const char *>(data.bytes), data.length);
                    }
                    dispatch_semaphore_signal(done);
                }];
            [task resume];
            dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
            res = out;
        }
        return res;
    }

    std::shared_ptr<WebSocket> connectWebSocket(const std::string &url, const Headers &headers, std::string &error)
    {
        @autoreleasepool {
            NSMutableURLRequest *req = makeRequest(url, headers, 15, error);
            if (!req)
                return nullptr;
            NSURLSessionWebSocketTask *task = [socketSession() webSocketTaskWithRequest:req];
            task.maximumMessageSize = kMaxBody;
            [task resume];
            // Los errores de conexión aparecen en el primer receive(): el conector reintenta.
            return std::make_shared<MacWebSocket>(task);
        }
    }

}  // namespace net
