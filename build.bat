@echo off
echo Building Augustus...

if exist build rmdir /s /q build
mkdir build
cd build

cmake -G "Visual Studio 17 2022" -A x64 -DAV1_VIDEO_SUPPORT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

if %errorlevel% neq 0 (
    echo CMake failed
    pause
    exit /b 1
)

cmake --build . --config RelWithDebInfo

if %errorlevel% neq 0 (
    echo Build failed
    pause
    exit /b 1
)

echo Build successful. Executable copied to G:\games\Caesar 3\