//
// Created by Plutex on 2026-11-10.
//

#include "TestFramework.h"

#include "PluSTL/Functional/MoveOnlyFunction.h"
#include "PluSTL/Queue/Queue.h"

#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>

using Task = Plu::MoveOnlyFunction<void()>;

// ============================================================================
// Compile-time guarantees
// ============================================================================

static_assert(!std::is_copy_constructible_v<Task>);
static_assert(!std::is_copy_assignable_v<Task>);
static_assert(std::is_nothrow_move_constructible_v<Task>);
static_assert(std::is_nothrow_move_assignable_v<Task>);
static_assert(std::is_constructible_v<Plu::MoveOnlyFunction<int(int)>, int (*)(int)>);
static_assert(!std::is_constructible_v<Plu::MoveOnlyFunction<int(int)>, void (*)()>);
static_assert(sizeof(Task) <= 64);

namespace
{
    // Counts live instances so the tests can check nothing is leaked or destroyed twice.
    struct Tracked
    {
        static inline int sLive = 0;
        int* calls;

        explicit Tracked(int* callCounter) : calls(callCounter) { ++sLive; }
        Tracked(Tracked&& other) noexcept : calls(other.calls) { ++sLive; }
        Tracked(const Tracked&) = delete;
        ~Tracked() { --sLive; }

        void operator()() { ++*calls; }
    };

    // Same, but too big for the inline buffer.
    struct BigTracked : Tracked
    {
        char padding[256] = {};
        using Tracked::Tracked;
    };

    int Twice(int value) { return value * 2; }

    struct Counter
    {
        int value = 0;
        int Add(int amount) { value += amount; return value; }
    };
}

// ============================================================================
// Basics
// ============================================================================

PLU_TEST(MoveOnlyFunction_DefaultIsEmpty)
{
    Task task;
    PLU_CHECK(task.IsEmpty());
    PLU_CHECK_FALSE(static_cast<bool>(task));
    PLU_CHECK(task == nullptr);

    Task fromNull = nullptr;
    PLU_CHECK(fromNull.IsEmpty());
}

PLU_TEST(MoveOnlyFunction_InvokingEmptyThrows)
{
    Task task;
    bool threw = false;
    try { task(); }
    catch (const std::bad_function_call&) { threw = true; }
    PLU_CHECK(threw);
}

PLU_TEST(MoveOnlyFunction_LambdaWithCaptures)
{
    int calls = 0;
    Task task = [&calls]() { ++calls; };
    PLU_CHECK_FALSE(task.IsEmpty());
    task();
    task();
    PLU_CHECK_EQ(calls, 2);
}

PLU_TEST(MoveOnlyFunction_ReturnsAndForwardsArguments)
{
    Plu::MoveOnlyFunction<int(int, int)> add = [](int a, int b) { return a + b; };
    PLU_CHECK_EQ(add(2, 3), 5);

    Plu::MoveOnlyFunction<void(int&)> increment = [](int& value) { ++value; };
    int value = 1;
    increment(value);
    PLU_CHECK_EQ(value, 2);

    Plu::MoveOnlyFunction<std::size_t(std::unique_ptr<int>)> sink = [](std::unique_ptr<int> p) {
        return p ? static_cast<std::size_t>(*p) : 0u;
    };
    PLU_CHECK_EQ(sink(std::make_unique<int>(7)), std::size_t{7});
}

PLU_TEST(MoveOnlyFunction_FunctionAndMemberPointers)
{
    Plu::MoveOnlyFunction<int(int)> twice = &Twice;
    PLU_CHECK_EQ(twice(21), 42);

    int (*nullFunction)(int) = nullptr;
    Plu::MoveOnlyFunction<int(int)> fromNull = nullFunction;
    PLU_CHECK(fromNull.IsEmpty());

    Counter counter;
    Plu::MoveOnlyFunction<int(Counter&, int)> add = &Counter::Add;
    add(counter, 3);
    PLU_CHECK_EQ(add(counter, 4), 7);
}

