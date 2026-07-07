#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include <string>
#include <vector>

using namespace std;

// Describes a single backend service for reverse proxy mode
struct BackendConfig
{
    std::string name;         // Logical name (from config key e.g. "api")
    std::string host;
    unsigned int port = 0;
    std::string path_prefix;  // e.g. "/api" or "/"
};

class Config
{
    public:
        string host;
        unsigned int port;
        int connection_limit;
        int thread_pool_size = 0; // 0 = auto-detect
        string root_dir;
        bool show;

        // Reverse proxy settings
        int backend_connect_timeout  = 30;  // seconds
        int backend_response_timeout = 60;  // seconds
        std::vector<BackendConfig> backends; // All configured backends

        void load(const string& file_path);

        // Find the best-matching backend for a given request path.
        // Returns nullptr if no match found.
        const BackendConfig* matchBackend(const std::string& path) const;
};


#endif
