// =============================================================================
// ZonesToCsv.cpp
// SD Trading V8 — Zone JSON to CSV Converter
// Standalone executable. Runs in a Sleep loop, converts zones_live JSON → CSV
// at a configurable interval so AmiBroker AFL can read zone data natively.
//
// Build (MSVC):
//   cl /EHsc /O2 /std:c++17 ZonesToCsv.cpp /Fe:ZonesToCsv.exe
//
// Build (MinGW/GCC on Windows):
//   g++ -std=c++17 -O2 -o ZonesToCsv.exe ZonesToCsv.cpp
//
// Run:
//   ZonesToCsv.exe [json_path] [csv_path] [interval_seconds]
//   ZonesToCsv.exe                          (uses defaults below)
// =============================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <filesystem>

// ---------------------------------------------------------------------------
// Defaults — edit these to match your V8 results folder
// ---------------------------------------------------------------------------
static const std::string DEFAULT_JSON_PATH =
    "C:/SDTrading/results/zones_live_NIFTY_FUT_master.json";

static const std::string DEFAULT_CSV_PATH =
    "C:/SDTrading/results/zones_export.csv";

static const int DEFAULT_INTERVAL_SECONDS = 300;   // 5 minutes = one bar
// ---------------------------------------------------------------------------

// CSV header — one line, AFL reads columns by index (0-based)
// Col: 0=zone_id 1=type 2=proximal 3=distal 4=state 5=touch_count
//      6=total_score 7=is_elite 8=formation_date 9=rr 10=rationale
//      11=departure_imbalance
static const std::string CSV_HEADER =
    "zone_id,type,proximal,distal,state,touch_count,total_score,"
    "is_elite,formation_date,rr,rationale,departure_imbalance\n";

// ---------------------------------------------------------------------------
// Minimal JSON string extractor — no external library dependency.
// Extracts the string value of a key from a flat JSON object fragment.
// e.g. extractString(obj, "state") -> "TESTED"
// ---------------------------------------------------------------------------
static std::string extractString(const std::string& json, const std::string& key)
{
    // Search for "key": or "key" :
    std::string needle = "\"" + key + "\"";
    size_t kpos = json.find(needle);
    if (kpos == std::string::npos) return "";

    size_t colon = json.find(':', kpos + needle.size());
    if (colon == std::string::npos) return "";

    // Skip whitespace after colon
    size_t vstart = colon + 1;
    while (vstart < json.size() && (json[vstart] == ' ' || json[vstart] == '\t' ||
           json[vstart] == '\r' || json[vstart] == '\n'))
        ++vstart;

    if (vstart >= json.size()) return "";

    if (json[vstart] == '"')
    {
        // String value
        size_t end = json.find('"', vstart + 1);
        if (end == std::string::npos) return "";
        return json.substr(vstart + 1, end - vstart - 1);
    }
    else
    {
        // Numeric / boolean value — read until , } or whitespace
        size_t end = vstart;
        while (end < json.size() && json[end] != ',' && json[end] != '}' &&
               json[end] != '\r' && json[end] != '\n' && json[end] != ' ')
            ++end;
        return json.substr(vstart, end - vstart);
    }
}

// Extract value from a nested sub-object e.g. zone_score.total_score
static std::string extractNested(const std::string& json,
                                  const std::string& subobj,
                                  const std::string& key)
{
    std::string needle = "\"" + subobj + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "0";

    // Find opening brace of sub-object
    size_t brace = json.find('{', pos + needle.size());
    if (brace == std::string::npos) return "0";

    // Find matching closing brace (depth tracking)
    int depth = 1;
    size_t cur = brace + 1;
    while (cur < json.size() && depth > 0)
    {
        if (json[cur] == '{') ++depth;
        else if (json[cur] == '}') --depth;
        ++cur;
    }
    std::string sub = json.substr(brace, cur - brace);
    return extractString(sub, key);
}

// ---------------------------------------------------------------------------
// Parse the formation_datetime field and return YYYY-MM-DD portion only
// Input:  "2026-04-24T15:20:00+05:30"
// Output: "2026-04-24"
// ---------------------------------------------------------------------------
static std::string parseFormationDate(const std::string& dt)
{
    if (dt.size() >= 10) return dt.substr(0, 10);
    return dt;
}

// ---------------------------------------------------------------------------
// Sanitise a string value for CSV — remove commas and newlines
// ---------------------------------------------------------------------------
static std::string csvSafe(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (c != ',' && c != '\r' && c != '\n' && c != '"') out += c;
    return out;
}

