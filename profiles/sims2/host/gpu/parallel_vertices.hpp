#pragma once
// Splits per-vertex work of large draws across a few worker threads.
//
// Each vertex is converted on its own from read-only draw state, so the
// results do not depend on which thread computes them or in what order: the
// output is identical to a single-threaded conversion. Small draws stay on the
// calling thread, where waking workers would cost more than it saves.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sims2::gpu {
class ParallelVertices {
public:
    // Draws with fewer vertices are converted on the calling thread.
    static constexpr std::size_t kMinimumVertices = 384;
    static constexpr std::size_t kChunk = 128;

    ParallelVertices() {
        // Leave cores for the audio, presentation and driver threads.
        // SIMS2_TEST_VERTEX_THREADS=0 measures conversion on one thread.
        unsigned workers = std::min(3u, std::max(2u, std::thread::hardware_concurrency()) - 2u);
        if (const auto *text = std::getenv("SIMS2_TEST_VERTEX_THREADS"))
            workers = unsigned(std::min(7ul, std::strtoul(text, nullptr, 10)));
        for (unsigned i = 0; i < workers; ++i)
            threads_.emplace_back([this] { work(); });
    }
    ~ParallelVertices() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (auto &thread : threads_)
            thread.join();
    }
    ParallelVertices(const ParallelVertices &) = delete;
    ParallelVertices &operator=(const ParallelVertices &) = delete;

    // Calls convert(begin, end) over [0, count) and returns when all of it is
    // done. convert must only write the elements of its own range.
    template <typename Convert> void run(std::size_t count, Convert &&convert) {
        if (threads_.empty() || count < kMinimumVertices) {
            convert(std::size_t{0}, count);
            return;
        }
        const std::function<void(std::size_t, std::size_t)> job = std::ref(convert);
        {
            std::lock_guard lock(mutex_);
            job_ = &job;
            count_ = count;
            next_.store(0, std::memory_order_relaxed);
            ++generation_;
        }
        wake_.notify_all();
        take_chunks(job, count);
        // Every chunk has been taken. Wait only for workers still finishing
        // one; a worker that wakes after this finds no job and goes back to
        // sleep, so the caller never waits for a thread to be scheduled.
        std::unique_lock lock(mutex_);
        done_.wait(lock, [&] { return active_ == 0; });
        job_ = nullptr;
    }

private:
    void take_chunks(const std::function<void(std::size_t, std::size_t)> &job, std::size_t count) {
        for (;;) {
            const auto begin = next_.fetch_add(kChunk, std::memory_order_relaxed);
            if (begin >= count)
                return;
            job(begin, std::min(count, begin + kChunk));
        }
    }
    void work() {
        std::uint64_t seen = 0;
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [&] { return stopping_ || (job_ && generation_ != seen); });
            if (stopping_)
                return;
            seen = generation_;
            const auto *job = job_;
            const auto count = count_;
            ++active_;
            lock.unlock();
            take_chunks(*job, count);
            lock.lock();
            if (--active_ == 0)
                done_.notify_one();
        }
    }

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(std::size_t, std::size_t)> *job_{};
    std::size_t count_{};
    std::atomic<std::size_t> next_{};
    unsigned active_{};
    std::uint64_t generation_{};
    bool stopping_{};
};
}
