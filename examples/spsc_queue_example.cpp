#include <lockedin/spsc_queue.hpp>

#include <iostream>
#include <thread>
#include <vector>

int main()
{
    const size_t capacity = 1024;
    const int iterations = 100000;

    lockedin::SPSCQ<int> queue(capacity);

    std::thread producer(
        [&]()
        {
            for (int i = 0; i < iterations; ++i)
                while (!queue.push(i))
                    std::this_thread::yield();
        });

    std::thread consumer(
        [&]()
        {
            int count = 0;
            int val = 0;
            while (count < iterations)
            {
                if (queue.pop(val))
                {
                    if (val != count)
                    {
                        std::cerr << "expected " << count << " got " << val << std::endl;
                        std::exit(1);
                    }
                    count++;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });

    producer.join();
    consumer.join();

    std::cout << "PASSED" << std::endl;

    return 0;
}