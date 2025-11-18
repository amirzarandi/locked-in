/**
 * @file abstract_queue.hpp
 * @brief Concept-checked CRTP base that enforces a queue API without virtual dispatch.
 */

#pragma once

#include <cstddef>
#include <concepts>
#include <utility>

namespace lockedin
{
    namespace detail
    {
        /**
         * @brief Compile-time contract for queue implementations used with AbstractQ.
         */
        template <typename Queue, typename Value>
        concept QueueInterface = requires(Queue& queue, const Queue& constQueue, Value& item) {
            { queue.push(item) } -> std::same_as<bool>;
            { queue.push(std::move(item)) } -> std::same_as<bool>;
            { queue.pop(item) } -> std::same_as<bool>;
            { constQueue.full() } -> std::same_as<bool>;
            { constQueue.empty() } -> std::same_as<bool>;
            { constQueue.size() } -> std::same_as<std::size_t>;
        };
    } // namespace detail

    /**
     * @tparam T        Element type stored in the queue.
     * @tparam Derived  Concrete queue implementation that satisfies QueueInterface.
     */
    template <typename T, class Derived> class AbstractQ
    {
    public:
        /**
         * @brief Enforce a uniform constructor signature while keeping dispatch static.
         */
        explicit constexpr AbstractQ(size_t /*capacity*/) noexcept
        {
            enforce_contract();
        }

    protected:
        ~AbstractQ() = default;

    private:
        static consteval void enforce_contract()
        {
            static_assert(detail::QueueInterface<Derived, T>,
                          "Derived queue does not satisfy the AbstractQ contract.");
        }
    };
}
