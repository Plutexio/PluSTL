import gdb
import gdb.printing


class BasicStringPrinter:
    """Pretty printer for Plu::BasicString<CharT, Allocator>"""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            # Get the character type
            char_type = self.val.type.template_argument(0)
            char_size = char_type.sizeof
            
            # Read the fields safely - cast them to the right types
            length_field = self.val['mLength']
            capacity_field = self.val['mCapacity']
            
            # Convert to a Python int through a cast
            size_t = gdb.lookup_type('size_t')
            length = int(length_field.cast(size_t))
            capacity = int(capacity_field.cast(size_t))
            
            # Compute the SSO capacity
            if char_size > 0:
                sso_capacity = (24 // char_size) - 1
            else:
                sso_capacity = 23  # default for char
            
            # Check whether it is on the heap
            is_heap = capacity > sso_capacity
            
            # Get the data
            if is_heap:
                data_ptr = self.val['mHeapData']
            else:
                # SSO uses the inline array
                data_ptr = self.val['mSsoBuffer'].address
            
            # Check the pointer
            if not data_ptr or int(data_ptr.cast(gdb.lookup_type('uintptr_t'))) == 0:
                return '""'
            
            # Read the string
            if char_type.name == 'char':
                try:
                    # Read the memory through the inferior
                    inferior = gdb.selected_inferior()
                    data_addr = int(data_ptr.cast(gdb.lookup_type('uintptr_t')))
                    memory = inferior.read_memory(data_addr, length)
                    string_value = memory.tobytes().decode('utf-8', errors='replace')
                    
                    storage = "heap" if is_heap else "SSO"
                    return f'"{string_value}" [len={length}, cap={capacity}, {storage}]'
                except:
                    # Fallback - try string()
                    try:
                        string_value = data_ptr.string()
                        storage = "heap" if is_heap else "SSO"
                        return f'"{string_value}" [len={length}, cap={capacity}, {storage}]'
                    except:
                        return f'"<read error>" [len={length}, cap={capacity}]'
            
            elif char_type.name == 'wchar_t':
                try:
                    inferior = gdb.selected_inferior()
                    data_addr = int(data_ptr.cast(gdb.lookup_type('uintptr_t')))
                    wchar_size = char_size
                    memory = inferior.read_memory(data_addr, length * wchar_size)
                    
                    # Convert wchar_t to a string
                    chars = []
                    for i in range(length):
                        offset = i * wchar_size
                        if wchar_size == 4:
                            wc = int.from_bytes(memory[offset:offset+4].tobytes(), 'little')
                        else:  # wchar_size == 2
                            wc = int.from_bytes(memory[offset:offset+2].tobytes(), 'little')
                        
                        if wc < 128 and wc > 0:
                            chars.append(chr(wc))
                        else:
                            chars.append(f'\\u{wc:04x}')
                    
                    string_value = ''.join(chars)
                    storage = "heap" if is_heap else "SSO"
                    return f'L"{string_value}" [len={length}, cap={capacity}, {storage}]'
                except Exception as e:
                    return f'L"<read error: {e}>" [len={length}, cap={capacity}]'
            
            else:
                return f'"<unsupported type: {char_type.name}>"'
        
        except gdb.error as e:
            return f'"<GDB error: {e}>"'
        except Exception as e:
            return f'"<error: {e}>"'

    def display_hint(self):
        return 'string'


def build_pretty_printer():
    """Builds the pretty printer object to register with GDB"""
    pp = gdb.printing.RegexpCollectionPrettyPrinter("Plu")
    pp.add_printer('BasicString', '^Plu::BasicString<.*>$', BasicStringPrinter)
    return pp


# Register the pretty printer
gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    build_pretty_printer(),
    replace=True
)

print("GDB pretty printer for Plu::BasicString loaded!")
