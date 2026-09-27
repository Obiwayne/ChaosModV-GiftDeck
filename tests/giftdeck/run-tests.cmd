@echo off
rem Builds and runs the GiftDeck GameLink tests (no game needed).
rem   1) gamelink_test.exe + test_gamelink.py: real GameLinkClient.cpp vs. a Python stand-in for GiftDeck
rem   2) dump_hello.exe: loads the built ChaosMod.asi offline and dumps the hello (full effect list)
rem Usage: tests\giftdeck\run-tests.cmd [path to ChaosMod.asi] [effects.json to use for enabled/disabled state]
rem Needs Python 3 with "pip install websockets".
setlocal
set HERE=%~dp0
set ROOT=%HERE%..\..
set ASI=%~1
if "%ASI%"=="" set ASI=%ROOT%\ChaosMod\build-Release\ChaosMod.asi
if "%VSROOT%"=="" set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set OUT=%HERE%out
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I "%HERE%." /I "%ROOT%\ChaosMod" /I "%ROOT%\vendor" ^
   "%HERE%gamelink_test.cpp" "%ROOT%\ChaosMod\Util\GameLinkClient.cpp" /Fo"%OUT%\\" /Fe"%OUT%\gamelink_test.exe" winhttp.lib || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT "%HERE%dump_hello.cpp" /Fo"%OUT%\\" /Fe"%OUT%\dump_hello.exe" || exit /b 1

echo.
echo === GameLink protocol test ===
python "%HERE%test_gamelink.py" "%OUT%\gamelink_test.exe" 21299 || set FAILED=1

echo.
echo === Offline effect list dump (%ASI%) ===
if exist "%OUT%\dumpwork" rmdir /s /q "%OUT%\dumpwork"
mkdir "%OUT%\dumpwork\chaosmod\configs"
if not "%~2"=="" copy /y "%~2" "%OUT%\dumpwork\chaosmod\configs\effects.json" >nul
"%OUT%\dump_hello.exe" "%ASI%" "%OUT%\dumpwork" "%OUT%\hello.json" || set FAILED=1
python "%HERE%check_hello.py" "%OUT%\hello.json" "%ROOT%\ConfigApp\Effects.cs" || set FAILED=1

if defined FAILED (echo TESTS FAILED & exit /b 1)
echo ALL TESTS PASSED
endlocal
