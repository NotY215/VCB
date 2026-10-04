# tests\elf_matrix.ps1
# Linux ELF test matrix for VCB.  Builds each .vcbir under examples\
# with --target elf and validates the header via `vcb elfheaders`.
# Execution requires a Linux host (Docker Desktop, VM, or remote).

param(
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
$root   = Split-Path -Parent $PSScriptRoot
$vcb    = Join-Path $root "build\x64-Release\bin\vcb.exe"
$outDir = Join-Path $root "_out\elf_matrix"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $vcb)) {
    Write-Host "missing $vcb -- build VCB first" -ForegroundColor Red
    exit 2
}

$tests = @(
    "t01_empty", "t02_arith", "t03_print_int", "t04_print_str",
    "t05_print_bool", "t06_list_create", "t07_list_push",
    "t08_multiple_calls", "t09_str_const", "t10_large_text",
    "t11_multi_import"
)

$pass = 0
$fail = 0
$skip = 0

foreach ($name in $tests) {
    $ir  = Join-Path $root "examples\$name.vcbir"
    $elf = Join-Path $outDir "$name.elf"

    if (-not (Test-Path $ir)) {
        Write-Host ("SKIP  {0}  (missing {1})" -f $name, $ir) -ForegroundColor Yellow
        $skip++
        continue
    }

    & $vcb build $ir -o $elf --target elf *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("FAIL  {0}  build exit={1}" -f $name, $LASTEXITCODE) -ForegroundColor Red
        $fail++
        continue
    }

    $hdr = & $vcb elfheaders $elf 2>&1 | Out-String
    if ($hdr -notmatch "ELF64" -or $hdr -notmatch "x86-64") {
        Write-Host ("FAIL  {0}  elfheaders" -f $name) -ForegroundColor Red
        Write-Host $hdr
        $fail++
        continue
    }

    Write-Host ("PASS  {0}" -f $name) -ForegroundColor Green
    $pass++
}

Write-Host ""
Write-Host ("total: {0} pass, {1} fail, {2} skip" -f $pass, $fail, $skip)
exit ($(if ($fail -gt 0) { 1 } else { 0 }))