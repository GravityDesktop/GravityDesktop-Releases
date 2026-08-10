#include "Localization.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
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
        (Lowercase(preferred->second) == L"zh-cn" ||
         Lowercase(preferred->second) == L"en-us")) {
        result.languageMode = LanguageMode::Explicit;
        result.preferredLanguage = preferred->second;
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
    const std::wstring normalized = Lowercase(std::wstring(locale));
    if (normalized != L"zh-cn" && normalized != L"en-us") return false;
    preferences_.languageMode = LanguageMode::Explicit;
    preferences_.preferredLanguage = normalized == L"zh-cn" ? L"zh-CN" : L"en-US";
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
        ULONG size = 0;
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &size, nullptr,
                                        nullptr) != FALSE && size > 1) {
            std::vector<wchar_t> languages(size, L'\0');
            ULONG count = 0;
            if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &size,
                                            languages.data(), &count) != FALSE) {
                requested = Lowercase(languages.data());
            }
        }
    }
    if (requested == L"zh-cn" || requested == L"zh-sg" ||
        requested == L"zh-hans" || StartsWith(requested, L"zh-hans-")) {
        return L"zh-CN";
    }
    if (StartsWith(requested, L"en-")) return L"en-US";
    return L"en-US";
}

void Localization::ResolveAndLoadLanguage() {
    currentLocale_ = ResolveLanguage();
    active_ = english_;
    if (currentLocale_ == L"zh-CN") {
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
    return std::wstring(locale);
}

bool Localization::RunSelfTest() {
    Localization localization;
    localization.english_ = BuiltInEnglish();
    localization.active_ = localization.english_;
    return localization.Text("toolbar.create_core") == L"Create Core" &&
        localization.Format("settings.language.current",
                            {{L"language", L"English"}}) ==
            L"Current language: English" &&
        localization.Text("missing.key") == L"missing.key" &&
        DisplayName(L"zh-CN") == L"简体中文";
}

} // namespace gravity::i18n
