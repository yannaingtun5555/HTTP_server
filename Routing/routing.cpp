#include "routing.hpp"
#include "../Error_handling/error_handling.hpp"
#include "../File_Management/file_handler.hpp"
#include "../Configuration/server_config.hpp"
#include "../Core/response_generator.hpp"
#include "../Monitoring/monitoring.hpp"
#include "../Thread/thread.hpp"
#include "../Core/global.hpp"
#include <boost/beast/http.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

Routing::Routing() {}

static std::string stripQueryAndFragment(std::string path)
{
    auto pos = path.find_first_of("?#");
    if (pos != std::string::npos)
        path.erase(pos);
    return path;
}

// ─────────────────────────────────────────────────────────────
//  readAbsoluteFile — reads a file from an absolute path directly
//  (bypasses FileManager::rootDirectory prepending)
// ─────────────────────────────────────────────────────────────
static std::string readAbsoluteFile(const std::string& abs_path)
{
    std::ifstream f(abs_path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ─────────────────────────────────────────────────────────────
//  resolvePath — map (domain, url_path) → absolute filesystem path
//  e.g. ("example.com", "/about") → "./sites/example.com/public/about"
// ─────────────────────────────────────────────────────────────
std::string Routing::resolvePath(const std::string& domain,
                                  const std::string& url_path) const
{
    std::string base;
    std::string path = stripQueryAndFragment(url_path);

    if (domain.empty())
    {
        // Fallback: legacy root layout keeps public files under
        // <root_dir>/www/ while error pages live under <root_dir>/error/.
        if (path.rfind("/error/", 0) == 0)
            return config.root_dir + path;

        base = config.root_dir + "/www";
    }
    else
    {
        base = server_config.sites_root + "/" + domain + "/public";
    }

    if (path.empty() || path == "/")
    {
        path = "/index.html";
    }

    return base + path;
}

// ─────────────────────────────────────────────────────────────
//  processRequest
// ─────────────────────────────────────────────────────────────
void Routing::processRequest(
    bool request_valid,
    const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
    std::shared_ptr<Session> session,
    const std::string& domain)
{
    try
    {
        std::string content;
        std::string target = std::string(req.target());

        if (!request_valid)
        {
            std::string err_path = resolvePath(domain, "/error/400.html");
            if (!fs::exists(err_path))
                err_path = resolvePath("", "/error/400.html");
            content = readAbsoluteFile(err_path);
            if (content.empty()) content = "<h1>400 Bad Request</h1>";
            response.generateAndSendResponse(req, session, content, 400);
            if (req.keep_alive()) session->do_read();
            return;
        }

        if (req.method_string() == "GET")
        {
            std::string file_path = resolvePath(domain, target);

            // Try the exact path first
            if (fs::exists(file_path) && fs::is_regular_file(file_path))
            {
                content = readAbsoluteFile(file_path);
                response.generateAndSendResponse(req, session, content, 200);
            }
            else
            {
                // If path is a directory, try index.html inside it
                std::string idx_path = file_path;
                if (!idx_path.empty() && idx_path.back() != '/')
                    idx_path += "/index.html";
                else
                    idx_path += "index.html";

                if (fs::exists(idx_path) && fs::is_regular_file(idx_path))
                {
                    content = readAbsoluteFile(idx_path);
                    response.generateAndSendResponse(req, session, content, 200);
                }
                else
                {
                    // 404
                    std::string err = resolvePath(domain, "/error/404.html");
                    if (!fs::exists(err))
                        err = resolvePath("", "/error/404.html");
                    content = readAbsoluteFile(err);
                    if (content.empty()) content = "<h1>404 Not Found</h1>";
                    response.generateAndSendResponse(req, session, content, 404);
                }
            }
        }
        else if (req.method_string() == "POST")
        {
            std::string request_body = boost::beast::buffers_to_string(req.body().data());
            std::string file_path = resolvePath(domain, target);
            try
            {
                // Ensure parent directory exists
                fs::create_directories(fs::path(file_path).parent_path());
                std::ofstream out(file_path, std::ios::out | std::ios::app);
                if (out.is_open())
                {
                    out << request_body;
                    out.close();
                    response.generateAndSendResponse(req, session, content, 201);
                }
                else
                {
                    throw std::runtime_error("Cannot open file for writing");
                }
            }
            catch (const std::exception& e)
            {
                std::string err = resolvePath(domain, "/error/500.html");
                if (!fs::exists(err))
                    err = resolvePath("", "/error/500.html");
                content = readAbsoluteFile(err);
                if (content.empty()) content = "<h1>500 Internal Server Error</h1>";
                response.generateAndSendResponse(req, session, content, 500);
            }
        }
        else
        {
            content = "Method Not Allowed";
            response.generateAndSendResponse(req, session, content, 405);
        }

        if (req.keep_alive())
            session->do_read();
    }
    catch (const std::exception& e)
    {
        // Silence on hot path
    }
}
