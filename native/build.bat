@echo off
rem Build the SourceKrillion native app on Windows. Needs Visual Studio 2022 (C++ workload), CMake 3.24+ and git.
cd /d "%~dp0"
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
echo Built build\Release\SourceKrillion.exe
