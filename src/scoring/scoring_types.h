#ifndef SCORING_TYPES_H
#define SCORING_TYPES_H

namespace SDTrading {
namespace Core {

struct ZoneQualityScore {
    double zone_strength = 0.0;
    double touch_count = 0.0;
    double zone_age = 0.0;
    double zone_height = 0.0;
    double total = 0.0;
};

struct EntryValidationScore {
    double trend_alignment = 0.0;
    double momentum_state = 0.0;
    double trend_strength = 0.0;
    double volatility_context = 0.0;
    double total = 0.0;
};

} // namespace Core
} // namespace SDTrading

#endif // SCORING_TYPES_H
