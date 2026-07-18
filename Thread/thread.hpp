#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <memory>
#include <cstddef>
#include <thread>

class ThreadPool {
public:
    // Construct with initial thread count
    explicit ThreadPool(std::size_t num_threads)
        : pool_(std::make_unique<boost::asio::thread_pool>(num_threads))
        , size_(num_threads)
    {}

    template<typename Task>
    void post(Task&& task) {
        boost::asio::post(*pool_, std::forward<Task>(task));
    }

    // Restart the pool with a new thread count (call before any work is posted)
    void resize(std::size_t num_threads) {
        if (num_threads == size_) return;
        pool_->stop();
        pool_->join();
        pool_ = std::make_unique<boost::asio::thread_pool>(num_threads);
        size_ = num_threads;
    }

    std::size_t size() const { return size_; }

    void run()  { pool_->join(); }
    void join() { pool_->join(); }

private:
    std::unique_ptr<boost::asio::thread_pool> pool_;
    std::size_t size_;
};

#endif
