@echo off
rem Builds Blockfall with CMake + Visual Studio. Output: build\Release\Blockfall.exe
where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found.
    echo Open "x64 Native Tools Command Prompt for VS 2022" from the Start menu and run build.bat from there,
    echo or install Visual Studio 2022 with the "Desktop development with C++" workload.
    pause
    exit /b 1
)
cmake -S "%~dp0." -B "%~dp0build" -A x64
if errorlevel 1 goto failed
cmake --build "%~dp0build" --config Release
if errorlevel 1 goto failed
echo.
echo Done! Run build\Release\Blockfall.exe
pause
exit /b 0
:failed
echo.
echo Build failed - see the messages above.
pause
exit /b 1
