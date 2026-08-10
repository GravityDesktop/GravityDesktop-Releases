#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gravity::i18n {

enum class LanguageMode {
    Automatic,
    Explicit,
};

struct Preferences {
    LanguageMode languageMode = LanguageMode::Automatic;
    std::wstring preferredLanguage = L"zh-CN";
};

// Application-level localization. It deliberately has no dependency on the
// workspace state, layout, relationships, or any managed file transaction.
class Localization {
public:
    Localization() = default;

    bool Load();
    bool SetAutomatic();
    bool SetExplicitLanguage(std::wstring_view locale);

    [[nodiscard]] const Preferences& GetPreferences() const {
        return preferences_;
    }
    [[nodiscard]] const std::wstring& CurrentLocale() const {
        return currentLocale_;
    }
    [[nodiscard]] std::wstring Text(std::string_view key) const;
    [[nodiscard]] std::wstring Format(
        std::string_view key,
        const std::unordered_map<std::wstring, std::wstring>& values) const;

    [[nodiscard]] static std::wstring DisplayName(std::wstring_view locale);
    [[nodiscard]] static std::filesystem::path PreferencesPath();
    [[nodiscard]] static bool RunSelfTest();

private:
    bool SavePreferences() const;
    void ResolveAndLoadLanguage();
    [[nodiscard]] std::wstring ResolveLanguage() const;

    Preferences preferences_;
    std::wstring currentLocale_ = L"en-US";
    std::unordered_map<std::string, std::wstring> english_;
    std::unordered_map<std::string, std::wstring> active_;
};

} // namespace gravity::i18n
