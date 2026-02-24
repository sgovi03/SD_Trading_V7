#include <gtest/gtest.h>
#include "common_types.h"
#include "core/config_loader.h"
#include <fstream>
#include <cstdio>

using namespace SDTrading::Core;

// ============================================================
// CONFIG STRUCTURE TESTS
// ============================================================

TEST(Config, DefaultConstructor) {
    Config config;
    
    // Check default values
    EXPECT_EQ(config.starting_capital, 100000);
    EXPECT_EQ(config.risk_per_trade_pct, 1.0);
    EXPECT_EQ(config.consolidation_min_bars, 1);
    EXPECT_EQ(config.consolidation_max_bars, 10);
    EXPECT_EQ(config.atr_period, 14);
    EXPECT_TRUE(config.only_fresh_zones);
    EXPECT_TRUE(config.use_adaptive_entry);
}

TEST(Config, ValidateSuccess) {
    Config config;  // Default config should be valid
    EXPECT_TRUE(config.validate());
}

TEST(Config, ValidateFailureNegativeCapital) {
    Config config;
    config.starting_capital = -1000;
    EXPECT_FALSE(config.validate());
}

TEST(Config, ValidateFailureInvalidRisk) {
    Config config;
    config.risk_per_trade_pct = 150;  // > 100
    EXPECT_FALSE(config.validate());
}

TEST(Config, ValidateFailureInvalidConsolidationBars) {
    Config config;
    config.consolidation_min_bars = 10;
    config.consolidation_max_bars = 5;  // min > max
    EXPECT_FALSE(config.validate());
}

TEST(Config, ValidateFailureInvalidZoneStrength) {
    Config config;
    config.min_zone_strength = 150;  // > 100
    EXPECT_FALSE(config.validate());
}

TEST(Config, ValidateFailureInvalidLotSize) {
    Config config;
    config.lot_size = -10;
    EXPECT_FALSE(config.validate());
}

// ============================================================
// SCORING CONFIG TESTS
// ============================================================

TEST(ScoringConfig, DefaultConstructor) {
    ScoringConfig scoring;
    
    EXPECT_DOUBLE_EQ(scoring.weight_base_strength, 0.40);
    EXPECT_DOUBLE_EQ(scoring.weight_elite_bonus, 0.25);
    EXPECT_DOUBLE_EQ(scoring.weight_regime_alignment, 0.20);
    EXPECT_DOUBLE_EQ(scoring.weight_state_freshness, 0.10);
    EXPECT_DOUBLE_EQ(scoring.weight_rejection_confirmation, 0.05);
}

TEST(ScoringConfig, ValidateWeightsSumToOne) {
    ScoringConfig scoring;
    EXPECT_TRUE(scoring.validate());
}

TEST(ScoringConfig, ValidateFailureWeightsDontSumToOne) {
    ScoringConfig scoring;
    scoring.weight_base_strength = 0.50;  // Now sum > 1.0
    EXPECT_FALSE(scoring.validate());
}

TEST(ScoringConfig, ValidateFailureInvalidEntryScore) {
    ScoringConfig scoring;
    scoring.entry_minimum_score = 150;  // > 100
    EXPECT_FALSE(scoring.validate());
}

TEST(ScoringConfig, ValidateFailureInvalidRRRatio) {
    ScoringConfig scoring;
    scoring.rr_base_ratio = 5.0;
    scoring.rr_max_ratio = 2.0;  // max < base
    EXPECT_FALSE(scoring.validate());
}

// ============================================================
// CONFIG LOADER TESTS
// ============================================================

class ConfigLoaderTest : public ::testing::Test {
protected:
    std::string test_config_file;
    
    void SetUp() override {
        test_config_file = "test_config.txt";
    }
    
    void TearDown() override {
        std::remove(test_config_file.c_str());
    }
    
    void create_test_config() {
        std::ofstream file(test_config_file);
        file << "# Test configuration\n";
        file << "starting_capital=200000\n";
        file << "risk_per_trade_pct=2.0\n";
        file << "consolidation_min_bars=2\n";
        file << "consolidation_max_bars=15\n";
        file << "only_fresh_zones=false\n";
        file << "lot_size=75\n";
        file.close();
    }
};

TEST_F(ConfigLoaderTest, LoadFromFileSuccess) {
    create_test_config();
    
    Config config = ConfigLoader::load_from_file(test_config_file);
    
    EXPECT_EQ(config.starting_capital, 200000);
    EXPECT_EQ(config.risk_per_trade_pct, 2.0);
    EXPECT_EQ(config.consolidation_min_bars, 2);
    EXPECT_EQ(config.consolidation_max_bars, 15);
    EXPECT_FALSE(config.only_fresh_zones);
    EXPECT_EQ(config.lot_size, 75);
}

TEST_F(ConfigLoaderTest, LoadFromFileNotFound) {
    EXPECT_THROW(
        ConfigLoader::load_from_file("nonexistent_file.txt"),
        std::runtime_error
    );
}

TEST_F(ConfigLoaderTest, LoadWithComments) {
    std::ofstream file(test_config_file);
    file << "# This is a comment\n";
    file << "starting_capital=150000\n";
    file << "# Another comment\n";
    file << "risk_per_trade_pct=1.5\n";
    file << "\n";  // Empty line
    file << "lot_size=100\n";
    file.close();
    
    Config config = ConfigLoader::load_from_file(test_config_file);
    
    EXPECT_EQ(config.starting_capital, 150000);
    EXPECT_EQ(config.risk_per_trade_pct, 1.5);
    EXPECT_EQ(config.lot_size, 100);
}

TEST_F(ConfigLoaderTest, LoadWithBooleans) {
    std::ofstream file(test_config_file);
    file << "only_fresh_zones=true\n";
    file << "use_adaptive_entry=false\n";
    file << "close_eod=1\n";
    file << "use_trailing_stop=0\n";
    file.close();
    
    Config config = ConfigLoader::load_from_file(test_config_file);
    
    EXPECT_TRUE(config.only_fresh_zones);
    EXPECT_FALSE(config.use_adaptive_entry);
    EXPECT_TRUE(config.close_eod);
    EXPECT_FALSE(config.use_trailing_stop);
}

// ============================================================
// ZONE SCORE TESTS
// ============================================================

TEST(ZoneScore, DefaultConstructor) {
    ZoneScore score;
    
    EXPECT_EQ(score.base_strength_score, 0);
    EXPECT_EQ(score.elite_bonus_score, 0);
    EXPECT_EQ(score.swing_position_score, 0);
    EXPECT_EQ(score.total_score, 0);
    EXPECT_EQ(score.recommended_rr, 2.0);
}

TEST(ZoneScore, CalculateComposite) {
    ZoneScore score;
    score.base_strength_score = 25.0;
    score.elite_bonus_score = 20.0;
    score.swing_position_score = 15.0;
    score.regime_alignment_score = 10.0;
    score.state_freshness_score = 5.0;
    score.rejection_confirmation_score = 3.0;
    
    score.calculate_composite();
    
    EXPECT_DOUBLE_EQ(score.total_score, 78.0);
    EXPECT_DOUBLE_EQ(score.entry_aggressiveness, 0.78);
    EXPECT_FALSE(score.score_breakdown.empty());
}

// ============================================================
// MAIN
// ============================================================

// Note: main() is provided by gtest_main library
