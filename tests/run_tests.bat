@echo off
setlocal
set "VCB_EXE=%~dp0..\out\build\x64-Debug\vcb.exe"
set "BUILDT=%~dp0..\out\build\x64-Debug\tests"
set "GCC=gcc"

if not exist "%VCB_EXE%" (
    echo [error] vcb.exe not found at %VCB_EXE%
    exit /b 1
)
if not exist "%BUILDT%" mkdir "%BUILDT%"

echo VCB test suite
echo ==============

for %%F in ("%~dp0*.vcb") do call :run_one "%%~fF"

echo.
echo Done.
exit /b 0

:run_one
setlocal
set "SRC=%~1"
set "NAME=%~n1"
echo.
echo --- %NAME% ---
"%VCB_EXE%" "%SRC%" -o "%BUILDT%\%NAME%.s" || (echo   [FAIL] vcb & exit /b 0)
%GCC% -no-pie "%BUILDT%\%NAME%.s" -o "%BUILDT%\%NAME%.exe" || (echo   [FAIL] gcc & exit /b 0)
"%BUILDT%\%NAME%.exe"
echo   exit code: %errorlevel%
exit /b 0