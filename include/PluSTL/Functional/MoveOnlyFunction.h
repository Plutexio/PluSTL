//
// Created by Plutex on 2026-11-10.
//

#ifndef PLUSTL_MOVEONLYFUNCTION_H
#define PLUSTL_MOVEONLYFUNCTION_H

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace Plu
{
    // ========================================================================
    // MoveOnlyFunction<R(Args...)> — type-erased, move-only callable
    // ========================================================================
    //
    // The C++23 std::move_only_function for a C++20 codebase. It holds any callable
    // (lambda, functor, function pointer, member pointer) that can be invoked as
    // R(Args...). Unlike std::function, the callable does NOT need to be copyable, so
    // lambdas capturing a std::unique_ptr, a TOwningPointer moved in, a std::promise
    // or a std::packaged_task all fit. That is what a task queue wants:
    //
    //     Plu::MoveOnlyFunction<void()> task = [data = std::move(bigBuffer)]() { Process(data); };
    //     queue.PushBack(std::move(task));
    //
    // Arguments are bound by capturing them, not by the queue:
    //
    //     template<typename F, typename... Args>
    //     void Run(F&& f, Args&&... args)
    //     {
    //         Push([f = std::forward<F>(f), ...a = std::forward<Args>(args)]() mutable {
    //             std::invoke(f, a...);
    //         });
    //     }
    //
    // Storage: callables up to InlineSize bytes (and no stricter than max_align_t, and
    // nothrow-move-constructible) live inline — no allocation. Anything bigger goes to
    // the heap. With the default InlineSize of 48 the object is 64 bytes on x86-64 (vtable
    // pointer + padding to max_align_t + the buffer), so a lambda with up to six
    // pointer-sized captures stays inline.
    // Raise InlineSize per use site if profiling shows the heap path.
    //
    // Differences from std::move_only_function, on purpose:
    //  - only the plain R(Args...) signature; no const / noexcept / &-qualified forms.
    //    operator() is non-const, as in the standard type's default form.
    //  - invoking an empty MoveOnlyFunction throws std::bad_function_call (the standard
    //    type makes it UB).
    //
    // Not thread-safe, like every non-Concurrent PluSTL type: move it into a concurrent
    // queue, do not share one instance between threads.
    template<typename Signature, std::size_t InlineSize = 48>
    class MoveOnlyFunction;

    template<typename T>
    struct IsMoveOnlyFunction : std::false_type {};

    template<typename Signature, std::size_t InlineSize>
    struct IsMoveOnlyFunction<MoveOnlyFunction<Signature, InlineSize>> : std::true_type {};

    template<typename R, typename... Args, std::size_t InlineSize>
    class MoveOnlyFunction<R(Args...), InlineSize>
    {
        static_assert(InlineSize >= sizeof(void*), "InlineSize must fit at least a pointer (the heap path stores one)");

        // One static table per stored callable type.
        struct VTable
        {
            R (*Invoke)(void* storage, Args&&... args);
            // Move-constructs into dst from src and destroys src. Never throws.
            void (*Relocate)(void* dst, void* src) noexcept;
            void (*Destroy)(void* storage) noexcept;
        };

        template<typename F>
        static constexpr bool StoredInline =
            sizeof(F) <= InlineSize &&
            alignof(F) <= alignof(std::max_align_t) &&
            std::is_nothrow_move_constructible_v<F>;

        template<typename F>
        static F& Access(void* storage) noexcept
        {
            if constexpr (StoredInline<F>)
                return *std::launder(reinterpret_cast<F*>(storage));
            else
                return **reinterpret_cast<F**>(storage);
        }

        template<typename F>
        static R InvokeImpl(void* storage, Args&&... args)
        {
            if constexpr (std::is_void_v<R>)
                std::invoke(Access<F>(storage), std::forward<Args>(args)...);
            else
                return std::invoke(Access<F>(storage), std::forward<Args>(args)...);
        }

        template<typename F>
        static void RelocateImpl(void* dst, void* src) noexcept
        {
            if constexpr (StoredInline<F>)
            {
                F& source = Access<F>(src);
                ::new (dst) F(std::move(source));
                source.~F();
            }
            else
            {
                *reinterpret_cast<F**>(dst) = *reinterpret_cast<F**>(src);
            }
        }

        template<typename F>
        static void DestroyImpl(void* storage) noexcept
        {
            if constexpr (StoredInline<F>)
                Access<F>(storage).~F();
            else
                delete *reinterpret_cast<F**>(storage);
        }

        template<typename F>
        static constexpr VTable VTableFor = { &InvokeImpl<F>, &RelocateImpl<F>, &DestroyImpl<F> };

        template<typename T>
        static constexpr bool IsSelf = std::is_same_v<std::remove_cvref_t<T>, MoveOnlyFunction>;

        // A null function/member pointer, or an empty MoveOnlyFunction of another
        // InlineSize, produces an empty MoveOnlyFunction instead of one that wraps nothing.
        template<typename F>
        static bool IsNullCallable(const F& callable) noexcept
        {
            if constexpr (std::is_pointer_v<F> || std::is_member_pointer_v<F>)
                return callable == nullptr;
            else if constexpr (IsMoveOnlyFunction<F>::value)
                return callable.IsEmpty();
            else
                return false;
        }

    public:
        using ResultType = R;

        MoveOnlyFunction() noexcept = default;
        MoveOnlyFunction(std::nullptr_t) noexcept {}

        template<typename F>
            requires (!IsSelf<F> &&
                      std::is_constructible_v<std::decay_t<F>, F> &&
                      std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
        MoveOnlyFunction(F&& callable)
        {
            using Stored = std::decay_t<F>;
            if (IsNullCallable(callable))
                return;

            if constexpr (StoredInline<Stored>)
                ::new (static_cast<void*>(mStorage)) Stored(std::forward<F>(callable));
            else
                *reinterpret_cast<Stored**>(mStorage) = new Stored(std::forward<F>(callable));

            // Set last: if the constructor above throws, this stays empty and the
            // destructor has nothing to do.
            mVTable = &VTableFor<Stored>;
        }

        MoveOnlyFunction(MoveOnlyFunction&& other) noexcept
        {
            MoveFrom(other);
        }

        MoveOnlyFunction& operator=(MoveOnlyFunction&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(other);
            }
            return *this;
        }

        MoveOnlyFunction& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        template<typename F>
            requires (!IsSelf<F> &&
                      std::is_constructible_v<std::decay_t<F>, F> &&
                      std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
        MoveOnlyFunction& operator=(F&& callable)
        {
            // Build first, then swap in: a throwing constructor leaves *this untouched.
            MoveOnlyFunction temporary(std::forward<F>(callable));
            Swap(temporary);
            return *this;
        }

        MoveOnlyFunction(const MoveOnlyFunction&) = delete;
        MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

        ~MoveOnlyFunction()
        {
            Reset();
        }

        R operator()(Args... args)
        {
            if (mVTable == nullptr)
                throw std::bad_function_call();
            return mVTable->Invoke(mStorage, std::forward<Args>(args)...);
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return mVTable == nullptr; }
        explicit operator bool() const noexcept { return mVTable != nullptr; }

        friend bool operator==(const MoveOnlyFunction& function, std::nullptr_t) noexcept
        {
            return function.IsEmpty();
        }

        // Destroys the held callable (and frees its heap block, if any).
        void Reset() noexcept
        {
            if (mVTable != nullptr)
            {
                mVTable->Destroy(mStorage);
                mVTable = nullptr;
            }
        }

        void Swap(MoveOnlyFunction& other) noexcept
        {
            if (this == &other)
                return;
            MoveOnlyFunction temporary(std::move(other));
            other.MoveFrom(*this);
            MoveFrom(temporary);
        }

        friend void swap(MoveOnlyFunction& a, MoveOnlyFunction& b) noexcept { a.Swap(b); }

    private:
        // Precondition: *this is empty.
        void MoveFrom(MoveOnlyFunction& other) noexcept
        {
            if (other.mVTable != nullptr)
            {
                other.mVTable->Relocate(mStorage, other.mStorage);
                mVTable = other.mVTable;
                other.mVTable = nullptr;
            }
        }

        const VTable* mVTable = nullptr;
        alignas(std::max_align_t) unsigned char mStorage[InlineSize];
    };
}

#endif //PLUSTL_MOVEONLYFUNCTION_H
