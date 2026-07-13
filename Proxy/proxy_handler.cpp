#include "proxy_handler.hpp"
#include "../Core/global.hpp"
#include "../Monitoring/monitoring.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace asio  = boost::asio;
using tcp       = asio::ip::tcp;

namespace {

constexpr std::chrono::seconds kPoolIdleTimeout{60};
constexpr std::size_t kResolveTimeoutSec = 10;

bool is_hop_by_hop(http::field name)
{
    switch (name)
    {
        case http::field::connection:
        case http::field::keep_alive:
        case http::field::proxy_authenticate:
        case http::field::proxy_authorization:
        case http::field::te:
        case http::field::trailer:
        case http::field::transfer_encoding:
        case http::field::upgrade:
            return true;
        default:
            return false;
    }
}

bool is_hop_by_hop_string(boost::string_view name)
{
    return boost::iequals(name, "connection")          ||
           boost::iequals(name, "keep-alive")          ||
           boost::iequals(name, "proxy-authenticate")  ||
           boost::iequals(name, "proxy-authorization") ||
           boost::iequals(name, "te")                  ||
           boost::iequals(name, "trailer")             ||
           boost::iequals(name, "transfer-encoding")   ||
           boost::iequals(name, "upgrade")             ||
           boost::iequals(name, "expect");
}

std::string pool_key(const std::string& host, unsigned port)
{
    return host + ":" + std::to_string(port);
}

// ---------------------------------------------------------------------------
// Simple per-(host,port) connection pool with idle expiry
// ---------------------------------------------------------------------------
class BackendConnectionPool
{
public:
    static BackendConnectionPool& instance()
    {
        static BackendConnectionPool pool;
        return pool;
    }

    std::shared_ptr<beast::tcp_stream> acquire(
        asio::io_context& ioc,
        const std::string& host,
        unsigned port)
    {
        const auto key = pool_key(host, port);
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(mutex_);
        prune_expired_locked(now);

        auto& bucket = pools_[key];
        while (!bucket.empty())
        {
            auto entry = std::move(bucket.front());
            bucket.pop_front();

            if (!entry.stream)
                continue;

            if (now - entry.idle_since > kPoolIdleTimeout)
                continue;

            if (!entry.stream->socket().is_open())
                continue;

            return entry.stream;
        }
        return std::make_shared<beast::tcp_stream>(ioc);
    }

    void release(
        const std::string& host,
        unsigned port,
        std::shared_ptr<beast::tcp_stream> stream,
        bool reusable)
    {
        if (!stream)
            return;

        if (!reusable || !stream->socket().is_open())
        {
            beast::error_code ec;
            stream->socket().shutdown(tcp::socket::shutdown_both, ec);
            stream->socket().close(ec);
            return;
        }

        const auto key = pool_key(host, port);
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(mutex_);
        prune_expired_locked(now);
        pools_[key].push_back(PoolEntry{stream, now});
    }

private:
    struct PoolEntry
    {
        std::shared_ptr<beast::tcp_stream> stream;
        std::chrono::steady_clock::time_point idle_since;
    };

    void prune_expired_locked(std::chrono::steady_clock::time_point now)
    {
        for (auto& [key, bucket] : pools_)
        {
            bucket.remove_if([&](const PoolEntry& e) {
                return !e.stream ||
                       !e.stream->socket().is_open() ||
                       (now - e.idle_since > kPoolIdleTimeout);
            });
        }
    }

