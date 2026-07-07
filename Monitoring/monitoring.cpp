#include "monitoring.hpp"
#include "../Error_handling/error_handling.hpp"
#include "../Core/global.hpp"
#include "../Thread/thread.hpp"
#include <memory>
#include <boost/filesystem.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <json/json.h> 
#include <thread>
#include <mutex>

std::string Monitoring::log_root;
std::mutex file_mutex;

void Monitoring::init(const std::string& log_directory)
{
    log_root =  log_directory;
    boost::filesystem::create_directories(log_root);
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << log_root << "server_log/" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S") << ".log";
    std::string log_file = ss.str();

    setupLogging(log_file);

     boost::log::add_common_attributes();
}

void Monitoring::log_info(bool show,const std::string& client_ip, const std::string& message) 
{
    auto task = [show,client_ip,message]()
    {
        std::lock_guard<std::mutex> lock(file_mutex);
        BOOST_LOG_SCOPED_THREAD_TAG("ClientIP", client_ip);
        BOOST_LOG_TRIVIAL(info) << client_ip << " : " << message;

        if (show) 
        {
            if(client_ip == "-")
            {
                std::cout <<  message << std::endl;
            }
            else
            {
                std::cout << client_ip << " : " << message << std::endl;
            }
        }
    };
    global_thread_pool.post(task);
}

void Monitoring::log_error(const std::string& client_ip, const std::string& message) 
{
    
    auto  task = [client_ip,message]()
    {
        BOOST_LOG_SCOPED_THREAD_TAG("ClientIP", client_ip);
        BOOST_LOG_TRIVIAL(error) << client_ip << " : " << message;
    };
    global_thread_pool.post(task);

}


void Monitoring::logUserActivity(const std::string& client_ip,
                                 const std::string& user_agent,
                                 const std::string& http_target,
                                 const Json::Value& json_payload) 
{
    // Use Boost.Log instead of per-request JSON file I/O.
    // The old approach (read→parse→update→write JSON file per request)
    // was the #1 performance bottleneck (~5ms per request).
    BOOST_LOG_TRIVIAL(info) << client_ip << " : "
                            << "[" << user_agent << "] "
                            << http_target;
}


void Monitoring::setupLogging(const std::string& log_file) 
{
    boost::log::add_file_log(
        boost::log::keywords::file_name = log_file,
        boost::log::keywords::rotation_size = std::size_t(10 * 1024 * 1024), 
        boost::log::keywords::format = "[%TimeStamp%] [%Severity%]: %Message%",
        boost::log::keywords::auto_flush = true
    );
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::info
    );
    boost::log::add_common_attributes();
}



