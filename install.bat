@echo off
REM ============================================================
REM SD Trading System - Spring Boot Integration Installer
REM Windows Batch Script
REM Installs HTTP order submission components
REM ============================================================

setlocal enabledelayedexpansion

echo ============================================================
echo SD Trading System - Spring Boot Integration Installer
echo ============================================================
echo.

REM Get project root from command line or use default
set "PROJECT_ROOT=%~1"
if "%PROJECT_ROOT%"=="" set "PROJECT_ROOT=..\.."

REM Check if project root exists
if not exist "%PROJECT_ROOT%" (
    echo ERROR: Project root not found: %PROJECT_ROOT%
    echo Usage: install.bat [PROJECT_ROOT]
    echo.
    echo Example: install.bat D:\SD_System\OrderSubmit\SD_Trading_V4
    pause
    exit /b 1
)

echo Project Root: %PROJECT_ROOT%
echo.

REM Check prerequisites
echo Checking prerequisites...

REM Check for curl (Windows 10+ has curl built-in)
where curl >nul 2>&1
if errorlevel 1 (
    echo WARNING: curl not found in PATH
    echo Please ensure libcurl is installed for your build environment
    echo   - MSYS2/MinGW: pacman -S mingw-w64-x86_64-curl
    echo   - vcpkg: vcpkg install curl
) else (
    curl --version | findstr /C:"curl"
    echo.
)

REM Check for CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo WARNING: CMake not found in PATH
    echo Please install CMake from https://cmake.org/download/
) else (
    for /f "tokens=3" %%v in ('cmake --version ^| findstr /C:"version"') do (
        echo [OK] CMake found: %%v
    )
    echo.
)

REM Backup existing files
echo Creating backups...
if exist "%PROJECT_ROOT%\system_config.json" (
    copy /Y "%PROJECT_ROOT%\system_config.json" "%PROJECT_ROOT%\system_config.json.bak" >nul
    echo [OK] Backed up system_config.json
)

if exist "%PROJECT_ROOT%\src\system_config.h" (
    copy /Y "%PROJECT_ROOT%\src\system_config.h" "%PROJECT_ROOT%\src\system_config.h.bak" >nul
    echo [OK] Backed up system_config.h
)
echo.

REM Install HTTP utilities
echo Installing HTTP utilities...
if not exist "%PROJECT_ROOT%\src\utils\http\" mkdir "%PROJECT_ROOT%\src\utils\http"
xcopy /Y /Q "src\utils\http\*.*" "%PROJECT_ROOT%\src\utils\http\" >nul
echo [OK] Copied HTTP client files
echo.

REM Install test program
echo Installing test program...
copy /Y "src\test_order_submitter.cpp" "%PROJECT_ROOT%\src\" >nul
echo [OK] Copied test program
echo.

REM Update configuration
echo Updating configuration...

REM Update system_config.json
if exist "%PROJECT_ROOT%\system_config.json" (
    findstr /C:"order_submitter" "%PROJECT_ROOT%\system_config.json" >nul 2>&1
    if errorlevel 1 (
        echo Adding order_submitter section to system_config.json...
        copy /Y "config\system_config.json" "%PROJECT_ROOT%\system_config.json" >nul
        echo [OK] Updated system_config.json
    ) else (
        echo [WARNING] order_submitter section already exists in system_config.json
        echo            Please merge manually if needed
    )
) else (
    copy /Y "config\system_config.json" "%PROJECT_ROOT%\system_config.json" >nul
    echo [OK] Installed system_config.json
)
echo.

REM Install documentation
echo Installing documentation...
if not exist "%PROJECT_ROOT%\docs\" mkdir "%PROJECT_ROOT%\docs"
xcopy /Y /Q "docs\*.md" "%PROJECT_ROOT%\docs\" >nul 2>&1
echo [OK] Copied documentation
echo.

REM Create CMakeLists.txt additions
echo Creating CMakeLists.txt additions...
(
echo # ============================================================
echo # ADD THESE LINES TO YOUR CMakeLists.txt
echo # ============================================================
echo.
echo # Find libcurl
echo find_package^(CURL REQUIRED^)
echo.
echo # Add HTTP utils subdirectory
echo add_subdirectory^(src/utils/http^)
echo.
echo # Link to your live_engine target
echo target_link_libraries^(live_engine
echo     PRIVATE
echo     http_utils
echo     ${CURL_LIBRARIES}
echo     # ... your other libraries
echo ^)
echo.
echo # Optional: Build test program
echo add_executable^(test_order_submitter
echo     src/test_order_submitter.cpp
echo ^)
echo.
echo target_link_libraries^(test_order_submitter
echo     PRIVATE
echo     http_utils
echo     ${CURL_LIBRARIES}
echo ^)
echo.
echo # ============================================================
) > "%PROJECT_ROOT%\CMakeLists_additions.txt"
echo [OK] Created CMakeLists_additions.txt
echo.

