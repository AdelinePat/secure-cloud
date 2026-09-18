@echo off
setlocal EnableDelayedExpansion

echo ========================================
echo   Secure CLoud Client Build Script
echo ========================================
echo.

set "BUILD_TYPE=Release"
if /I "%~1"=="Debug" set "BUILD_TYPE=Debug"
if /I "%~1"=="Release" set "BUILD_TYPE=Release"

echo [INFO] Build type: %BUILD_TYPE%
echo.

REM This script is inside client/ directory

REM Navigate to script's directory (client/)
REM Use pushd instead of cd for better compatibility
pushd "%~dp0" 2>nul
if errorlevel 1 (
    REM Fallback: try to extract path from %0
    set "SCRIPT_PATH=%~dp0"
    if defined SCRIPT_PATH (
        cd /d "%SCRIPT_PATH%" 2>nul
    )
)

REM Verify we're in the right place
if not exist "CMakeLists.txt" (
    REM Try going to client/ subdirectory if we're in root
    if exist "client\CMakeLists.txt" (
        cd client
    )
)

echo [INFO] Working directory: %CD%
echo.

REM Check if Qt6 is in PATH
where /q qmake6 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Qt6 not found in PATH!
    echo.
    echo Please ensure Qt is in your PATH, or run this from:
    echo   - Qt command prompt
    echo   - OR Qt Creator's terminal
    echo.
    pause
    exit /b 1
)

echo [OK] Qt6 found
qmake6 --version
echo.

REM Verify we have CMakeLists.txt in current directory
if not exist "CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found!
    echo This script must be in the client/ directory.
    pause
    exit /b 1
)

echo [INFO] Building from: %CD%
echo.

REM Clean build directory if it exists (with retry for locked files)
if exist "build" (
    echo [INFO] Cleaning old build directory...
    %SystemRoot%\System32\timeout.exe /t 1 /nobreak >nul
    rmdir /s /q build 2>nul
    if exist "build" (
        echo [WARNING] Could not delete build directory (files in use)
        echo [INFO] Trying to force clean...
        %SystemRoot%\System32\timeout.exe /t 1 /nobreak >nul
        rmdir /s /q build 2>nul
        if exist "build" (
            echo [ERROR] Build directory is locked!
            echo Please close any programs using files in build/ and try again
            pause
            exit /b 1
        )
    )
)

REM Create fresh build directory
mkdir build
cd build

echo ========================================
echo Step 1: Configuring with CMake
echo ========================================
echo.

REM Get Qt installation root from qmake6
for /f "delims=" %%i in ('where qmake6') do set QT_QMAKE=%%i
for %%i in ("%QT_QMAKE%") do set QT_BIN_DIR=%%~dpi

REM Remove trailing backslash
if "%QT_BIN_DIR:~-1%"=="\" set "QT_BIN_DIR=%QT_BIN_DIR:~0,-1%"

REM Qt's standard installation structure:
REM - qmake6 is in:     C:\Qt\6.10.1\mingw_64\bin\qmake.exe
REM - Compilers are in: C:\Qt\Tools\mingw1310_64\bin\gcc.exe
REM We need to go from C:\Qt\6.10.1\mingw_64\bin up to C:\Qt

REM Extract just the drive and path (remove qmake6.exe)
for %%i in ("%QT_QMAKE%") do set QT_PATH=%%~dpi
REM Remove trailing backslash: C:\Qt\6.10.1\mingw_64\bin\
if "%QT_PATH:~-1%"=="\" set "QT_PATH=%QT_PATH:~0,-1%"

REM Go up: bin -> mingw_64 -> 6.10.1 -> Qt
for %%i in ("%QT_PATH%") do set QT_PATH=%%~dpi
if "%QT_PATH:~-1%"=="\" set "QT_PATH=%QT_PATH:~0,-1%"

