#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <filesystem>

int main() {
    namespace fs = std::filesystem;
    
    std::cout << "=====================================" << std::endl;
    std::cout << "  LIVE ENGINE TIMING ANALYSIS" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << std::endl;
    
    // Timing Point 1: Program start
    auto t_start = std::chrono::high_resolution_clock::now();
    std::cout << "[START] Program initialization" << std::endl;
    
    // Timing Point 2: CSV file access
    std::string csv_path = "d:\\ClaudeAi\\Unified_Engines\\SD_Trading_V4_FIxing\\data\\live_data.csv";
    
    std::cout << std::endl;
    std::cout << "--- CSV FILE INFORMATION ---" << std::endl;
    
    if (fs::exists(csv_path)) {
        auto file_size = fs::file_size(csv_path);
        std::cout << "File size: " << file_size << " bytes (" << (file_size / 1024.0 / 1024.0) << " MB)" << std::endl;
        
        // Count lines
        std::ifstream file(csv_path);
        int line_count = 0;
        std::string line;
        
        auto t_file_open = std::chrono::high_resolution_clock::now();
        std::cout << "Opening CSV file..." << std::endl;
        
        if (file.is_open()) {
            auto t_read_start = std::chrono::high_resolution_clock::now();
            
            while (std::getline(file, line)) {
                line_count++;
            }
            
            auto t_read_end = std::chrono::high_resolution_clock::now();
            auto read_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_read_end - t_read_start).count();
            
            file.close();
            
            std::cout << "File read complete" << std::endl;
            std::cout << "Lines in file: " << line_count << std::endl;
            std::cout << "Time to read all lines: " << read_ms << "ms (" << (read_ms / 1000.0) << "s)" << std::endl;
            std::cout << "Average per line: " << (read_ms / (double)line_count) << "ms" << std::endl;
        } else {
            std::cout << "ERROR: Could not open file" << std::endl;
        }
    } else {
        std::cout << "ERROR: File not found: " << csv_path << std::endl;
    }
    
    auto t_end = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    
    std::cout << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "  TIMING RESULTS" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "Total execution time: " << total_ms << "ms (" << (total_ms / 1000.0) << "s)" << std::endl;
    std::cout << std::endl;
    
    std::cout << "INTERPRETATION:" << std::endl;
    std::cout << "- If CSV reading takes <100ms: network/broker delay is the culprit" << std::endl;
    std::cout << "- If CSV reading takes >1000ms: file I/O is inefficient" << std::endl;
    std::cout << "- Live engine loads 20k bars, so multiply file time by 2x (20k/11k)" << std::endl;
    std::cout << std::endl;
    
    return 0;
}
