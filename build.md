# VCB Build Guide

This document contains the complete build instructions for **VCB — Vayu Compiler Backend**.

VCB is written in **C++20** and currently supports building with **CMake** or **Visual Studio**.

---

## Requirements

- C++20-compatible compiler
- CMake
- Visual Studio 2022 or another supported C++ toolchain
- Git

---

## Build with CMake

### Configure

From the VCB repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### Build

```bash
cmake --build build --config Release
```

The resulting executable will be generated inside the CMake build output directory.

---

## Build with Visual Studio

1. Open the VCB project in **Visual Studio**.
2. Make sure the project uses **C++20**.
3. Configure the project for **x64**.
4. Select the **Release** configuration.
5. Build the project.

### Recommended MSVC settings

For optimized Release builds:

```text
/std:c++20
/O2
/GL
/fp:fast
/arch:AVX2
/MT
/LTCG
/W4
```

These are recommended compiler/linker options rather than mandatory requirements.

---

## Project Structure

The main build-relevant files are:

```text
VCB/
├── include/
│   └── vcb.hpp
├── src/
│   ├── main.cpp
│   ├── parser.cpp
│   ├── analysis.cpp
│   ├── optimization.cpp
│   ├── lowering.cpp
│   ├── codegen.cpp
│   └── target.cpp
├── test/
│   └── tests.cpp
├── tests/
├── CMakeLists.txt
├── build.md
└── LICENSE
```

---

## Running VCB

After building, VCB can compile an IR file:

```bash
vcb input.vcb -o out.s
```

Or read IR from standard input:

```bash
vcb < input.vcb > out.s
```

For help:

```bash
vcb --help
```

The generated output is GNU-as-compatible assembly using Intel syntax.

---

## Building Generated Assembly

For example, with GCC:

```bash
vcb input.vcb -o out.s
gcc out.s -o out
```

The exact command may vary depending on the target platform and toolchain.

---

## Testing

VCB includes IR test programs covering arithmetic, bitwise operations, branches, function calls, comparisons, conversions, loops, and smoke tests.

The test files are located in:

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

---

## Troubleshooting

### CMake cannot find a compiler

Make sure a C++20-compatible compiler is installed and available to CMake.

### Build configuration issues

Try configuring from a clean build directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Then rebuild:

```bash
cmake --build build --config Release
```

### Visual Studio configuration

Make sure the active configuration is:

```text
Release | x64
```

and that the project is configured for C++20.

---

## License

VCB is released under the **Apache License 2.0**.

See the repository license:

[LICENSE](https://github.com/NotY215/VCB/blob/master/LICENSE "LICENSE")

---

**VCB — Building the native backend for Vayu.**
