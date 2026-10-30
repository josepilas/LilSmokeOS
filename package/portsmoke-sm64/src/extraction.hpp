#pragma once
#include "asset_pack.hpp"
#include <atomic>

namespace portsmoke::sm64 {
enum class ExtractionPhase { Checking,
    Extracting,
    Verifying,
    RemovingOriginal,
    Complete };
struct ExtractionStatus {
    std::atomic<std::uint32_t> completed { 0 }, extracted { 0 }, total { 0 };
    std::atomic<ExtractionPhase> phase { ExtractionPhase::Checking };
    std::atomic<bool> cancel { false }, done { false };
    // Written before done's release store; read only after an acquire load.
    std::string error;
    bool originalRemoved = false, usedCache = false;
};
const GameDefinition& allAssetDefinition();
const GameDefinition& allSegmentDefinition();
void extractGameData(const std::filesystem::path& original, const std::filesystem::path& cacheDirectory,
    ExtractionStatus& status);
void verifyGameData(const std::filesystem::path& cacheDirectory);
}
