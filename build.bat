@echo off
REM Change to build directory
cd build

REM Generate Visual Studio 2022 solution (64-bit)
cmake .. -G "Visual Studio 17 2022" -A x64

REM Build the debug config 
cmake --build . --config Debug

REM Return to project root
cd .. 

REM Execute the executable
echo Running executable...
.\build\Debug\plasterEngine_app.exe
echo.
echo Executable exited with code: %ERRORLEVEL%
pause
