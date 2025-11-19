#include <lockedin/abstract_queue.hpp>
#include <lockedin/spsc_queue.hpp>

#include <cassert>
#include <optional>
#include <string>
#include <vector>

template <class Q>
requires lockedin::detail::QueueInterface<Q, int>
void unitTest(Q& q, const int fillLimit)
{
    assert(q.empty());
    assert(!q.full());

    for (int i = 0; i < fillLimit; ++i)
        assert(q.push(i));
    assert(q.full());
    assert(!q.push(10));

    int popped = 0;
    assert(q.pop(popped) && popped == 0);
    assert(q.pop(popped) && popped == 1);
    assert(q.size() == static_cast<size_t>(fillLimit - 2));

    assert(q.push(11));
    assert(q.size() == static_cast<size_t>(fillLimit - 1));
}

int main()
{
    lockedin::SPSCQ<int> stub{4};
    unitTest(stub, 4);

    return 0;
}
