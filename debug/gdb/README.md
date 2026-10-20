# GDB pretty printers for PluSTL

GDB pretty printers that show PluSTL containers and smart pointers in a readable form
instead of their raw fields (`mData`, `mSlots`, `control`, …).

## Files

| File | Types |
|---|---|
| `gdb_plustl.py` | `Plu::DynamicArray<T>`, `Plu::HashMap<K,V>`, `Plu::HashSet<T>`, `Plu::Path`/`PathW` (`BasicPath`), `Plu::TOwningPointer<T>`, `Plu::TUsePointer<T>` |
| `gdb_plustring.py` | `Plu::String`/`StringW` (`BasicString<CharT>`) — tells SSO from heap storage |

## What they show

```
DynamicArray [size=3, cap=4] = {10, 20, 30}
"short" [len=5, cap=23, SSO]
"/home/user/projects/some/file.txt" [len=33, cap=33, heap]
HashSet [size=3, cap=16, deleted=0] = {2, 1, 3}
HashMap [elements=2, buckets=8] = {[200] = "two hundred", [100] = "one hundred"}
TOwningPointer @ 0x55…e020 [strong=1, uses=1] = {pointee = {a = 7, b = 9}}
TUsePointer @ 0x55…e020 [strong=1, uses=1] = {pointee = {a = 7, b = 9}}
```

- `strong` = number of `TOwningPointer`s (strongCount).
- `uses` = number of live `TUsePointer`s (`weakCount - 1`, i.e. without the owners'
  collective reference). An expired object shows `<expired>`; an empty pointer `= nullptr`.
- Strings show `len`, `cap` and whether they sit in the SSO buffer or on the heap.

## Loading

Paths below assume PluSTL is checked out as `PluSTL/` in your project root (e.g. as a git
submodule) — adjust them to wherever it lives.

### `.gdbinit` in the project root

```
source ./PluSTL/debug/gdb/gdb_plustl.py
source ./PluSTL/debug/gdb/gdb_plustring.py
```

The paths are relative, so GDB has to start in the project directory, and GDB only reads a
project `.gdbinit` from a directory allowed by `auto-load safe-path`.

### By hand in a GDB session

```gdb
source /path/to/PluSTL/debug/gdb/gdb_plustl.py
source /path/to/PluSTL/debug/gdb/gdb_plustring.py
```

### CLion — the usual "I only see raw fields" problem

CLion runs its **own bundled GDB** (`…/clion/bin/gdb/linux/x64/bin/gdb`) and does **not**
source the project `.gdbinit`: `auto-load safe-path` blocks it, and on top of that relative
`./PluSTL/debug/gdb/…` paths do not resolve when the debugger's working directory is not the
project directory.

The fix, independent of the working directory, is an entry with absolute paths in
**`~/.gdbinit`**, which CLion's bundled GDB reads on startup:

```gdb
add-auto-load-safe-path /path/to/your/project
source /path/to/PluSTL/debug/gdb/gdb_plustl.py
source /path/to/PluSTL/debug/gdb/gdb_plustring.py
```

If you still see raw fields after that, check two CLion settings and restart the debug session:
1. **Settings → Build, Execution, Deployment → Debugger → Data Views → C/C++** →
   enable *"Enable Python renderers / GDB pretty printers"* (without it the Variables view
   ignores the printers even when they are loaded).
2. **Settings → Build, Execution, Deployment → Debugger** → the option to load `.gdbinit`
   (where your version has it) must be enabled.

To verify: open the GDB console tab in a debug session and type `print yourVariable` — at
startup you should also see the message "PluSTL pretty printers loaded: …".

### VS Code (cppdbg)

In the configuration in `launch.json`:
```json
"setupCommands": [
  { "text": "source ${workspaceFolder}/PluSTL/debug/gdb/gdb_plustl.py" },
  { "text": "source ${workspaceFolder}/PluSTL/debug/gdb/gdb_plustring.py" }
]
```

## Implementation notes

- The printers read fields by the names used in the PluSTL headers. After a container's
  layout changes (field names in `Array.h`, `HashMap.h`, `HashSet.h`, `Path.h`,
  `ControlBlock.h`), the matching printer has to be updated.
- `strongCount`/`weakCount` are `std::atomic<int>` — read through `_M_i` (libstdc++), with a
  fallback.
- Needs a build with debug symbols.
