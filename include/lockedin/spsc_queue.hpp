/**
 * @file spsc_queue.hpp
 * @brief Header‑only **single‑producer / single‑consumer (SPSC) ring buffer**
 *
 * This implementation provides a wait‑free circular buffer that uses standard
 * C++ atomics to synchronize a single writer and single reader. It avoids locks
 * entirely by relying on acquire/release semantics:
 *
 * * Producer loads `readIdx_` to check available space, writes data, then
 * publishes the new `writeIdx_` via `store(release)`.
 * * Consumer loads `writeIdx_` to check for data, moves data out, then
 * updates `readIdx_` via `store(release)`.
 *
 * **False sharing is actively mitigated** by aligning the atomic indices to
 * `std::hardware_destructive_interference_size`, ensuring the producer and
 * consumer cursors reside on separate cache lines.
 *
 * ## Complexity
 * * `push()` – *O(1)* / wait‑free (returns false immediately if full).
 * * `pop()`  – *O(1)* / wait‑free (returns false immediately if empty).
 *
 * ## Memory ordering
 * * Producer        – `load(acquire)` on read index (to ensure space),
 * `store(release)` on write index (to commit data).
 * * Consumer        – `load(acquire)` on write index (to ensure data visibility),
 * `store(release)` on read index (to mark slot free).
 */

#pragma once

#include <lockedin/abstract_queue.hpp>

#include <atomic>
#include <bitset>
#include <climits>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

// Used to align atomic indices to separate cache lines to prevent false sharing
#ifdef _cpp_lib_hardware_interference_size
using std::hardware_destructive_interference_size;
#else
static inline constexpr std::size_t hardware_destructive_interference_size = 128UL;
#endif
namespace lockedin
{

    /**
     * @tparam T            Element type.
     *
     * @class SPSCQ
     * @brief Lock‑free, wait‑free ring buffer for one producer and one consumer.
     */
    template <typename T> class SPSCQ : public AbstractQ<T, SPSCQ<T>>
    {
    public:
        /**
         * @brief Construct with a specific capacity.
         * @param capacity Must be a **power of 2** (e.g., 64, 1024) to allow
         * efficient bitwise wrapping.
         * @throws std::logic_error if capacity is invalid (<2 or not power of 2).
         */
        explicit SPSCQ(size_t capacity)
            : AbstractQ<T, SPSCQ<T>>(capacity), capacity_{capacity},
              items_{std::make_unique<T[]>(capacity)}
        {
            if (capacity < 2 || std::bitset<sizeof(size_t) * CHAR_BIT>(capacity).count() != 1)
                throw std::logic_error("Capacity must be a power of 2, and greater than 1.");
        }

        SPSCQ(const SPSCQ&) = delete;
        SPSCQ& operator=(const SPSCQ&) = delete;
        SPSCQ(SPSCQ&&) = delete;
        SPSCQ& operator=(SPSCQ&&) = delete;

        ~SPSCQ() = default;

        /* ------------------------------------------------------------------
         * Producer API
         * ----------------------------------------------------------------*/

        /**
         * @brief Enqueues an item by copy.
         * @return true if successful, false if buffer is full.
         */
        bool push(const T& item)
        {
            const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
            const auto readIdx = readIdx_.load(std::memory_order_acquire);

            const auto nextWriteIdx = (writeIdx + 1) & (capacity_ - 1);

            if (nextWriteIdx == readIdx)
                return false; // Full

            items_[writeIdx] = item;
            writeIdx_.store(nextWriteIdx, std::memory_order_release);

            return true;
        }

        /**
         * @brief Enqueues an item by move.
         * @return true if successful, false if buffer is full.
         */
        bool push(T&& item)
        {
            const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
            const auto readIdx = readIdx_.load(std::memory_order_acquire);

            const auto nextWriteIdx = (writeIdx + 1) & (capacity_ - 1);

            if (nextWriteIdx == readIdx)
                return false; // Full

            items_[writeIdx] = std::move(item);
            writeIdx_.store(nextWriteIdx, std::memory_order_release);

            return true;
        }

        /* ------------------------------------------------------------------
         * Consumer API
         * ----------------------------------------------------------------*/

        /**
         * @brief Dequeues an item.
         * @param item Reference to store the moved-out element.
         * @return true if successful, false if buffer is empty.
         */
        bool pop(T& item)
        {
            const auto readIdx = readIdx_.load(std::memory_order_relaxed);
            const auto writeIdx = writeIdx_.load(std::memory_order_acquire);

            if (readIdx == writeIdx)
                return false; // Empty

            item = std::move(items_[readIdx]);

            const auto nextReadIdx = (readIdx + 1) & (capacity_ - 1);
            readIdx_.store(nextReadIdx, std::memory_order_release);

            return true;
        }

        /* ------------------------------------------------------------------
         * Status API
         * ----------------------------------------------------------------*/

        /**
         * @brief Checks if the queue is effectively full.
         * @note Conservative check (may return true even if space just became available).
         */
        [[nodiscard]] bool full() const
        {
            const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
            const auto readIdx = readIdx_.load(std::memory_order_relaxed);
            const auto nextWriteIdx = (writeIdx + 1) & (capacity_ - 1);
            return nextWriteIdx == readIdx;
        }

        [[nodiscard]] bool empty() const
        {
            const auto readIdx = readIdx_.load(std::memory_order_relaxed);
            const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
            return readIdx == writeIdx;
        }

        [[nodiscard]] size_t size() const
        {
            const auto readIdx = readIdx_.load(std::memory_order_relaxed);
            const auto writeIdx = writeIdx_.load(std::memory_order_relaxed);
            // Bitwise calculation for power-of-2 capacity
            return (writeIdx - readIdx) & (capacity_ - 1);
        }

    private:
        /* ------------------------------------------------------------------
         * Storage
         * ----------------------------------------------------------------*/
        size_t capacity_;            ///< total usable slots (power of 2)
        std::unique_ptr<T[]> items_; ///< heap allocated buffer

        alignas(hardware_destructive_interference_size) std::atomic<size_t> readIdx_{
            0}; ///< consumer cursor
        alignas(hardware_destructive_interference_size) std::atomic<size_t> writeIdx_{
            0}; ///< producer cursor
    };
}