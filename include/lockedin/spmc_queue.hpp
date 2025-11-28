/**
 * @file spmc_queue.hpp
 * @brief Header-only **single-consumer / multi-producer (SPMC) ring buffer** skeleton.
 *
 * This implementation mirrors the documentation and structural layout of the real queue
 * while leaving the algorithms unimplemented for now. The focus is to ensure that the
 * producer/consumer handles are wired through `AbstractSharedQ`, so concept checking keeps
 * the API honest even before the wait-free logic is complete.
 *
 * ## Complexity goals
 * * `push()` – *O(1)* / wait-free (returns false immediately if full).
 * * `pop()`  – *O(1)* / wait-free (returns false immediately if empty).
 *
 * ## Memory ordering goals
 * * Producers load the consumer cursor (`readIdx_`) with acquire semantics, publish their
 *   writes via release stores to the producer cursor (`writeIdx_`).
 * * The consumer mirrors that pattern: loads the producer cursor with acquire, then releases
 *   progress with a store once the slot has been reclaimed.
 *
 * ## Credit
 *
 * ![David Gross __When Nanoseconds Matter: Ultrafast Trading Systems in C++__ (CppCon 2024)]
 * (https://github.com/CppCon/CppCon2024/blob/main/Presentations/When_Nanoseconds_Matter.pdf)
 * ![Erez Strouss __Multi Producer Multi Consumer Lock Free Atomic Queue__ (CppCon 2024)]
 * (https://github.com/CppCon/CppCon2024/blob/main/Presentations/Multi_Producer_Multi_Consumer_Lock_Free_Atomic_Queue.pdf)
 */

#ifndef LOCKEDIN_SPMC_QUEUE_HPP
#define LOCKEDIN_SPMC_QUEUE_HPP

#include <lockedin/abstract_queue.hpp>

#include <atomic>
#include <bitset>
#include <climits>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <stdexcept>

namespace lockedin
{
    namespace detail
    {
#if defined(__cpp_lib_hardware_interference_size)
        inline constexpr std::size_t cacheline_size =
            std::hardware_destructive_interference_size;
#else
        inline constexpr std::size_t cacheline_size = 64;
#endif
    } // namespace detail

    template <typename T> class SPMCQ;
    template <typename T> class SPMCProducer;
    template <typename T> class SPMCConsumer;
    template <typename T> struct SPMCQEntry;

    /**
     * @brief struct for an element inside the queue containing the data and version number.
     */
    template <typename T> struct SPMCQEntry {
        T data;
        alignas(detail::cacheline_size) uint32_t version{0};
    };

    /**
     * @tparam T Element type transported through the queue.
     *
     * @class SPMCQ
     * @brief Lock-free, wait-free ring buffer skeleton with one consumer and N producers.
     */
    template <typename T> class SPMCQ : public AbstractSharedQ<T, SPMCQ<T>>
    {
    public:
        using elem = SPMCQEntry<T>;

        /**
         * @brief Construct with a specific capacity.
         * @param capacity Must be a **power of 2** (e.g., 64, 1024) to allow efficient wrapping.
         * @throws std::logic_error if capacity is invalid (<2 or not power of 2).
         */
        explicit SPMCQ(size_t capacity)
            : AbstractSharedQ<T, SPMCQ<T>>(capacity),
              capacity_{capacity},
              items_{std::make_unique<elem[]>(capacity)}
        {
            if (capacity < 2 || std::bitset<sizeof(size_t) * CHAR_BIT>(capacity).count() != 1)
                throw std::logic_error("Capacity must be a power of 2, and greater than 1.");
        }

        SPMCQ(const SPMCQ&) = delete;
        SPMCQ& operator=(const SPMCQ&) = delete;
        SPMCQ(SPMCQ&&) = delete;
        SPMCQ& operator=(SPMCQ&&) = delete;

        ~SPMCQ() = default;

        /* ------------------------------------------------------------------
         * Shared queue API
         * ----------------------------------------------------------------*/

        /**
         * @brief Obtain a producer handle sharing this queue.
         */
        [[nodiscard]] constexpr SPMCProducer<T> getProducer() const noexcept
        {
            return SPMCProducer<T>(const_cast<SPMCQ<T>&>(*this));
        }

        /**
         * @brief Obtain a consumer handle sharing this queue.
         */
        [[nodiscard]] SPMCConsumer<T> getConsumer() const noexcept
        {
            return SPMCConsumer<T>(const_cast<SPMCQ<T>&>(*this));
        }

        /* ------------------------------------------------------------------
         * Status API
         * ----------------------------------------------------------------*/

        /**
         * @brief Checks if the queue is effectively full.
         * @note Conservative check (may return true even if space just became available).
         */
        [[nodiscard]] bool full() const noexcept
        {
            const auto writeIdx = mWriteIndex.load(std::memory_order_relaxed);
            const auto readIdx = mReadIndex.load(std::memory_order_relaxed);
            const auto nextWriteIdx = (writeIdx + 1U) & (capacity_ - 1U);
            return nextWriteIdx == readIdx;
        }

        /**
         * @brief Checks whether the queue is empty.
         */
        [[nodiscard]] bool empty() const noexcept
        {
            const auto readIdx = mReadIndex.load(std::memory_order_relaxed);
            const auto writeIdx = mWriteIndex.load(std::memory_order_relaxed);
            return readIdx == writeIdx;
        }

        /**
         * @brief Returns the number of slots currently filled.
         */
        [[nodiscard]] size_t size() const noexcept
        {
            const auto readIdx = mReadIndex.load(std::memory_order_relaxed);
            const auto writeIdx = mWriteIndex.load(std::memory_order_relaxed);
            return (writeIdx - readIdx) & (capacity_ - 1U);
        }

    private:
        friend class SPMCProducer<T>;
        friend class SPMCConsumer<T>;

        /* ------------------------------------------------------------------
         * Storage
         * ----------------------------------------------------------------*/
        const size_t capacity_;            ///< total usable slots (power of 2)
        std::unique_ptr<elem[]> items_; ///< heap allocated buffer shared by handles

        // Align atomic indices to separate cache lines to prevent false sharing
        alignas(detail::cacheline_size) std::atomic<size_t> mReadIndex{0};
        alignas(detail::cacheline_size) std::atomic<size_t> mWriteIndex{0};
    };