    std::mutex mutex_;
    std::unordered_map<std::string, std::list<PoolEntry>> pools_;
};

void async_send_error(
    std::shared_ptr<Session> session,
    const std::string& client_ip,
    unsigned status_code,
    const std::string& reason);

// ---------------------------------------------------------------------------
// Per-request async proxy state machine
// ---------------------------------------------------------------------------
class ProxySession : public std::enable_shared_from_this<ProxySession>
{
public:
    ProxySession(
        std::shared_ptr<http::request<http::dynamic_body>> client_req,
        std::shared_ptr<Session> session,
        BackendConfig backend)
        : client_req_(std::move(client_req))
        , session_(std::move(session))
        , backend_(std::move(backend))
        , resolver_(io_context)
        , resolve_timer_(io_context)
        , backend_stream_(BackendConnectionPool::instance().acquire(
              io_context, backend.host, backend.port))
        , read_buffer_(std::make_shared<beast::flat_buffer>())
        , backend_res_(std::make_shared<http::response<http::dynamic_body>>())
    {
        try
        {
            client_ip_ = session_->socket().remote_endpoint().address().to_string();
        }
        catch (...)
        {
            client_ip_ = "unknown";
        }

        method_ = std::string(client_req_->method_string());
        path_   = std::string(client_req_->target());
        start_time_ = std::chrono::steady_clock::now();
    }

    void start()
    {
        Monitoring::log_info(false, client_ip_,
            "PROXY " + method_ + " " + path_ +
            " → " + backend_.host + ":" + std::to_string(backend_.port));

        if (expects_continue_())
        {
            send_100_continue([self = shared_from_this()](bool ok) {
                if (!ok)
                    return;
                self->begin_backend();
            });
            return;
        }

        begin_backend();
    }

private:
    bool expects_continue_() const
    {
        auto it = client_req_->find(http::field::expect);
        return it != client_req_->end() &&
               boost::iequals(it->value(), "100-continue");
    }

    void send_100_continue(std::function<void(bool)> on_done)
    {
        auto continue_res = std::make_shared<http::response<http::empty_body>>();
        continue_res->version(client_req_->version());
        continue_res->result(http::status::continue_);
        continue_res->set(http::field::server, "HTTP_Server/Proxy");
        continue_res->keep_alive(client_req_->keep_alive());

        http::async_write(
            session_->socket(),
            *continue_res,
            [self = shared_from_this(), continue_res, on_done = std::move(on_done)]
            (beast::error_code ec, std::size_t)
            {
                if (ec)
                {
                    Monitoring::log_error(self->client_ip_,
                        "PROXY: Failed sending 100 Continue — " + ec.message());
                    on_done(false);
                    return;
                }
                on_done(true);
            });
    }

    void begin_backend()
    {
        backend_req_ = build_backend_request();

        if (backend_stream_->socket().is_open())
        {
            reused_pooled_ = true;
            do_write_request();
            return;
        }

        do_resolve();
    }

    std::shared_ptr<http::request<http::dynamic_body>> build_backend_request()
    {
        auto out = std::make_shared<http::request<http::dynamic_body>>();

        out->method(client_req_->method());
        out->target(client_req_->target());
        out->version(11);
        out->body() = std::move(client_req_->body());

        for (const auto& field : client_req_->base())
        {
            if (is_hop_by_hop(field.name()) || is_hop_by_hop_string(field.name_string()))
                continue;
            out->set(field.name(), field.value());
        }

        std::ostringstream host_value;
        host_value << backend_.host;
        if ((backend_.port != 80))
            host_value << ':' << backend_.port;
        out->set(http::field::host, host_value.str());

        out->set("X-Forwarded-For", client_ip_);
        out->set("Via", "1.1 http_server_proxy");
        out->set("X-Proxy-By", "http_server_proxy");

        if (!client_req_->keep_alive())
            out->set(http::field::connection, "close");

        out->prepare_payload();
        client_keep_alive_ = client_req_->keep_alive();
        return out;
    }

    void do_resolve()
    {
        resolve_timer_.expires_after(std::chrono::seconds(kResolveTimeoutSec));
        resolve_timer_.async_wait(
            [self = shared_from_this()](beast::error_code ec) {
                if (!ec)
                {
                    self->resolver_.cancel();
                    self->fail(504, "Gateway Timeout: DNS resolve timed out");
                }
            });

        resolver_.async_resolve(
            backend_.host,
            std::to_string(backend_.port),
            [self = shared_from_this()](
                beast::error_code ec,
                tcp::resolver::results_type results)
            {
                self->resolve_timer_.cancel();
                if (ec)
                {
                    if (ec == asio::error::operation_aborted)
                        return;
                    Monitoring::log_error(self->client_ip_,
                        "PROXY: DNS resolve failed — " + ec.message());
                    self->fail(502, "Bad Gateway: DNS resolve failed — " + ec.message());
                    return;
                }
                self->do_connect(std::move(results));
            });
    }

