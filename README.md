# VCB — Vayu Compiler Backend

<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="150">
</a>

<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="150">
</a>

### Vayu Compiler Backend

**A small, fast, self-contained native compiler backend built around VCB IL.**

<a href="https://github.com/NotY215/Vayu"><img src="https://img.shields.io/badge/Vayu-Language-111827?style=for-the-badge" alt="Vayu"></a>
<a href="https://github.com/NotY215/VCB"><img src="https://img.shields.io/badge/VCB-Compiler%20Backend-111827?style=for-the-badge" alt="VCB"></a>
<a href="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge" alt="C++20"></a>
<a href="https://github.com/NotY215/VCB/blob/master/LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-111827?style=for-the-badge" alt="Apache 2.0"></a>

</div>

---

## What It Is

VCB is a **pure compiler backend written in C++20**. It consumes an SSA-based intermediate language — **VCB IL** — and is designed to emit **x86-64 assembly in Intel syntax**, targeting the **System V AMD64 ABI**.

VCB has **no frontend and no language runtime**. A frontend produces textual VCB IL; VCB takes over from there:

```text
Frontend
   │
   ▼
VCB IL
   │
   ▼
Parse → Optimize → Lower → Regalloc → Emit
   │
   ▼
x86-64 Intel Assembly
```

Structurally, VCB occupies the same kind of niche as **QBE**: it is a backend component rather than a complete compiler suite like LLVM or GCC.

The intended boundary is simple:

- **Frontend:** understands the source language.
- **VCB IL:** defines the compiler/backend boundary.
- **VCB:** parses, analyzes, optimizes, lowers, allocates registers, selects instructions, and emits assembly.
- **System toolchain:** assembles and links the generated assembly into an executable.

> **Status:** VCB is under active development. Performance figures described as targets are design goals, not completed VCB benchmark results.

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

### For Language Designers

If you are building a programming language, VCB provides a clean native-code boundary:

```text
Your Language
     │
     ▼
Your Frontend
     │
     ▼
VCB IL
     │
     ▼
VCB
     │
     ▼
Native Assembly
```

The frontend and backend can evolve independently. This is the same general architectural model that makes compact backends such as QBE useful to language projects.

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

## VCB IL

VCB IL is a **textual, typed, SSA-based intermediate language** intended to be both machine-generated and human-readable.

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

VCB IL is designed to support:

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
                    Vayu / Other Frontend
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

Compile VCB IL into assembly:

```bash
vcb input.vcb -o out.s
```

Read IR from standard input:

```bash
vcb < input.vcb > out.s
```

Show help:

```bash
vcb --help
```

Example toolchain flow:

```bash
vcb input.vcb -o out.s
gcc out.s -o out
```

For build and installation instructions, see **[build.md](build.md)**.

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

VCB does not parse C, C++, Rust, Python, or Vayu source code directly.

A frontend must emit VCB IL.

### Not a Complete Compiler Suite

VCB is a backend component. It is intentionally not a complete language toolchain.

### Not Yet a Production-Stability Promise

VCB is actively evolving. Its IL, ABI behavior, target support, APIs, and backend internals may change as development continues.

---

## Vayu + VCB

VCB is being developed alongside **Vayu** as its dedicated native backend.

<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="100">
</a>

&nbsp;&nbsp;→&nbsp;&nbsp;

<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="140">
</a>

</div>

Intended compiler flow:

```text
Vayu Source
     │
     ▼
Vayu Frontend
     │
     ▼
Vayu IR / Lowered Representation
     │
     ▼
VCB IL
     │
     ▼
VCB — Analysis / Optimization / Lowering / Regalloc / Codegen
     │
     ▼
x86-64 Assembly
     │
     ▼
Executable
```

Keeping VCB separate from the Vayu frontend allows both projects to evolve independently while maintaining a clear compiler boundary.

**[→ Vayu repository](https://github.com/NotY215/Vayu)**

---

## Roadmap

- [ ] Finalize and document the VCB IL specification
- [ ] Expand SSA and CFG analysis
- [ ] Strengthen constant propagation and folding
- [ ] Global value numbering (GVN)
- [ ] Global code motion (GCM)
- [ ] Loop optimizations
- [ ] If-elimination
- [ ] Improve register allocation
- [ ] Improve phi resolution and lowering
- [ ] Expand x86-64 instruction selection
- [ ] Improve floating-point code generation
- [ ] Expand calling-convention support
- [ ] Add reproducible compile-time benchmarks
- [ ] Add generated-code benchmarks
- [ ] Integrate VCB with the Vayu frontend
- [ ] Complete Vayu native compilation through VCB
- [ ] Evaluate additional targets in the future

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
