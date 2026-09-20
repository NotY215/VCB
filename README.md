# VCB — Vayu Compiler Backend

<div align="center">

<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_banner.svg" alt="VCB animated banner" width="900">
</a>

<br><br>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="145">
</a>
&nbsp;&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="145">
</a>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://readme-typing-svg.demolab.com?font=JetBrains+Mono&size=22&duration=3000&pause=900&center=true&vCenter=true&width=820&lines=VCB+%E2%80%94+Vayu+Compiler+Backend;The+native+backend+for+Vayu;From+.vcbir+to+native+machine+code;No+QBE+%E2%86%92+.s+%E2%86%92+gcc+chain;Windows-first+single-binary+toolchain" alt="Animated VCB presentation">
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

VCB is the native compiler backend specifically designed for the **Vayu Programming Language**.

Its role is to transform the compiler representation produced by Vayu into native machine code through analysis, optimization, lowering, register allocation, instruction selection, and code generation.

<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="100">
</a>
&nbsp;&nbsp;&nbsp;→&nbsp;&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="135">
</a>

<br><br>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://readme-typing-svg.demolab.com?font=JetBrains+Mono&size=18&duration=2800&pause=800&center=true&vCenter=true&width=760&lines=.vyu+%E2%86%92+Vayu+Compiler+%E2%86%92+.vcbir+%E2%86%92+VCB+%E2%86%92+Native+Machine+Code;Vayu+defines+the+language.+VCB+builds+the+native+backend.;Phase+23+%E2%80%94+Single-Binary+Native+Toolchain" alt="Animated Vayu and VCB pipeline">
</a>

</div>

### The compiler boundary

~~~text
Vayu Source (.vyu)
        │
        ▼
Vayu Compiler / Frontend
        │
        ▼
      .vcbir
        │
        ▼
        VCB
        │
        ▼
Native Machine Code
~~~

### Vayu Source — .vyu

Vayu programs are written using the **.vyu** extension.

Examples:

~~~text
main.vyu
game.vyu
math.vyu
~~~

.vyu represents actual **Vayu source code**. It is not an intermediate representation and is not intended to follow the syntax of VCB, QBE, LLVM, or another compiler backend.

### VCBIR — .vcbir

Before Vayu code reaches VCB, it can be represented using VCB's internal intermediate representation:

~~~text
.vcbir
~~~

VCBIR provides the interface between the Vayu compiler and the VCB backend. It contains the compiler-level information required for backend analysis, optimization, lowering, register allocation, instruction selection, and native code generation.

VCBIR is an **internal compiler representation**, not another programming language.

### VCB

VCB is written in **C++20** and is designed specifically around Vayu's requirements.

Its backend responsibilities include:

- Compiler analysis
- SSA-based processing
- Optimization
- Control-flow handling
- Lowering
- Register allocation
- Instruction selection
- Peephole optimization
- Target-specific processing
- Native code generation

VCB takes inspiration from compact backend architectures such as QBE, but **Vayu remains its language target**. VCB is not intended to be a generic compiler backend.

### The Vayu Compiler Stack

~~~text
┌──────────────────────────────┐
│        Vayu Program          │
│           main.vyu           │
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│       Vayu Compiler          │
│          / Frontend          │
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│            VCBIR             │
│           .vcbir             │
│      Internal Backend IR     │
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│             VCB              │
│     Vayu Compiler Backend    │
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│     Native Machine Code      │
└──────────────────────────────┘
~~~

> **Vayu defines the language. VCB turns Vayu into efficient native code.**

---

## Phase 23 — Single-Binary Native Toolchain

Phase 23 moves Vayu from an external backend chain toward a **single-binary native toolchain**.

### Previous direction

~~~text
Vayu → QBE → .s → gcc → executable
~~~

### Phase 23 direction

~~~text
Vayu Source (.vyu)
        │
        ▼
   Vayu Compiler
        │
        ▼
     .vcbir
        │
        ▼
       VCB
        │
        ▼
Native Machine Code
        │
        ▼
   Vayu Executable
~~~

The target is to retire the **QBE → .s → gcc** backend chain. **vayuc** will link no external QBE or GCC backend and will use VCB for native code generation.

The rollout is **Windows-first**, followed by **ELF** and **Mach-O** targets.

> **Phase 23 status: future — VCB in development.**

---

## Design Goals

### 1. Very Fast Compilation

VCB is designed around a deliberately short backend pipeline:

```text
parse → optimize → lower → regalloc → emit
```

The design intentionally avoids a large machine-IR pipeline, SelectionDAG-style infrastructure, and unnecessary intermediate representation rewrites.

The parser is designed around efficient primitives such as:

- `std::from_chars` for numeric names
- A custom open-addressing string interner for symbolic names
- Compact `uint32_t`-based value identifiers