REM Create integration checklist
echo Creating integration checklist...
(
echo INTEGRATION CHECKLIST
echo =====================
echo.
echo Step 1: Update CMakeLists.txt
echo    [ ] Add contents of CMakeLists_additions.txt to your CMakeLists.txt
echo    [ ] Open CMake GUI or run: cmake ..
echo    [ ] Build: cmake --build . --config Release
echo.
echo Step 2: Update system_config.h
echo    [ ] Add contents of config\system_config_additions.h to your system_config.h
echo        ^(Add before the closing }; of the class^)
echo.
echo Step 3: Test Installation
echo    [ ] Build test program in Visual Studio or with CMake
echo    [ ] Run test ^(dry-run^): test_order_submitter.exe
echo    [ ] Verify no compilation errors
echo.
echo Step 4: Start Spring Boot Service
echo    [ ] Start your Java Spring Boot microservice
echo    [ ] Verify it's running: curl http://localhost:8080/
echo    [ ] Test order endpoint:
echo        curl -X POST http://localhost:8080/multipleOrderSubmit -d "quantity=1&strategyNumber=13&weekNum=0&monthNum=0&type=1"
echo.
echo Step 5: Test HTTP Connection
echo    [ ] Run: test_order_submitter.exe --live
echo    [ ] Verify connection test passes
echo    [ ] Check Spring Boot logs for received requests
echo.
echo Step 6: Modify live_engine.h
echo    [ ] Add: #include "../utils/http/order_submitter.h"
echo    [ ] Add member: std::unique_ptr^<OrderSubmitter^> order_submitter_;
echo    [ ] See: docs\LIVE_ENGINE_MODIFICATIONS.md for details
echo.
echo Step 7: Modify live_engine.cpp
echo    [ ] Update constructor to create OrderSubmitter
echo    [ ] Update initialize^(^) to initialize OrderSubmitter
echo    [ ] Update check_for_entries^(^) to submit orders
echo    [ ] Update manage_position^(^) to square off
echo    [ ] See: docs\LIVE_ENGINE_MODIFICATIONS.md for code examples
echo.
echo Step 8: Test Integration ^(Dry-Run^)
echo    [ ] Set enabled=false in system_config.json
echo    [ ] Run LiveEngine
echo    [ ] Verify console shows: [DRY-RUN] Order submission disabled
echo.
echo Step 9: Test Integration ^(Live^)
echo    [ ] Set enabled=true in system_config.json
echo    [ ] Run LiveEngine with SMALL POSITION SIZE
echo    [ ] Verify order submission works
echo    [ ] Check Fyers for executed orders
echo.
echo Step 10: Production Deployment
echo    [ ] Monitor for several days with small positions
echo    [ ] Review logs for any errors
echo    [ ] Gradually increase position sizes
echo    [ ] Set up monitoring and alerts
echo.
echo DOCUMENTATION
echo =============
echo README.md                          - Overview and quick start
echo docs\SPRING_BOOT_INTEGRATION.md    - Complete integration guide
echo docs\LIVE_ENGINE_MODIFICATIONS.md  - Detailed modification steps
echo.
echo SUPPORT
echo =======
echo Check logs: logs\sd_engine.log
echo Test connection: curl http://localhost:8080/
echo Enable verbose: Set verbose=true in HttpClient constructor
) > "%PROJECT_ROOT%\INTEGRATION_CHECKLIST.txt"
echo [OK] Created INTEGRATION_CHECKLIST.txt
echo.

REM Summary
echo ============================================================
echo INSTALLATION COMPLETE!
echo ============================================================
echo.
echo Files installed:
echo   [OK] HTTP utilities -^> src\utils\http\
echo   [OK] Test program -^> src\test_order_submitter.cpp
echo   [OK] Documentation -^> docs\
echo   [OK] Configuration -^> system_config.json ^(updated^)
echo.
echo Next steps:
echo   1. Review: INTEGRATION_CHECKLIST.txt
echo   2. Update: CMakeLists.txt ^(see CMakeLists_additions.txt^)
echo   3. Update: src\system_config.h ^(see config\system_config_additions.h^)
echo   4. Read: docs\LIVE_ENGINE_MODIFICATIONS.md
echo.
echo To test installation:
echo   cd build
echo   cmake ..
echo   cmake --build . --config Release
echo   Release\test_order_submitter.exe
echo.
echo OR if using Visual Studio:
echo   1. Open CMakeLists.txt in Visual Studio
echo   2. Build Solution
echo   3. Run test_order_submitter
echo.
echo WARNING: Test in dry-run mode first before enabling live trading!
echo.
pause
