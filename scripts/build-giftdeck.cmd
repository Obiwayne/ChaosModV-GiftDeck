@echo off
rem GiftDeck build of Chaos Mod V: builds ChaosMod.asi with Visual Studio 2026 (MSVC x64) and its bundled
rem CMake + Ninja, then assembles dist\ (drop-in files for the GTA V folder) and release\ (zip + commands.json).
rem
rem Usage: scripts\build-giftdeck.cmd [Release|Debug]
rem   VSROOT       Visual Studio install (default: C:\Program Files\Microsoft Visual Studio\18\Community)
rem   CMAKE_EXTRA  extra CMake arguments
rem
rem Same CMake setup as upstream CI (-DWITH_DEBUG_PANEL_SUPPORT=1), plus a static C runtime so the .asi does not
rem depend on the Visual C++ 2015-2026 redistributable version installed on the player's PC.
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release
set ROOT=%~dp0..
if "%VSROOT%"=="" set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set BUILD=%ROOT%\ChaosMod\build-giftdeck-%CONFIG%

cmake -S "%ROOT%\ChaosMod" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
      -DWITH_DEBUG_PANEL_SUPPORT=1 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$^<$^<CONFIG:Debug^>:Debug^> %CMAKE_EXTRA% || exit /b 1
cmake --build "%BUILD%" || exit /b 1

if /i "%CONFIG%"=="Release" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\make-dist.ps1" -Asi "%BUILD%\ChaosMod.asi" || exit /b 1
)
endlocal
