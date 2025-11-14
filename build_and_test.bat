@echo off
REM Snow Deformation - Build and Test Script
REM Windows batch file for building and testing the snow deformation system

echo ============================================
echo OpenMW Snow Deformation Build Script
echo ============================================
echo.

REM Check if we're in the correct directory
if not exist "CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found!
    echo Please run this script from the openmw-snow root directory.
    pause
    exit /b 1
)

echo Step 1: Creating build directory...
if not exist "build" (
    mkdir build
    echo Build directory created.
) else (
    echo Build directory already exists.
)
echo.

echo Step 2: Running CMake configuration...
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)
echo CMake configuration complete.
echo.

echo Step 3: Building OpenMW...
echo This may take several minutes...
cmake --build . --config Release --target openmw -j 8
if errorlevel 1 (
    echo ERROR: Build failed!
    echo.
    echo Check the error messages above.
    echo Common issues:
    echo - Missing dependencies
    echo - Shader API compatibility issues
    echo - See IMPLEMENTATION_NOTES.md for solutions
    pause
    exit /b 1
)
echo.
echo ============================================
echo Build successful!
echo ============================================
echo.

echo Step 4: Verifying files...
echo.
echo Checking for snowdeformation object files...
if exist "apps\openmw\CMakeFiles\openmw.dir\mwrender\snowdeformation.cpp.obj" (
    echo [OK] snowdeformation.cpp compiled
) else (
    echo [WARNING] snowdeformation.cpp.obj not found
)
echo.

echo Checking shader files...
cd ..
if exist "files\shaders\snow_deformation.vert" (
    echo [OK] snow_deformation.vert
) else (
    echo [ERROR] snow_deformation.vert missing!
)

if exist "files\shaders\snow_deformation.frag" (
    echo [OK] snow_deformation.frag
) else (
    echo [ERROR] snow_deformation.frag missing!
)

if exist "files\shaders\snow_footprint.vert" (
    echo [OK] snow_footprint.vert
) else (
    echo [ERROR] snow_footprint.vert missing!
)

if exist "files\shaders\snow_footprint.frag" (
    echo [OK] snow_footprint.frag
) else (
    echo [ERROR] snow_footprint.frag missing!
)

if exist "files\shaders\snow_decay.frag" (
    echo [OK] snow_decay.frag
) else (
    echo [ERROR] snow_decay.frag missing!
)

if exist "files\shaders\snow_fullscreen.vert" (
    echo [OK] snow_fullscreen.vert
) else (
    echo [ERROR] snow_fullscreen.vert missing!
)
echo.

echo ============================================
echo Build verification complete!
echo ============================================
echo.
echo Next steps:
echo 1. Run OpenMW: build\Release\openmw.exe
echo 2. Load a saved game
echo 3. Walk around to test the system
echo 4. Check console for any shader errors
echo.
echo For debugging:
echo - See IMPLEMENTATION_NOTES.md
echo - Check openmw.log for errors
echo.
echo Press any key to exit...
pause > nul
