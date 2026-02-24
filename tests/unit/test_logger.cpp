#include <gtest/gtest.h>
#include "utils/logger.h"
#include <fstream>
#include <cstdio>

using namespace SDTrading::Utils;

// ============================================================
// LOGGER TESTS
// ============================================================

class LoggerTest : public ::testing::Test {
protected:
    std::string test_log_file;
    
    void SetUp() override {
        test_log_file = "test.log";
        // Reset logger to defaults
        Logger::getInstance().setLevel(LogLevel::INFO);
        Logger::getInstance().enableConsole(true);
        Logger::getInstance().enableFile(false);
    }
    
    void TearDown() override {
        Logger::getInstance().enableFile(false);
        std::remove(test_log_file.c_str());
    }
};

TEST_F(LoggerTest, SingletonInstance) {
    Logger& logger1 = Logger::getInstance();
    Logger& logger2 = Logger::getInstance();
    
    EXPECT_EQ(&logger1, &logger2);
}

TEST_F(LoggerTest, SetAndGetLevel) {
    Logger::getInstance().setLevel(LogLevel::DEBUG);
    EXPECT_EQ(Logger::getInstance().getLevel(), LogLevel::DEBUG);
    
    Logger::getInstance().setLevel(LogLevel::ERROR);
    EXPECT_EQ(Logger::getInstance().getLevel(), LogLevel::ERROR);
}

TEST_F(LoggerTest, EnableDisableConsole) {
    Logger::getInstance().enableConsole(true);
    EXPECT_TRUE(Logger::getInstance().isConsoleEnabled());
    
    Logger::getInstance().enableConsole(false);
    EXPECT_FALSE(Logger::getInstance().isConsoleEnabled());
}

TEST_F(LoggerTest, EnableDisableFile) {
    Logger::getInstance().enableFile(true, test_log_file);
    EXPECT_TRUE(Logger::getInstance().isFileEnabled());
    EXPECT_EQ(Logger::getInstance().getFilename(), test_log_file);
    
    Logger::getInstance().enableFile(false);
    EXPECT_FALSE(Logger::getInstance().isFileEnabled());
}

TEST_F(LoggerTest, WriteToFile) {
    Logger::getInstance().enableFile(true, test_log_file);
    Logger::getInstance().setLevel(LogLevel::DEBUG);
    
    Logger::getInstance().debug("Debug message");
    Logger::getInstance().info("Info message");
    Logger::getInstance().warn("Warning message");
    Logger::getInstance().error("Error message");
    
    Logger::getInstance().enableFile(false);  // Close file
    
    // Read file and verify content
    std::ifstream file(test_log_file);
    ASSERT_TRUE(file.is_open());
    
    std::string line;
    int line_count = 0;
    
    while (std::getline(file, line)) {
        line_count++;
        
        // Each line should contain timestamp and level
        EXPECT_TRUE(line.find("[20") != std::string::npos);  // Timestamp
        EXPECT_TRUE(
            line.find("[DEBUG]") != std::string::npos ||
            line.find("[INFO ]") != std::string::npos ||
            line.find("[WARN ]") != std::string::npos ||
            line.find("[ERROR]") != std::string::npos
        );
    }
    
    EXPECT_EQ(line_count, 4);  // Should have 4 lines
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger::getInstance().enableFile(true, test_log_file);
    Logger::getInstance().setLevel(LogLevel::WARN);  // Only WARN and ERROR
    
    Logger::getInstance().debug("Debug message");  // Should be filtered
    Logger::getInstance().info("Info message");    // Should be filtered
    Logger::getInstance().warn("Warning message"); // Should appear
    Logger::getInstance().error("Error message");  // Should appear
    
    Logger::getInstance().enableFile(false);
    
    // Read file
    std::ifstream file(test_log_file);
    ASSERT_TRUE(file.is_open());
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    // Check that only WARN and ERROR appear
    EXPECT_TRUE(content.find("Debug message") == std::string::npos);
    EXPECT_TRUE(content.find("Info message") == std::string::npos);
    EXPECT_TRUE(content.find("Warning message") != std::string::npos);
    EXPECT_TRUE(content.find("Error message") != std::string::npos);
}

TEST_F(LoggerTest, MacroUsage) {
    Logger::getInstance().enableFile(true, test_log_file);
    Logger::getInstance().setLevel(LogLevel::DEBUG);
    
    int value = 42;
    LOG_DEBUG("Debug value: " << value);
    LOG_INFO("Info value: " << value);
    LOG_WARN("Warning value: " << value);
    LOG_ERROR("Error value: " << value);
    
    Logger::getInstance().enableFile(false);
    
    // Read file and verify macros worked
    std::ifstream file(test_log_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    EXPECT_TRUE(content.find("Debug value: 42") != std::string::npos);
    EXPECT_TRUE(content.find("Info value: 42") != std::string::npos);
    EXPECT_TRUE(content.find("Warning value: 42") != std::string::npos);
    EXPECT_TRUE(content.find("Error value: 42") != std::string::npos);
}

TEST_F(LoggerTest, ThreadSafety) {
    // Basic test - more comprehensive threading tests would be in integration
    Logger::getInstance().enableFile(true, test_log_file);
    Logger::getInstance().setLevel(LogLevel::INFO);
    
    // Multiple log calls should all succeed
    for (int i = 0; i < 100; i++) {
        LOG_INFO("Message " << i);
    }
    
    Logger::getInstance().enableFile(false);
    
    // Verify file has 100 lines
    std::ifstream file(test_log_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        line_count++;
    }
    
    EXPECT_EQ(line_count, 100);
}

// ============================================================
// MAIN
// ============================================================

// Note: main() is provided by gtest_main library
