#include "Localization.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace gravity::i18n {
namespace {

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           input.data(),
                                           static_cast<int>(input.size()),
                                           nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), result.data(),
                            length) <= 0) {
        return {};
    }
    return result;
}

std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           input.data(),
                                           static_cast<int>(input.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), result.data(),
                            length, nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

std::optional<std::string> ReadUtf8File(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input), {});
}

bool WriteUtf8Atomic(const std::filesystem::path& target,
                     const std::string& data) {
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return false;
    const std::filesystem::path temporary = target.wstring() + L".tmp";
    const std::filesystem::path backup = target.wstring() + L".bak";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool writtenAll = data.size() <= MAXDWORD &&
        WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                  nullptr) != FALSE && written == data.size() &&
        FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!writtenAll) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES &&
        ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(),
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE) {
        return true;
    }
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
        FALSE) {
        return true;
    }
    DeleteFileW(temporary.c_str());
    return false;
}

void SkipWhitespace(std::string_view source, std::size_t& position) {
    while (position < source.size() &&
           std::isspace(static_cast<unsigned char>(source[position])) != 0) {
        ++position;
    }
}

std::optional<std::string> ParseJsonString(std::string_view source,
                                            std::size_t& position) {
    SkipWhitespace(source, position);
    if (position >= source.size() || source[position] != '"') return std::nullopt;
    ++position;
    std::string output;
    while (position < source.size()) {
        const char value = source[position++];
        if (value == '"') return output;
        if (static_cast<unsigned char>(value) < 0x20) return std::nullopt;
        if (value != '\\') {
            output.push_back(value);
            continue;
        }
        if (position >= source.size()) return std::nullopt;
        const char escaped = source[position++];
        switch (escaped) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
            if (position + 4 > source.size()) return std::nullopt;
            unsigned int codePoint = 0;
            for (int index = 0; index < 4; ++index) {
                const char hex = source[position++];
                codePoint <<= 4;
                if (hex >= '0' && hex <= '9') codePoint |= hex - '0';
                else if (hex >= 'a' && hex <= 'f') codePoint |= hex - 'a' + 10;
                else if (hex >= 'A' && hex <= 'F') codePoint |= hex - 'A' + 10;
                else return std::nullopt;
            }
            wchar_t unicode = static_cast<wchar_t>(codePoint);
            output += WideToUtf8(std::wstring(1, unicode));
            break;
        }
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::unordered_map<std::string, std::wstring>>
ParseStringMap(std::string_view source) {
    std::size_t position = 0;
    SkipWhitespace(source, position);
    if (position >= source.size() || source[position++] != '{') return std::nullopt;
    std::unordered_map<std::string, std::wstring> result;
    for (;;) {
        SkipWhitespace(source, position);
        if (position < source.size() && source[position] == '}') {
            ++position;
            SkipWhitespace(source, position);
            return position == source.size()
                ? std::optional<std::unordered_map<std::string, std::wstring>>(
                      std::move(result))
                : std::nullopt;
        }
        const auto key = ParseJsonString(source, position);
        if (!key.has_value()) return std::nullopt;
        SkipWhitespace(source, position);
        if (position >= source.size() || source[position++] != ':') return std::nullopt;
        const auto value = ParseJsonString(source, position);
        if (!value.has_value()) return std::nullopt;
        const std::wstring wide = Utf8ToWide(*value);
        if (wide.empty() && !value->empty()) return std::nullopt;
        result[*key] = wide;
        SkipWhitespace(source, position);
        if (position >= source.size()) return std::nullopt;
        if (source[position] == '}') continue;
        if (source[position++] != ',') return std::nullopt;
    }
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                   });
    return value;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() &&
        value.substr(0, prefix.size()) == prefix;
}

std::wstring ResolveRequestedLanguage(std::wstring requested) {
    requested = Lowercase(std::move(requested));
    if (requested == L"zh-cn" || requested == L"zh-sg" ||
        requested == L"zh-hans" || StartsWith(requested, L"zh-hans-")) {
        return L"zh-CN";
    }
    if (StartsWith(requested, L"en-")) return L"en-US";
    if (requested == L"ja" || StartsWith(requested, L"ja-")) return L"ja-JP";
    if (requested == L"de" || StartsWith(requested, L"de-")) return L"de-DE";
    if (requested == L"es" || StartsWith(requested, L"es-")) return L"es-ES";
    if (requested == L"fr" || StartsWith(requested, L"fr-")) return L"fr-FR";
    return L"en-US";
}