// ---------------------------------------------------------------------------
// Convert one JSON file → one CSV file.
// Returns true on success.
// ---------------------------------------------------------------------------
static bool convertJsonToCsv(const std::string& jsonPath,
                               const std::string& csvPath)
{
    // --- Read entire JSON file into memory ---
    std::ifstream fin(jsonPath, std::ios::binary);
    if (!fin.is_open())
    {
        std::cerr << "[ERROR] Cannot open JSON: " << jsonPath << "\n";
        return false;
    }
    std::ostringstream buf;
    buf << fin.rdbuf();
    const std::string json = buf.str();
    fin.close();

    if (json.empty())
    {
        std::cerr << "[ERROR] JSON file is empty: " << jsonPath << "\n";
        return false;
    }

    // --- Extract metadata ---
    std::string symbol      = extractString(json, "symbol");
    std::string generatedAt = extractString(json, "generated_at");
    std::string mode        = extractString(json, "mode");

    // --- Find the "zones" array ---
    size_t zonesPos = json.find("\"zones\"");
    if (zonesPos == std::string::npos)
    {
        std::cerr << "[ERROR] 'zones' key not found in JSON\n";
        return false;
    }
    size_t arrStart = json.find('[', zonesPos);
    if (arrStart == std::string::npos) return false;

    // --- Write CSV to a temp file first, then rename (atomic for AFL reader) ---
    std::string tmpPath = csvPath + ".tmp";
    std::ofstream fout(tmpPath);
    if (!fout.is_open())
    {
        std::cerr << "[ERROR] Cannot write CSV: " << tmpPath << "\n";
        return false;
    }

    // Metadata comment lines — AFL skips lines starting with '#'
    fout << "# symbol=" << symbol
         << " generated_at=" << generatedAt
         << " mode=" << mode << "\n";
    fout << CSV_HEADER;

    // --- Iterate over zone objects in the array ---
    int zoneCount = 0;
    size_t pos = arrStart + 1;

    while (pos < json.size())
    {
        // Skip whitespace and commas between objects
        while (pos < json.size() &&
               (json[pos] == ' ' || json[pos] == '\t' ||
                json[pos] == '\r' || json[pos] == '\n' || json[pos] == ','))
            ++pos;

        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') { ++pos; continue; }

        // Find matching closing brace for this zone object (depth tracking)
        int depth = 1;
        size_t objStart = pos;
        size_t cur = pos + 1;
        while (cur < json.size() && depth > 0)
        {
            if      (json[cur] == '{') ++depth;
            else if (json[cur] == '}') --depth;
            ++cur;
        }
        std::string zoneObj = json.substr(objStart, cur - objStart);
        pos = cur;

        // --- Extract fields ---
        std::string zone_id     = extractString(zoneObj, "zone_id");
        std::string type        = extractString(zoneObj, "type");
        std::string proximal    = extractString(zoneObj, "proximal_line");
        std::string distal      = extractString(zoneObj, "distal_line");
        std::string state       = extractString(zoneObj, "state");
        std::string touch       = extractString(zoneObj, "touch_count");
        std::string is_elite    = extractString(zoneObj, "is_elite");
        std::string is_active   = extractString(zoneObj, "is_active");
        std::string dep_imbal   = extractString(zoneObj, "departure_imbalance");
        std::string formation_dt = extractString(zoneObj, "formation_datetime");

        // Nested: zone_score.total_score and zone_score.recommended_rr
        std::string total_score = extractNested(zoneObj, "zone_score", "total_score");
        std::string rr          = extractNested(zoneObj, "zone_score", "recommended_rr");
        std::string rationale   = extractNested(zoneObj, "zone_score", "entry_rationale");

        // Skip inactive zones
        if (is_active == "false") continue;

        // Normalise boolean → 0/1 for AFL numeric use
        std::string elite_flag = (is_elite == "true") ? "1" : "0";

        // Formation date (date only, no time)
        std::string fdate = parseFormationDate(formation_dt);

        // Guard against empty critical fields
        if (zone_id.empty() || type.empty() || proximal.empty() || distal.empty())
            continue;

        fout << zone_id      << ","
             << csvSafe(type) << ","
             << proximal      << ","
             << distal        << ","
             << csvSafe(state) << ","
             << touch         << ","
             << total_score   << ","
             << elite_flag    << ","
             << fdate         << ","
             << rr            << ","
             << csvSafe(rationale)  << ","
             << dep_imbal     << "\n";

        ++zoneCount;
    }

    fout.flush();
    fout.close();

    // Atomic rename: replace old CSV with new one
    // AFL may be reading the old CSV — rename ensures it sees a complete file
    std::error_code ec;
    std::filesystem::rename(tmpPath, csvPath, ec);
    if (ec)
    {
        // Fallback: copy then delete (Windows rename across drives)
        std::filesystem::copy_file(tmpPath, csvPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmpPath, ec);
    }

    // Timestamp log
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char tsbuf[32];
    std::strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    std::cout << "[" << tsbuf << "] Converted " << zoneCount
              << " active zones → " << csvPath << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::string jsonPath = DEFAULT_JSON_PATH;
    std::string csvPath  = DEFAULT_CSV_PATH;
    int intervalSec      = DEFAULT_INTERVAL_SECONDS;

    if (argc >= 2) jsonPath    = argv[1];
    if (argc >= 3) csvPath     = argv[2];
    if (argc >= 4) intervalSec = std::stoi(argv[3]);

    std::cout << "=== SD Trading V8 — ZonesToCsv ===\n";
    std::cout << "JSON : " << jsonPath  << "\n";
    std::cout << "CSV  : " << csvPath   << "\n";
    std::cout << "Interval: " << intervalSec << " seconds\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    // Run once immediately on startup, then loop
    while (true)
    {
        convertJsonToCsv(jsonPath, csvPath);
        std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
    }

    return 0;
}
