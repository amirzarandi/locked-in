/**
 * @file abstract_queue.hpp
 * @brief Concept-checked CRTP base that enforces a queue API without virtual dispatch.
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <utility>

namespace lockedin
{
    namespace detail
    {
        /**
         * @brief Compile-time contract for queue implementations used with AbstractQ
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

        /**
         * @brief contract for consumer implementations used with SharedQ::getConsumer
         */
        template <typename Consumer, typename Value>
        concept ConsumerInterface =
            requires(Consumer& cons, const Consumer& constCons, Value& item) {
                { cons.pop(item) } -> std::same_as<bool>;
            };
        /**
         * @brief contract for producer implementations used with SharedQ::getProducer
         */
        template <typename Producer, typename Value>
        concept ProducerInterface =
            requires(Producer& prod, const Producer& constProd, Value& item) {
                { prod.push(item) } -> std::same_as<bool>;
                { prod.push(std::move(item)) } -> std::same_as<bool>;
            };
        /**
         * @brief contract for SharedQ implementations enforcing getters for the producer and
         * consumer
         * @tparam Producer (implementing push) sharing this Queue.
         * SharedQ::getProducer() creates a producer, possibly just an alias, for ex. with SPSC/SPMC
         * @tparam Consumer (implementing pop) sharing this Queue.
         * SharedQ::getConsumer() creates a consumer, possibly just an alias, for ex. with SPSC/MPSC
         */
        template <typename SharedQueue, typename Producer, typename Consumer, typename Value>
        concept SharedQInterface =
            requires(SharedQueue& queue, const SharedQueue& constQueue, Value& item) {
                { constQueue.getProducer() } -> std::same_as<Producer>;
                { constQueue.getConsumer() } -> std::same_as<Consumer>;
                ProducerInterface<Producer, Value>;
                ConsumerInterface<Consumer, Value>;
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

    /**
     * @tparam T        Element type stored in the queue.
     * @tparam Derived  Concrete shared queue implementation that satisfies SharedQInterface.
     */
    template <typename T, class Derived> class AbstractSharedQ
    {
    public:
        /**
         * @brief Enforce a uniform constructor signature while keeping dispatch static.
         */
        explicit constexpr AbstractSharedQ(size_t /*capacity*/) noexcept
        {
            enforce_contract();
        }

    protected:
        ~AbstractSharedQ() = default;

    private:
        static consteval void enforce_contract()
        {
            static_assert(
                detail::SharedQInterface<Derived,
                                         decltype(std::declval<const Derived&>().getProducer()),
                                         decltype(std::declval<const Derived&>().getConsumer()), T>,
                "Derived queue does not satisfy the AbstractSharedQ contract.");
        }
    };
}
