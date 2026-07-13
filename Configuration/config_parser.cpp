#include "config_parser.hpp"
#include "../Error_handling/error_handling.hpp"
#include "../Monitoring/monitoring.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>
#include <filesystem>

using namespace std;


void Config::load(const string& file_path)
{
    Monitoring::log_info(true,"-","Attempting to open configuration file");
    ifstream config_file(file_path);
    
    if (!config_file.is_open())
    {
        cerr << "Failed to open configuration file: " << file_path << endl;
        General_error error(false, "Error occurred in opening configuration file", WARNING);
        Monitoring::log_error("-","Error occue in opeing configuration file");
        try 
        {
            std::ofstream def_config_file(file_path);
            if (!def_config_file.is_open()) 
            {
                throw General_error(true, "Error occurred in creating configuration file", ERROR);
            }

            def_config_file << "server_host=0.0.0.0\n";
            def_config_file << "server_port=8000\n";
            def_config_file << "root_dir=./var\n";

            Monitoring::log_info(true,"-","Default configuration file created.");
        } 
        catch (const General_error& e) {
            
            General_error(true,"Failed to create the default configuration file",ERROR);
            Monitoring::log_error("-","Error on creation of the default configuration file.");
            return;
        }

        config_file.open(file_path);
        if (!config_file.is_open())
        {
            Monitoring::log_error("-", "Failed to reopen configuration file after creating defaults.");
            return;
        }
     }            

    Monitoring::log_info(true,"-","Configuration file opened successfully.");

    backends.clear();

    // Temporary map to accumulate backend fields keyed by backend name
    // e.g.  partial_backends["api"]["host"] = "127.0.0.1"
    std::map<std::string, std::map<std::string, std::string>> partial_backends;

    string line;
    while(getline(config_file,line))
    {
        // Skip blank lines and comment lines
        if (line.empty() || line[0] == '#')
            continue;

        istringstream per_line(line);
        string value, key;
        if (getline(per_line, key, '=') && getline(per_line, value))
        {
            // Trim trailing whitespace/carriage returns
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            if (key == "server_host") 
                host = value;
            else if (key == "server_port") 
                port = std::stoi(value);
            else if (key == "root_dir") 
                root_dir = value;
            else if (key == "thread_pool_size")
                thread_pool_size = std::stoi(value);
            else if (key == "backend_connect_timeout")
                backend_connect_timeout = std::stoi(value);
            else if (key == "backend_response_timeout")
                backend_response_timeout = std::stoi(value);
            else if (key.rfind("backend.", 0) == 0)
            {
                // Parse "backend.<name>.<field>" entries
                std::string rest = key.substr(8); // strip "backend."
                auto dot = rest.find('.');
                if (dot != std::string::npos)
                {
                    std::string bname = rest.substr(0, dot);
                    std::string field = rest.substr(dot + 1);
                    partial_backends[bname][field] = value;
                }
            }
        }
    }
    config_file.close();

    // Assemble BackendConfig objects from accumulated partial data
    for (auto& [name, fields] : partial_backends)
    {
        BackendConfig bc;
        bc.name = name;
        if (fields.count("host"))        bc.host        = fields["host"];
        if (fields.count("port"))        bc.port        = std::stoi(fields["port"]);
        if (fields.count("path_prefix")) bc.path_prefix = fields["path_prefix"];

        if (!bc.host.empty() && bc.port != 0 && !bc.path_prefix.empty())
        {
            backends.push_back(bc);
            Monitoring::log_info(true, "-", "Registered backend '" + bc.name +
                "' at " + bc.host + ":" + std::to_string(bc.port) +
                " for prefix '" + bc.path_prefix + "'");
        }
        else
        {
            Monitoring::log_error("-", "Incomplete backend config for '" + name +
                "' — skipping (need host, port, path_prefix)");
        }
    }

    std::filesystem::path root_path(root_dir);
    if (!std::filesystem::exists(root_path) || !std::filesystem::is_directory(root_path))
    {
        try
        {
            std::filesystem::create_directories(root_path);
            Monitoring::log_info(true, "-", "Created missing root directory at " + root_dir);
        }
        catch (const std::exception& e)
        {
            General_error(true, "Root directory loading failed: " + std::string(e.what()), ERROR);
            Monitoring::log_error("-", "Root directory loading failed: " + std::string(e.what()));
            return;
        }
    }

    Monitoring::log_info(true,"-","The root directory is available at " + root_dir);
}

// Find the backend whose path_prefix is the longest prefix of `path`.
// Returns nullptr if no backend matches.
const BackendConfig* Config::matchBackend(const std::string& path) const
{
    const BackendConfig* best = nullptr;
    std::size_t best_len = 0;

    for (const auto& bc : backends)
    {
        const std::string& pfx = bc.path_prefix;
        if (path.rfind(pfx, 0) == 0)   // path starts with pfx
        {
            // Make sure it's a real prefix boundary (avoid /ap matching /api)
            bool boundary = (pfx == "/" ||
                             path.size() == pfx.size() ||
                             path[pfx.size()] == '/');
            if (boundary && pfx.size() > best_len)
            {
                best_len = pfx.size();
                best = &bc;
            }
        }
    }
    return best;
}
