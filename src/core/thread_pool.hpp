#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace katman {

// Persistent zero-spawn-overhead, zero-allocation thread pool with dynamic work-stealing chunks.
// Automatically degrades to zero-overhead inline execution under Emscripten/WASM or 1 thread.
class ThreadPool {
public:
    explicit ThreadPool(uint32_t num_threads = 0) {
#ifdef __EMSCRIPTEN__
        thread_count_ = 1;
        (void)num_threads;
#else
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 4;
        }
        thread_count_ = std::clamp(num_threads, 1u, 32u);
        if (thread_count_ > 1) {
            workers_.reserve(thread_count_ - 1);
            for (uint32_t i = 1; i < thread_count_; ++i) {
                workers_.emplace_back([this, i]() { worker_loop(i); });
            }
        }
#endif
    }

    ~ThreadPool() {
#ifndef __EMSCRIPTEN__
        if (!workers_.empty()) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                stop_ = true;
                ++epoch_;
            }
            cv_start_.notify_all();
            for (auto& t : workers_) {
                if (t.joinable()) t.join();
            }
        }
#endif
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    uint32_t size() const { return thread_count_; }

    // Dynamic work-stealing parallel_for: fn(uint32_t worker_id, size_t begin, size_t end)
    template <typename Fn>
    void parallel_for_chunks(size_t count, size_t chunk_size, Fn&& fn) {
        if (count == 0) return;
        if (thread_count_ <= 1 || count <= chunk_size) {
            fn(0u, 0, count);
            return;
        }
#ifdef __EMSCRIPTEN__
        fn(0u, 0, count);
#else
        struct Context {
            std::atomic<size_t> cursor{0};
            size_t count;
            size_t chunk_size;
            std::remove_reference_t<Fn>* fn;
        };
        Context ctx{{0}, count, chunk_size, &fn};

        auto trampoline = [](void* raw, uint32_t tid) {
            auto* c = static_cast<Context*>(raw);
            while (true) {
                size_t b = c->cursor.fetch_add(c->chunk_size, std::memory_order_relaxed);
                if (b >= c->count) break;
                size_t e = std::min(c->count, b + c->chunk_size);
                (*c->fn)(tid, b, e);
            }
        };

        {
            std::lock_guard<std::mutex> lk(mu_);
            current_fn_ = trampoline;
            current_ctx_ = &ctx;
            active_workers_.store(static_cast<uint32_t>(workers_.size()), std::memory_order_release);
            ++epoch_;
        }
        cv_start_.notify_all();

        // Main thread participates as worker 0
        trampoline(&ctx, 0u);

        if (active_workers_.load(std::memory_order_acquire) > 0) {
            std::unique_lock<std::mutex> lk(mu_);
            cv_done_.wait(lk, [this]() {
                return active_workers_.load(std::memory_order_acquire) == 0;
            });
        }
        current_fn_ = nullptr;
        current_ctx_ = nullptr;
#endif
    }

    template <typename Fn>
    void parallel_for(size_t count, Fn&& fn, size_t min_chunk = 1) {
        if (count == 0) return;
        size_t chunk = std::max<size_t>(min_chunk, count / (static_cast<size_t>(thread_count_) * 6 + 1));
        if (chunk == 0) chunk = 1;
        parallel_for_chunks(count, chunk, [&](uint32_t tid, size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) fn(tid, i);
        });
    }

private:
    uint32_t thread_count_ = 1;
#ifndef __EMSCRIPTEN__
    using TaskFn = void (*)(void*, uint32_t);
    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable cv_start_;
    std::condition_variable cv_done_;
    uint64_t epoch_ = 0;
    bool stop_ = false;
    std::atomic<uint32_t> active_workers_{0};
    TaskFn current_fn_ = nullptr;
    void* current_ctx_ = nullptr;

    void worker_loop(uint32_t tid) {
        uint64_t seen_epoch = 0;
        while (true) {
            TaskFn fn = nullptr;
            void* ctx = nullptr;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_start_.wait(lk, [&]() { return stop_ || epoch_ != seen_epoch; });
                if (stop_) return;
                seen_epoch = epoch_;
                fn = current_fn_;
                ctx = current_ctx_;
            }
            if (fn) fn(ctx, tid);
            if (active_workers_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(mu_);
                cv_done_.notify_one();
            }
        }
    }
#endif
};

}  // namespace katman
