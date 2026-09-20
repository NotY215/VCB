# VCB — Vayu Compiler Backend

<div align="center">

<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_banner.svg" alt="VCB animated banner" width="900">
</a>

<br><br>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="135">
</a>
&nbsp;&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="135">
</a>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://readme-typing-svg.demolab.com?font=JetBrains+Mono&size=22&duration=3000&pause=900&center=true&vCenter=true&width=820&lines=VCB+%E2%80%94+Vayu+Compiler+Backend;The+native+backend+for+Vayu;From+.vcbir+to+native+machine+code;Vayu-first+native+compilation" alt="Animated VCB presentation">
</a>

### Vayu Compiler Backend

**A compact, fast, Vayu-first C++20 backend for native machine code generation.**

<a href="https://github.com/NotY215/Vayu"><img src="https://img.shields.io/badge/Vayu-Language-111827?style=for-the-badge" alt="Vayu"></a>
<a href="https://github.com/NotY215/VCB"><img src="https://img.shields.io/badge/VCB-Compiler%20Backend-111827?style=for-the-badge" alt="VCB"></a>
<a href="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge" alt="C++20"></a>
<a href="https://github.com/NotY215/VCB/blob/master/LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-111827?style=for-the-badge" alt="Apache 2.0"></a>

</div>

---

## Identity

> **VCB is the backend for Vayu.**  
> **`.vyu` is Vayu source.**  
> **`.vcbir` is VCB's internal IR.**

VCB transforms the compiler representation produced by Vayu into native machine code.

<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="90">
</a>
&nbsp;&nbsp;→&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="120">
</a>

<br><br>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://readme-typing-svg.demolab.com?font=JetBrains+Mono&size=18&duration=2800&pause=800&center=true&vCenter=true&width=760&lines=.vyu+%E2%86%92+Vayu+Compiler+%E2%86%92+.vcbir+%E2%86%92+VCB+%E2%86%92+Native+Machine+Code;Vayu+defines+the+language.+VCB+builds+the+native+backend." alt="Animated Vayu and VCB pipeline">
</a>

</div>

### Compiler Boundary

```text
Vayu Source (.vyu)
        │
        ▼
Vayu Compiler / Frontend
        │
        ▼
VCBIR (.vcbir)
        │
        ▼
VCB
        │
        ▼
Native Machine Code
```

VCB does **not** parse `.vyu` directly. The Vayu frontend produces `.vcbir`, which VCB consumes.

### VCBIR

VCBIR is VCB's typed, SSA-based internal intermediate representation.

It provides the boundary between the Vayu compiler and backend and represents:

- SSA values and basic blocks
- Phi nodes
- Calls and branches
- Loads and stores
- Integer and floating-point operations
- Explicit conversions

VCBIR is an **internal compiler representation**, not another programming language.

### Backend

VCB is written in **C++20** and contains the backend pipeline for:

- Analysis
- Optimization
- Lowering
- Register allocation
- Instruction selection
- Peephole optimization
- Native code generation

VCB takes inspiration from compact backend architectures such as QBE, while **Vayu remains its language target**.

---

## Why I Am Making VCB

VCB exists to give Vayu its own compact native backend instead of depending on a large external compiler framework.

| Goal | Reason |
|---|---|
| **Vayu-first** | Built around Vayu's own compiler requirements |
| **Fast** | Keep the compilation path short and lightweight |
| **Compact** | Make the backend understandable and maintainable |
| **Native** | Generate machine code directly from VCBIR |
| **Independent** | Avoid making Vayu depend on another backend |
| **Experimental** | Keep backend development easy to change and test |

---

## VCBIR Example

```text
export function i32 main() {
entry:
    %1 = add i32 5, 3
    %2 = mul i32 %1, 2
    ret i32 %2
}
```

---

## Testing

Tests are kept under `tests/` and cover the current backend source:

```text
tests/
├── arith.vcb
├── bitwise.vcb
├── branch.vcb
├── call.vcb
├── coalesce.vcb
├── compare.vcb
├── conv.vcb
├── dense.vcb
├── loop.vcb
├── peephole.vcb
├── smoke.vcb
└── symbolic.vcb
```

The test suite covers arithmetic, bitwise operations, branches, calls, comparisons, conversions, loops, register coalescing, dense control flow, peephole optimization, symbolic handling, and smoke tests.

---

## Building

Build, CMake, compiler, Visual Studio, testing, and troubleshooting instructions are maintained in:

**[→ build.md](build.md)**

---

## Vayu + VCB

<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="100">
</a>
&nbsp;&nbsp;×&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="125">
</a>

<br><br>

```text
.vyu → Vayu Compiler → .vcbir → VCB → Native Machine Code
```

**[→ Vayu repository](https://github.com/NotY215/Vayu)**

</div>

---

## What VCB Is Not

- **Not a frontend** — Vayu handles `.vyu` parsing.
- **Not a generic backend** — VCB is built for Vayu.
- **Not LLVM/GCC** — it intentionally has a much smaller scope.
- **Not another language** — VCBIR is an internal compiler representation.

---

## Contributing

Contributions are welcome in:

- VCBIR
- Backend optimization
- Analysis and SSA
- Register allocation
- Instruction selection
- x86-64 code generation
- Tests
- Vayu integration

---

## License

VCB is released under the **Apache License 2.0**.

**[LICENSE](https://github.com/NotY215/VCB/blob/master/LICENSE)**

---

<div align="center">

### VCB

**A compact native backend for Vayu.**

</div>
