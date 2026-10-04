# VCB — Vayu Compiler Backend

<div align="center">

<a href="https://vayu.gt.tc/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_banner.svg" alt="VCB animated banner" width="900">
</a>

<br><br>

<a href="https://vayu.gt.tc">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="125">
</a>
&nbsp;&nbsp;&nbsp;
<a href="https://vayu.gt.tc/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="125">
</a>

<br><br>

**[Vayu](https://vayu.gt.tc) · [VCB Website](https://vayu.gt.tc/VCB) · [GitHub](https://github.com/NotY215/VCB)**

</div>

---

## Identity

> **VCB is the backend for Vayu.**  
> **`.vyu⟧ is Vayu source.**  
> **`.vcbir⟧ is VCB's internal IR.**

VCB is a standalone **C++20** native compiler backend for the Vayu programming language.

The compiler boundary is:

```text
Vayu Source (.vyu)
        │
        ▼
Vayu Frontend
        │
        ▼
VCBIR (.vcbir)
        │
        ▼
VCB
        │
        ▼
Native Machine Code / Executable
```

VCB does not parse `.vyu⟧ directly. The Vayu compiler produces VCBIR and VCB consumes it.

---

## Current Status

**VCB 0.5.3 — Phase 27 Parts 1–14 complete; Part 15 current; Parts 16–17 next**

The current backend provides:

- VCBIR parsing and pretty-printing
- SSA-style IR with typed values and basic blocks
- x86-64 native code generation
- PE executable generation
- ELF executable generation
- Windows runtime imports and native runtime helpers
- Linux syscall-based runtime support
- Linux `brk⟧-based bump allocation
- Linux list and map runtime support
- string and collection printing support
- demand-driven runtime emission
- separate `.text⟧ and `.rdata⟧ handling
- atomic PE and ELF output
- PE and ELF header inspection
- output-directory creation by the driver
- PE image padding and related diagnostics

The latest implementation is the Phase 27 backend through the x64 helper consolidation and PE unwind metadata work. Part 15 currently emits `.pdata` / `.xdata` for user functions and the entry stub. Runtime helper unwind coverage and the Linux ELF validation matrix are not yet complete.

---

## Phase 27 Roadmap

Phase 27 tracks native-backend correctness, runtime coverage, executable-format support, and cross-target validation.

| Part | Status | Scope |
|---|---|---|
| 1 | Done | ELF writer and Linux syscall runtime subset |
| 2 | Done | Linux `brk` heap, list/map runtime, string methods |
| 3 | Done | PE padding heuristic |
| 4 | Done | `DYNAMIC_BASE` handling, dynamic sections, shadow space |
| 5 | Done | Minimal `.reloc` support |
| 6 | Done | ASLR re-enabled; `.pdata` / `.xdata` for user functions and entry stub |
| 7 | Done | kernel32 heap APIs |
| 8 | Done | Linux `vayu_print_float` |
| 9 | Done | `.rdata` emission, padding, `RELOCS_STRIPPED` and ASLR handling |
| 10 | Done | `vcb sign`, `vcb verify`, `vcb build --sign` |
| 11 | Partial | WDAC supplemental policy generation; full policy acceptance depends on Microsoft-trusted signing or a machine without the inbox policy |
| 12 | Done | Import-table construction remains in `X64.cpp`; runtime `.pdata` deferred to Part 15 |
| 13 | Done | `X64Common.hpp` extracted; `X64.cpp` reduced to a thin wrapper |
| 14 | Done | PE test matrix covering 11 programs; `t10` fixed |
| 15 | Working | `.pdata` / `.xdata` for user functions and the entry stub; runtime helper unwind coverage remains |
| 16 | Next | Add `tests\\elf_matrix.ps1` for repeatable Linux ELF validation |
| 17 | Planned | Move import-table construction into `writePe`, remove `kIdataRva` hardcode, add Linux `.eh_frame` |

---
## Why I Am Making VCB

VCB exists to give Vayu its own native backend instead of depending on a large external compiler backend.

| Goal | Purpose |
|---|---|
| **Vayu-first** | Designed around Vayu's compiler requirements |
| **Fast** | Keep the native compilation path compact |
| **Compact** | Make backend development easier to understand |
| **Native** | Generate native output directly from VCBIR |
| **Independent** | Keep frontend and backend development separate |
| **Experimental** | Make compiler/backend research easier to iterate |

QBE influenced the original compact-backend direction, but **Vayu is VCB's language target**.

---

## VCBIR

VCBIR is VCB's internal typed intermediate representation. It is not another programming language.

Current IR concepts include:

- typed SSA values
- basic blocks
- `phi⟧
- branches and jumps
- calls and returns
- stack allocation
- loads and stores
- integer and floating-point operations
- comparisons and bitwise operations
- conversions
- string constants

Example:

```text
func add(a: i64, b: i64) -> i64 {
entry:
  i64 %t0 = add a, b
  ret %t0
}
```

---

## Native Targets

VCB currently targets **x86-64** and can emit:

| Target | Output |
|---|---|
| `pe⟧ | Windows PE executable |
| `elf⟧ | Linux x86-64 ELF executable |

The target is selected with:

```text
--target pe
--target elf
```

The backend writes the native image itself rather than requiring an external assembly-to-executable backend for these paths.

---

## Commands

```text
vcb version
vcb dump <file.vcbir>
vcb build <file.vcbir> -o <out> [--target pe|elf]
vcb headers <file.exe>
vcb elfheaders <file.elf>
```

Examples:

```text
vcb dump examples/hello.vcbir

vcb build examples/hello.vcbir -o hello.exe --target pe

vcb build examples/hello.vcbir -o hello --target elf

vcb headers hello.exe

vcb elfheaders hello
```

---

## Examples

Current VCBIR examples include:

```text
examples/
├── hello.vcbir
├── list_test.vcbir
├── loop.vcbir
└── print.vcbir
```

These examples exercise native functions, loops, runtime printing, and collection/runtime paths.

---

## Build

VCB uses **CMake 3.20+** and **C++20**.

```text
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is produced under:

```text
build/bin/vcb.exe
```

---

## Vayu Integration

VCB is the native backend used by Vayu's native compilation path.

```text
.vyu
 │
 ▼
Vayu Frontend
 │
 ▼
VCBIR
 │
 ▼
VCB
 ├── x86-64 Code Generation
 ├── Runtime Emission
 ├── PE Writer
 └── ELF Writer
 │
 ▼
Native Executable
```

The old **QBE → assembly → GCC** backend chain is no longer the native backend path for Vayu.

**[→ Vayu Compiler](https://github.com/NotY215/Vayu)**

**[→ VCB Website](https://vayu.gt.tc/VCB)**

---

## What VCB Is Not

- **Not a frontend** — Vayu handles `.vyu⟧ source.
- **Not a generic backend** — VCB is built for Vayu.
- **Not LLVM/GCC** — its scope is intentionally much smaller.
- **Not another language** — VCBIR is an internal compiler representation.

---

## License

VCB is released under the **Apache License 2.0**.

**[LICENSE](LICENSE)**

<div align="center">

### VCB

**A compact native backend for Vayu.**

**[vayu.gt.tc/VCB](https://vayu.gt.tc/VCB)**

</div>
