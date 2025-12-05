#pragma once

#include <lockedin/abstract_queue.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lockedin
{
    template <typename T> class MPSCQ : public AbstractQ<T, MPSCQ<T>>
    {
    public:
        explicit MPSCQ(std::size_t capacity)
            : AbstractQ<T, MPSCQ<T>>(capacity), capacity_{capacity}, mask_{capacity_ - 1},
              buffer_{std::make_unique<Cell[]>(capacity_)}
        {
            if (capacity_ < 2 || (capacity_ & (capacity_ - 1)) != 0)
                throw std::logic_error("Capacity must be a power of 2 and > 1");

            for (std::size_t i = 0; i < capacity_; ++i)
                buffer_[i].sequence.store(i, std::memory_order_relaxed);

            head_.store(0, std::memory_order_relaxed);
            tail_.store(0, std::memory_order_relaxed);
        }

        MPSCQ(const MPSCQ&) = delete;
        MPSCQ& operator=(const MPSCQ&) = delete;
        MPSCQ(MPSCQ&&) = delete;
        MPSCQ& operator=(MPSCQ&&) = delete;

        ~MPSCQ() = default;

        // Enqueue by copy. Return false if queue appears full.
        bool push(const T& item)
        {
            return emplace_impl(item);
        }

        bool push(T&& item)
        {
            return emplace_impl(std::move(item));
        }

        bool pop(T& out)
        {
            return pop_impl(out);
        }

        [[nodiscard]] bool empty() const
        {
            return size() == 0;
        }

        [[nodiscard]] bool full() const
        {
            return size() >= capacity_;
        }

        [[nodiscard]] std::size_t size() const
        {
            const auto head = head_.load(std::memory_order_relaxed);
            const auto tail = tail_.load(std::memory_order_relaxed);
            return head - tail;
        }

    private:
        struct Cell
        {
            std::atomic<std::size_t> sequence;
            T value;
        };

        std::size_t capacity_;
        std::size_t mask_;
        std::unique_ptr<Cell[]> buffer_;

        alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_{0};

        alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_{0};

        template <typename U> bool emplace_impl(U&& value)
        {
            Cell* cell;
            std::size_t pos = head_.load(std::memory_order_relaxed);

            for (;;)
            {
                cell = &buffer_[pos & mask_];

                std::size_t seq = cell->sequence.load(std::memory_order_acquire);
                std::intptr_t diff =
                    static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

                if (diff == 0)
                {
                    if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_acq_rel,
                                                    std::memory_order_relaxed))
                    {
                        break
                    }
                }
                else if (diff < 0)
                {
                    return false;
                }
                else
                {
                    pos = head_.load(std::memory_order_relaxed);
                }
            }

            cell->value = std::forward<U>(value);
            cell->sequence.store(pos + 1, std::memory_order_release);
            return true;
        }

        bool pop_impl(T& out)
        {
            std::size_t pos = tail_.load(std::memory_order_relaxed);
            Cell* cell = &buffer_[pos & mask_];

            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            std::intptr_t diff =
                static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff < 0)
                return false;

            out = std::move(cell->value);
            cell->sequence.store(pos + capacity_, std::memory_order_release);
            tail_.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
    };
}
