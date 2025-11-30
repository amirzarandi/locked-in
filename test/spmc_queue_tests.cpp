#include <lockedin/spmc_queue.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static void single_thread_smoke()
{
    lockedin::SPMCQ<int> q{8};
    auto prod = q.getProducer();
    auto cons = q.getConsumer();

    assert(prod.push(1));
    assert(prod.push(2));
    assert(prod.push(3));

    int v = 0;
    assert(cons.pop(v) && v == 1);
    assert(cons.pop(v) && v == 2);
    assert(cons.pop(v) && v == 3);
}

// All consumers see identical order regardless of interleaving.
static void order_consistent_across_consumers()
{
    constexpr int N = 32;
    lockedin::SPMCQ<int> q{256}; // ensure no overlap

    auto c1 = q.getConsumer();
    auto c2 = q.getConsumer();
    std::vector<int> seen1, seen2;
    seen1.reserve(N);
    seen2.reserve(N);

    // Start consumer 2 slightly later than consumer 1
    std::thread t1(
        [c1, &seen1]() mutable
        {
            int v = 0;
            while (seen1.size() < N)
                if (c1.pop(v))
                    seen1.push_back(v);
                else
                    std::this_thread::yield();
        });

    std::thread t2(
        [c2, &seen2]() mutable
        {
            int v = 0;
            // stagger start
            std::this_thread::sleep_for(200us);
            while (seen2.size() < N)
                if (c2.pop(v))
                    seen2.push_back(v);
                else
                    std::this_thread::yield();
        });

    auto p = q.getProducer();
    for (int i = 0; i < N; ++i)
    {
        while (!p.push(i))
            std::this_thread::yield();
        // tiny delay to vary interleaving without risking overlap
        std::this_thread::sleep_for(50us);
    }

    t1.join();
    t2.join();

    // Both must match the produced order exactly
    for (int i = 0; i < N; ++i)
    {
        assert(seen1[i] == i);
        assert(seen2[i] == i);
    }
}

// Force a slow consumer to be overlapped while a fast consumer continues
// to consume in order without being affected.
static void overlapping_consumer_does_not_break_others()
{
    constexpr int capacity = 8;
    constexpr int total = capacity * 2 + 1; // wrap and end not at 0 to trigger overlap path
    lockedin::SPMCQ<int> q{capacity};

    auto fast = q.getConsumer();
    auto slow = q.getConsumer();
    std::vector<int> fast_seen;
    fast_seen.reserve(total);

    std::thread fastThread(
        [&]() mutable
        {
            int v = 0;
            while ((int)fast_seen.size() < total)
                if (fast.pop(v))
                    fast_seen.push_back(v);
                else
                    std::this_thread::yield();
        });

    auto p = q.getProducer();
    // Slow down producer slightly to let fast consumer keep up deterministically
    for (int i = 0; i < total; ++i)
    {
        while (!p.push(i))
            std::this_thread::yield();
        std::this_thread::sleep_for(100us);
    }

    // Slow consumer starts after producer finished -> guaranteed overlap
    bool overlapped = false;
    try
    {
        int dummy = 0;
        (void)slow.pop(dummy); // should throw due to version mismatch/overlap
    }
    catch (const std::runtime_error&)
    {
        overlapped = true;
    }
    assert(overlapped);

    fastThread.join();

    // Fast consumer received all items in order
    assert((int)fast_seen.size() == total);
    for (int i = 0; i < total; ++i)
        assert(fast_seen[i] == i);
}

int main()
{
    single_thread_smoke();
    order_consistent_across_consumers();
    overlapping_consumer_does_not_break_others();
    std::cout << "PASSED\n";
    return 0;
}
