#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace gravitydesktop::zip {

enum class ResultCode {
    Success,
    Cancelled,
    SourceUnavailable,
    TargetUnavailable,
    TargetExists,
    InvalidTarget,
    ReadFailure,
    WriteFailure,
    ArchiveValidationFailed,
};

struct Request {
    std::filesystem::path sourcePath;
    std::filesystem::path targetZipPath;
    bool allowReplaceExisting = false;
    std::atomic_bool* cancellation = nullptr;
};

struct Result {
    ResultCode code = ResultCode::WriteFailure;
    std::wstring detail;
    std::uint64_t entryCount = 0;
    std::filesystem::path temporaryPath;

    [[nodiscard]] bool Succeeded() const noexcept {
        return code == ResultCode::Success;
    }
};

// Creates a standard, UTF-8, ZIP64-capable archive. This module neither knows
// nor changes Gravity Desktop workspace, relationship, or recovery state.
Result CreateArchive(const Request& request);

// Deterministic isolated verification for the ZIP writer. It creates and
// removes only a caller-provided TEMP root.
int RunSelfTest(const std::filesystem::path& testRoot, std::wstring* failure);

}  // namespace gravitydesktop::zip
