// Minimal Single-Producer / Multi-Consumer example
#include <lockedin/spmc_queue.hpp>

#include <iostream>
#include <thread>
#include <vector>

int main()
{
    constexpr std::size_t capacity = 64;
    constexpr int items = 16;   // small demo
    constexpr int consumersN = 2; // minimal multi-reader demo

    lockedin::SPMCQ<int> q(capacity);

    // Start two consumers; each will read exactly `items` values
    std::vector<std::thread> consumers;
    std::vector<int> counts(consumersN, 0);
    consumers.reserve(consumersN);
    for (int c = 0; c < consumersN; ++c) {
        auto cons = q.getConsumer();
        consumers.emplace_back([cons, &counts, c]() mutable {
            int v = 0;
            while (counts[c] < items) {
                if (cons.pop(v))
                    ++counts[c];
                else
                    std::this_thread::yield();
            }
        });
    }

    // Single producer
    auto prod = q.getProducer();
    for (int i = 0; i < items; ++i)
        while (!prod.push(i))
            std::this_thread::yield();

    for (auto& t : consumers) t.join();
    std::cout << "SPMC minimal example PASSED\n";
    return 0;
}
