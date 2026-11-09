#pragma once
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <type_traits>
#include "PluSTL/Random/Random.h"

namespace Plu
{

// Fixed-size contiguous array (std::array). The size is part of the type, the elements live
// inline (no allocation), and the API mirrors DynamicArray minus everything that would change
// the size (PushBack, Remove*, Insert, Resize, ...).
//
// StaticArray is an aggregate, so it is initialized with braces like a C array:
//     Plu::StaticArray<int, 3> a = {1, 2, 3};   // missing trailing elements are value-initialized
//     Plu::StaticArray b{1.0f, 2.0f};           // deduced as StaticArray<float, 2>
// and almost everything is constexpr.
template<typename T, size_t N>
struct StaticArray {
    static_assert(N > 0, "StaticArray<T, 0> is not supported");

    using ValueType = T;
    using SizeType = size_t;
    using Iterator = T*;
    using ConstIterator = const T*;

    static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);

    // Public only because an aggregate needs it — treat it as private and go through
    // operator[] / Data().
    T mData[N];

    // Element access
    constexpr T& operator[](SizeType index) { return mData[index]; }
    constexpr const T& operator[](SizeType index) const { return mData[index]; }

    constexpr T& At(SizeType index) {
        if (index >= N) throw std::out_of_range("Index out of range");
        return mData[index];
    }

    constexpr const T& At(SizeType index) const {
        if (index >= N) throw std::out_of_range("Index out of range");
        return mData[index];
    }

    constexpr T& Front() { return mData[0]; }
    constexpr const T& Front() const { return mData[0]; }
    constexpr T& Back() { return mData[N - 1]; }
    constexpr const T& Back() const { return mData[N - 1]; }
    constexpr T* Data() { return mData; }
    constexpr const T* Data() const { return mData; }

    // Size
    [[nodiscard]] static constexpr SizeType Size() { return N; }
    [[nodiscard]] static constexpr bool IsEmpty() { return false; }
    [[nodiscard]] static constexpr bool IsValidIndex(SizeType index) { return index < N; }

    // Iterators
    constexpr Iterator Begin() { return mData; }
    constexpr ConstIterator Begin() const { return mData; }
    constexpr Iterator End() { return mData + N; }
    constexpr ConstIterator End() const { return mData + N; }

    constexpr Iterator begin() { return Begin(); }
    constexpr ConstIterator begin() const { return Begin(); }
    constexpr Iterator end() { return End(); }
    constexpr ConstIterator end() const { return End(); }

    // Utility methods - Search
    constexpr Iterator Find(const T& value) {
        for (SizeType i = 0; i < N; ++i) {
            if (mData[i] == value) return &mData[i];
        }
        return End();
    }

    constexpr ConstIterator Find(const T& value) const {
        for (SizeType i = 0; i < N; ++i) {
            if (mData[i] == value) return &mData[i];
        }
        return End();
    }

    template<typename Predicate>
    constexpr Iterator FindIf(Predicate pred) {
        for (SizeType i = 0; i < N; ++i) {
            if (pred(mData[i])) return &mData[i];
        }
        return End();
    }

    template<typename Predicate>
    constexpr ConstIterator FindIf(Predicate pred) const {
        for (SizeType i = 0; i < N; ++i) {
            if (pred(mData[i])) return &mData[i];
        }
        return End();
    }

    constexpr bool Contains(const T& value) const { return Find(value) != End(); }

    constexpr SizeType IndexOf(const T& value) const {
        for (SizeType i = 0; i < N; ++i) {
            if (mData[i] == value) return i;
        }
        return InvalidIndex;
    }

    template<typename Predicate>
    constexpr SizeType IndexOfIf(Predicate pred) const {
        for (SizeType i = 0; i < N; ++i) {
            if (pred(mData[i])) return i;
        }
        return InvalidIndex;
    }

    // Utility methods - Queries
    template<typename Predicate>
    constexpr bool ContainsIf(Predicate pred) const { return IndexOfIf(pred) != InvalidIndex; }

    template<typename Predicate>
    constexpr bool Any(Predicate pred) const { return IndexOfIf(pred) != InvalidIndex; }

    template<typename Predicate>
    constexpr bool All(Predicate pred) const {
        for (SizeType i = 0; i < N; ++i) {
            if (!pred(mData[i])) return false;
        }
        return true;
    }

    template<typename Predicate>
    constexpr SizeType CountIf(Predicate pred) const {
        SizeType count = 0;
        for (SizeType i = 0; i < N; ++i) {
            if (pred(mData[i])) ++count;
        }
        return count;
    }

    constexpr SizeType Count(const T& value) const {
        SizeType count = 0;
        for (SizeType i = 0; i < N; ++i) {
            if (mData[i] == value) ++count;
        }
        return count;
    }

