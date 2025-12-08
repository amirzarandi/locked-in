#include <benchmark/benchmark.h>

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <lockedin/mpsc_queue.hpp>
#include <lockedin/spmc_queue.hpp>
#include <lockedin/spsc_queue.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <iostream>

static constexpr size_t queue_size = 1024 << 4;

enum class queue_type
{
    spsc,
    mpsc,
    spmc,
    boost_spsc,
    boost_mpsc,
    mutex
};

template <typename T, queue_type type> struct queue_wrapper
{
};

template <typename T> struct queue_wrapper<T, queue_type::spsc> : public lockedin::SPSCQ<T>
{
    explicit queue_wrapper(size_t n_elements) : lockedin::SPSCQ<T>(n_elements)
    {
    }

    void push(const T& value)
    {
        while (!lockedin::SPSCQ<T>::push(value))
        {
        }
    }
};

template <typename T> struct queue_wrapper<T, queue_type::boost_spsc>
{
    boost::lockfree::spsc_queue<T> queue;
    explicit queue_wrapper(size_t n_elements) : queue(n_elements)
    {
    }

    void push(const T& value)
    {
        while (!queue.push(value))
        {
        }
    }

    bool pop(T& value)
    {
        return queue.pop(value);
    }
};

template <typename T> struct queue_wrapper<T, queue_type::mpsc> : public lockedin::MPSCQ<T>
{
    explicit queue_wrapper(size_t n_elements) : lockedin::MPSCQ<T>(n_elements)
    {
    }
};

template <typename T> struct queue_wrapper<T, queue_type::spmc>
{
    explicit queue_wrapper(size_t n_elements)
        : queue(n_elements), producer(queue.getProducer()), default_consumer(queue.getConsumer())
    {
    }

    void push(const T& value)
    {
        while (!producer.push(value))
        {
        }
    }

    bool pop(T& value)
    {
        return default_consumer.pop(value);
    }

    lockedin::SPMCConsumer<T> make_consumer()
    {
        return queue.getConsumer();
    }

private:
    lockedin::SPMCQ<T> queue;
    lockedin::SPMCProducer<T> producer;
    lockedin::SPMCConsumer<T> default_consumer;
};

template <typename T> struct queue_wrapper<T, queue_type::boost_mpsc>
{
    boost::lockfree::queue<T> queue;
    explicit queue_wrapper(size_t n_elements) : queue(n_elements)
    {
    }

    void push(const T& value)
    {
        while (!queue.push(value))
        {
        }
    }

    bool pop(T& value)
    {
        return queue.pop(value);
    }
};

template <typename T> struct queue_wrapper<T, queue_type::mutex>
{
    explicit queue_wrapper([[maybe_unused]] size_t n_elements)
    {
    }
    std::queue<T> queue;
    std::mutex mutex;
    void push(const T& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(value);
    }
    void push(T&& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(value));
    }
    bool pop(T& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.size() == 0)
            return false;
        value = queue.front();
        queue.pop();
        return true;
    }
};

template <queue_type type> static void callsite_push_latency_single_producer(benchmark::State& st)
{
    queue_wrapper<size_t, type> q(queue_size);
    std::atomic<bool> should_run = true;
    std::atomic_flag started = false;

    std::thread thread(
        [&]()
        {
            started.test_and_set();
            started.notify_all();
            size_t next = 0;

            while (should_run.load(std::memory_order_relaxed))
            {
                size_t out = 0;
                bool popped = q.pop(out);
                if (popped)
                {
                    if constexpr (type != queue_type::mpsc)
                        if (out != (next))
                            throw std::runtime_error("oops");
                    next++;
                }
                benchmark::DoNotOptimize(popped);
                benchmark::DoNotOptimize(out);
            }
        });

    started.wait(false);

    size_t iteration = 0;
    for ([[maybe_unused]] auto _ : st)
        q.push(iteration++);

    should_run = false;
    if (thread.joinable())
        thread.join();

    st.SetItemsProcessed(st.iterations());
}

template <queue_type type> static void roundtrip_single_producer(benchmark::State& st)
{
    queue_wrapper<size_t, type> q1(queue_size);
    queue_wrapper<size_t, type> q2(queue_size);
    std::atomic<bool> should_run = true;
    std::atomic_flag started = false;

    std::thread thread(
        [&]()
        {
            started.test_and_set();
            started.notify_all();

            while (should_run.load(std::memory_order_relaxed))
            {
                size_t out = 0;
                if (q1.pop(out))
                    q2.push(out);
            }
        });

    started.wait(false);

    size_t iteration = 0;
    for ([[maybe_unused]] auto _ : st)
    {
        const size_t to_send = iteration++;
        q1.push(to_send);
        size_t to_recv = 0;
        while (!q2.pop(to_recv))
        {
        }
        if constexpr (type != queue_type::mpsc)
            if (to_send != to_recv)
                throw std::runtime_error("oops");
    }

    should_run = false;
    if (thread.joinable())
        thread.join();

    st.SetItemsProcessed(st.iterations());
}