for %%i in ("%QT_PATH%") do set QT_PATH=%%~dpi
if "%QT_PATH:~-1%"=="\" set "QT_PATH=%QT_PATH:~0,-1%"

for %%i in ("%QT_PATH%") do set QT_ROOT=%%~dpi
if "%QT_ROOT:~-1%"=="\" set "QT_ROOT=%QT_ROOT:~0,-1%"

echo [INFO] Qt root: %QT_ROOT%
echo [INFO] Qt libraries: %QT_BIN_DIR%
echo.

REM Find MinGW toolchain directory
set "MINGW_BIN="
if exist "%QT_ROOT%\Tools\mingw1310_64\bin" (
    set "MINGW_BIN=%QT_ROOT%\Tools\mingw1310_64\bin"
) else if exist "%QT_ROOT%\Tools\mingw1120_64\bin" (
    set "MINGW_BIN=%QT_ROOT%\Tools\mingw1120_64\bin"
) else if exist "%QT_ROOT%\Tools\mingw_64\bin" (
    set "MINGW_BIN=%QT_ROOT%\Tools\mingw_64\bin"
) else (
    echo [ERROR] Could not find MinGW toolchain in %QT_ROOT%\Tools\
    echo.
    echo Please ensure Qt was installed with MinGW compiler.
    echo Expected location: %QT_ROOT%\Tools\mingw*_64\bin
    echo.
    pause
    exit /b 1
)

echo [INFO] MinGW toolchain: %MINGW_BIN%
echo.

REM Verify compilers exist
if not exist "%MINGW_BIN%\gcc.exe" (
    echo [ERROR] gcc.exe not found at: %MINGW_BIN%\gcc.exe
    pause
    exit /b 1
)

if not exist "%MINGW_BIN%\g++.exe" (
    echo [ERROR] g++.exe not found at: %MINGW_BIN%\g++.exe
    pause
    exit /b 1
)

if not exist "%MINGW_BIN%\mingw32-make.exe" (
    echo [ERROR] mingw32-make.exe not found at: %MINGW_BIN%\mingw32-make.exe
    pause
    exit /b 1
)

echo [OK] All required tools found
echo.

REM Build full paths to tools
set "QT_GCC=%MINGW_BIN%\gcc.exe"
set "QT_GPP=%MINGW_BIN%\g++.exe"
set "QT_MAKE=%MINGW_BIN%\mingw32-make.exe"
set "WINDEPLOYQT=%QT_BIN_DIR%\windeployqt.exe"

if not exist "%WINDEPLOYQT%" (
    echo [ERROR] windeployqt.exe not found:
    echo %WINDEPLOYQT%
    pause
    exit /b 1
)

echo [INFO] Using compilers:
echo   GCC:  %QT_GCC%
echo   G++:  %QT_GPP%
echo   MAKE: %QT_MAKE%
echo   WINDEPLOYQT: %WINDEPLOYQT%
echo.

REM CRITICAL: Prepend both Qt bin and MinGW bin to PATH
REM This ensures Qt's tools are found first, before MSYS2
set "PATH=%MINGW_BIN%;%QT_BIN_DIR%;%PATH%"

echo [INFO] PATH updated (temporary - only for this build)
echo.

@REM TODO : BEGIN OF OPENSSL HANDLING

@REM  set "OPENSSL_PATH="

@REM  REM Search locations where OpenSSL may exist

@REM  set "OPENSSL_CANDIDATES="

@REM  REM MSYS2 MinGW64
@REM  set "OPENSSL_CANDIDATES=!OPENSSL_CANDIDATES! C:\msys64\mingw64"

@REM  REM MSYS2 UCRT64
@REM  set "OPENSSL_CANDIDATES=!OPENSSL_CANDIDATES! C:\msys64\ucrt64"

@REM  REM Common vcpkg locations
@REM  set "OPENSSL_CANDIDATES=!OPENSSL_CANDIDATES! %USERPROFILE%\vcpkg\installed\x64-mingw-static"
@REM  set "OPENSSL_CANDIDATES=!OPENSSL_CANDIDATES! %USERPROFILE%\vcpkg\installed\x64-windows"

