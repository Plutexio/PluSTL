//
// Created by Plutex on 1/16/26.
//

#ifndef PLUENGINE_HASHSET_H
#define PLUENGINE_HASHSET_H

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "PluSTL/Allocators/Default.h"
#include "PluSTL/Hashers/Default.h"

namespace Plu
{
	// ============================================================================
    // HashSet - Open addressing hash set with linear probing
    // ============================================================================
    template
    <
        typename T,
        typename Hasher = Plu::DefaultHash<T>,
        typename Allocator = DefaultAllocator<T>
    >
    class HashSet {
    private:
        // State of a slot in the hash table
        enum class SlotState : unsigned char {
            Empty,    // Never used
            Occupied, // Holds a value
            Deleted   // Was used, but its value has been removed
        };

        // A single slot in the hash table
        struct Slot {
            alignas(T) unsigned char Storage[sizeof(T)]; // Aligned storage for T
            SlotState State;

            Slot() noexcept : State(SlotState::Empty) {}

            T* GetValue() noexcept {
                return reinterpret_cast<T*>(Storage);
            }

            const T* GetValue() const noexcept {
                return reinterpret_cast<const T*>(Storage);
            }
        };

        Slot* mSlots;              // Slot array
        std::size_t mCapacity;     // Table capacity
        std::size_t mSize;         // Element count
        std::size_t mDeletedCount; // Deleted slot count
        Hasher mHasher;            // Hash function
        Allocator mAllocator;      // Allocator

        static constexpr std::size_t kDefaultCapacity = 16;
        static constexpr float kMaxLoadFactor = 0.75f;
        static constexpr float kRehashThreshold = 0.5f; // deleted/capacity ratio that triggers a rehash

        // The caller's allocator, re-targeted at Slot. It used to be a hardcoded
        // DefaultAllocator<Slot>, so a custom Allocator only ever got Construct and
        // Destroy while every byte of the table came from global new.
        using SlotAllocator = RebindAllocatorT<Allocator, Slot>;
        [[no_unique_address]] SlotAllocator mSlotAllocator{};

    public:
        // ========================================================================
        // Iterators
        // ========================================================================
        class Iterator {
            friend class HashSet;

            Slot* mCurrent;
            Slot* mEnd;

            Iterator(Slot* current, Slot* end) noexcept
                : mCurrent(current), mEnd(end) {
                // Skip to the first occupied slot
                while (mCurrent != mEnd && mCurrent->State != SlotState::Occupied) {
                    ++mCurrent;
                }
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            T& operator*() const noexcept {
                return *mCurrent->GetValue();
            }

            T* operator->() const noexcept {
                return mCurrent->GetValue();
            }

            Iterator& operator++() noexcept {
                ++mCurrent;
                while (mCurrent != mEnd && mCurrent->State != SlotState::Occupied) {
                    ++mCurrent;
                }
                return *this;
            }

            Iterator operator++(int) noexcept {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const Iterator& other) const noexcept {
                return mCurrent == other.mCurrent;
            }

            bool operator!=(const Iterator& other) const noexcept {
                return mCurrent != other.mCurrent;
            }
        };

        class ConstIterator {
            friend class HashSet;

            const Slot* mCurrent;
            const Slot* mEnd;

            ConstIterator(const Slot* current, const Slot* end) noexcept
                : mCurrent(current), mEnd(end) {
                while (mCurrent != mEnd && mCurrent->State != SlotState::Occupied) {
                    ++mCurrent;
                }
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = const T*;
            using reference = const T&;

            const T& operator*() const noexcept {
                return *mCurrent->GetValue();
            }

            const T* operator->() const noexcept {
                return mCurrent->GetValue();
            }

            ConstIterator& operator++() noexcept {
                ++mCurrent;
                while (mCurrent != mEnd && mCurrent->State != SlotState::Occupied) {
                    ++mCurrent;
                }
                return *this;
            }

            ConstIterator operator++(int) noexcept {
                ConstIterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const ConstIterator& other) const noexcept {
                return mCurrent == other.mCurrent;
            }

            bool operator!=(const ConstIterator& other) const noexcept {
                return mCurrent != other.mCurrent;
            }
        };

        // ========================================================================
        // Constructors and destructor
        // ========================================================================
        HashSet() noexcept
            : mSlots(nullptr)
            , mCapacity(0)
            , mSize(0)
            , mDeletedCount(0) {
            Reserve(kDefaultCapacity);
        }

        explicit HashSet(std::size_t initialCapacity) noexcept
            : mSlots(nullptr)
            , mCapacity(0)
            , mSize(0)
            , mDeletedCount(0) {
            Reserve(initialCapacity);
        }

        ~HashSet() noexcept {
            Clear();
            if (mSlots) {
                mSlotAllocator.Deallocate(mSlots, mCapacity);
            }
        }

