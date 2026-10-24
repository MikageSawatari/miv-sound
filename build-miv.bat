@echo off
setlocal
rem mIV Sound: VS 2026 BuildTools, CMake 4.3, bundled Ninja, Node 22.
rem C4458 is suppressed for MSVC 14.50 under /WX.
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Program Files\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set MSBUILDDISABLENODEREUSE=1
cd /d "%~dp0" || exit /b 1
node -e "process.exit(process.versions.node.split('.')[0] === '22' ? 0 : 1)" || exit /b 1
set "MIV_FETCH_OPTION=-DFETCHCONTENT_FULLY_DISCONNECTED=OFF"
if exist "build\windows-release\_deps\vst3sdk-src\CMakeLists.txt" set "MIV_FETCH_OPTION=-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
cmake --preset windows-release "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /wd4458" %MIV_FETCH_OPTION% || exit /b 2
cmake --build --preset windows-release || exit /b 3
echo BUILD-OK
if "%~1"=="--no-test" exit /b 0
ctest --preset windows-release --output-on-failure || exit /b 4
echo TEST-OK
exit /b 0