template <queue_type type> static void roundtrip_single_thread(benchmark::State& st)
{
    queue_wrapper<size_t, type> q1(queue_size);

    size_t iteration = 0;
    for ([[maybe_unused]] auto _ : st)
    {
        const size_t to_send = iteration++;
        q1.push(to_send);
        size_t to_recv = 0;
        q1.pop(to_recv);
        if constexpr (type != queue_type::mpsc)
            if (to_recv != to_send)
                throw std::runtime_error("oops");
    }

    st.SetItemsProcessed(st.iterations());
}

static void roundtrip_single_producer_spmc(benchmark::State& st)
{
    queue_wrapper<size_t, queue_type::spmc> q1(queue_size);
    queue_wrapper<size_t, queue_type::spmc> q2(queue_size);
    auto responder_consumer = q1.make_consumer();
    auto main_consumer = q2.make_consumer();
    std::atomic<bool> should_run = true;
    std::atomic_flag started = false;

    std::thread thread(
        [&, responder_consumer = std::move(responder_consumer)]() mutable
        {
            started.test_and_set();
            started.notify_all();

            while (should_run.load(std::memory_order_relaxed))
            {
                size_t out = 0;
                try
                {
                    if (responder_consumer.pop(out))
                        q2.push(out);
                }
                catch (const std::runtime_error&)
                {
                    responder_consumer.respawn();
                }
            }
        });

    started.wait(false);

    size_t iteration = 0;
    for ([[maybe_unused]] auto _ : st)
    {
        const size_t to_send = iteration++;
        q1.push(to_send);

        size_t to_recv = 0;
        bool received = false;
        while (!received)
        {
            try
            {
                received = main_consumer.pop(to_recv);
            }
            catch (const std::runtime_error&)
            {
                main_consumer.respawn();
            }
        }

        if (to_send != to_recv)
            throw std::runtime_error("oops");
    }

    should_run = false;
    if (thread.joinable())
        thread.join();

    st.SetItemsProcessed(st.iterations());
}

static void roundtrip_single_thread_spmc(benchmark::State& st)
{
    queue_wrapper<size_t, queue_type::spmc> q1(queue_size);
    auto consumer = q1.make_consumer();

    size_t iteration = 0;
    for ([[maybe_unused]] auto _ : st)
    {
        const size_t to_send = iteration++;
        q1.push(to_send);

        size_t to_recv = 0;
        bool received = false;
        while (!received)
        {
            try
            {
                received = consumer.pop(to_recv);
            }
            catch (const std::runtime_error&)
            {
                consumer.respawn();
            }
        }

        if (to_recv != to_send)
            throw std::runtime_error("oops");
    }

    st.SetItemsProcessed(st.iterations());
}

static void callsite_push_latency_spmc_multi_consumer(benchmark::State& st)
{
    const size_t n_consumers = static_cast<size_t>(st.range(0));
    queue_wrapper<size_t, queue_type::spmc> q(queue_size);
    std::atomic<bool> should_run = true;
    std::atomic_flag started = ATOMIC_FLAG_INIT;
    std::atomic<size_t> ready_consumers = 0;

    std::vector<std::thread> consumers;
    consumers.reserve(n_consumers);

    for (size_t i = 0; i < n_consumers; ++i)
    {
        consumers.emplace_back(
            [&, consumer = q.make_consumer()]() mutable
            {
                ready_consumers.fetch_add(1, std::memory_order_release);
                started.wait(false);

                size_t previous_value = 0;
                bool has_value = false;
                while (should_run.load(std::memory_order_relaxed))
                {
                    size_t value = 0;
                    try {
                        if (consumer.pop(value))
                        {
                            if (has_value && value <= previous_value) {
                                throw std::runtime_error("oops:");
                            }
                            previous_value = value;
                            has_value = true;
                        }
                    } catch (const std::runtime_error&)
                    {
                        consumer.respawn();
                    }
                }
            });
    }

    while (ready_consumers.load(std::memory_order_acquire) < n_consumers)
    {
        std::this_thread::yield();
    }

    started.test_and_set();
    started.notify_all();

    size_t iteration = 0;
    for ([[maybe_unused]] auto _ : st)
        q.push(++iteration);

    should_run = false;
    for (auto& consumer : consumers)
    {
        if (consumer.joinable())
            consumer.join();
    }

    st.SetItemsProcessed(st.iterations());
}

BENCHMARK(callsite_push_latency_single_producer<queue_type::spsc>)->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::mpsc>)->Args({});
BENCHMARK(callsite_push_latency_spmc_multi_consumer)->Arg(1)->Arg(2)->Arg(4);
BENCHMARK(callsite_push_latency_single_producer<queue_type::boost_spsc>)->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::boost_mpsc>)->Args({});
BENCHMARK(callsite_push_latency_single_producer<queue_type::mutex>)->Args({});

BENCHMARK(roundtrip_single_producer<queue_type::spsc>)->Args({});
BENCHMARK(roundtrip_single_producer_spmc)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::mpsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::boost_spsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::boost_mpsc>)->Args({});
BENCHMARK(roundtrip_single_producer<queue_type::mutex>)->Args({});

BENCHMARK(roundtrip_single_thread<queue_type::spsc>)->Args({});
BENCHMARK(roundtrip_single_thread_spmc)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::mpsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::boost_spsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::boost_mpsc>)->Args({});
BENCHMARK(roundtrip_single_thread<queue_type::mutex>)->Args({});

BENCHMARK_MAIN();
