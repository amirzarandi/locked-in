#include <lockedin/abstract_queue.hpp>
#include <lockedin/spsc_queue.hpp>
#include <lockedin/spmc_queue.hpp>

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

namespace throughput_benchmark
{
    struct ThroughputResult
    {
        std::vector<std::size_t> readerSuccesses;
        std::vector<std::size_t> writerSuccesses;
        double elapsedSeconds{0.0};
    };

    template <class Q>
        requires lockedin::detail::QueueInterface<Q, int>
    void readerLoop(Q&& q, int nIter, std::size_t& successes, std::latch& sync)
    {
        sync.wait();
        int tmp = 0;
        for (int i = 0; i < nIter; i++)
        {
            if (q.pop(tmp))
                ++successes;
        }
    }

    template <class Q>
        requires lockedin::detail::QueueInterface<Q, int>
    void writerLoop(Q&& q, int nIter, std::size_t& successes, std::latch& sync)
    {
        sync.wait();
        for (int i = 0; i < nIter; i++)
        {
            if (q.push(i))
                ++successes;
        }
    }

    template <class Q>
        requires lockedin::detail::QueueInterface<Q, int>
    ThroughputResult runBenchmark(Q&& q, int nReaders, int nWriters, int nIter)
    {
        ThroughputResult result{
            std::vector<std::size_t>(nReaders, 0),
            std::vector<std::size_t>(nWriters, 0),
            0.0};
        std::vector<std::thread> readers;
        std::vector<std::thread> writers;
        std::latch sync{nReaders + nWriters};

        const auto start = std::chrono::steady_clock::now();
        for (int wi = 0; wi < nWriters; wi++)
        {
            writers.emplace_back([&, wi]() { writerLoop(q, nIter, result.writerSuccesses[wi], sync); });
            sync.count_down();
        }
        for (int ri = 0; ri < nReaders; ri++)
        {
            readers.emplace_back([&, ri]() { readerLoop(q, nIter, result.readerSuccesses[ri], sync); });
            sync.count_down();
        }
        for (auto& t : writers)
            if (t.joinable())
                t.join();
        for (auto& t : readers)
            if (t.joinable())
                t.join();

        const auto end = std::chrono::steady_clock::now();
        result.elapsedSeconds = std::chrono::duration<double>(end - start).count();
        return result;
    }
}

int main()
{
    lockedin::SPSCQ<int> q{1 << 14};
    constexpr int readers = 1;
    constexpr int writers = 1;
    constexpr int iterations = 1 << 15;
    auto result = throughput_benchmark::runBenchmark(q, readers, writers, iterations);

    auto succReader = std::accumulate(result.readerSuccesses.begin(), result.readerSuccesses.end(), 0ULL);
    auto succWriter = std::accumulate(result.writerSuccesses.begin(), result.writerSuccesses.end(), 0ULL);

    const double elapsedSeconds = result.elapsedSeconds;
    const double readerThroughput = elapsedSeconds > 0.0 ? succReader / elapsedSeconds : 0.0;
    const double writerThroughput = elapsedSeconds > 0.0 ? succWriter / elapsedSeconds : 0.0;

    std::cout << "Reader throughput: " << readerThroughput << " ops/sec\n";
    std::cout << "Reader success rate:         " << succReader << "/" << iterations * readers << "("
              << 100.0 * succReader / iterations / readers << "%)\n";
    std::cout << "Writer throughput: " << writerThroughput << " ops/sec\n";
    std::cout << "Writer success rate:         " << succWriter << "/" << iterations * writers << "("
              << 100.0 * succWriter / iterations / writers << "%)\n";

    return 0;
}