@REM  REM Chocolatey / manual installs
@REM  set "OPENSSL_CANDIDATES=!OPENSSL_CANDIDATES! C:\tools\OpenSSL"
@REM  set "OPENSSL_CANDIDATES=!OPENSSL_CANDIDATES! C:\Program Files\OpenSSL-Win64"


@REM  for %%O in (!OPENSSL_CANDIDATES!) do (
@REM      if not defined OPENSSL_PATH (
@REM          if exist "%%O\include\openssl\ssl.h" (
@REM              if exist "%%O\lib\libssl.dll.a" (
@REM                  if exist "%%O\lib\libcrypto.dll.a" (
@REM                      set "OPENSSL_PATH=%%O"
@REM                  )
@REM              )
@REM          )
@REM      )
@REM  )


@REM  if defined OPENSSL_PATH (
@REM      echo [OK] Found MinGW OpenSSL:
@REM      echo      !OPENSSL_PATH!
@REM  ) else (
@REM      echo [WARNING] OpenSSL development files not found
@REM  )

@REM  echo.

@REM TODO : END OF OPENSSL HANDLING

REM Configure with CMake using explicit tool paths
REM Build the command step by step to handle spaces in paths properly
set "CMAKE_CMD=cmake .. -G "MinGW Makefiles""
set "CMAKE_CMD=!CMAKE_CMD! -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
set "CMAKE_CMD=!CMAKE_CMD! -DCMAKE_CXX_COMPILER="%QT_GPP%""
set "CMAKE_CMD=!CMAKE_CMD! -DCMAKE_MAKE_PROGRAM="%QT_MAKE%""
set "CMAKE_CMD=!CMAKE_CMD! -DCMAKE_PREFIX_PATH="%QT_BIN_DIR%\.""

REM Add OpenSSL explicitly if found
if defined OPENSSL_PATH (
    set "CMAKE_CMD=!CMAKE_CMD! -DOPENSSL_ROOT_DIR="%OPENSSL_PATH%""
)

!CMAKE_CMD!

echo CMake returned %ERRORLEVEL%

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed!
    echo.
    echo Troubleshooting:
    echo 1. Close Qt Creator if it's open
    echo 2. Check compiler paths above are correct
    echo 3. Try deleting build/ directory manually
    echo 4. Or open CMakeLists.txt in Qt Creator
    echo.
    pause
    exit /b 1
)

echo.
echo [OK] Configuration successful
echo.

echo ========================================
echo Step 2: Building Client
echo ========================================
echo.

cmake --build . --config %BUILD_TYPE% --parallel 4
if errorlevel 1 (
    echo.
    echo [FAILED] Build failed!
    echo Check the errors above
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo Step 3: Deploying Qt Runtime
echo ========================================
echo.

if exist "%WINDEPLOYQT%" (
    "%WINDEPLOYQT%" --no-translations dashboard.exe

    if errorlevel 1 (
        echo [WARNING] windeployqt failed.
        echo The executable may require Qt DLLs to be present in PATH.
    ) else (
        echo [OK] Qt runtime deployed.
    )
) else (
    echo [WARNING] windeployqt not found:
    echo %WINDEPLOYQT%
)

echo.
echo ========================================
echo   Build Successful!
echo ========================================
echo.

REM Find the executable
if exist "dashboard.exe" (
    echo Executable: %CD%\dashboard.exe
    echo.
) else if exist "Release\dashboard.exe" (
    echo Executable: %CD%\Release\dashboard.exe
    echo.
) else (
    echo Executable built - check build\ directory
    dir /b *.exe 2>nul
    echo.
)

echo To run the dashboard:
echo   cd build
echo   dashboard.exe
echo.
echo Or double-click the .exe file in File Explorer
echo.

pause