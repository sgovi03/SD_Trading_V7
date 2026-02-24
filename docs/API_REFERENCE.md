# API REFERENCE - Milestone 1
## SD Trading System V4.0

---

## Common Types (`include/common_types.h`)

### Namespace: `SDTrading::Core`

### Enumerations

```cpp
enum class ZoneType { DEMAND, SUPPLY };
enum class ZoneState { FRESH, TESTED, VIOLATED };
enum class MarketRegime { BULL, BEAR, RANGING };
```

### Structures

#### Bar
```cpp
struct Bar {
    std::string datetime;
    double open, high, low, close, volume, oi;
};
```

#### Zone
```cpp
struct Zone {
    ZoneType type;
    double base_low, base_high;
    double distal_line, proximal_line;
    int formation_bar;
    std::string formation_datetime;
    double strength;
    ZoneState state;
    int touch_count;
    bool is_elite;
    // ... more fields
};
```

#### Config
```cpp
struct Config {
    double starting_capital;
    double risk_per_trade_pct;
    int consolidation_min_bars;
    // ... 40+ configuration parameters
    
    bool validate() const;  // Returns true if valid
};
```

---

## Logger (`src/utils/logger.h`)

### Namespace: `SDTrading::Utils`

### Log Levels

```cpp
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    NONE = 4
};
```

### Logger Class

```cpp
class Logger {
public:
    static Logger& getInstance();
    
    void setLevel(LogLevel level);
    void enableConsole(bool enable);
    void enableFile(bool enable, const std::string& filename);
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
};
```

### Macros

```cpp
LOG_DEBUG("Message with value: " << value);
LOG_INFO("Information message");
LOG_WARN("Warning message");
LOG_ERROR("Error message");
```

### Usage Example

```cpp
#include "utils/logger.h"
using namespace SDTrading::Utils;

Logger::getInstance().setLevel(LogLevel::DEBUG);
Logger::getInstance().enableFile(true, "app.log");

LOG_INFO("Application started");
LOG_DEBUG("Debug value: " << 42);
```

---

## Config Loader (`src/core/config_loader.h`)

### Namespace: `SDTrading::Core`

### Class

```cpp
class ConfigLoader {
public:
    static Config load_from_file(const std::string& filepath);
    static bool save_to_file(const Config& config, const std::string& filepath);
};
```

### Usage Example

```cpp
#include "core/config_loader.h"
using namespace SDTrading::Core;

// Load config
Config config = ConfigLoader::load_from_file("config.txt");

// Validate
if (!config.validate()) {
    std::cerr << "Invalid configuration" << std::endl;
    return 1;
}

// Use config
std::cout << "Capital: $" << config.starting_capital << std::endl;
```

### File Format

```
# Comments start with #
starting_capital=300000
risk_per_trade_pct=1.0
only_fresh_zones=true
```

---

## Thread Safety

- **Logger:** Thread-safe (uses mutex)
- **Config:** Thread-safe for reading (immutable after load)
- **ConfigLoader:** Not thread-safe (static methods, no shared state)

---

## Error Handling

- **Logger:** No exceptions, errors logged to console
- **ConfigLoader:** Throws `std::runtime_error` on errors
- **Config::validate():** Returns false, prints errors to std::cerr

---

For more examples, see the unit tests in `tests/unit/`.
