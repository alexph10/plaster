@echo off
setlocal

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :fail

cmake --build . --config Debug
if errorlevel 1 goto :fail

cd ..
echo.
echo Running plaster...
".\build\Debug\plaster.exe"
echo.
echo Exit code: %ERRORLEVEL%
goto :eof

:fail
cd ..
echo.
echo Build failed.
exit /b 1
