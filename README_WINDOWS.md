# WINDOWS BUILD GUIDE - Quick Start
## SD Trading System V4.0

---

## 🚀 FASTEST WAY TO BUILD (3 Steps)

### Step 1: Extract
```cmd
tar -xzf SD_Trading_V4_Milestone1.tar.gz
cd SD_Trading_V4
```

### Step 2: Run build script
**Option A:** Double-click `build_debug.bat`

**Option B:** Command line
```cmd
build_debug.bat
```

### Step 3: Done!
Tests will run automatically. You should see:
```
[==========] Running 23 tests
[  PASSED  ] 23 tests
```

---

## 📂 INCLUDED SCRIPTS

### Batch Files (Double-click to run)

- **build_debug.bat** - Build debug version + run tests
- **build_release.bat** - Build optimized version + run tests  
- **build_and_test.bat** - Quick build & test
- **clean.bat** - Remove build directory

### PowerShell Script (Advanced)

```powershell
# Debug build
.\Build-Project.ps1 -Configuration Debug -RunTests

# Release build  
.\Build-Project.ps1 -Configuration Release -RunTests

# Clean build
.\Build-Project.ps1 -Clean -Configuration Debug -RunTests
```

---

## 🛠️ PREREQUISITES

You need ONE of these:

**Option 1: Visual Studio** (Recommended)
- Visual Studio 2017 or later
- Install "Desktop development with C++" workload
- Free: https://visualstudio.microsoft.com/downloads/

**Option 2: MinGW-w64**
- Download: https://www.mingw-w64.org/
- Add to PATH

**Option 3: MSYS2**
- Download: https://www.msys2.org/
- Install gcc and cmake via pacman

---

## ⚡ MANUAL BUILD (if scripts don't work)

### Using Visual Studio

```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Debug
Debug\bin\run_tests.exe
```

### Using MinGW

```cmd
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
bin\run_tests.exe
```

---

## 🐛 TROUBLESHOOTING

### "cmake: command not found"

**Solution:** Install CMake from https://cmake.org/download/

Or use full path:
```cmd
"C:\Program Files\CMake\bin\cmake.exe" ..
```

### "Cannot find compiler"

**Solution:** Install Visual Studio with C++ tools

### Build hangs during Google Test download

**Solution:** Check internet connection or corporate firewall

---

## 📊 EXPECTED OUTPUT

After successful build + test:

```
============================================================
  SD Trading System V4.0 - Debug Build
============================================================

Creating build directory...
Configuring project with CMake...
-- Building for: Visual Studio 16 2019
-- The CXX compiler identification is MSVC 19.29.30147.0
-- Detecting CXX compiler ABI info - done
-- Configuring done
-- Generating done
-- Build files have been written to: C:/Projects/SD_Trading_V4/build

Building project (Debug)...
...
Build succeeded.

============================================================
  Build Successful!
============================================================

Running unit tests...

[==========] Running 23 tests from 5 test suites.
[----------] Global test environment set-up.
[----------] 8 tests from Config
[ RUN      ] Config.DefaultConstructor
[       OK ] Config.DefaultConstructor (0 ms)
...
[==========] 23 tests from 5 test suites ran. (XX ms total)
[  PASSED  ] 23 tests.

============================================================
  Build script complete!
============================================================
```

---

## 📁 OUTPUT LOCATIONS

**Debug build:**
```
build\Debug\lib\sdcore.lib          # Core library
build\Debug\bin\run_tests.exe       # Test executable
```

**Release build:**
```
build\Release\lib\sdcore.lib        # Core library
build\Release\bin\run_tests.exe     # Test executable
```

---

## ✅ WHAT'S NEXT?

1. ✅ All 23 tests pass
2. 📖 Read `docs\MILESTONE_1_REPORT.md`
3. 📚 Check `docs\API_REFERENCE.md`
4. 🚀 Ready for Milestone 2!

---

**For detailed Windows instructions, see:** `WINDOWS_BUILD_INSTRUCTIONS.md`