The goal is for parsing and representation overhead to remain small relative to the actual backend work.

### 2. QBE-Class Generated Code

VCB aims for useful native performance while keeping compilation fast and the implementation compact.

Current optimization work includes:

- Constant folding
- Dead-code elimination
- Common-subexpression elimination
- Algebraic simplification
- Peephole optimization

Planned optimization work includes:

- Global value numbering (GVN)
- Global code motion (GCM)
- Loop optimizations
- If-elimination
- Stronger register allocation

**Important:** VCB should not currently be described as matching QBE 1.3 in generated-code performance until reproducible benchmarks demonstrate that result.

### 3. A Backend One Developer Can Understand

VCB deliberately favors a small, explicit architecture over a large compiler framework.

The design target is:

- Under **3,000 lines** of core backend code
- A small number of focused source files
- Simple data structures
- Minimal abstraction layers
- No plugin architecture
- No inheritance-heavy compiler framework
- Explicit pass boundaries
- Direct tracing from IR parsing to assembly emission

The goal is that a developer can understand the complete value flow without navigating millions of lines of infrastructure.

### 4. Zero Third-Party Compiler Dependencies

VCB is designed without dependencies on:

- LLVM
- GCC compiler libraries
- Boost
- ICU
- zlib
- External optimization frameworks

The backend itself is intended to rely on the C++ standard library and the platform assembler/linker toolchain.

---

## Why Use VCB?

### For Vayu / Backend Development

For Vayu, VCB provides a clean native-code boundary:

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
Native Machine Code
```

The Vayu frontend and VCB backend can evolve independently while sharing the `.vcbir` compiler boundary. This is the same general architectural model that makes compact backends such as QBE useful to language projects.

### For Compiler Researchers

VCB is intended to be a compact backend experimentation platform.

Backend work can focus directly on:

- SSA and CFG analysis
- Optimization passes
- Phi resolution
- Lowering
- Register allocation
- Instruction selection
- Assembly emission

The small architecture is intended to make experimentation easier without requiring a large compiler-framework contribution.

### For JITs and Query Compilers

Compilation time matters when compilation is part of application latency.

VCB's short pipeline and compact IR are intended to make it suitable for experimentation with:

- Small JITs
- Embedded scripting engines
- Expression evaluators
- Database query compilers
- Dynamic language runtimes

### For Education

VCB keeps the major backend concepts visible instead of hiding them behind a large framework:

- IR parsing
- SSA values
- CFG analysis
- Optimization
- Phi handling
- Lowering
- Register allocation
- Instruction selection
- Assembly emission

---

## VCBIR

VCBIR is a **typed, SSA-based internal intermediate representation** used by VCB. It is an implementation format, not another programming language.

### Types

```text
i8
i16
i32
i64
f32
f64
ptr
```

### Core Concepts

VCBIR is designed to support:

- SSA values
- Basic blocks
- Phi nodes
- Calls
- Branches
- Loads and stores
- Stack allocation
- Integer and floating-point operations
- Explicit conversions

Conversions include:

```text
trunc
zext
sext
sitofp
fptosi
```

Example:

```text
export function i32 main() {
entry:
    %1 = add i32 5, 3
    %2 = mul i32 %1, 2
    ret i32 %2
}
```

The IR is intentionally explicit: operations work on typed values and avoid hidden compiler state.

---

## Architecture

```text
                    Vayu Frontend
                             │
                             ▼
                          VCB IL
                             │
                             ▼
                    ┌─────────────────┐
                    │   VCB Backend   │
                    └─────────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          ▼                  ▼                  ▼
       Analysis         Optimization         Lowering
          │                  │                  │
          └──────────────────┼──────────────────┘
                             ▼
                    Register Allocation
                             │
                             ▼
                    Instruction Selection
                             │
                             ▼
                       Code Emission
                             │
                             ▼
                    x86-64 Assembly
