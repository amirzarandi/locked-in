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
    using loopResult = std::vector<std::pair<bool, int>>;
    using threadResult = std::vector<loopResult>;
    using RW_Result = std::pair<threadResult, threadResult>;
    template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
    void readerLoop(Q&& q, int nIter, loopResult& results, std::latch& sync) {
        sync.wait();
        for (int i=0; i<nIter; i++) {
            int tmp = -1;
            auto start = std::chrono::high_resolution_clock::now();
            bool didRead = q.pop(tmp);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start);
            results[i].first = didRead;
            results[i].second = duration.count();
        }
    }

    template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
    void writerLoop(Q&& q, int nIter, loopResult& results, std::latch& sync) {
        sync.wait();
        for (int i=0; i<nIter; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            bool didWrite = q.push(i);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start);
            results[i].first = didWrite;
            results[i].second = duration.count();
        }
    }

    double averageLatency(const loopResult& samples)
    {
        if (samples.empty())
            return 0.0;
        const auto sum = std::accumulate(samples.begin(), samples.end(), 0LL, [](auto a, const auto& b){return a + b.second;});
        return static_cast<double>(sum) / samples.size();
    }
    int nSuccesses(const loopResult& samples)
    {
        if (samples.empty())
            return 0.0;
        const auto sum = std::accumulate(samples.begin(), samples.end(), 0LL, [](auto a, const auto& b){
            int acc = b.first ? 1 : 0;
            return a + acc;
        });
        return sum;
    }

    template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
    RW_Result runBenchmark(Q&& q, int nReaders, int nWriters, int nIter) {
        RW_Result rwResults(
            {
                threadResult(nReaders, loopResult(nIter, {false, 0})),
                threadResult(nWriters, loopResult(nIter, {false, 0}))
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
    auto succReader = latency_benchmark::nSuccesses(readerSamples);
    auto avgWriter = latency_benchmark::averageLatency(writerSamples);
    auto succWriter = latency_benchmark::nSuccesses(writerSamples);

    std::cout << "Reader latency average (ns): " << avgReader << '\n';
    std::cout << "Reader success rate:         " << succReader <<"/"<< iterations << '\n';
    std::cout << "Writer latency average (ns): " << avgWriter << '\n';
    std::cout << "Writer success rate:         " << succWriter <<"/"<< iterations << '\n';

    return 0;
}
