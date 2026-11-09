//
// Created by Plutex on 2026-11-09.
//

#include "TestFramework.h"

#include "PluSTL/Array/StaticArray.h"
#include "PluSTL/String/String.h"

#include <stdexcept>
#include <type_traits>

// ============================================================================
// Compile-time guarantees
// ============================================================================

static_assert(std::is_aggregate_v<Plu::StaticArray<int, 4>>);
static_assert(std::is_trivially_copyable_v<Plu::StaticArray<int, 4>>);
static_assert(sizeof(Plu::StaticArray<int, 4>) == sizeof(int) * 4);
static_assert(Plu::StaticArray<int, 7>::Size() == 7);
static_assert(std::is_same_v<decltype(Plu::StaticArray{1, 2, 3}), Plu::StaticArray<int, 3>>);

static_assert([] {
    Plu::StaticArray<int, 5> array = {5, 3, 1, 4, 2};
    array.Sort();
    return array == Plu::StaticArray<int, 5>{1, 2, 3, 4, 5} && array.Sum() == 15;
}());

// ============================================================================
// Basics
// ============================================================================

PLU_TEST(StaticArray_AccessAndSize)
{
    Plu::StaticArray<int, 4> array = {10, 20, 30, 40};
    PLU_CHECK_EQ(array.Size(), std::size_t{4});
    PLU_CHECK_FALSE(array.IsEmpty());
    PLU_CHECK_EQ(array.Front(), 10);
    PLU_CHECK_EQ(array.Back(), 40);
    PLU_CHECK_EQ(array[2], 30);
    PLU_CHECK_EQ(array.At(1), 20);
    PLU_CHECK_EQ(array.Data(), &array[0]);
    PLU_CHECK(array.IsValidIndex(3));
    PLU_CHECK_FALSE(array.IsValidIndex(4));
}

PLU_TEST(StaticArray_AtThrowsOutOfRange)
{
    Plu::StaticArray<int, 2> array = {1, 2};
    bool thrown = false;
    try { (void)array.At(2); } catch (const std::out_of_range&) { thrown = true; }
    PLU_CHECK(thrown);
}

PLU_TEST(StaticArray_PartialInitValueInitializesTheRest)
{
    Plu::StaticArray<int, 4> array = {7};
    PLU_CHECK_EQ(array[0], 7);
    PLU_CHECK_EQ(array[1], 0);
    PLU_CHECK_EQ(array[3], 0);
}

PLU_TEST(StaticArray_RangeForAndCopy)
{
    Plu::StaticArray<Plu::String, 3> names = {"a", "b", "c"};
    Plu::String joined;
    for (const Plu::String& name : names) joined += name;
    PLU_CHECK(joined == "abc");

    Plu::StaticArray<Plu::String, 3> copy = names;
    PLU_CHECK(copy == names);
    copy[0] = "z";
    PLU_CHECK(copy != names);
    PLU_CHECK(names[0] == "a");
}

// ============================================================================
// Search & queries
// ============================================================================

PLU_TEST(StaticArray_SearchAndQueries)
{
    Plu::StaticArray<int, 6> array = {4, 8, 15, 16, 23, 42};
    PLU_CHECK(array.Contains(15));
    PLU_CHECK_FALSE(array.Contains(5));
    PLU_CHECK_EQ(array.IndexOf(23), std::size_t{4});
    PLU_CHECK_EQ(array.IndexOf(5), decltype(array)::InvalidIndex);
    PLU_CHECK(array.Find(99) == array.End());
    PLU_CHECK_EQ(*array.FindIf([](int v) { return v > 10; }), 15);
    PLU_CHECK_EQ(array.CountIf([](int v) { return v % 2 == 0; }), std::size_t{4});
    PLU_CHECK(array.All([](int v) { return v > 0; }));
    PLU_CHECK(array.Any([](int v) { return v == 42; }));
    PLU_CHECK_EQ(*array.MinElement(), 4);
    PLU_CHECK_EQ(*array.MaxElement(), 42);
    PLU_CHECK_EQ(array.Sum<long long>(), 108LL);
}

// ============================================================================
// Mutation & transformations
// ============================================================================

PLU_TEST(StaticArray_FillReverseSwap)
{
    Plu::StaticArray<int, 5> array = {1, 2, 3, 4, 5};
    array.Reverse();
    PLU_CHECK((array == Plu::StaticArray<int, 5>{5, 4, 3, 2, 1}));

    Plu::StaticArray<int, 5> other{};
    other.Fill(9);
    array.Swap(other);
    PLU_CHECK_EQ(array.Count(9), std::size_t{5});
    PLU_CHECK_EQ(other[0], 5);
}

PLU_TEST(StaticArray_SortWithComparatorIsStable)
{
    struct Item { int Key; int Order; };
    Plu::StaticArray<Item, 5> items = {{{2, 0}, {1, 1}, {2, 2}, {1, 3}, {0, 4}}};
    items.Sort([](const Item& a, const Item& b) { return a.Key < b.Key; });
    PLU_CHECK_EQ(items[0].Order, 4);
    PLU_CHECK_EQ(items[1].Order, 1);
    PLU_CHECK_EQ(items[2].Order, 3);
    PLU_CHECK_EQ(items[3].Order, 0);
    PLU_CHECK_EQ(items[4].Order, 2);
}

PLU_TEST(StaticArray_MapAndReduce)
{
    Plu::StaticArray<int, 3> array = {1, 2, 3};
    auto squares = array.Map([](int v) { return v * v; });
    static_assert(std::is_same_v<decltype(squares), Plu::StaticArray<int, 3>>);
    PLU_CHECK((squares == Plu::StaticArray<int, 3>{1, 4, 9}));

    const int product = array.Reduce(1, [](int acc, int v) { return acc * v; });
    PLU_CHECK_EQ(product, 6);
}

PLU_TEST(StaticArray_ShuffleKeepsElements)
{
    Plu::StaticArray<int, 8> array = {0, 1, 2, 3, 4, 5, 6, 7};
    array.Shuffle();
    PLU_CHECK_EQ(array.Sum(), 28);
    for (int i = 0; i < 8; ++i) PLU_CHECK(array.Contains(i));

    PLU_CHECK(array.IsValidIndex(array.GetRandomIndex()));
    PLU_CHECK(array.GetRandomItemIf([](int v) { return v > 100; }) == nullptr);
    int* odd = array.GetRandomItemIf([](int v) { return v % 2 == 1; });
    PLU_CHECK(odd != nullptr && *odd % 2 == 1);
}