```

| Stage | Responsibility |
|---|---|
| Parsing | Parse textual VCB IL |
| Analysis | CFG, SSA and data-flow information |
| Optimization | Simplify and improve IR |
| Lowering | Convert IR into target-oriented operations |
| Register allocation | Assign values to registers and stack locations |
| Instruction selection | Select x86-64 instructions |
| Emission | Produce Intel-syntax assembly |

---

## Source Layout

```text
VCB/
├── include/
│   └── vcb.hpp
├── src/
│   ├── main.cpp          # CLI and pipeline orchestration
│   ├── parser.cpp        # VCB IL parsing
│   ├── analysis.cpp      # CFG and compiler analysis
│   ├── optimization.cpp  # IR optimization
│   ├── lowering.cpp      # Lowering and peephole optimization
│   ├── codegen.cpp       # x86-64 code generation
│   └── target.cpp        # Target information
├── test/
│   └── tests.cpp
├── tests/
│   ├── arith.vcb
│   ├── bitwise.vcb
│   ├── branch.vcb
│   ├── call.vcb
│   ├── compare.vcb
│   ├── conv.vcb
│   ├── loop.vcb
│   └── smoke.vcb
├── CMakeLists.txt
├── build.md
└── LICENSE
```

---

## Usage

VCB's Phase 23 direction is direct native code generation from Vayu's .vcbir representation.

~~~text
.vyu → Vayu Compiler → .vcbir → VCB → Native Machine Code
~~~

The exact single-binary vayuc invocation will be documented as the native backend integration is implemented.

For current build and development instructions, see **[build.md](build.md)**.

---

# Performance and Benchmark Positioning

VCB's performance philosophy is deliberately ambitious, but comparisons must distinguish **published results from other projects** and **VCB's own targets**.

## Compile-Time Comparison

| Compiler | Reference point |
|---|---|
| LLVM -O0 | Baseline commonly used in TPDE comparisons |
| QBE 1.3 | Reported substantially faster than LLVM -O0 on selected workloads |
| TPDE | Published work reports roughly 8–26× LLVM -O0 compilation speed in its SPECint 2017 comparison |
| **VCB** | **Target:** competitive with or faster than TPDE for direct VCB IL → assembly workloads |

VCB's architectural hypothesis is that making **VCB IL the native compilation format** removes the need for an LLVM-IR-to-backend translation stage.

That does **not** prove VCB is faster than TPDE. A fair benchmark requires identical hardware, equivalent programs, equivalent output requirements, comparable optimization settings, and reproducible measurement methodology.

### Why the IR boundary matters

A backend can spend substantial time translating one representation into another before optimization and code generation begin. VCB avoids that particular boundary for VCB IL because VCB IL is the format it directly consumes.

TPDE's published work also discusses translation costs in its broader compilation pipelines. Those measurements are useful architectural reference points, but they are not evidence of a VCB performance result.

## Generated-Code Quality

VCB's current design target is **QBE-class generated code**, not LLVM/GCC-class optimization breadth.

| Backend | Role / reference point |
|---|---|
| GCC -O2 | Mature optimized native-code reference |
| LLVM | Broad optimization pipeline and mature target support |
| QBE | Compact backend focused on useful generated code |
| TPDE | Fast compilation with published runtime-performance comparisons |
| **VCB** | **Target:** QBE-class generated code as optimization maturity increases |

VCB should not currently claim that it matches QBE 1.3, LLVM, or GCC in runtime performance without reproducible VCB benchmarks.

### Honest current assessment

The intended optimization path is:

```text
Current:
constant folding
DCE
CSE
algebraic simplification
peephole optimization
linear-scan register allocation