bool IsSupportedLocale(std::wstring_view locale) {
    const std::wstring normalized = Lowercase(std::wstring(locale));
    return normalized == L"zh-cn" || normalized == L"en-us" ||
           normalized == L"ja-jp" || normalized == L"de-de" ||
           normalized == L"es-es" || normalized == L"fr-fr";
}

std::wstring CanonicalLocale(std::wstring_view locale) {
    const std::wstring normalized = Lowercase(std::wstring(locale));
    if (normalized == L"zh-cn") return L"zh-CN";
    if (normalized == L"ja-jp") return L"ja-JP";
    if (normalized == L"de-de") return L"de-DE";
    if (normalized == L"es-es") return L"es-ES";
    if (normalized == L"fr-fr") return L"fr-FR";
    return L"en-US";
}

std::wstring FirstWindowsUserLanguage() {
    DWORD bytes = 0;
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            L"Control Panel\\International\\User Profile",
            L"Languages", RRF_RT_REG_MULTI_SZ,
            nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) * 2) {
        return {};
    }
    std::vector<wchar_t> languages(
        bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            L"Control Panel\\International\\User Profile",
            L"Languages", RRF_RT_REG_MULTI_SZ,
            nullptr, languages.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return languages.front() == L'\0'
        ? std::wstring{} : std::wstring(languages.data());
}

std::filesystem::path ExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                  static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size() - 1) {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }
        path.resize(path.size() * 2);
    }
}

std::optional<std::unordered_map<std::string, std::wstring>>
LoadLocaleFile(std::wstring_view locale) {
    const std::filesystem::path path =
        ExecutableDirectory() / L"locales" / (std::wstring(locale) + L".json");
    const auto text = ReadUtf8File(path);
    if (!text.has_value()) return std::nullopt;
    return ParseStringMap(*text);
}

std::vector<std::wstring> ExtractPlaceholders(std::wstring_view text) {
    std::vector<std::wstring> placeholders;
    std::size_t position = 0;
    while ((position = text.find(L'{', position)) != std::wstring_view::npos) {
        const std::size_t end = text.find(L'}', position + 1);
        if (end == std::wstring_view::npos) break;
        placeholders.emplace_back(text.substr(position, end - position + 1));
        position = end + 1;
    }
    std::sort(placeholders.begin(), placeholders.end());
    placeholders.erase(
        std::unique(placeholders.begin(), placeholders.end()),
        placeholders.end());
    return placeholders;
}

bool ValidateLocaleFiles() {
    const auto english = LoadLocaleFile(L"en-US");
    if (!english.has_value() || english->empty()) return false;
    constexpr std::array<std::wstring_view, 5> locales{
        L"zh-CN", L"ja-JP", L"de-DE", L"es-ES", L"fr-FR"};
    for (const std::wstring_view locale : locales) {
        const auto translated = LoadLocaleFile(locale);
        if (!translated.has_value() ||
            translated->size() != english->size()) {
            return false;
        }
        for (const auto& [key, source] : *english) {
            const auto found = translated->find(key);
            if (found == translated->end() ||
                ExtractPlaceholders(source) !=
                    ExtractPlaceholders(found->second)) {
                return false;
            }
        }
    }
    return true;
}

