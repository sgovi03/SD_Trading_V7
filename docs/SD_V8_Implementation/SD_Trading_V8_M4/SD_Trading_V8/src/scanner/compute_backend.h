// ============================================================
// SD TRADING V8 - COMPUTE BACKEND INTERFACE
// src/scanner/compute_backend.h
// Milestone M4.2
//
// Abstract interface for the scoring compute layer.
// Implementations: CpuBackend (primary), CudaBackend (M7).
// ============================================================

#ifndef SDTRADING_COMPUTE_BACKEND_H
#define SDTRADING_COMPUTE_BACKEND_H

#include <vector>
#include <string>
#include "symbol_engine_context.h"

namespace SDTrading {
namespace Scanner {

class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;

    // Process one bar for a set of symbols.
    // Returns SignalResult per symbol (empty result if no signal).
    // Called once per bar close from ScannerOrchestrator.
    virtual std::vector<SignalResult> score_symbols(
        std::vector<SymbolEngineContext*>& contexts,
        const std::vector<Core::Bar>& bars    // one bar per context, same index
    ) = 0;

    virtual std::string name() const = 0;
    virtual bool is_available() const = 0;
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_COMPUTE_BACKEND_H
