@echo off
REM ============================================================
REM  Mix2Go - Clean Build Reset Script
REM  Run this whenever CMake / VS Code gets into a broken state.
REM  After running: open VS Code, select amd64 kit, configure.
REM ============================================================

echo Stopping any running build processes...
taskkill /F /IM ninja.exe  >nul 2>&1
taskkill /F /IM msbuild.exe >nul 2>&1

echo Deleting stale build directory...
if exist build (
    rmdir /S /Q build
    echo   build\ deleted.
) else (
    echo   build\ does not exist, nothing to delete.
)

echo.
echo Done. Next steps in VS Code:
echo   1. Ctrl+Shift+P  ->  CMake: Select a Kit
echo      Choose:  Visual Studio Build Tools ... - amd64   (NOT x86!)
echo   2. Ctrl+Shift+P  ->  CMake: Configure
echo   3. Ctrl+Shift+P  ->  CMake: Build
echo.
echo The compiled VST3 will be auto-copied to:
echo   C:\Program Files\Common Files\VST3\Mix2Go.vst3
echo Rescan plugins in FL Studio after the build completes.
pause
