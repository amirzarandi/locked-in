#include <lockedin/mpsc_queue.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main()
{
    constexpr int producers = 3;
    constexpr int per_producer = 5;
    constexpr int total = producers * per_producer;

    lockedin::MPSCQ<int> q{64};
    std::vector<std::thread> ps;
    ps.reserve(producers);

    for (int pid = 0; pid < producers; ++pid)
    {
        ps.emplace_back(
            [pid, &q]()
            {
                for (int i = 0; i < per_producer; ++i)
                {
                    const int value = pid * 100 + i;
                    while (!q.push(value))
                        std::this_thread::yield();
                    std::this_thread::sleep_for(50us);
                }
            });
    }

    std::vector<int> seen;
    seen.reserve(total);
    while (seen.size() < static_cast<size_t>(total))
    {
        int v = 0;
        if (q.pop(v))
            seen.push_back(v);
        else
            std::this_thread::yield();
    }

    for (auto& t : ps)
        t.join();

    std::sort(seen.begin(), seen.end());
    for (int pid = 0, idx = 0; pid < producers; ++pid)
    {
        for (int i = 0; i < per_producer; ++i, ++idx)
            assert(seen[idx] == pid * 100 + i);
    }

    std::cout << "PASSED\n";
    return 0;
}
