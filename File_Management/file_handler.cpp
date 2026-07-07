#include "file_handler.hpp"
#include "../Error_handling/error_handling.hpp"
#include "../Configuration/config_parser.hpp"
#include "../Monitoring/monitoring.hpp"
#include "../Core/global.hpp"
#include <iostream>
#include <filesystem>
#include <sys/stat.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <thread>
#include "file_cache.hpp"
#include <mutex>
#include <shared_mutex>
#include <boost/beast/http.hpp>

using namespace std;

static FileCache fileCache;
static std::shared_mutex fileMutex_; // reader-writer lock (many readers, few writers)

FileManager::FileManager()
{
    rootDirectory = config.root_dir;
}

void FileManager::createFile(const std::string& filePath) {
    std::ofstream file(rootDirectory + filePath);
    if (!file.is_open()) {
        General_error err(true, "Failed to create file: " + filePath, ERROR);
        return;
    }
    file.close();
}

std::string FileManager::readFile(const std::string& filePath) {
    // Fast path: check cache (already thread-safe inside FileCache)
    std::string cached = fileCache.get(filePath);
    if (!cached.empty()) {
        return cached;  // No logging on hot path — too expensive at 10K+ req/s
    }

    // Slow path: read from disk (rare after warmup)
    std::shared_lock<std::shared_mutex> lock(fileMutex_);
    try {
        std::ifstream file(rootDirectory + filePath);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            file.close();
            fileCache.put(filePath, content);
            return content;
        } else {
            return "";
        }
    } catch (const std::exception& e) {
        return "";
    }
}

bool FileManager::fileExists(const std::string& filePath) 
{
    // Check cache first (no syscall needed)
    std::string cached = fileCache.get(filePath);
    if (!cached.empty()) {
        return true;
    }
    // Fall back to filesystem check
    std::filesystem::path path(rootDirectory + filePath);
    return std::filesystem::exists(path);
}

bool FileManager::writeFile(const std::string& filePath, const std::string& content) {
    std::unique_lock<std::shared_mutex> lock(fileMutex_);
    std::ofstream file(rootDirectory + filePath, std::ios::out | std::ios::app);
    if (file.is_open()) 
    {
        file << content;
        file.close();
        // Invalidate cache for this file
        fileCache.put(filePath, content);
        return true;
    } 
    else 
    {
        return false;
    }
}

bool FileManager::deleteFile(const std::string& filePath) 
{
    std::unique_lock<std::shared_mutex> lock(fileMutex_);
    std::filesystem::path path(rootDirectory + filePath);
    return std::filesystem::remove(path);
}

bool FileManager::copyFile(const std::string& sourcePath, const std::string& destinationPath) {
    std::filesystem::path srcPath(rootDirectory + sourcePath);
    std::filesystem::path destPath(rootDirectory + destinationPath);
    try {
        std::filesystem::copy(srcPath, destPath, std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        General_error err(true, "File copy error: " + std::string(e.what()), ERROR);
        return false;
    }
}

bool FileManager::renameFile(const std::string& oldPath, const std::string& newPath) {
    std::filesystem::path oldFilePath(rootDirectory + oldPath);
    std::filesystem::path newFilePath(rootDirectory + newPath);
    try {
        std::filesystem::rename(oldFilePath, newFilePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        General_error err(true, "File rename error: " + std::string(e.what()), ERROR);
        return false;
    }
}

std::size_t FileManager::getFileSize(const std::string& filePath) {
    std::filesystem::path path(rootDirectory + filePath);
    try {
        if (std::filesystem::exists(path)) {
            return std::filesystem::file_size(path);
        } else {
            return 0;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        General_error err(true, "File size error: " + std::string(e.what()), ERROR);
        return 0;
    }
}
