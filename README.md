<div align="center">

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="150">
</a>

# VCB
### Vayu Compiler Backend

<a href="https://github.com/NotY215/Vayu">
  <img src="https://raw.githubusercontent.com/NotY215/Vayu/master/assets/logo.svg" alt="Vayu Logo" width="90">
</a>
&nbsp;&nbsp;&nbsp;
<a href="https://github.com/NotY215/VCB">
  <img src="https://raw.githubusercontent.com/NotY215/VCB/master/Assets/VCB_logo.png" alt="VCB Logo" width="150">
</a>

**A compact, fast, self-contained compiler backend for VCB IL and Vayu.**

<a href="https://github.com/NotY215/Vayu"><img src="https://img.shields.io/badge/Vayu-Language-111827?style=for-the-badge" alt="Vayu"></a>
<a href="https://github.com/NotY215/VCB"><img src="https://img.shields.io/badge/VCB-Compiler%20Backend-111827?style=for-the-badge" alt="VCB"></a>
<a href="https://github.com/NotY215/VCB/blob/master/LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-111827?style=for-the-badge" alt="Apache 2.0"></a>

</div>

---

# VCB — Vayu Compiler Backend

## What it is

VCB is a **pure compiler backend written in C++20**. It consumes an SSA-based intermediate language called **VCB IL** and emits **x86-64 assembly in Intel syntax**, targeting the **System V AMD64 ABI**. It has no frontend, no language runtime, and no third-party compiler-framework dependency.

VCB is a component, not a complete compiler suite. A frontend emits VCB IL; VCB handles parsing, analysis, optimization, lowering, register allocation, instruction selection, and assembly emission. Structurally, it occupies the same niche as QBE: the frontend owns the source language, while VCB owns the native backend.

~~~text
Frontend
   │
   ▼
VCB IL
   │
   ▼
Parse → Analyze → Optimize → Lower → Regalloc → Emit
   │
   ▼
x86-64 Intel Assembly
~~~

The architectural niche is similar to **QBE**: VCB is intended to sit underneath a language frontend rather than replace a full toolchain such as LLVM or GCC.

> **Status:** VCB is under active development. Performance numbers marked as targets are design goals, not completed VCB benchmark results.

---

## Core Goals

### 1. Compile-time performance

The intended backend pipeline is deliberately short and direct:

~~~text
parse → optimize → lower → regalloc → emit
~~~

VCB avoids a separate machine-IR pipeline, SelectionDAG-style infrastructure, and unnecessary intermediate representation rewrites. The goal is to keep the backend close to a five-stage path: parse, optimize, lower, register-allocate, emit.

The parser is designed around efficient primitives such as std::from_chars for numeric names and a custom open-addressing string interner for symbolic names. The goal is for parsing to remain a small fraction of total compilation time.

### 2. Code quality in the QBE class

VCB targets useful native performance while keeping the implementation small. This is a design target, not a claim that current VCB output already matches QBE 1.3.

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

### 3. A codebase one person can hold in their head

VCB deliberately avoids a huge compiler-framework architecture.

Design principles:

- Small source files
- Simple data structures
- Minimal abstraction layers
- No plugin architecture
- No inheritance-heavy compiler framework
- Explicit pass boundaries
- Easy tracing from IR parsing to assembly emission

The long-term target is **under 3,000 lines across seven core source files**, small enough for one developer to trace from parsing to assembly emission in an afternoon.

### 4. Zero external dependencies

VCB is designed without dependencies on:

- LLVM
- Boost
- ICU
- zlib
- External optimization frameworks

The backend is written in C++20 and is intended to rely only on the standard library plus the platform assembler/linker toolchain. VCB does not depend on LLVM, Boost, ICU, zlib, or another external compiler framework.

---

## Why VCB?

### Language designers

VCB gives a language project a clean native-code boundary:

~~~text
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
~~~

This lets the frontend and backend evolve independently.

### Compiler researchers

VCB is intended as a compact backend research and experimentation platform. A new optimization pass should fit into a focused source unit, and a new x86-64 instruction-selection rule should remain close to the code generator. The linear-scan allocator is deliberately kept small and explicit.

### JITs and query compilers

Fast compilation is useful when compilation itself contributes to application latency. VCB's small IR and short pipeline are intended for:

- Small JITs
- Embedded scripting engines
- Expression evaluators
- Database query compilers
- Experimental language runtimes

### Education

VCB keeps the important backend concepts visible:

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

VCB IL is a **textual, typed, SSA-based intermediate language** designed to be both machine-generated and human-readable.

### Types

~~~text
i8
i16
i32
i64
f32
f64
ptr
~~~

### Operations and concepts

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

~~~text
trunc
zext
sext
sitofp
fptosi
~~~

Example:

~~~text
export function i32 main() {
entry:
    %1 = add i32 5, 3
    %2 = mul i32 %1, 2
    ret i32 %2
}
~~~

The IR is intentionally explicit: instructions operate on typed values and avoid hidden compiler state.

---

## Architecture

~~~text
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
~~~

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

~~~text
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
~~~

---

## Usage

Compile VCB IL into assembly:

~~~bash
vcb input.vcb -o out.s
~~~

Read IR from standard input:

~~~bash
vcb < input.vcb > out.s
~~~

Show help:

~~~bash
vcb --help
~~~

Example toolchain flow:

~~~bash
vcb input.vcb -o out.s
gcc out.s -o out
~~~

For complete build and installation instructions, see **[build.md](build.md)**.

---