        // Copy constructor
        HashSet(const HashSet& other) noexcept
            : mSlots(nullptr)
            , mCapacity(0)
            , mSize(0)
            , mDeletedCount(0)
            , mHasher(other.mHasher) {
            Reserve(other.mCapacity);
            for (std::size_t i = 0; i < other.mCapacity; ++i) {
                if (other.mSlots[i].State == SlotState::Occupied) {
                    Insert(*other.mSlots[i].GetValue());
                }
            }
        }

        // Move constructor
        HashSet(HashSet&& other) noexcept
            : mSlots(other.mSlots)
            , mCapacity(other.mCapacity)
            , mSize(other.mSize)
            , mDeletedCount(other.mDeletedCount)
            , mHasher(std::move(other.mHasher)) {
            other.mSlots = nullptr;
            other.mCapacity = 0;
            other.mSize = 0;
            other.mDeletedCount = 0;
        }

        // Copy assignment
        HashSet& operator=(const HashSet& other) noexcept {
            if (this != &other) {
                Clear();
                mHasher = other.mHasher;
                Reserve(other.mCapacity);
                for (std::size_t i = 0; i < other.mCapacity; ++i) {
                    if (other.mSlots[i].State == SlotState::Occupied) {
                        Insert(*other.mSlots[i].GetValue());
                    }
                }
            }
            return *this;
        }

        // Move assignment
        HashSet& operator=(HashSet&& other) noexcept {
            if (this != &other) {
                Clear();
                if (mSlots) {
                    mSlotAllocator.Deallocate(mSlots, mCapacity);
                }

                mSlots = other.mSlots;
                mCapacity = other.mCapacity;
                mSize = other.mSize;
                mDeletedCount = other.mDeletedCount;
                mHasher = std::move(other.mHasher);

                other.mSlots = nullptr;
                other.mCapacity = 0;
                other.mSize = 0;
                other.mDeletedCount = 0;
            }
            return *this;
        }

        // ========================================================================
        // Main operations
        // ========================================================================

        // Insert by const reference
        bool Insert(const T& value) noexcept {
            return InsertInternal(value);
        }

        // Insert with move semantics
        bool Insert(T&& value) noexcept {
            return InsertInternal(std::move(value));
        }

        // Builds the element from `arguments` and inserts it. Unlike a map's Emplace
        // this cannot skip the construction: a set probes by the value itself, so the
        // value has to exist before the slot is known. It saves the caller a named
        // temporary, not the construction.
        template<typename... Args>
        bool Emplace(Args&&... arguments) noexcept {
            return InsertInternal(T(std::forward<Args>(arguments)...));
        }

        // Remove element
        bool Remove(const T& value) noexcept {
            if (mCapacity == 0) return false;

            std::size_t index = FindSlotIndex(value);
            Slot& slot = mSlots[index];

            if (slot.State != SlotState::Occupied) {
                return false;
            }

            // Destroy the value and mark the slot as deleted
            mAllocator.Destroy(slot.GetValue());
            slot.State = SlotState::Deleted;
            --mSize;
            ++mDeletedCount;

            // Rehash if there are too many deleted slots
            if (mDeletedCount > mCapacity * kRehashThreshold) {
                Rehash(mCapacity);
            }

            return true;
        }

        // Find element (returns iterator)
        Iterator Find(const T& value) noexcept {
            if (mCapacity == 0) {
                return end();
            }

            std::size_t index = FindSlotIndex(value);
            if (mSlots[index].State == SlotState::Occupied) {
                return Iterator(&mSlots[index], mSlots + mCapacity);
            }
            return end();
        }

        ConstIterator Find(const T& value) const noexcept {
            if (mCapacity == 0) {
                return cend();
            }

            std::size_t index = FindSlotIndex(value);
            if (mSlots[index].State == SlotState::Occupied) {
                return ConstIterator(&mSlots[index], mSlots + mCapacity);
            }
            return cend();
        }

        // Contains check
        bool Contains(const T& value) const noexcept {
            return Find(value) != cend();
        }

        // Clear all elements
        void Clear() noexcept {
            if (!mSlots) return;

            for (std::size_t i = 0; i < mCapacity; ++i) {
                if (mSlots[i].State == SlotState::Occupied) {
                    mAllocator.Destroy(mSlots[i].GetValue());
                }
                mSlots[i].State = SlotState::Empty;
            }
            mSize = 0;
            mDeletedCount = 0;
        }

        // Reserve capacity
        void Reserve(std::size_t newCapacity) noexcept {
            if (newCapacity <= mCapacity) return;

            // Round up to a power of two for faster indexing
            newCapacity = NextPowerOfTwo(newCapacity);
            Rehash(newCapacity);
        }

