# VCB — Vayu Compiler Backend

> **VCB (Vayu Compiler Backend)** is the native backend and code-generation layer of the [Vayu programming language](https://github.com/NotY215/Vayu), designed to turn Vayu's intermediate representation into optimized x86-64 assembly.

VCB is being developed as a **compact, fast, self-contained compiler backend** written in modern **C++20**. Its architecture is inspired by lightweight compiler backends such as QBE while remaining purpose-built for Vayu.

> 🚧 **Status:** Early development — the backend architecture, IR pipeline, optimization passes, lowering, and x86-64 code generation are actively being developed.

## Features

- C++20 implementation
- Custom VCB intermediate representation
- IR parser
- Control-flow and compiler analysis
- Optimization pipeline
- Constant folding
- Dead-code elimination (DCE)
- Common-subexpression elimination (CSE)
- IR simplification
- Lowering and peephole optimization
- x86-64 code generation
- GNU-as-compatible Intel-syntax assembly output
- Standalone command-line compiler driver
- CMake and Visual Studio build support
- Built-in IR test programs

## Architecture

VCB is organized as a compiler backend pipeline:

```text
                    Vayu Compiler
                         │
                         ▼
                 Vayu Intermediate
                    Representation
                         │
                         ▼
                    ┌─────────┐
                    │   VCB   │
                    └────┬────┘
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
       Analysis      Optimization     Lowering
          │              │              │
          └──────────────┼──────────────┘
                         ▼
                     Codegen
                         │
                         ▼
                    x86-64 ASM
                         │
                         ▼
                  Native executable
```

### Current source layout

```text
VCB/
├── include/
│   └── vcb.hpp
├── src/
│   ├── main.cpp          # CLI and pipeline orchestration
│   ├── parser.cpp        # IR parsing
│   ├── analysis.cpp      # CFG and compiler analysis
│   ├── optimization.cpp  # IR optimization passes
│   ├── lowering.cpp      # Lowering and peephole optimization
│   ├── codegen.cpp       # x86-64 assembly generation
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

## Backend Pipeline

```text
IR text
   │
   ▼
parseIR()
   │
   ▼
Analysis
   │
   ▼
Optimization
   ├── Constant Folding
   ├── Dead-Code Elimination
   ├── Common-Subexpression Elimination
   └── Simplification
   │
   ▼
Lowering
   └── Peephole Optimization
   │
   ▼
x86-64 Code Generation
   │
   ▼
Intel-syntax Assembly
```

## VCB IR

VCB currently uses a compact QBE-inspired textual IR.

Example:

```text
export function i32 main() {
entry:
    %1 = add i32 5, 3
    %2 = mul i32 %1, 2
    ret i32 %2
}
```

### Types

```text
void
i8
i16
i32
i64
f32
f64
ptr
```

### Supported operations

```text
add sub mul div rem
and or xor shl shr sar
neg not
ceq cne clt cle cgt cge
load store alloc
trunc zext sext
jmp jnz
ret call
phi copy
```

The IR is intentionally small so that it can provide a clean boundary between the Vayu frontend and the native backend.

## Usage

VCB can read IR from a file or from standard input.

### Compile an IR file

```bash
vcb input.vcb -o out.s
```

### Read from stdin

```bash
vcb < input.vcb > out.s
```

### Help

```bash
vcb --help
```

The generated output is GNU-as-compatible assembly using Intel syntax.

For example:

```bash
vcb input.vcb -o out.s
gcc out.s -o out
```

For complete installation, compiler, CMake, Visual Studio, testing, and troubleshooting instructions, see **[BUILD.md](build.md)**.

## Testing

The repository contains small IR programs covering arithmetic, bitwise operations, branches, function calls, comparisons, conversions, loops, and smoke tests.

```text
tests/
├── arith.vcb
├── bitwise.vcb
├── branch.vcb
├── call.vcb
├── compare.vcb
├── conv.vcb
├── loop.vcb
├── run_tests.bat
└── smoke.vcb
```

## Roadmap

The backend is under active development. Planned work includes:

- [ ] Expand Vayu IR support
- [ ] Improve CFG and data-flow analysis
- [ ] SSA-based optimization
- [ ] Stronger register allocation
- [ ] Improved calling-convention handling
- [ ] More complete x86-64 instruction selection
- [ ] Better floating-point support
- [ ] Global optimization passes
- [ ] More comprehensive backend tests
- [ ] Performance and code-size benchmarking
- [ ] Integration with the Vayu frontend
- [ ] Native Vayu-to-executable compilation pipeline
- [ ] Additional target architectures in the future

## Relationship with Vayu

VCB is a component of the **Vayu compiler ecosystem**.

```text
Vayu Source
     │
     ▼
Vayu Frontend
     │
     ▼
Vayu IR
     │
     ▼
VCB — Vayu Compiler Backend
     │
     ▼
Native Assembly
     │
     ▼
Executable
```

The goal is to keep the frontend and backend clearly separated so that Vayu can evolve its language features without tightly coupling them to machine-code generation.

## Project Goals

VCB is being built around a few core goals:

- **Small:** keep the backend understandable and maintainable.
- **Fast:** optimize both compilation time and generated code.
- **Native:** produce real machine-oriented output rather than relying on a virtual machine.
- **Modular:** keep parsing, analysis, optimization, lowering, and code generation separated.
- **Vayu-focused:** provide a backend specifically designed to serve the Vayu language.
- **Extensible:** make future targets and optimization passes easier to add.

## Contributing

VCB is currently under active development. Contributions, bug reports, optimization ideas, backend experiments, and compiler research are welcome.

Before contributing, please check the existing source structure and tests to keep new backend components consistent with the current architecture.

## License

VCB is released under the **Apache License 2.0**.

See the license included in the repository:

[LICENSE](https://github.com/NotY215/VCB/blob/master/LICENSE "LICENSE")

## Related Project

**Vayu Programming Language:**  
https://github.com/NotY215/Vayu

---

**VCB — Building the native backend for Vayu.**