    void do_connect(tcp::resolver::results_type results)
    {
        backend_stream_->expires_after(
            std::chrono::seconds(config.backend_connect_timeout));

        backend_stream_->async_connect(
            std::move(results),
            [self = shared_from_this()](
                beast::error_code ec,
                const tcp::endpoint&)
            {
                self->backend_stream_->expires_never();
                if (ec)
                {
                    if (ec == asio::error::operation_aborted)
                    {
                        self->fail(504, "Gateway Timeout: backend connect timed out");
                        return;
                    }
                    Monitoring::log_error(self->client_ip_,
                        "PROXY: Connect failed to " + self->backend_.host + ":" +
                        std::to_string(self->backend_.port) + " — " + ec.message());
                    self->fail(502,
                        "Bad Gateway: cannot connect to backend — " + ec.message());
                    return;
                }
                self->do_write_request();
            });
    }

    void do_write_request()
    {
        backend_stream_->expires_after(
            std::chrono::seconds(config.backend_connect_timeout));

        req_serializer_ = std::make_shared<http::request_serializer<http::dynamic_body>>(
            *backend_req_);

        http::async_write(
            *backend_stream_,
            *req_serializer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                self->backend_stream_->expires_never();
                if (ec)
                {
                    if (self->reused_pooled_ && !self->retried_with_fresh_connection_)
                    {
                        self->retried_with_fresh_connection_ = true;
                        self->reused_pooled_ = false;
                        beast::error_code close_ec;
                        self->backend_stream_->socket().close(close_ec);
                        self->backend_stream_ = std::make_shared<beast::tcp_stream>(io_context);
                        self->do_resolve();
                        return;
                    }
                    Monitoring::log_error(self->client_ip_,
                        "PROXY: Failed writing request to backend — " + ec.message());
                    self->fail(502,
                        "Bad Gateway: failed to forward request — " + ec.message());
                    return;
                }
                self->do_read_response();
            });
    }

