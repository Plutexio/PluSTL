#pragma once
#include <stdexcept>
#include "PluSTL/Pointers/ControlBlock.h"

namespace Plu
{
    template<typename T>
    struct ControlBlock;
    
    template<typename T>
    class TOwningPointer
    {
        ControlBlockBase* control;

        template<typename U>
        friend class TOwningPointer;
        
        template<typename U>
        friend class TUsePointer;

        // Friend functions for the casts
        template<typename To, typename From>
        friend TOwningPointer<To> DynamicCast(const TOwningPointer<From>& from);
        
        template<typename To, typename From>
        friend TOwningPointer<To> StaticCast(const TOwningPointer<From>& from);

    public:
        // Takes ownership of a control block built by the caller — a ControlBlock<T> subclass
        // that carries extra state (e.g. a scripting-side keeper, usually with externallyOwned
        // set). The block must be freshly constructed (strong = weak = 1) and not yet handed to
        // any other pointer; the returned owner is that one strong reference.
        static TOwningPointer AdoptControlBlock(ControlBlock<T>* block)
        {
            TOwningPointer result;
            result.control = block;
            return result;
        }

        // Default constructor
        TOwningPointer() : control(nullptr) {}
        
        // Constructor from nullptr
        TOwningPointer(std::nullptr_t) : control(nullptr) {}
        
        // Constructor from a raw pointer
        explicit TOwningPointer(T* ptr)
            : control(ptr ? new ControlBlock<T>(ptr) : nullptr) {}
        
        // Destructor - decrements the owner count
        ~TOwningPointer()
        {
            Release();
        }

        // Copy constructor - increments the owner count
        TOwningPointer(const TOwningPointer& other) : control(other.control)
        {
            if (control)
            {
                PLU_PTR_ASSERT_OWNER(control);
                control->strongCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Converting copy constructor (Derived -> Base)
        template<typename U>
        TOwningPointer(const TOwningPointer<U>& other) : control(other.control)
        {
            static_assert(std::is_base_of_v<T, U> || std::is_same_v<T, U> || std::is_base_of_v<U, T>,
                "Types must be related through inheritance");
            if (control)
            {
                PLU_PTR_ASSERT_OWNER(control);
                control->strongCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Copy assignment
        TOwningPointer& operator=(const TOwningPointer& other)
        {
            if (this != &other)
            {
                Release();
                control = other.control;
                if (control)
                {
                    PLU_PTR_ASSERT_OWNER(control);
                    control->strongCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        // Converting copy assignment (Derived -> Base)
        template<typename U>
        TOwningPointer& operator=(const TOwningPointer<U>& other)
        {
            static_assert(std::is_base_of<T, U>::value || std::is_same<T, U>::value || std::is_base_of<U, T>::value,
                "Types must be related through inheritance or be the same type");
            Release();
            control = other.control;
            if (control)
            {
                PLU_PTR_ASSERT_OWNER(control);
                control->strongCount.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // Move constructor
        TOwningPointer(TOwningPointer&& other) noexcept : control(other.control)
        {
            other.control = nullptr;
        }

        // Move assignment
        TOwningPointer& operator=(TOwningPointer&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                control = other.control;
                other.control = nullptr;
            }
            return *this;
        }

        // Raw pointer assignment
        TOwningPointer& operator=(T* ptr)
        {
            Release();
            control = ptr ? new ControlBlock<T>(ptr) : nullptr;
            return *this;
        }

        // nullptr assignment
        TOwningPointer& operator=(std::nullptr_t)
        {
            Release();
            control = nullptr;
            return *this;
        }

        // Getters
        [[nodiscard]] T* GetRaw() const
        {
            return control ? static_cast<ControlBlock<T>*>(control)->Get() : nullptr;
        }

        [[nodiscard]] T* Get() const
        {
            return GetRaw();
        }

        // Operators
        [[nodiscard]] T* operator->() const
        {
            PLU_PTR_ASSERT_OWNER(control);
            return GetRaw();
        }

        [[nodiscard]] T& operator*() const
        {
            PLU_PTR_ASSERT_OWNER(control);
            return *GetRaw();
        }

        [[nodiscard]] operator bool() const
        {
            return control != nullptr && control->ptr != nullptr;
        }

        [[nodiscard]] bool operator==(const TOwningPointer& other) const
        {
            return control == other.control;
        }

        [[nodiscard]] bool operator!=(const TOwningPointer& other) const
        {
            return control != other.control;
        }
        

        // Diagnostics
        [[nodiscard]] int GetOwningCount() const
        {
            return control ? control->strongCount.load(std::memory_order_relaxed) : 0;
        }

        // Number of live TUsePointers (weakCount minus the owners' collective reference).
        [[nodiscard]] int GetUseCount() const
        {
            if (!control) return 0;
            const int weak   = control->weakCount.load(std::memory_order_relaxed);
            const int strong = control->strongCount.load(std::memory_order_relaxed);
            return strong > 0 ? weak - 1 : weak;
        }

    private:
        void Release()
        {
            if (!control) return;
            PLU_PTR_ASSERT_OWNER(control);

            // Last owner gone: destroy the managed object — unless its lifetime belongs to
            // someone else (e.g. a script interpreter), in which case we only drop refs.
            if (control->strongCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                if (!control->externallyOwned)
                    delete static_cast<ControlBlock<T>*>(control)->Get();
                control->ptr = nullptr;

                // Drop the owners' collective weak ref; frees the block if it was the last.
                if (control->weakCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    delete control;
            }
            control = nullptr;
        }
    };
}

namespace std
{
    template<typename T>
    struct hash<Plu::TOwningPointer<T>>
    {
        size_t operator()(const Plu::TOwningPointer<T>& obj) const noexcept
        {
            return std::hash<void*>()(obj.GetRaw());
        }
    };
}