bool ValidateSettingsLayoutAtSupportedDpis() {
    constexpr std::array<std::wstring_view, 6> locales{
        L"zh-CN", L"en-US", L"ja-JP", L"de-DE", L"es-ES", L"fr-FR"};
    constexpr std::array<int, 4> dpis{96, 120, 144, 192};
    struct ControlTextLimit {
        std::string_view key;
        int widthDip;
    };
    // Navigation and choice buttons must remain fully readable. Action buttons
    // intentionally support end ellipsis, while wrapped descriptions measure
    // their height dynamically in the settings window.
    constexpr std::array<ControlTextLimit, 7> navigationLimits{{
        {"settings.general", 208},
        {"settings.appearance", 208},
        {"settings.workspace", 208},
        {"settings.data", 208},
        {"settings.core_management", 208},
        {"settings.help", 208},
        {"settings.advanced", 208},
    }};
    constexpr std::array<std::string_view, 3> densityKeys{
        "settings.density.compact", "settings.density.standard",
        "settings.density.clear"};
    constexpr std::array<std::string_view, 4> animationKeys{
        "settings.animation.off", "settings.animation.simple",
        "settings.animation.standard", "settings.animation.soft"};
    constexpr std::array<std::string_view, 3> selectionKeys{
        "settings.selection.ripple", "settings.selection.once",
        "settings.selection.static"};

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) return false;
    bool valid = true;
    for (const std::wstring_view locale : locales) {
        const auto resource = LoadLocaleFile(locale);
        if (!resource.has_value()) {
            valid = false;
            break;
        }
        for (const int dpi : dpis) {
            HFONT font = CreateFontW(
                -MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI");
            if (font == nullptr) {
                valid = false;
                break;
            }
            HGDIOBJ previous = SelectObject(screen, font);
            const auto textFits = [&](std::string_view key, int widthDip) {
                const auto found = resource->find(std::string(key));
                if (found == resource->end()) return false;
                SIZE size{};
                return GetTextExtentPoint32W(
                           screen, found->second.c_str(),
                           static_cast<int>(found->second.size()), &size) != FALSE &&
                    size.cx + MulDiv(4, dpi, 96) <=
                        MulDiv(widthDip, dpi, 96);
            };
            for (const ControlTextLimit& limit : navigationLimits) {
                if (!textFits(limit.key, limit.widthDip)) {
                    valid = false;
                    break;
                }
            }
            const bool chinese = locale == L"zh-CN";
            const int densityWidth = chinese ? 82 : 118;
            const int animationWidth = chinese ? 70 : 88;
            for (const std::string_view key : densityKeys) {
                valid = valid && textFits(key, densityWidth);
            }
            for (const std::string_view key : animationKeys) {
                valid = valid && textFits(key, animationWidth);
            }
            if (chinese) {
                valid = valid && textFits(selectionKeys[0], 116) &&
                    textFits(selectionKeys[1], 142) &&
                    textFits(selectionKeys[2], 116);
            } else {
                for (const std::string_view key : selectionKeys) {
                    valid = valid && textFits(key, 390);
                }
            }
            // Verify the actual button grids still fit the fixed content rail.
            constexpr int contentRightDip = 406;
            valid = valid &&
                16 + 3 * densityWidth + 2 * 10 <= contentRightDip &&
                16 + 4 * animationWidth + 3 * 8 <= contentRightDip;
            SelectObject(screen, previous);
            DeleteObject(font);
            if (!valid) break;
        }
        if (!valid) break;
    }
    ReleaseDC(nullptr, screen);
    return valid;
}