    void do_read_response()
    {
        backend_stream_->expires_after(
            std::chrono::seconds(config.backend_response_timeout));

        http::async_read(
            *backend_stream_,
            *read_buffer_,
            *backend_res_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                self->backend_stream_->expires_never();
                if (ec && ec != http::error::end_of_stream)
                {
                    if (ec == asio::error::operation_aborted)
                    {
                        self->fail(504, "Gateway Timeout: backend response timed out");
                        return;
                    }
                    Monitoring::log_error(self->client_ip_,
                        "PROXY: Failed reading backend response — " + ec.message());
                    self->fail(502,
                        "Bad Gateway: error reading backend response — " + ec.message());
                    return;
                }
                self->patch_backend_response();
                self->do_write_client_response();
            });
    }

    void patch_backend_response()
    {
        backend_res_->set("X-Proxy", "http_server_proxy");
        backend_res_->set("X-Proxy-By", "http_server_proxy");

        // Honour client keep-alive preference on the outward response
        const bool backend_keep_alive = backend_res_->keep_alive();
        backend_res_->keep_alive(client_keep_alive_ && backend_keep_alive);

        if (!backend_res_->keep_alive())
            backend_res_->set(http::field::connection, "close");

        backend_keep_alive_ = backend_res_->keep_alive();
    }

    void do_write_client_response()
    {
        res_serializer_ = std::make_shared<http::response_serializer<http::dynamic_body>>(
            *backend_res_);

        http::async_write(
            session_->socket(),
            *res_serializer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec)
                {
                    Monitoring::log_error(self->client_ip_,
                        "PROXY: Failed writing response to client — " + ec.message());
                }
                else
                {
                    self->log_success();
                }
                self->release_backend_connection();
            });
    }

    void release_backend_connection()
    {
        const bool reusable = backend_keep_alive_ && !failed_;
        BackendConnectionPool::instance().release(
            backend_.host, backend_.port, backend_stream_, reusable);
    }

    void log_success()
    {
        const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time_).count();

        std::ostringstream log_line;
        log_line << "PROXY " << method_ << " " << path_
                 << " → " << backend_.host << ":" << backend_.port
                 << " status=" << static_cast<unsigned>(backend_res_->result())
                 << " latency=" << latency_ms << "ms";

        Monitoring::log_info(true, client_ip_, log_line.str());
        std::cout << "[PROXY] " << log_line.str() << std::endl;
    }

    void fail(unsigned status_code, const std::string& reason)
    {
        if (failed_)
            return;
        failed_ = true;

        Monitoring::log_error(client_ip_, "PROXY: " + reason);
        send_error(status_code, reason);
        release_backend_connection();
    }

    void send_error(unsigned status_code, const std::string& reason)
    {
        async_send_error(session_, client_ip_, status_code, reason);
    }

    std::shared_ptr<http::request<http::dynamic_body>> client_req_;
    std::shared_ptr<Session> session_;
    BackendConfig backend_;   // stored by value — avoids dangling ref
    std::string client_ip_;
    std::string method_;
    std::string path_;

    tcp::resolver resolver_;
    asio::steady_timer resolve_timer_;
    std::shared_ptr<beast::tcp_stream> backend_stream_;

    std::shared_ptr<http::request<http::dynamic_body>> backend_req_;
    std::shared_ptr<http::request_serializer<http::dynamic_body>> req_serializer_;
    std::shared_ptr<beast::flat_buffer> read_buffer_;
    std::shared_ptr<http::response<http::dynamic_body>> backend_res_;
    std::shared_ptr<http::response_serializer<http::dynamic_body>> res_serializer_;

    bool client_keep_alive_{false};
    bool backend_keep_alive_{false};
    bool reused_pooled_{false};
    bool retried_with_fresh_connection_{false};
    bool failed_{false};
    std::chrono::steady_clock::time_point start_time_;
};

void async_send_error(
    std::shared_ptr<Session> session,
    const std::string& client_ip,
    unsigned status_code,
    const std::string& reason)
{
    auto res = std::make_shared<http::response<http::string_body>>();

    switch (status_code)
    {
        case 502: res->result(http::status::bad_gateway);         break;
        case 504: res->result(http::status::gateway_timeout);     break;
        default:  res->result(http::status::internal_server_error); break;
    }

    res->version(11);
    res->set(http::field::server, "HTTP_Server/Proxy");
    res->set(http::field::content_type, "text/plain");
    res->keep_alive(false);
    res->body() = std::to_string(status_code) + " " + reason + "\r\n";
    res->prepare_payload();

    http::async_write(
        session->socket(),
        *res,
        [res, client_ip](beast::error_code ec, std::size_t) {
            if (ec)
            {
                Monitoring::log_error(client_ip,
                    "PROXY: Failed sending error response — " + ec.message());
            }
        });
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void ProxyHandler::forwardRequest(
    std::shared_ptr<http::request<http::dynamic_body>> req,
    std::shared_ptr<Session> session)
{
    const std::string path(req->target().data(), req->target().size());

    std::string client_ip;
    try
    {
        client_ip = session->socket().remote_endpoint().address().to_string();
    }
    catch (...)
    {
        client_ip = "unknown";
    }

    const BackendConfig* backend = config.matchBackend(path);
    if (!backend)
    {
        Monitoring::log_error(client_ip,
            "PROXY: No backend matched path '" + path + "' — returning 502");
        async_send_error(session, client_ip, 502,
            "Bad Gateway: no backend configured for path '" + path + "'");
        return;
    }

    auto proxy = std::make_shared<ProxySession>(std::move(req), std::move(session), *backend);
    proxy->start();
}

void ProxyHandler::forwardRequest(
    std::shared_ptr<http::request<http::dynamic_body>> req,
    std::shared_ptr<Session> session,
    const BackendConfig& backend)
{
    auto proxy = std::make_shared<ProxySession>(std::move(req), std::move(session), backend);
    proxy->start();
}
