#include <lockedin/abstract_queue.hpp>
#include <lockedin/spsc_queue.hpp>

#include <cassert>
#include <iostream>

template <class Q>
    requires lockedin::detail::QueueInterface<Q, int>
void unitTest(Q& q)
{
    const int fillLimit = 3;
    assert(q.empty());
    assert(!q.full());

    assert(q.push(1));
    assert(q.push(2));
    assert(q.push(3));
    assert(q.full());
    assert(!q.push(10));

    int popped = 0;
    assert(q.pop(popped) && popped == 1);
    assert(q.pop(popped) && popped == 2);
    assert(q.size() == static_cast<size_t>(fillLimit - 2));

    assert(q.push(11));
    assert(q.size() == static_cast<size_t>(fillLimit - 1));
    std::cout << "PASSED\n";
}

int main()
{
    lockedin::SPSCQ<int> stub{4};
    unitTest(stub);

    return 0;
}
