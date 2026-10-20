#pragma once
#include <stdexcept>
#include "PluSTL/Pointers/ControlBlock.h"

namespace Plu
{
    template<typename T>
    class TOwningPointer;

    template<typename T>
    struct ControlBlock;
    
    template<typename T>
    class TUsePointer
    {
        ControlBlockBase* control;

        template<typename U>
        friend class TUsePointer;

        // Friend functions for the casts
        template<typename To, typename From>
        friend TUsePointer<To> DynamicCast(const TUsePointer<From>& from);
        
        template<typename To, typename From>
        friend TUsePointer<To> StaticCast(const TUsePointer<From>& from);

    public:
        // Default constructor
        TUsePointer() : control(nullptr) {}
        
        // Constructor from nullptr
        TUsePointer(std::nullptr_t) : control(nullptr) {}
        
        // Constructor from TOwningPointer
        TUsePointer(const TOwningPointer<T>& owner) : control(owner.control)
        {
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Converting constructor from TOwningPointer (Derived -> Base)
        template<typename U>
        TUsePointer(const TOwningPointer<U>& owner) : control(owner.control)
        {
            static_assert(std::is_base_of<T, U>::value || std::is_same<T, U>::value || std::is_base_of<U, T>::value, 
                "Types must be related through inheritance or be the same type");
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Destructor
        ~TUsePointer()
        {
            Release();
        }

        // Copy constructor
        TUsePointer(const TUsePointer& other) : control(other.control)
        {
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Converting copy constructor (Derived -> Base)
        template<typename U>
        TUsePointer(const TUsePointer<U>& other) : control(other.control)
        {
            static_assert(std::is_base_of<T, U>::value || std::is_same<T, U>::value || std::is_base_of<U, T>::value, 
                "Types must be related through inheritance or be the same type");
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Copy assignment
        TUsePointer& operator=(const TUsePointer& other)
        {
            if (this != &other)
            {
                Release();
                control = other.control;
                if (control)
                {
                    control->weakCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        // Converting copy assignment (Derived -> Base)
        template<typename U>
        TUsePointer& operator=(const TUsePointer<U>& other)
        {
            static_assert(std::is_base_of<T, U>::value || std::is_same<T, U>::value || std::is_base_of<U, T>::value, 
                "Types must be related through inheritance or be the same type");
            Release();
            control = other.control;
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // Move constructor
        TUsePointer(TUsePointer&& other) noexcept : control(other.control)
        {
            other.control = nullptr;
        }

        // Move assignment
        TUsePointer& operator=(TUsePointer&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                control = other.control;
                other.control = nullptr;
            }
            return *this;
        }

        // Assignment from TOwningPointer
        TUsePointer& operator=(const TOwningPointer<T>& owner)
        {
            Release();
            control = owner.control;
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // Converting assignment from TOwningPointer (Derived -> Base)
        template<typename U>
        TUsePointer& operator=(const TOwningPointer<U>& owner)
        {
            static_assert(std::is_base_of<T, U>::value || std::is_same<T, U>::value || std::is_base_of<U, T>::value, 
                "Types must be related through inheritance or be the same type");
            Release();
            control = owner.control;
            if (control)
            {
                control->weakCount.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // nullptr assignment
        TUsePointer& operator=(std::nullptr_t)
        {
            Release();
            control = nullptr;
            return *this;
        }

        // Getters - throw if there are no owners left
        [[nodiscard]] T* GetRaw() const
        {
            if (!control) return nullptr;
            if (control->strongCount.load(std::memory_order_acquire) == 0)
                throw std::runtime_error("TUsePointer: Object has no owners!");
            return static_cast<ControlBlock<T>*>(control)->Get();
        }

        [[nodiscard]] T* Get() const
        {
            return GetRaw();
        }

        // Operators - throw if there are no owners left
        [[nodiscard]] T* operator->() const
        {
            return GetRaw();
        }

        [[nodiscard]] T& operator*() const
        {
            return *GetRaw();
        }

        // Whether the object is still alive
        [[nodiscard]] bool IsValid() const
        {
            return control && control->strongCount.load(std::memory_order_acquire) > 0 && control->ptr != nullptr;
        }

        [[nodiscard]] explicit operator bool() const
        {
            return IsValid();
        }

        [[nodiscard]] bool operator==(const TUsePointer& other) const
        {
            return GetRaw() == other.GetRaw();
        }

        [[nodiscard]] bool operator!=(const TUsePointer& other) const
        {
            return GetRaw() != other.GetRaw();
        }

        [[nodiscard]] bool operator==(const std::nullptr_t other) const
        {
            if (!control)
            {
                return true;
            }
            return GetRaw() == nullptr;
        }

        [[nodiscard]] bool operator!=(const std::nullptr_t other) const
        {
            if (!control)
            {
                return false;
            }
            if (control->strongCount.load(std::memory_order_acquire) == 0)
            {
                return false;
            }
            return GetRaw() != nullptr;
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

            // weakCount reached 0: no owners' collective ref and no observers left → free the block.
            if (control->weakCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete control;

            control = nullptr;
        }
    };
}
