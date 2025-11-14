@echo off
echo ===============================================
echo Snow Deformation Debug Test
echo ===============================================
echo.

echo Setting environment for maximum debug output...
set OPENMW_LOG_LEVEL=Debug

echo.
echo Running OpenMW with snow deformation debugging...
echo.
echo Look for these log messages:
echo   - "SnowDeformation: RTT cameras created"
echo   - "SnowDeformation: Active footprints=..."
echo   - "SnowDeformation: Added footprint at..."
echo   - "SnowDeformation (material.cpp): First setSnowDeformationData call"
echo.

REM Run OpenMW and filter output to show only snow deformation messages
openmw.exe 2>&1 | findstr /I "snow deformation terrain" > snow_debug.log

echo.
echo Log saved to snow_debug.log
echo.
pause