    /**
     * @class SPMCProducer
     * @brief Producer facade exposing the push API enforced by SharedQInterface.
     *        Instances are reference wrappers returned by `SPMCQ::getProducer()`.
     */
    template <typename T> class SPMCProducer
    {
    public:
        using elem = SPMCQEntry<T>;
        /**
         * @brief Enqueues an item by copy.
         * @return true if successful, false if buffer is full.
         */
        bool push(const T& item)
        {
            const auto nxtWriteIdx_nowrap = (lWriteIdx + 1);
            const auto nxtVersion = lVersion + 
                            static_cast<decltype(lVersion)>(nxtWriteIdx_nowrap == capacity_);
            const auto nxtWriteIdx = nxtWriteIdx_nowrap & (capacity_ - 1);

            queue_.mWriteIndex.store(nxtWriteIdx, std::memory_order_release); // update view for writers

            queue_.items_[lWriteIdx] = elem{item, lVersion}; // copy into buffer

            queue_.mReadIndex.store(nxtWriteIdx, std::memory_order_release); // update view for readers

            lWriteIdx = nxtWriteIdx;
            lVersion = nxtVersion;
            return true;
        }

        /**
         * @brief Enqueues an item by move.
         * @return true if successful, false if buffer is full.
         */
        bool push(T&& item)
        {
            const auto nxtWriteIdx_nowrap = (lWriteIdx + 1);
            const auto nxtVersion = lVersion + 
                            static_cast<decltype(lVersion)>(nxtWriteIdx_nowrap == capacity_);
            const auto nxtWriteIdx = nxtWriteIdx_nowrap & (capacity_ - 1);

            queue_.mWriteIndex.store(nxtWriteIdx, std::memory_order_release); // update view for writers

            queue_.items_[lWriteIdx] = elem{std::move(item), lVersion}; // copy into buffer

            queue_.mReadIndex.store(nxtWriteIdx, std::memory_order_release); // update view for readers

            lWriteIdx = nxtWriteIdx;
            lVersion = nxtVersion;
            return true;
        }

    private:
        friend class SPMCQ<T>;

        explicit constexpr SPMCProducer(SPMCQ<T>& queue) noexcept : queue_{queue}, capacity_{queue.capacity_} {}

        SPMCQ<T>& queue_;
        const size_t capacity_;
        alignas(detail::cacheline_size) size_t lWriteIdx{0};
        alignas(detail::cacheline_size) uint32_t lVersion{0};
    };

    /**
     * @class SPMCConsumer
     * @brief Consumer facade exposing the pop API enforced by SharedQInterface.
     *        Instances can only be obtained through `SPMCQ::getConsumer()`.
     */
    template <typename T> class SPMCConsumer
    {
    public:
        using elem = SPMCQEntry<T>;
        SPMCConsumer() = default;

        /**
         * @brief Dequeues an item. Raises an exception if consumer is overlapped
         * @return true if successful, exception if consumer is overlapped by producer, false if queue is empty
         */
        bool pop(T& item)
        {
            if (lReadIdx == queue_.mReadIndex.load(std::memory_order_acquire))
                return false; // empty

            const elem& val = queue_.items_[lReadIdx];
            if (val.version != lVersion)
                throw std::runtime_error("consumer overlapped at index "+std::to_string(lReadIdx));// reader too slow

            item = val.data; // have to copy, move would invalidate other readers

            const auto nxtReadIdx_nowrap = (lReadIdx + 1);
            const auto nxtVersion = lVersion + static_cast<decltype(lVersion)>(nxtReadIdx_nowrap==capacity_);
            lReadIdx = nxtReadIdx_nowrap & (capacity_ - 1);
            lVersion = nxtVersion;
            return true;
        }

    private:
        friend class SPMCQ<T>;

        explicit constexpr SPMCConsumer(SPMCQ<T>& queue) noexcept : queue_{queue}, capacity_{queue.capacity_} {}

        SPMCQ<T>& queue_{};
        const size_t capacity_;
        // Local cursors kept for documentation purposes; real implementation will advance them.
        alignas(detail::cacheline_size) size_t lReadIdx{0};
        alignas(detail::cacheline_size) uint32_t lVersion{0};
    };
} // namespace lockedin

#endif // LOCKEDIN_SPMC_QUEUE_HPP
