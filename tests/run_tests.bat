@echo off
setlocal
set "VCB_EXE=%~dp0..\out\build\x64-Debug\vcb.exe"
set "GCC=gcc"

if not exist "%VCB_EXE%" (
    echo [error] vcb.exe not found at %VCB_EXE%
    echo         edit VCB_EXE at the top of this script to match your build dir
    exit /b 1
)

set "ROOT=%~dp0.."
set "BUILDT=%~dp0..\out\build\x64-Debug\tests"
if not exist "%BUILDT%" mkdir "%BUILDT%"

echo VCB smoke suite
echo ===============

for %%F in ("%~dp0*.vcb") do (
    call :run_one "%%~fF"
)

echo.
echo Done.
exit /b 0

:run_one
setlocal
set "SRC=%~1"
set "NAME=%~n1"
echo.
echo --- %NAME% ---
"%VCB_EXE%" "%SRC%" -o "%BUILDT%\%NAME%.s"
if errorlevel 1 (
    echo   [FAIL] vcb failed to compile %NAME%
    exit /b 0
)
%GCC% -no-pie "%BUILDT%\%NAME%.s" -o "%BUILDT%\%NAME%.exe"
if errorlevel 1 (
    echo   [FAIL] gcc failed to assemble %NAME%
    exit /b 0
)
"%BUILDT%\%NAME%.exe"
echo   exit code: %errorlevel%
exit /b 0