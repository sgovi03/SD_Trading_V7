# Scoring module CMakeLists.txt

set(SCORING_SOURCES
    zone_scorer.cpp
    entry_decision_engine.cpp
    zone_quality_scorer.cpp
    entry_validation_scorer.cpp
)

# Export sources to parent
set(SCORING_SOURCES ${SCORING_SOURCES} PARENT_SCOPE)
