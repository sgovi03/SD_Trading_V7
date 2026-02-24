# Analysis module CMakeLists.txt

set(ANALYSIS_SOURCES
    market_analyzer.cpp
)

# Export sources to parent
set(ANALYSIS_SOURCES ${ANALYSIS_SOURCES} PARENT_SCOPE)