std::unordered_map<std::string, std::wstring> BuiltInEnglish() {
    return {
        {"app.title", L"Gravity Desktop"},
        {"settings.title", L"Gravity Desktop Settings"},
        {"settings.general", L"General"},
        {"settings.language", L"Language"},
        {"settings.language.follow_windows", L"Follow Windows language"},
        {"settings.language.automatic", L"Automatic"},
        {"settings.language.current", L"Current language: {language}"},
        {"settings.autostart", L"Start Gravity Desktop when I sign in to Windows"},
        {"settings.autostart.current_user", L"This setting affects only the current Windows user."},
        {"settings.autostart.repair", L"The startup entry points to an old path. Turn it on again to repair it."},
        {"settings.autostart.disabled_by_user", L"Windows has disabled this startup app. Re-enable Gravity Desktop in Windows Startup apps."},
        {"settings.autostart.disabled_by_policy", L"A Windows policy has disabled this startup app. Contact your device administrator."},
        {"status.autostart_disabled_by_user", L"Windows Startup apps has disabled Gravity Desktop. Re-enable it there to use sign-in startup."},
        {"status.autostart_disabled_by_policy", L"A Windows policy has disabled Gravity Desktop startup."},
        {"settings.restart_required", L"Some interfaces will update after Gravity Desktop restarts."},
        {"common.later", L"Later"},
        {"common.restart_now", L"Restart now"},
        {"toolbar.create_core", L"Create Core"},
        {"toolbar.undo", L"Undo"},
        {"toolbar.organize", L"Organize workspace"},
        {"toolbar.settings", L"Settings"},
        {"toolbar.hide_to_tray", L"Hide to tray"},
        {"toolbar.fullscreen", L"Full-screen work mode"},
        {"toolbar.exit", L"Exit"},
        {"core.default_name", L"File Core {index}"},
        {"tray.show", L"Show Gravity Desktop"},
        {"tray.hide", L"Hide Gravity Desktop"},
        {"tray.pause", L"Pause desktop interaction"},
        {"tray.resume", L"Resume desktop interaction"},
        {"tray.settings", L"Settings..."},
        {"tray.help", L"Help and tutorial"},
        {"tray.exit", L"Exit Gravity Desktop"},
        {"recovery.title", L"Gravity Desktop Emergency Recovery"},
        {"recovery.restore", L"Restore"},
        {"recovery.cancel", L"Cancel"},
        {"recovery.confirm", L"Confirm"},
        {"recovery.error", L"Error"},
        {"recovery.success", L"Success"},
        {"recovery.file_conflict", L"File conflict"},
        {"recovery.retry", L"Retry"},
        {"recovery.skip", L"Skip"},
        {"recovery.manifest_error", L"The recovery manifest could not be read. No files were moved.\n\nMake sure the Gravity Desktop library is still intact."},
        {"recovery.no_files", L"No managed files need to be restored."},
        {"recovery.desktop_error", L"The current Windows Desktop location could not be read."},
        {"recovery.already_restored", L"All managed files have already been restored to the Windows Desktop."},
        {"recovery.prompt", L"Found {count} file(s) managed by Gravity Desktop.\n\nRestore them to the Windows Desktop?\nCore relationship records will be kept and can be imported again later."},
        {"recovery.journal_error", L"The recovery transaction log could not be written. No files were moved."},
        {"recovery.result.complete", L"Recovery complete"},
        {"recovery.result.incomplete", L"Recovery incomplete"},
        {"recovery.result.restored", L"Restored this time: {count}"},
        {"recovery.result.already", L"Previously restored: {count}"},
        {"recovery.result.conflicts", L"Conflicts: {count}"},
        {"recovery.result.missing", L"Source files missing: {count}"},
        {"recovery.result.failed", L"Restore failures: {count}"},
        {"recovery.main_running", L"Gravity Desktop is still running. Exit it safely before emergency recovery.\nNo files were moved."},
    };
}

Preferences ParsePreferences(const std::string& text) {
    Preferences result;
    const auto map = ParseStringMap(text);
    if (!map.has_value()) return result;
    const auto mode = map->find("languageMode");
    const auto preferred = map->find("preferredLanguage");
    if (mode != map->end() && mode->second == L"explicit" &&
        preferred != map->end() &&
        IsSupportedLocale(preferred->second)) {
        result.languageMode = LanguageMode::Explicit;
        result.preferredLanguage = CanonicalLocale(preferred->second);
    }
    return result;
}

std::string EscapeJson(const std::wstring& input) {
    std::ostringstream output;
    output << '"';
    for (const char value : WideToUtf8(input)) {
        switch (value) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default: output << value; break;
        }
    }
    output << '"';
    return output.str();
}

} // namespace

std::filesystem::path Localization::PreferencesPath() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                    nullptr, &local))) {
        return std::filesystem::current_path() / L"preferences.json";
    }
    const std::filesystem::path path(local);
    CoTaskMemFree(local);
    return path / L"GravityDesktop" / L"preferences.json";
}

bool Localization::Load() {
    preferences_ = {};
    if (const auto text = ReadUtf8File(PreferencesPath()); text.has_value()) {
        preferences_ = ParsePreferences(*text);
    }
    english_ = BuiltInEnglish();
    if (const auto pack = LoadLocaleFile(L"en-US"); pack.has_value()) {
        for (const auto& [key, value] : *pack) {
            english_[key] = value;
        }
    }
    ResolveAndLoadLanguage();
    return true;
}

bool Localization::SetAutomatic() {
    preferences_.languageMode = LanguageMode::Automatic;
    if (!SavePreferences()) return false;
    ResolveAndLoadLanguage();
    return true;
}

bool Localization::SetExplicitLanguage(std::wstring_view locale) {
    if (!IsSupportedLocale(locale)) return false;
    preferences_.languageMode = LanguageMode::Explicit;
    preferences_.preferredLanguage = CanonicalLocale(locale);
    if (!SavePreferences()) return false;
    ResolveAndLoadLanguage();
    return true;
}

