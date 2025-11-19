#include <lockedin/abstract_queue.hpp>
#include <lockedin/spsc_queue.hpp>

#include <cassert>
#include <optional>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <latch>
#include <numeric>
#include <thread>

namespace latency_benchmark
{
    template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
    void readerLoop(Q&& q, int nIter, std::vector<int>& results, std::latch& sync) {
        sync.wait();
        for (int i=0; i<nIter; i++) {
            int tmp = -1;
            auto start = std::chrono::high_resolution_clock::now();
            q.pop(tmp);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start);
            results[i] = duration.count();
        }
    }

    template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
    void writerLoop(Q&& q, int nIter, std::vector<int>& results, std::latch& sync) {
        sync.wait();
        for (int i=0; i<nIter; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            q.push(i);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start);
            results[i] = duration.count();
        }
    }

    double averageLatency(const std::vector<int>& samples)
    {
        if (samples.empty())
            return 0.0;
        const auto sum = std::accumulate(samples.begin(), samples.end(), 0LL);
        return static_cast<double>(sum) / samples.size();
    }

    template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
    std::pair<std::vector<std::vector<int>>, std::vector<std::vector<int>>> runBenchmark(Q&& q, int nReaders, int nWriters, int nIter) {
        std::pair<std::vector<std::vector<int>>, std::vector<std::vector<int>>> rwResults(
            {
                std::vector<std::vector<int>>(nReaders, std::vector<int>(nIter)),
                std::vector<std::vector<int>>(nWriters, std::vector<int>(nIter))
            }
        );
        std::vector<std::jthread> readers;
        std::vector<std::jthread> writers;
        std::latch sync{nReaders + nWriters};
        for (int wi = 0; wi < nWriters; wi++) {
            writers.emplace_back([&, wi]() {
                writerLoop(q, nIter, rwResults.second[wi], sync);
            });
            sync.count_down();
        }
        for (int ri = 0; ri < nReaders; ri++) {
            readers.emplace_back([&, ri]() {
                readerLoop(q, nIter, rwResults.first[ri], sync);
            });
            sync.count_down();
        }
        return rwResults;
    }
}

int main()
{
    lockedin::SPSCQ<int> q{512};
    constexpr int readers = 1;
    constexpr int writers = 1;
    constexpr int iterations = 2048;
    auto results = latency_benchmark::runBenchmark(q, readers, writers, iterations);

    const auto& readerSamples = results.first.front();
    const auto& writerSamples = results.second.front();

    auto avgReader = latency_benchmark::averageLatency(readerSamples);
    auto avgWriter = latency_benchmark::averageLatency(writerSamples);

    std::cout << "Average reader latency (ns): " << avgReader << '\n';
    std::cout << "Average writer latency (ns): " << avgWriter << '\n';

    return 0;
}