## Performance Targets and Honest Comparison

VCB's comparisons should be interpreted as architectural targets and references, not as claims that VCB has already reproduced another project's benchmark results.

### Compilation time

| Backend | Reference / target |
|---|---|
| LLVM -O0 | Baseline used in TPDE comparisons |
| QBE 1.3 | Reported substantially faster than LLVM -O0 on selected workloads |
| TPDE | Published results report roughly 8–26× LLVM -O0 compilation speed in its SPECint 2017 comparison |
| VCB | **Target:** competitive with or faster than TPDE for direct VCB IL → assembly workloads |

A major intended advantage is that **VCB IL is VCB's native compilation format**. A frontend targeting VCB does not need to translate LLVM IR into a separate backend representation first.

This is an architectural hypothesis, not proof of a benchmark result. Fair comparisons require identical hardware, inputs, output requirements, optimization settings, and measurement methodology.

### Generated-code quality

| Compiler/backend | Reference point |
|---|---|
| GCC -O2 | Reference for optimized native code |
| LLVM | Broad optimization pipeline and mature target support |
| QBE | Lightweight backend focused on useful code quality |
| TPDE | Fast compilation with runtime performance reported as comparable to LLVM -O0 |
| VCB | **Target:** QBE-class generated code as optimization maturity increases |

VCB should not currently be described as matching QBE 1.3, LLVM, or GCC in generated-code performance without reproducible benchmarks.

### Why these numbers must not be treated as a ranking

Compiler performance depends on:

- CPU architecture
- Benchmark suite
- Optimization level
- IR translation cost
- Assembler/linker time
- Register allocation
- Target ISA features
- Code-size requirements

These projects use different hardware, workloads, optimization settings, IRs, and measurement methods. The figures are therefore reference points rather than a universal ranking. VCB will publish its own reproducible compile-time and generated-code benchmarks before making strong performance claims.

---

## VCB vs LLVM, GCC, QBE and TPDE

| Dimension | LLVM | GCC | QBE | TPDE | VCB |
|---|---|---|---|---|---|
| Role | Compiler framework/toolchain | Compiler/toolchain | Backend | Fast compiler backend | Fast compiler backend |
| Native code generation | Yes | Yes | Yes | Yes | Yes |
| x86-64 | Yes | Yes | Yes | Yes | Yes |
| Optimization scope | Very broad | Very broad | Compact | Fast-compilation focused | Compact / fast focused |
| IR | LLVM IR | GIMPLE/RTL | QBE IL | CLIF/TPDE pipeline | VCB IL |
| Design target | Maximum capability | Maximum capability | Small backend | Very fast compilation | Small, fast backend |
| Vayu integration | Possible | Possible | Possible | Possible | Primary target |

### VCB vs TPDE

VCB is designed for very fast IR-to-assembly compilation. VCB IL is the backend's native input format, so the intended VCB path has no separate LLVM-IR-to-backend translation stage.

TPDE's published work shows that IR translation can be a meaningful part of compilation latency in domain-specific pipelines. VCB's design removes that boundary by making VCB IL the compilation format itself.

That does **not** prove VCB is faster. A meaningful comparison needs the same hardware, equivalent programs, equivalent output requirements, and reproducible measurements.

### VCB vs LLVM

VCB is intentionally much smaller in scope. LLVM provides extensive optimization research, vectorization, LTO, debug information, sanitizers, many targets, and a large ecosystem. VCB does not attempt to reproduce those capabilities.

### VCB vs QBE

VCB occupies a similar architectural niche: a frontend emits a compact IR and the backend emits native code.

The intended differences are:

- C++20 implementation
- SSA-based VCB IL
- Explicit typed IR
- Short backend pipeline
- Custom parser/interner design
- Vayu-first integration
- Extensible backend architecture

VCB's QBE 1.3-class runtime-performance target remains a target until backed by benchmarks. In particular, the current backend should not be described as already matching QBE 1.3.

---

## What VCB Is Not

### Not an LLVM replacement

VCB does not attempt to provide:

- LLVM-scale whole-program optimization
- Automatic vectorization
- Auto-parallelization
- LTO
- Full debug-information infrastructure
- A huge multi-target ecosystem
- LLVM-compatible IR

### Not a frontend

VCB does not parse C, C++, Rust, Python, or Vayu source code directly.

A frontend must emit VCB IL.

### Not a production compiler suite

VCB is actively evolving. Its IR, ABI behavior, target support, and backend internals may change as the project develops.

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

~~~text
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
~~~

Keeping VCB separate from the Vayu frontend allows both projects to evolve independently while sharing a clear compiler boundary.

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

Current test programs cover areas including arithmetic, bitwise operations, branches, calls, comparisons, conversions, loops, and smoke tests.

~~~text
tests/
├── arith.vcb
├── bitwise.vcb
├── branch.vcb
├── call.vcb
├── compare.vcb
├── conv.vcb
├── loop.vcb
└── smoke.vcb
~~~

---

## Building

Build, compiler, CMake, Visual Studio, testing, and troubleshooting instructions are maintained in:

**[→ build.md](build.md)**

---

## Contributing

VCB is under active development. Contributions are welcome in:

- IR design and specification
- Backend optimizations
- Register allocation
- x86-64 instruction selection
- Compiler benchmarks
- Tests
- Documentation
- Vayu frontend integration

Please keep changes consistent with the project's small, explicit architecture.

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

**VCB — QBE's simplicity. TPDE's speed focus. VCB's own small native backend.**

</div>