    // Min/Max by a "less than" comparator. The array is never empty, so these never return End().
    template<typename Comparator>
    constexpr Iterator MinElement(Comparator comp) {
        SizeType best = 0;
        for (SizeType i = 1; i < N; ++i) {
            if (comp(mData[i], mData[best])) best = i;
        }
        return &mData[best];
    }

    template<typename Comparator>
    constexpr Iterator MaxElement(Comparator comp) {
        SizeType best = 0;
        for (SizeType i = 1; i < N; ++i) {
            if (comp(mData[best], mData[i])) best = i;
        }
        return &mData[best];
    }

    constexpr Iterator MinElement() { return MinElement([](const T& a, const T& b) { return a < b; }); }
    constexpr Iterator MaxElement() { return MaxElement([](const T& a, const T& b) { return a < b; }); }

    // Sum of elements; R avoids overflow (e.g. Sum<UInt64>()).
    template<typename R = T>
    constexpr R Sum() const {
        R total{};
        for (SizeType i = 0; i < N; ++i) total += static_cast<R>(mData[i]);
        return total;
    }

    // Utility methods - Mutation
    constexpr void Fill(const T& value) {
        for (SizeType i = 0; i < N; ++i) mData[i] = value;
    }

    constexpr void SwapItems(SizeType a, SizeType b) {
        if (a == b || a >= N || b >= N) return;
        T temp = std::move(mData[a]);
        mData[a] = std::move(mData[b]);
        mData[b] = std::move(temp);
    }

    // Element-wise swap — O(N), unlike DynamicArray::Swap, which only swaps pointers.
    constexpr void Swap(StaticArray& other) noexcept(std::is_nothrow_swappable_v<T>) {
        for (SizeType i = 0; i < N; ++i) {
            using std::swap;
            swap(mData[i], other.mData[i]);
        }
    }

    constexpr void Reverse() {
        for (SizeType i = 0; i < N / 2; ++i) SwapItems(i, N - 1 - i);
    }

    // Insertion sort: stable, allocation-free and constexpr-friendly. Fixed-size arrays are
    // usually small; for large N use a DynamicArray or sort Data() with std::sort.
    template<typename Comparator>
    constexpr void Sort(Comparator comp) {
        for (SizeType i = 1; i < N; ++i) {
            T key = std::move(mData[i]);
            SizeType j = i;
            while (j > 0 && comp(key, mData[j - 1])) {
                mData[j] = std::move(mData[j - 1]);
                --j;
            }
            mData[j] = std::move(key);
        }
    }

    constexpr void Sort() {
        Sort([](const T& a, const T& b) { return a < b; });
    }

    // Utility methods - Random draws (PluRandom, thread_local engine)
    [[nodiscard]] SizeType GetRandomIndex() const {
        return static_cast<SizeType>(PluRandom::NextIndex(N));
    }

    T& GetRandomItem() { return mData[GetRandomIndex()]; }
    const T& GetRandomItem() const { return mData[GetRandomIndex()]; }

    // Random element matching the predicate (reservoir sampling — single pass).
    // nullptr when nothing matches.
    template<typename Predicate>
    T* GetRandomItemIf(Predicate pred) {
        T* picked = nullptr;
        SizeType matches = 0;
        for (SizeType i = 0; i < N; ++i) {
            if (!pred(mData[i])) continue;
            ++matches;
            if (PluRandom::NextIndex(matches) == 0) picked = &mData[i];
        }
        return picked;
    }

    // In-place Fisher-Yates shuffle.
    void Shuffle() {
        for (SizeType i = N; i > 1; --i) {
            const SizeType j = static_cast<SizeType>(PluRandom::NextIndex(i));
            SwapItems(i - 1, j);
        }
    }

    // Utility methods - Transformations
    // 1:1 mapping into a StaticArray of the same size; the element type is deduced from the function.
    template<typename Func>
    constexpr auto Map(Func func) const -> StaticArray<std::remove_cvref_t<decltype(func(std::declval<const T&>()))>, N> {
        StaticArray<std::remove_cvref_t<decltype(func(std::declval<const T&>()))>, N> result{};
        for (SizeType i = 0; i < N; ++i) result.mData[i] = func(mData[i]);
        return result;
    }

    // Folds into a single value: acc = func(acc, item), front to back.
    template<typename R, typename Func>
    constexpr R Reduce(R init, Func func) const {
        R acc = std::move(init);
        for (SizeType i = 0; i < N; ++i) acc = func(std::move(acc), mData[i]);
        return acc;
    }

    // Comparisons
    constexpr bool operator==(const StaticArray& other) const {
        for (SizeType i = 0; i < N; ++i) {
            if (!(mData[i] == other.mData[i])) return false;
        }
        return true;
    }

    constexpr bool operator!=(const StaticArray& other) const { return !(*this == other); }
};

// StaticArray a{1, 2, 3}; -> StaticArray<int, 3>
template<typename T, typename... U>
StaticArray(T, U...) -> StaticArray<T, 1 + sizeof...(U)>;

} // namespace Plu
