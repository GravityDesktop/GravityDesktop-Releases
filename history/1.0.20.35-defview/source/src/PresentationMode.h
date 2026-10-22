#pragma once

// Production has one presentation path: the validated DefView child scene.
// The historical modes remain for explicit offline/LOCALTEST diagnostics only.
enum class PresentationMode { LegacyChild, TopmostDiagnostic, DesktopCandidate, DefViewChild };
inline PresentationMode gPresentationMode = PresentationMode::DesktopCandidate;

inline bool PresentationDiagnosticsAllowed(std::string_view channel, std::string_view build) {
    return channel == "LOCALTEST" && build.starts_with("117-019-PRESENTATION-V2-");
}

inline const char* PresentationModeName(PresentationMode mode) {
    switch (mode) {
    case PresentationMode::DefViewChild: return "DEFVIEW_CHILD";
    case PresentationMode::TopmostDiagnostic: return "TOPLEVEL_TOPMOST_DIAG";
    case PresentationMode::DesktopCandidate: return "TOPLEVEL_DESKTOP_CANDIDATE";
    default: return "LEGACY_CHILD";
    }
}

inline std::optional<PresentationMode> ParsePresentationMode(const std::string& text) {
    if (text == "LEGACY_CHILD") return PresentationMode::LegacyChild;
    if (text == "TOPLEVEL_TOPMOST_DIAG") return PresentationMode::TopmostDiagnostic;
    if (text == "TOPLEVEL_DESKTOP_CANDIDATE") return PresentationMode::DesktopCandidate;
    return std::nullopt;
}

inline bool PresentationV2() {
    return gPresentationMode != PresentationMode::LegacyChild;
}

inline bool DefViewPresentation() {
    return gPresentationMode == PresentationMode::DefViewChild;
}

inline bool LoadPresentationMode() {
    gPresentationMode = PresentationMode::DesktopCandidate;
    if (gravity::defview::kPresentationCompiled) {
        // The fixture build must still pass the complete isolation gate.
        // A production build keeps existing data paths and cannot silently
        // fall back to the old top-level presentation on a missing host.
        if constexpr (gravity::defview::kCompiled) {
            if (gravity::defview::isolatedRoot.empty()) return false;
        }
        gPresentationMode = PresentationMode::DefViewChild;
        return true;
    }
    if (!PresentationDiagnosticsAllowed(GRAVITY_BUILD_CHANNEL, GRAVITY_BUILD_ID))
        return true; // Do not even read a diagnostic override in production.
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        return false;
    const auto path = std::filesystem::path(local) / L"GravityDesktop" / L"presentation-v2.mode";
    CoTaskMemFree(local);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return !error;
    std::ifstream stream(path, std::ios::binary);
    std::string text;
    if (!stream || !std::getline(stream, text)) return false;
    if (!text.empty() && text.back() == '\r') text.pop_back();
    // Extra lines are rejected, not interpreted as alternative modes.
    if (stream.peek() != std::char_traits<char>::eof()) return false;
    const auto parsed = ParsePresentationMode(text);
    if (!parsed) return false;
    gPresentationMode = *parsed; // Assigned once, before any application HWND.
    return true;
}