bool Localization::SavePreferences() const {
    const std::string mode = preferences_.languageMode == LanguageMode::Automatic
        ? "automatic" : "explicit";
    const std::string serialized = "{\n  \"languageMode\": \"" + mode +
        "\",\n  \"preferredLanguage\": " +
        EscapeJson(preferences_.preferredLanguage) + "\n}\n";
    return WriteUtf8Atomic(PreferencesPath(), serialized);
}

std::wstring Localization::ResolveLanguage() const {
    std::wstring requested;
    if (preferences_.languageMode == LanguageMode::Explicit) {
        requested = Lowercase(preferences_.preferredLanguage);
    } else {
        requested = Lowercase(FirstWindowsUserLanguage());
        if (requested.empty()) {
            ULONG size = 0;
            if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &size, nullptr,
                                            nullptr) != FALSE && size > 1) {
                std::vector<wchar_t> languages(size, L'\0');
                ULONG count = 0;
                if (GetUserPreferredUILanguages(
                        MUI_LANGUAGE_NAME, &size,
                        languages.data(), &count) != FALSE) {
                    requested = Lowercase(languages.data());
                }
            }
        }
        if (requested.empty()) {
            wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
            if (GetUserDefaultLocaleName(
                    localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
                requested = Lowercase(localeName);
            }
        }
    }
    return ResolveRequestedLanguage(std::move(requested));
}

void Localization::ResolveAndLoadLanguage() {
    currentLocale_ = ResolveLanguage();
    active_ = english_;
    if (currentLocale_ != L"en-US") {
        const auto pack = LoadLocaleFile(currentLocale_);
        if (!pack.has_value()) {
            currentLocale_ = L"en-US";
            return;
        }
        for (const auto& [key, value] : *pack) {
            active_[key] = value;
        }
    }
}

std::wstring Localization::Text(std::string_view key) const {
    const std::string lookup(key);
    if (const auto found = active_.find(lookup); found != active_.end()) {
        return found->second;
    }
    if (const auto fallback = english_.find(lookup); fallback != english_.end()) {
        return fallback->second;
    }
    return std::wstring(key.begin(), key.end());
}

std::wstring Localization::Format(
    std::string_view key,
    const std::unordered_map<std::wstring, std::wstring>& values) const {
    std::wstring result = Text(key);
    for (const auto& [name, value] : values) {
        const std::wstring marker = L"{" + name + L"}";
        std::size_t position = 0;
        while ((position = result.find(marker, position)) != std::wstring::npos) {
            result.replace(position, marker.size(), value);
            position += value.size();
        }
    }
    return result;
}

std::wstring Localization::DisplayName(std::wstring_view locale) {
    if (locale == L"zh-CN") return L"简体中文";
    if (locale == L"en-US") return L"English";
    if (locale == L"ja-JP") return L"日本語";
    if (locale == L"de-DE") return L"Deutsch";
    if (locale == L"es-ES") return L"Español";
    if (locale == L"fr-FR") return L"Français";
    return std::wstring(locale);
}

bool Localization::RunSelfTest() {
    Localization localization;
    localization.english_ = BuiltInEnglish();
    localization.active_ = localization.english_;
    const auto parsed = ParseStringMap(
        "{\"text\":\"Fran\\u00e7ais \\u00fc \\u65e5\\u672c\\u8a9e\"}");
    return localization.Text("toolbar.create_core") == L"Create Core" &&
        localization.Format("settings.language.current",
                            {{L"language", L"English"}}) ==
            L"Current language: English" &&
        localization.Text("missing.key") == L"missing.key" &&
        DisplayName(L"zh-CN") == L"简体中文" &&
        ResolveRequestedLanguage(L"zh-SG") == L"zh-CN" &&
        ResolveRequestedLanguage(L"en-GB") == L"en-US" &&
        ResolveRequestedLanguage(L"zh-Hant") == L"en-US" &&
        ResolveRequestedLanguage(L"ja-JP") == L"ja-JP" &&
        ResolveRequestedLanguage(L"de-AT") == L"de-DE" &&
        ResolveRequestedLanguage(L"es-MX") == L"es-ES" &&
        ResolveRequestedLanguage(L"fr-CA") == L"fr-FR" &&
        parsed.has_value() && parsed->at("text") == L"Français ü 日本語" &&
        ValidateLocaleFiles() &&
        ValidateSettingsLayoutAtSupportedDpis();
}

} // namespace gravity::i18n
