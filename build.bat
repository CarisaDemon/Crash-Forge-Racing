@echo off
setlocal EnableExtensions

rem Crash Forge Racing - Windows build script
rem Builds the native 32-bit Windows executable with MSYS2 MinGW32.

cd /d "%~dp0"
if errorlevel 1 (
    echo ERROR: Could not enter the source directory.
    exit /b 1
)

echo.
echo === Crash Forge Racing build ===
echo Source: %CD%
echo.
set "CFR_SOURCE_CMAKE=%CD:\=/%"

rem Help fresh MSYS2 installs work even when MinGW32 is not in the user's PATH.
if exist "C:\msys64\mingw32\bin\i686-w64-mingw32-gcc.exe" (
    set "PATH=C:\msys64\mingw32\bin;%PATH%"
)

set "CFR_MSYS2_PACKAGES=git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make mingw-w64-i686-libvorbis"

if not exist "main.c" (
    echo ERROR: main.c is missing from the source root.
    echo This repository is incomplete and cannot be built.
    exit /b 1
)

if not exist "CMakeLists.txt" (
    echo ERROR: CMakeLists.txt is missing from the source root.
    exit /b 1
)

if not exist "externals\SDL\CMakeLists.txt" (
    echo ERROR: externals\SDL is missing or incomplete.
    echo Make sure the repository was cloned/downloaded with the vendored SDL source.
    exit /b 1
)

where i686-w64-mingw32-gcc >nul 2>&1
if errorlevel 1 goto :missing_tools

where i686-w64-mingw32-g++ >nul 2>&1
if errorlevel 1 goto :missing_tools

where cmake >nul 2>&1
if errorlevel 1 goto :missing_tools

where mingw32-make >nul 2>&1
if errorlevel 1 goto :missing_tools

call :check_vorbis
if errorlevel 1 (
    echo.
    echo Vorbis development files are missing.
    echo Crash Forge Racing uses OGG Vorbis for custom music.

    if exist "C:\msys64\usr\bin\bash.exe" (
        echo Installing mingw-w64-i686-libvorbis with MSYS2...
        "C:\msys64\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-i686-libvorbis"
        if errorlevel 1 (
            echo.
            echo ERROR: Could not install the Vorbis dependency.
            echo Run manually in an MSYS2 shell:
            echo   pacman -S --needed mingw-w64-i686-libvorbis
            exit /b 1
        )

        call :check_vorbis
        if errorlevel 1 (
            echo ERROR: Vorbis was installed, but the MinGW32 compiler still cannot find vorbis/vorbisfile.h.
            exit /b 1
        )
    ) else (
        echo ERROR: Automatic dependency installation requires the standard C:\msys64 installation.
        echo Install the MinGW32 Vorbis package manually:
        echo   pacman -S --needed mingw-w64-i686-libvorbis
        exit /b 1
    )
)

rem A copied/moved build directory may contain a CMake cache that points at an
rem older source location. Detect that case and regenerate it automatically.
set "CFR_NEED_CONFIGURE=1"
if exist "build\CMakeCache.txt" (
    findstr /C:"CMAKE_HOME_DIRECTORY:INTERNAL=%CFR_SOURCE_CMAKE%" "build\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo Existing CMake cache belongs to another source path.
        echo Recreating build directory...
        rmdir /s /q "build"
        if errorlevel 1 (
            echo ERROR: Could not remove the stale build directory.
            exit /b 1
        )
    ) else (
        set "CFR_NEED_CONFIGURE=0"
    )
)

if "%CFR_NEED_CONFIGURE%"=="1" (
    echo Configuring...
    cmake -S "%CD%" -B "%CD%\build" -G "MinGW Makefiles" ^
        -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc ^
        -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ ^
        -DCMAKE_MAKE_PROGRAM=mingw32-make ^
        -DCMAKE_BUILD_TYPE=Release ^
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" ^
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    if errorlevel 1 (
        echo.
        echo ERROR: CMake configure failed.
        exit /b 1
    )
) else (
    echo Using existing CMake configuration.
)

echo.
echo Building...
cmake --build "%CD%\build" --parallel

if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    exit /b 1
)

if not exist "build\ctr_native.exe" (
    echo.
    echo ERROR: Build finished without producing build\ctr_native.exe.
    exit /b 1
)

echo.
echo ==========================================
echo Build succeeded.
echo EXE: %CD%\build\ctr_native.exe
echo ==========================================
exit /b 0

:check_vorbis
set "CFR_VORBIS_TEST=%TEMP%\cfr_vorbis_%RANDOM%.c"
>"%CFR_VORBIS_TEST%" echo #include ^<vorbis/vorbisfile.h^>
i686-w64-mingw32-gcc -E -x c "%CFR_VORBIS_TEST%" >nul 2>&1
set "CFR_VORBIS_RESULT=%errorlevel%"
del /q "%CFR_VORBIS_TEST%" >nul 2>&1
exit /b %CFR_VORBIS_RESULT%

:missing_tools
echo.
echo ERROR: Required MSYS2 MinGW32 build tools were not found.
echo.
echo Install MSYS2, then run:
echo   pacman -S --needed %CFR_MSYS2_PACKAGES%
echo.
echo Expected tool folder:
echo   C:\msys64\mingw32\bin
exit /b 1
