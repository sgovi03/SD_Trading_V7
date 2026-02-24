# SD Trading System V4.0 - Milestone 1: Core Foundation

## 📋 Overview

This is **Milestone 1** of the SD Trading System V4.0 refactoring project. This milestone establishes the core foundation with:

- ✅ Hierarchical CMake build system
- ✅ Common types and data structures
- ✅ Logger with runtime configurable levels
- ✅ Configuration loader with validation
- ✅ Google Test framework integration
- ✅ Comprehensive unit tests

---

## 🏗️ Project Structure

```
SD_Trading_V4/
├── CMakeLists.txt                # Root CMake configuration
├── README.md                     # This file
│
├── include/                      # Public headers
│   └── common_types.h           # Shared structs (Zone, Bar, Trade, Config)
│
├── src/                         # Source code
│   ├── CMakeLists.txt
│   ├── core/                    # Core library
│   │   ├── CMakeLists.txt
│   │   ├── config_loader.h
│   │   └── config_loader.cpp
│   │
│   ├── utils/                   # Utilities
│   │   ├── CMakeLists.txt
│   │   └── logger.h            # Logger (header-only)
│   │
│   ├── persistence/            # Persistence layer (Milestone 4)
│   ├── engines/                # Engines (Milestone 5)
│
├── tests/                      # Test code
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_config.cpp
│   │   └── test_logger.cpp
│   ├── integration/            # (Later milestones)
│   └── data/                   # Test data
│
├── docs/                       # Documentation
│   ├── MILESTONE_1_REPORT.md
│   └── API_REFERENCE.md
│
├── examples/                   # Example files
│   └── sample_config.txt
│
└── build/                      # Build output (created by CMake)
    ├── lib/
    │   └── libsdcore.a
    └── bin/
        └── run_tests
```

---

## 🔧 Building the Project

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14 or higher
- Git (for fetching Google Test)
- Internet connection (first build only, to fetch Google Test)

### Build Commands

```bash
# 1. Navigate to project directory
cd SD_Trading_V4

# 2. Create build directory
mkdir -p build && cd build

# 3. Configure with CMake
cmake ..

# 4. Build
make -j$(nproc)

# 5. Run tests
./bin/run_tests

# Or use CTest
ctest --output-on-failure
```

### Build Options

```bash
# Debug build (with debug symbols, no optimization)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build (optimized, no debug symbols)
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Verbose build (see all compiler commands)
make VERBOSE=1
```

---

## 🧪 Running Tests

After building, run the test suite:

```bash
cd build

# Run all tests
./bin/run_tests

# Run tests with verbose output
./bin/run_tests --gtest_verbose

# Run specific test
./bin/run_tests --gtest_filter=Config.DefaultConstructor

# List all tests
./bin/run_tests --gtest_list_tests
```

### Expected Output

```
[==========] Running 23 tests from 5 test suites.
[----------] Global test environment set-up.
[----------] 8 tests from Config
[ RUN      ] Config.DefaultConstructor
[       OK ] Config.DefaultConstructor (0 ms)
...
[----------] 15 tests from LoggerTest
...
[==========] 23 tests from 5 test suites ran. (XX ms total)
[  PASSED  ] 23 tests.
```

---

## 📚 API Overview

### Common Types

Located in `include/common_types.h`:

- `Bar` - OHLC candlestick data
- `Zone` - Supply/Demand zone structure
- `Trade` - Trade record
- `Config` - System configuration
- `ZoneScore` - Multi-dimensional zone scoring
- `ScoringConfig` - Scoring parameters
- `EntryDecision` - Entry signal decision

### Logger

Located in `src/utils/logger.h`:

```cpp
#include "utils/logger.h"
using namespace SDTrading::Utils;

// Configure logger
Logger::getInstance().setLevel(LogLevel::DEBUG);
Logger::getInstance().enableConsole(true);
Logger::getInstance().enableFile(true, "trading.log");

// Use logging macros
LOG_DEBUG("Detecting zones from bar " << start_idx);
LOG_INFO("Found " << zones.size() << " zones");
LOG_WARN("Zone strength below threshold");
LOG_ERROR("Failed to load configuration");
```

### Config Loader

Located in `src/core/config_loader.h`:

```cpp
#include "core/config_loader.h"
using namespace SDTrading::Core;

// Load configuration
Config config = ConfigLoader::load_from_file("config.txt");

// Validate
if (!config.validate()) {
    std::cerr << "Invalid configuration!" << std::endl;
    return 1;
}

// Use configuration
std::cout << "Capital: $" << config.starting_capital << std::endl;
std::cout << "Risk: " << config.risk_per_trade_pct << "%" << std::endl;
```

---

## 📝 Configuration File Format

See `examples/sample_config.txt` for a complete example.

```
# SD Trading System Configuration

# Capital & Risk
starting_capital=300000
risk_per_trade_pct=1.0

# Zone Detection
consolidation_min_bars=1
consolidation_max_bars=10
base_height_max_atr=1.5
min_impulse_atr=1.2

# Logging
enable_debug_logging=true
enable_score_logging=true
```

Comments start with `#` and are ignored. Empty lines are ignored.

---

## ✅ Milestone 1 Deliverables

### Completed

- [x] Project structure with hierarchical CMake
- [x] Common types extracted (Zone, Bar, Trade, Config)
- [x] Logger implementation (console + file output)
- [x] Config loader with validation
- [x] Google Test framework integration
- [x] Unit tests for Config (8 tests)
- [x] Unit tests for Logger (8 tests)
- [x] Documentation (README, API Reference, Milestone Report)

### Test Coverage

- Config validation: 6 tests
- Config loading: 4 tests
- Logger functionality: 8 tests
- **Total: 23 unit tests**

### Build Artifacts

- `lib/libsdcore.a` - Core static library
- `bin/run_tests` - Test executable

---

## 🐛 Known Issues

None at this time.

---

## 🚀 Next Steps (Milestone 2)

In Milestone 2, we will implement:

- Zone Detection module (extract from backtest)
- Market Analyzer (ATR, EMA, HTF analysis)
- Unit tests for zone detection
- Integration test with sample data

---

## 📞 Support

For issues or questions, please refer to:

- `docs/API_REFERENCE.md` - Detailed API documentation
- `docs/MILESTONE_1_REPORT.md` - What was implemented and why
- Unit tests - Examples of how to use each component

---

## 📄 License

SD Trading System V4.0 - Proprietary

---

**Built with:** C++17, CMake 3.14+, Google Test 1.12.1
