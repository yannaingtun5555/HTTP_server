#ifndef FILE_CACHE_HPP
#define FILE_CACHE_HPP

#include <unordered_map>
#include <list>
#include <string>
#include <shared_mutex>

// Thread-safe LRU cache using a read-write lock.
// Reads (cache hits) use a shared lock — many can proceed in parallel.
// Writes (cache misses/puts) use an exclusive lock.

class FileCache {
public:
    explicit FileCache(std::size_t maxBytes = 10 * 1024 * 1024) // default 10 MiB
        : maxBytes_(maxBytes), currentBytes_(0) {}

    // Retrieve cached content. Returns empty string if not present.
    std::string get(const std::string& key) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = cacheMap_.find(key);
        if (it == cacheMap_.end()) return {};
        // Move accessed entry to front (most recent).
        usage_.splice(usage_.begin(), usage_, it->second.second);
        return it->second.first;
    }

    // Insert or update a cache entry.
    void put(const std::string& key, const std::string& value) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = cacheMap_.find(key);
        std::size_t valueSize = value.size();
        if (it != cacheMap_.end()) {
            currentBytes_ -= it->second.first.size();
            usage_.splice(usage_.begin(), usage_, it->second.second);
            it->second.first = value;
        } else {
            usage_.push_front(key);
            cacheMap_[key] = {value, usage_.begin()};
        }
        currentBytes_ += valueSize;
        evictIfNeeded();
    }

    // Check if key exists without modifying LRU order
    bool contains(const std::string& key) {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return cacheMap_.find(key) != cacheMap_.end();
    }

private:
    void evictIfNeeded() {
        while (currentBytes_ > maxBytes_ && !usage_.empty()) {
            const std::string& oldKey = usage_.back();
            auto it = cacheMap_.find(oldKey);
            if (it != cacheMap_.end()) {
                currentBytes_ -= it->second.first.size();
                cacheMap_.erase(it);
            }
            usage_.pop_back();
        }
    }

    std::size_t maxBytes_;
    std::size_t currentBytes_;
    std::list<std::string> usage_;
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> cacheMap_;
    std::shared_mutex mtx_;
};

#endif // FILE_CACHE_HPP
