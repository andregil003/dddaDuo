@echo off
REM dddaDuo build - 32-bit only (DDDA.exe is x86). Produces everything into dist\
setlocal
cd /d "%~dp0"

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VCVARS% set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x86 >nul || (echo [FAIL] vcvarsall x86 & exit /b 1)

if not exist dist mkdir dist

echo.
echo === core ===
cl /nologo /LD /O2 /W3 /MT /EHsc src\core\ddpuck_core.cpp /link /OUT:dist\ddpuck_core.dll
if errorlevel 1 exit /b 1

echo.
echo === loader: xinput1_3 ===
cl /nologo /LD /O2 /W3 /MT /EHsc src\loader-xinput\dllmain.cpp /link /DEF:src\loader-xinput\xinput1_3.def /OUT:dist\xinput1_3.dll
if errorlevel 1 exit /b 1

echo.
echo === loader: dinput8 ===
cl /nologo /LD /O2 /W3 /MT /EHsc src\loader-dinput8\dllmain.cpp /link /DEF:src\loader-dinput8\dinput8.def /OUT:dist\dinput8.dll
if errorlevel 1 exit /b 1

echo.
echo === exports check ===
echo -- xinput1_3.dll --
dumpbin /exports dist\xinput1_3.dll | findstr /R "XInput GetStateEx"
echo -- dinput8.dll --
dumpbin /exports dist\dinput8.dll | findstr DirectInput8Create

echo.
echo === architecture check (must be x86) ===
dumpbin /headers dist\ddpuck_core.dll | findstr /C:"machine (x86)"
echo.
echo [OK] dist\
dir /b dist
endlocal