// ============================================================================
// Move-only captures — what std::function cannot hold
// ============================================================================

PLU_TEST(MoveOnlyFunction_HoldsMoveOnlyCapture)
{
    auto value = std::make_unique<int>(10);
    Plu::MoveOnlyFunction<int()> task = [p = std::move(value)]() { return *p + 1; };
    PLU_CHECK_EQ(task(), 11);
}

PLU_TEST(MoveOnlyFunction_HoldsPackagedTask)
{
    std::packaged_task<int()> packaged([]() { return 99; });
    std::future<int> future = packaged.get_future();

    Task task = std::move(packaged);
    task();
    PLU_CHECK_EQ(future.get(), 99);
}

PLU_TEST(MoveOnlyFunction_LivesInQueue)
{
    Plu::Queue<Task> queue;
    int sum = 0;
    for (int i = 1; i <= 100; ++i)
        queue.PushBack(Task([&sum, owned = std::make_unique<int>(i)]() { sum += *owned; }));

    Task task;
    while (queue.TryPopFront(task))
        task();
    PLU_CHECK_EQ(sum, 5050);
}

// ============================================================================
// Lifetime — inline and heap storage
// ============================================================================

template<typename Callable>
static void CheckLifetime()
{
    int calls = 0;
    {
        Task first = Callable(&calls);
        PLU_CHECK_EQ(Tracked::sLive, 1);

        Task second = std::move(first);
        PLU_CHECK(first.IsEmpty());
        PLU_CHECK_EQ(Tracked::sLive, 1);
        second();

        Task third;
        third = std::move(second);
        PLU_CHECK(second.IsEmpty());
        PLU_CHECK_EQ(Tracked::sLive, 1);
        third();

        Task& alias = third; // through a reference, so -Wself-move stays quiet
        third = std::move(alias); // self-move must not destroy it
        PLU_CHECK_FALSE(third.IsEmpty());
        third();

        Task other = Callable(&calls);
        PLU_CHECK_EQ(Tracked::sLive, 2);
        third.Swap(other);
        PLU_CHECK_EQ(Tracked::sLive, 2);
        third();
        other();

        other = nullptr;
        PLU_CHECK_EQ(Tracked::sLive, 1);

        third = Callable(&calls); // reassigning destroys the old callable
        PLU_CHECK_EQ(Tracked::sLive, 1);

        third.Reset();
        PLU_CHECK(third.IsEmpty());
        PLU_CHECK_EQ(Tracked::sLive, 0);

        third = Callable(&calls);
    }
    PLU_CHECK_EQ(Tracked::sLive, 0);
    PLU_CHECK_EQ(calls, 5);
}

PLU_TEST(MoveOnlyFunction_InlineLifetime)
{
    CheckLifetime<Tracked>();
}

PLU_TEST(MoveOnlyFunction_HeapLifetime)
{
    CheckLifetime<BigTracked>();
}

PLU_TEST(MoveOnlyFunction_CustomInlineSize)
{
    int calls = 0;
    Plu::MoveOnlyFunction<void(), 512> task = BigTracked(&calls);
    PLU_CHECK_EQ(Tracked::sLive, 1);
    Plu::MoveOnlyFunction<void(), 512> moved = std::move(task);
    moved();
    PLU_CHECK_EQ(calls, 1);
    moved.Reset();
    PLU_CHECK_EQ(Tracked::sLive, 0);
}

PLU_TEST(MoveOnlyFunction_WrapsOtherInlineSize)
{
    Plu::MoveOnlyFunction<int(), 128> empty;
    Plu::MoveOnlyFunction<int()> fromEmpty = std::move(empty);
    PLU_CHECK(fromEmpty.IsEmpty());

    Plu::MoveOnlyFunction<int(), 128> full = []() { return 5; };
    Plu::MoveOnlyFunction<int()> fromFull = std::move(full);
    PLU_CHECK_EQ(fromFull(), 5);
}