Next:
GVN
GCM
loop optimization
if-elimination
stronger register allocation
```

The remaining optimization work is important because QBE's later releases improved generated code through additional optimization work. VCB's goal is to reach that class of output while preserving its compact architecture.

---

## VCB vs LLVM, GCC, QBE and TPDE

| Dimension | LLVM | GCC | QBE | TPDE | VCB |
|---|---|---|---|---|---|
| Role | Compiler framework/toolchain | Compiler/toolchain | Backend | Fast compiler/backend technology | Fast compiler backend |
| Native code generation | Yes | Yes | Yes | Yes | Yes |
| x86-64 | Yes | Yes | Yes | Yes | Target |
| Optimization scope | Very broad | Very broad | Compact | Fast-compilation focused | Compact / fast focused |
| IR | LLVM IR | GIMPLE/RTL | QBE IL | CLIF/TPDE pipeline | VCB IL |
| Design focus | Maximum capability | Maximum capability | Small backend | Fast compilation | Small, fast backend |
| Vayu integration | Possible | Possible | Possible | Possible | Primary target |

### VCB vs TPDE

VCB is designed for fast **IR-to-assembly** compilation.

The key architectural difference is the input boundary: VCB IL is VCB's native compilation format, so a VCB frontend does not need to translate LLVM IR into another backend representation before VCB starts compiling it.

This is an architectural advantage VCB is designed to exploit, not a measured performance claim.

**Target:** VCB aims to be competitive with or faster than TPDE on direct VCB IL → assembly workloads.

### VCB vs LLVM

VCB intentionally has a much smaller scope.

LLVM provides a large ecosystem covering optimization research, vectorization, LTO, debug information, sanitizers, multiple architectures, language integrations, and many production-oriented features.

VCB does not attempt to reproduce that breadth. Its purpose is a compact backend with a short compilation path.

### VCB vs QBE

VCB occupies a similar architectural niche:

```text
Frontend → compact textual IR → backend → native code
```

The intended differences include:

- C++20 implementation
- SSA-based VCB IL
- Explicit typed IR
- Short backend pipeline
- Custom parser/interner design
- Vayu-first integration
- Extensible backend architecture

VCB's QBE-class performance goal remains a **target** until backed by reproducible benchmarks.

---

## What VCB Is Not

### Not an LLVM Replacement

VCB does not attempt to provide LLVM-scale capabilities such as:

- Broad automatic vectorization
- Auto-parallelization
- LTO
- Full debug-information infrastructure
- A huge multi-target ecosystem
- LLVM-compatible IR
- LLVM's decades of optimization infrastructure

### Not a Frontend

VCB does not parse `.vyu` source directly. The Vayu frontend is responsible for understanding Vayu source and producing `.vcbir`.

A frontend must emit VCB IL.

### Not a Complete Compiler Suite

VCB is a backend component. It is intentionally not a complete language toolchain.

### Not Yet a Production-Stability Promise

VCB is actively evolving. Its IL, ABI behavior, target support, APIs, and backend internals may change as development continues.

---

## Vayu + VCB

<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="105">
</a>
&nbsp;&nbsp;&nbsp;×&nbsp;&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="140">
</a>

<br><br>

<a href="https://github.com/NotY215/Vayu">
  <img src="https://readme-typing-svg.demolab.com?font=JetBrains+Mono&size=18&duration=2800&pause=800&center=true&vCenter=true&width=760&lines=.vyu+%E2%86%92+.vcbir+%E2%86%92+VCB+%E2%86%92+Native+Machine+Code;Vayu+defines+the+language.+VCB+builds+the+native+backend.;Single-Binary+Native+Toolchain" alt="Animated Vayu and VCB pipeline">
</a>

</div>

~~~text
Vayu Source
    │
    │ .vyu
    ▼
Vayu Compiler
    │
    ▼
.vcbir
    │
    ▼
VCB
    │
    ▼
Native Machine Code
    │
    ▼
Executable
~~~

VCB is the dedicated native backend for Vayu. Keeping the frontend and backend separate preserves a clear compiler boundary while allowing the final vayuc toolchain to become self-contained.

**[→ Vayu repository](https://github.com/NotY215/Vayu)**

---

## Roadmap

| Phase | Direction | Status |
|---:|---|---|
| 18 | Tooling — LSP, formatter, linter, VS Code extension | Future |
| 19 | GUI | Future |
| 20 | Graphics | Future |
| 21 | AI/ML | Future |
| 22 | Package registry — vayu install, public index | Future |
| **23** | **Single-binary toolchain — retire QBE + GCC backend chain; VCB emits native machine code directly. Windows-first, then ELF/Mach-O.** | **Future — VCB in development** |

### Phase 23 target

~~~text
.vyu
 │
 ▼
vayuc
 │
 ▼
.vcbir
 │
 ▼
VCB
 │
 ▼
Native Machine Code
 │
 ▼
Executable
~~~

The intended end state is a Vayu toolchain where **vayuc does not require QBE or GCC as its backend**.

---

## Testing

Current tests cover areas including:

- Arithmetic
- Bitwise operations
- Branches
- Calls
- Comparisons
- Conversions
- Loops
- Smoke tests

```text
tests/
├── arith.vcb
├── bitwise.vcb
├── branch.vcb
├── call.vcb
├── compare.vcb
├── conv.vcb
├── loop.vcb
└── smoke.vcb
```

---

## Building

Build, compiler, CMake, Visual Studio, testing, and troubleshooting instructions are maintained in:

**[→ build.md](build.md)**

---

## Contributing

VCB is under active development. Contributions are welcome in:

- VCB IL design and specification
- Backend optimizations
- SSA and CFG analysis
- Register allocation
- x86-64 instruction selection
- Compiler benchmarks
- Tests
- Documentation
- Vayu frontend integration

Please keep changes consistent with VCB's small, explicit architecture.

---

## License

VCB is released under the **Apache License 2.0**.

**[LICENSE](https://github.com/NotY215/VCB/blob/master/LICENSE)**

---

## Related Project

**Vayu Programming Language:**  
**[github.com/NotY215/Vayu](https://github.com/NotY215/Vayu)**

---

<div align="center">

### VCB

**A small native backend for language designers, compiler researchers, JITs, and people who want to understand the whole compiler path.**

</div>


---

## Visual Identity

VCB uses the following project assets:

| Asset | Purpose |
|---|---|
| `Assets/VCB_logo.png` | Primary VCB logo |
| `Assets/VCB_Fav.png` | Compact icon / favicon asset |
| `Assets/VCB_banner.svg` | Animated README banner |

<div align="center">

<img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="220">

<br><br>

<img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_Fav.png" alt="VCB Favicon" width="72">

</div>

---

<div align="center">

**Vayu → `.vcbir` → VCB → Native Code**

<sub>VCB is the backend for Vayu. `.vyu` is Vayu source. `.vcbir` is VCB's internal IR.</sub>

</div>
