#include <lockedin/abstract_queue.hpp>
#include <lockedin/spsc_queue.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <latch>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#define LATENCY_BENCHMARK_HAS_TSC 1
#else
#define LATENCY_BENCHMARK_HAS_TSC 0
#endif

namespace latency_benchmark
{
    class CycleClock
    {
    public:
#if LATENCY_BENCHMARK_HAS_TSC
        using stamp_type = std::uint64_t;
#else
        using stamp_type = std::chrono::steady_clock::time_point;
#endif

        CycleClock();
        [[nodiscard]] stamp_type now() const noexcept;
        [[nodiscard]] std::int64_t nanosecondsBetween(stamp_type start,
                                                      stamp_type end) const noexcept;

    private:
#if LATENCY_BENCHMARK_HAS_TSC
        static stamp_type readTsc() noexcept;
        static double calibrateNsPerCycle() noexcept;
        double nsPerCycle_;
#endif
    };

#if LATENCY_BENCHMARK_HAS_TSC
    inline CycleClock::CycleClock() : nsPerCycle_(calibrateNsPerCycle())
    {
    }

    inline CycleClock::stamp_type CycleClock::now() const noexcept
    {
        return readTsc();
    }

    inline std::int64_t CycleClock::nanosecondsBetween(stamp_type start,
                                                       stamp_type end) const noexcept
    {
        const auto delta = static_cast<std::int64_t>(end - start);
        return static_cast<std::int64_t>(static_cast<double>(delta) * nsPerCycle_);
    }

    inline CycleClock::stamp_type CycleClock::readTsc() noexcept
    {
        unsigned aux = 0;
        return __rdtscp(&aux);
    }

    inline double CycleClock::calibrateNsPerCycle() noexcept
    {
        using namespace std::chrono;
        constexpr auto calibrationSleep = milliseconds(5);
        double best = std::numeric_limits<double>::max();
        for (int i = 0; i < 5; ++i)
        {
            const auto wallStart = steady_clock::now();
            const auto start = readTsc();
            std::this_thread::sleep_for(calibrationSleep);
            const auto end = readTsc();
            const auto wallEnd = steady_clock::now();
            const auto wallNs = duration_cast<nanoseconds>(wallEnd - wallStart).count();
            const auto cycles = static_cast<double>(end - start);
            if (wallNs <= 0 || cycles <= 0.0)
                continue;
            const double candidate = static_cast<double>(wallNs) / cycles;
            best = std::min(best, candidate);
        }
        if (best == std::numeric_limits<double>::max())
            return 1.0; // fallback to avoid divide-by-zero later
        return best;
    }
#else
    inline CycleClock::CycleClock() = default;

    inline CycleClock::stamp_type CycleClock::now() const noexcept
    {
        return std::chrono::steady_clock::now();
    }

    inline std::int64_t CycleClock::nanosecondsBetween(stamp_type start,
                                                       stamp_type end) const noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
#endif

    using loopResult = std::vector<std::pair<bool, std::int64_t>>;
    using threadResult = std::vector<loopResult>;
    using RW_Result = std::pair<threadResult, threadResult>;
    template <class Q>
        requires lockedin::detail::QueueInterface<Q, int>
    void readerLoop(Q&& q, int nIter, loopResult& results, std::latch& sync,
                    const CycleClock& clock)
    {
        sync.wait();
        for (int i = 0; i < nIter; i++)
        {
            int tmp = -1;
            auto start = clock.now();
            bool didRead = q.pop(tmp);
            auto end = clock.now();
            auto duration = clock.nanosecondsBetween(start, end);
            results[i].first = didRead;
            results[i].second = duration;
        }
    }

    template <class Q>
        requires lockedin::detail::QueueInterface<Q, int>
    void writerLoop(Q&& q, int nIter, loopResult& results, std::latch& sync,
                    const CycleClock& clock)
    {
        sync.wait();
        for (int i = 0; i < nIter; i++)
        {
            auto start = clock.now();
            bool didWrite = q.push(i);
            auto end = clock.now();
            auto duration = clock.nanosecondsBetween(start, end);
            results[i].first = didWrite;
            results[i].second = duration;
        }
    }

    double averageLatency(const loopResult& samples)
    {
        if (samples.empty())
            return 0.0;
        const auto sum = std::accumulate(samples.begin(), samples.end(), 0LL,
                                         [](auto a, const auto& b) { return a + b.second; });
        return static_cast<double>(sum) / samples.size();
    }
    int nSuccesses(const loopResult& samples)
    {
        if (samples.empty())
            return 0.0;
        const auto sum = std::accumulate(samples.begin(), samples.end(), 0LL,
                                         [](auto a, const auto& b)
                                         {
                                             int acc = b.first ? 1 : 0;
                                             return a + acc;
                                         });
        return sum;
    }

    template <class Q>
        requires lockedin::detail::QueueInterface<Q, int>
    RW_Result runBenchmark(Q&& q, int nReaders, int nWriters, int nIter)
    {
        CycleClock clock;
        RW_Result rwResults({threadResult(nReaders, loopResult(nIter, {false, 0})),
                             threadResult(nWriters, loopResult(nIter, {false, 0}))});
        std::vector<std::thread> readers;
        std::vector<std::thread> writers;
        std::latch sync{nReaders + nWriters};
        for (int wi = 0; wi < nWriters; wi++)
        {
            writers.emplace_back([&, wi]()
                                 { writerLoop(q, nIter, rwResults.second[wi], sync, clock); });
            sync.count_down();
        }
        for (int ri = 0; ri < nReaders; ri++)
        {
            readers.emplace_back([&, ri]()
                                 { readerLoop(q, nIter, rwResults.first[ri], sync, clock); });
            sync.count_down();
        }
        for (auto& t : writers)
            if (t.joinable())
                t.join();
        for (auto& t : readers)
            if (t.joinable())
                t.join();
        return rwResults;
    }
}

int main()
{
    lockedin::SPSCQ<int> q{1 << 14};
    constexpr int readers = 1;
    constexpr int writers = 1;
    constexpr int iterations = 1 << 15;
    auto [rResults, wResults] = latency_benchmark::runBenchmark(q, readers, writers, iterations);

    auto avgReader =
        std::accumulate(rResults.begin(), rResults.end(), 0LL, [](auto a, const auto& b)
                        { return a + latency_benchmark::averageLatency(b); });
    auto succReader =
        std::accumulate(rResults.begin(), rResults.end(), 0LL,
                        [](auto a, const auto& b) { return a + latency_benchmark::nSuccesses(b); });
    auto avgWriter =
        std::accumulate(wResults.begin(), wResults.end(), 0LL, [](auto a, const auto& b)
                        { return a + latency_benchmark::averageLatency(b); });
    auto succWriter =
        std::accumulate(wResults.begin(), wResults.end(), 0LL,
                        [](auto a, const auto& b) { return a + latency_benchmark::nSuccesses(b); });

    std::cout << "Reader latency average (ns): " << avgReader << '\n';
    std::cout << "Reader success rate:         " << succReader << "/" << iterations * readers << "("
              << 100.0 * succReader / iterations / readers << "%)\n";
    std::cout << "Writer latency average (ns): " << avgWriter << '\n';
    std::cout << "Writer success rate:         " << succWriter << "/" << iterations * readers << "("
              << 100.0 * succWriter / iterations / writers << "%)\n";

    return 0;
}
