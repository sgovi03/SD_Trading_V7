#ifndef ENTRY_VALIDATION_SCORER_H
#define ENTRY_VALIDATION_SCORER_H

#include "scoring_types.h"
#include "common_types.h"

namespace SDTrading {
namespace Core {

class EntryValidationScorer {
public:
    explicit EntryValidationScorer(const Config& cfg);

    EntryValidationScore calculate(const Zone& zone,
                                   const std::vector<Bar>& bars,
                                   int current_index,
                                   double entry_price,
                                   double stop_loss) const;
    bool meets_threshold(double score) const;

private:
    const Config& config_;

    double calculate_trend_alignment_score(const Zone& zone,
                                           const std::vector<Bar>& bars,
                                           int current_index) const;
    double calculate_momentum_score(const Zone& zone,
                                    const std::vector<Bar>& bars,
                                    int current_index) const;
    double calculate_trend_strength_score(const std::vector<Bar>& bars,
                                          int current_index) const;
    double calculate_volatility_score(double entry_price,
                                      double stop_loss,
                                      const std::vector<Bar>& bars,
                                      int current_index) const;
};

} // namespace Core
} // namespace SDTrading

#endif // ENTRY_VALIDATION_SCORER_H