        // Rehash with new capacity
        void Rehash(std::size_t newCapacity) noexcept {
            if (newCapacity < mSize) {
                newCapacity = NextPowerOfTwo(mSize * 2);
            }

            Slot* oldSlots = mSlots;
            std::size_t oldCapacity = mCapacity;

            // Allocate the new table
            mSlots = mSlotAllocator.Allocate(newCapacity);
            if (!mSlots) {
                mSlots = oldSlots; // Restore the old pointer on failure
                return;
            }

            // Initialize the new slots
            for (std::size_t i = 0; i < newCapacity; ++i) {
                mSlotAllocator.Construct(&mSlots[i]);
            }

            mCapacity = newCapacity;
            mSize = 0;
            mDeletedCount = 0;

            // Move the old values across. This goes through InsertNoGrow, not
            // InsertInternal: the latter starts with the load-factor check and could
            // call Reserve — i.e. re-enter Rehash — while this loop is still reading
            // oldSlots. The new table is already big enough by construction, so the
            // check has nothing to do here anyway.
            if (oldSlots) {
                for (std::size_t i = 0; i < oldCapacity; ++i) {
                    if (oldSlots[i].State == SlotState::Occupied) {
                        InsertNoGrow(std::move(*oldSlots[i].GetValue()));
                        mAllocator.Destroy(oldSlots[i].GetValue());
                    }
                }
                mSlotAllocator.Deallocate(oldSlots, oldCapacity);
            }
        }

        // ========================================================================
        // Getters
        // ========================================================================
        std::size_t Size() const noexcept { return mSize; }
        std::size_t Capacity() const noexcept { return mCapacity; }
        bool IsEmpty() const noexcept { return mSize == 0; }
        float LoadFactor() const noexcept {
            return mCapacity > 0 ? static_cast<float>(mSize) / mCapacity : 0.0f;
        }

        // ========================================================================
        // Iterators
        // ========================================================================
        Iterator Begin() noexcept {
            return Iterator(mSlots, mSlots + mCapacity);
        }

        Iterator End() noexcept {
            return Iterator(mSlots + mCapacity, mSlots + mCapacity);
        }

        ConstIterator Begin() const noexcept {
            return ConstIterator(mSlots, mSlots + mCapacity);
        }

        ConstIterator End() const noexcept {
            return ConstIterator(mSlots + mCapacity, mSlots + mCapacity);
        }

        // Lowercase aliases so range-for works.
        Iterator begin() noexcept { return Begin(); }
        Iterator end() noexcept { return End(); }
        ConstIterator begin() const noexcept { return Begin(); }
        ConstIterator end() const noexcept { return End(); }

        ConstIterator cbegin() const noexcept { return Begin(); }
        ConstIterator cend() const noexcept { return End(); }

    private:
        // ========================================================================
        // Internal helpers
        // ========================================================================

        // Find the slot index for a value (linear probing)
        std::size_t FindSlotIndex(const T& value) const noexcept {
            std::size_t hash = mHasher(value);
            std::size_t index = hash & (mCapacity - 1); // Fast modulo for powers of two
            std::size_t firstDeleted = mCapacity;

            // Linear probing
            for (std::size_t i = 0; i < mCapacity; ++i) {
                std::size_t probeIndex = (index + i) & (mCapacity - 1);
                const Slot& slot = mSlots[probeIndex];

                if (slot.State == SlotState::Empty) {
                    // If we passed a deleted slot earlier, reuse it
                    return firstDeleted != mCapacity ? firstDeleted : probeIndex;
                }

                if (slot.State == SlotState::Deleted) {
                    if (firstDeleted == mCapacity) {
                        firstDeleted = probeIndex;
                    }
                    continue;
                }

                // Compare the values
                if (*slot.GetValue() == value) {
                    return probeIndex;
                }
            }

            return firstDeleted != mCapacity ? firstDeleted : index;
        }

        // Grow if needed, then place. Note: with mCapacity == 0 (e.g. a set after std::move)
        // mCapacity * 2 == 0, and Reserve(0) is a no-op — that would leave mSlots == nullptr
        // for the next line to dereference. Grow to kDefaultCapacity in that case.
        template<typename U>
        bool InsertInternal(U&& value) noexcept {
            if (mSize + 1 > mCapacity * kMaxLoadFactor) {
                Reserve(mCapacity == 0 ? kDefaultCapacity : mCapacity * 2);
            }
            return InsertNoGrow(std::forward<U>(value));
        }

        // Placement only — the caller guarantees there is room. Used by Rehash, where
        // growing mid-migration would re-enter Rehash on a half-moved table.
        template<typename U>
        bool InsertNoGrow(U&& value) noexcept {
            std::size_t index = FindSlotIndex(value);
            Slot& slot = mSlots[index];

            // Element already present
            if (slot.State == SlotState::Occupied) {
                return false;
            }

            // Insert the new element
            if (slot.State == SlotState::Deleted) {
                --mDeletedCount;
            }

            mAllocator.Construct(slot.GetValue(), std::forward<U>(value));
            slot.State = SlotState::Occupied;
            ++mSize;

            return true;
        }

        // Compute the next power of two
        static std::size_t NextPowerOfTwo(std::size_t n) noexcept {
            if (n <= 1) return 1;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            n |= n >> 32;
            return n + 1;
        }
    };
}

#endif //PLUENGINE_HASHSET_H