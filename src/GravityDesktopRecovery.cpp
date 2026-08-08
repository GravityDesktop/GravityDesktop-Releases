#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr wchar_t kRecoveryMutex[] =
    L"Local\\GravityDesktop.EmergencyRecovery.9D19E743";
constexpr wchar_t kMainMutex[] =
    L"Local\\GravityDesktop.SingleInstance.9D19E743";
constexpr wchar_t kTitle[] = L"引力桌面紧急恢复";

struct ManagedEntry {
    std::uint64_t id = 0;
    std::wstring source;
    std::wstring originalPath;
};

struct RecoveryResult {
    int restored = 0;
    int conflicts = 0;
    int missing = 0;
    int failed = 0;
    bool cancelled = false;
};

struct PreviousRecoveryEntry {
    std::filesystem::path destination;
    std::wstring state;
};

std::optional<std::string> ReadUtf8File(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(stream), {});
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
    const bool writtenAll =
        data.size() <= MAXDWORD &&
        WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                  nullptr) != FALSE &&
        written == data.size() && FlushFileBuffers(file) != FALSE;
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

std::string EscapeJson(const std::wstring& value) {
    std::ostringstream output;
    output << '"';
    for (const char character : WideToUtf8(value)) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                output << "\\u00" << std::hex << std::uppercase
                       << static_cast<int>(static_cast<unsigned char>(character))
                       << std::dec;
            } else {
                output << character;
            }
        }
    }
    output << '"';
    return output.str();
}

bool FileExists(const std::filesystem::path& path) {
    return !path.empty() && GetFileAttributesW(path.c_str()) !=
        INVALID_FILE_ATTRIBUTES;
}

std::wstring NormalizePath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path,
                                                                           error);
    if (error) normalized = path.lexically_normal();
    std::wstring result = normalized.wstring();
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t value) {
                       return static_cast<wchar_t>(towlower(value));
                   });
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) {
        result.pop_back();
    }
    return result;
}

bool IsPathInside(const std::filesystem::path& path,
                  const std::filesystem::path& root) {
    const std::wstring target = NormalizePath(path);
    std::wstring base = NormalizePath(root);
    if (target.empty() || base.empty()) return false;
    if (target == base) return false;
    if (base.back() != L'\\') base += L'\\';
    return target.rfind(base, 0) == 0;
}

std::optional<std::filesystem::path> CurrentDesktopPath() {
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT,
                                    nullptr, &desktop))) {
        return std::nullopt;
    }
    std::filesystem::path value(desktop);
    CoTaskMemFree(desktop);
    return value;
}

std::optional<std::filesystem::path> DefaultLibraryPath() {
    PWSTR documents = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT,
                                    nullptr, &documents))) {
        return std::nullopt;
    }
    std::filesystem::path value = std::filesystem::path(documents) /
        L"Gravity Desktop Files";
    CoTaskMemFree(documents);
    return value;
}

std::filesystem::path RunningLibraryPath() {
    std::wstring buffer(32768, L'\0');
    const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
    if (written != 0 && written < buffer.size()) {
        buffer.resize(written);
        const std::filesystem::path executable(buffer);
        // A durable copy lives in <library>\\恢复工具\\GravityDesktopRecovery.exe.
        if (executable.parent_path().filename() == L"恢复工具") {
            return executable.parent_path().parent_path();
        }
    }
    const auto fallback = DefaultLibraryPath();
    return fallback.value_or(std::filesystem::current_path() /
                             L"Gravity Desktop Files");
}

std::size_t SkipWhitespace(const std::string& text, std::size_t position) {
    while (position < text.size() &&
           (text[position] == ' ' || text[position] == '\t' ||
            text[position] == '\r' || text[position] == '\n')) {
        ++position;
    }
    return position;
}

bool ReadJsonString(const std::string& text, std::size_t& position,
                    std::string& value) {
    position = SkipWhitespace(text, position);
    if (position >= text.size() || text[position] != '"') return false;
    ++position;
    value.clear();
    while (position < text.size()) {
        const char character = text[position++];
        if (character == '"') return true;
        if (character != '\\') {
            value += character;
            continue;
        }
        if (position >= text.size()) return false;
        const char escaped = text[position++];
        switch (escaped) {
        case '"': value += '"'; break;
        case '\\': value += '\\'; break;
        case '/': value += '/'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        // The main program writes UTF-8, not escaped non-ASCII paths. Reject
        // uncommon unicode escapes rather than silently turning a path wrong.
        default: return false;
        }
    }
    return false;
}

std::optional<std::size_t> FindDirectMember(const std::string& object,
                                            const std::string& key) {
    bool string = false;
    bool escape = false;
    int depth = 0;
    for (std::size_t index = 0; index < object.size(); ++index) {
        const char character = object[index];
        if (string) {
            if (escape) escape = false;
            else if (character == '\\') escape = true;
            else if (character == '"') string = false;
            continue;
        }
        if (character == '"') {
            if (depth == 1 && object.compare(index, key.size(), key) == 0) {
                return index;
            }
            string = true;
        } else if (character == '{' || character == '[') {
            ++depth;
        } else if ((character == '}' || character == ']') && depth > 0) {
            --depth;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ReadStringMember(const std::string& object,
                                             const char* name) {
    const std::string key = std::string("\"") + name + "\"";
    const auto keyPosition = FindDirectMember(object, key);
    if (!keyPosition) return std::nullopt;
    std::size_t position = SkipWhitespace(object, *keyPosition + key.size());
    if (position >= object.size() || object[position++] != ':') return std::nullopt;
    std::string value;
    if (!ReadJsonString(object, position, value)) return std::nullopt;
    return value;
}

std::optional<std::uint64_t> ReadNumberMember(const std::string& object,
                                               const char* name) {
    const std::string key = std::string("\"") + name + "\"";
    const auto keyPosition = FindDirectMember(object, key);
    if (!keyPosition) return std::nullopt;
    std::size_t position = SkipWhitespace(object, *keyPosition + key.size());
    if (position >= object.size() || object[position++] != ':') return std::nullopt;
    position = SkipWhitespace(object, position);
    const std::size_t start = position;
    while (position < object.size() && object[position] >= '0' &&
           object[position] <= '9') ++position;
    if (position == start) return std::nullopt;
    try {
        return std::stoull(object.substr(start, position - start));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> JsonObjects(const std::string& text) {
    std::vector<std::string> objects;
    std::vector<std::size_t> starts;
    bool string = false;
    bool escape = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (string) {
            if (escape) escape = false;
            else if (character == '\\') escape = true;
            else if (character == '"') string = false;
            continue;
        }
        if (character == '"') { string = true; continue; }
        if (character == '{') {
            starts.push_back(index);
        } else if (character == '}' && !starts.empty()) {
            const std::size_t start = starts.back();
            starts.pop_back();
            objects.push_back(text.substr(start, index - start + 1));
        }
    }
    return objects;
}

bool ReadManagedEntries(const std::filesystem::path& manifest,
                        std::vector<ManagedEntry>& entries) {
    const auto text = ReadUtf8File(manifest);
    if (!text) return false;
    std::set<std::uint64_t> ids;
    for (const std::string& object : JsonObjects(*text)) {
        const auto storage = ReadStringMember(object, "storage_mode");
        if (!storage || *storage != "managed") continue;
        const auto id = ReadNumberMember(object, "file_stable_id");
        const auto managed = ReadStringMember(object, "managed_path");
        const auto original = ReadStringMember(object, "original_path");
        if (!id || *id == 0 || !managed || managed->empty() || !original ||
            !ids.insert(*id).second) {
            return false;
        }
        const std::wstring source = Utf8ToWide(*managed);
        const std::wstring originalPath = Utf8ToWide(*original);
        if (source.empty() || originalPath.empty()) return false;
        entries.push_back({*id, source, originalPath});
    }
    return true;
}

std::map<std::wstring, PreviousRecoveryEntry> ReadPreviousRecoveryJournal(
    const std::filesystem::path& journalPath) {
    std::map<std::wstring, PreviousRecoveryEntry> previous;
    const auto text = ReadUtf8File(journalPath);
    if (!text) return previous;
    for (const std::string& object : JsonObjects(*text)) {
        const auto source = ReadStringMember(object, "source_path");
        const auto destination = ReadStringMember(object, "destination_path");
        const auto state = ReadStringMember(object, "state");
        if (!source || !destination || !state || source->empty() ||
            destination->empty()) {
            continue;
        }
        const std::wstring sourcePath = Utf8ToWide(*source);
        const std::wstring destinationPath = Utf8ToWide(*destination);
        const std::wstring stateValue = Utf8ToWide(*state);
        if (!sourcePath.empty() && !destinationPath.empty() &&
            !stateValue.empty()) {
            previous.emplace(NormalizePath(sourcePath),
                             PreviousRecoveryEntry{
                                 std::filesystem::path(destinationPath),
                                 stateValue});
        }
    }
    return previous;
}

std::filesystem::path AvailableRestoreDestination(
    const std::filesystem::path& desktop, const std::filesystem::path& name) {
    const std::filesystem::path requested = desktop / name.filename();
    if (!FileExists(requested)) return requested;
    const std::wstring stem = name.stem().wstring();
    const std::wstring extension = name.extension().wstring();
    for (int index = 2; index <= 9999; ++index) {
        const std::filesystem::path candidate = desktop /
            (stem + L"（引力桌面恢复 " + std::to_wstring(index) + L"）" +
             extension);
        if (!FileExists(candidate)) return candidate;
    }
    return {};
}

bool MoveWithoutReplacement(const std::filesystem::path& source,
                            const std::filesystem::path& destination) {
    if (!FileExists(source) || FileExists(destination)) return false;
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    return MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_WRITE_THROUGH) != FALSE;
}

std::string BuildJournal(const std::vector<std::tuple<ManagedEntry,
                                                       std::filesystem::path,
                                                       std::wstring>>& items) {
    std::ostringstream output;
    output << "{\"schema_version\":1,\"entries\":[";
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& [entry, destination, status] = items[index];
        if (index != 0) output << ',';
        output << "{\"file_stable_id\":" << entry.id
               << ",\"source_path\":" << EscapeJson(entry.source)
               << ",\"destination_path\":" << EscapeJson(destination.wstring())
               << ",\"state\":" << EscapeJson(status) << '}';
    }
    output << "]}\n";
    return output.str();
}

std::wstring ResultText(const RecoveryResult& result) {
    std::wostringstream output;
    output << L"恢复完成\n\n成功恢复：" << result.restored
           << L"\n存在冲突：" << result.conflicts
           << L"\n源文件缺失：" << result.missing
           << L"\n恢复失败：" << result.failed;
    return output.str();
}

bool RunRecovery(const std::filesystem::path& library, bool interactive,
                 RecoveryResult& result) {
    const std::filesystem::path manifest = library / L"恢复清单.json";
    std::vector<ManagedEntry> entries;
    if (!ReadManagedEntries(manifest, entries)) {
        if (interactive) {
            MessageBoxW(nullptr,
                        L"无法读取恢复清单。未移动任何文件。\n\n"
                        L"请确认“引力桌面文件库”仍完整保留。",
                        kTitle, MB_OK | MB_ICONERROR);
        }
        return false;
    }
    if (entries.empty()) {
        if (interactive) {
            MessageBoxW(nullptr, L"没有发现需要恢复的托管文件。", kTitle,
                        MB_OK | MB_ICONINFORMATION);
        }
        return true;
    }
    const auto desktop = CurrentDesktopPath();
    if (!desktop) {
        if (interactive) MessageBoxW(nullptr, L"无法读取当前 Windows 桌面路径。",
                                     kTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    if (interactive) {
        const std::wstring prompt = L"发现 " + std::to_wstring(entries.size()) +
            L" 个由引力桌面托管的文件。\n\n"
            L"是否将它们恢复到 Windows 桌面？\n"
            L"核心关系记录会继续保留，以后可以重新导入。";
        if (MessageBoxW(nullptr, prompt.c_str(), kTitle,
                        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
            return true;
        }
    }

    std::vector<std::tuple<ManagedEntry, std::filesystem::path, std::wstring>> journal;
    journal.reserve(entries.size());
    const std::filesystem::path journalPath = library / L"恢复事务.json";
    const auto previous = ReadPreviousRecoveryJournal(journalPath);
    for (const ManagedEntry& entry : entries) {
        const std::filesystem::path source(entry.source);
        if (!IsPathInside(source, library)) {
            ++result.failed;
            journal.emplace_back(entry, std::filesystem::path{}, L"rejected");
            continue;
        }
        if (const auto found = previous.find(NormalizePath(source));
            found != previous.end() && FileExists(found->second.destination)) {
            // A prior recovery reached the destination before a crash.  Do not
            // manufacture a second desktop copy when the tool is restarted.
            journal.emplace_back(entry, found->second.destination, L"restored");
            ++result.restored;
            continue;
        }
        const std::filesystem::path original(entry.originalPath);
        std::filesystem::path destination;
        if (const auto found = previous.find(NormalizePath(source));
            found != previous.end() && !FileExists(found->second.destination)) {
            destination = found->second.destination;
        } else {
            destination = AvailableRestoreDestination(
                *desktop, original.empty() ? source.filename() : original.filename());
        }
        if (destination.empty()) {
            ++result.conflicts;
            journal.emplace_back(entry, std::filesystem::path{}, L"conflict");
            continue;
        }
        journal.emplace_back(entry, destination, L"pending");
    }

    if (!WriteUtf8Atomic(journalPath, BuildJournal(journal))) {
        if (interactive) MessageBoxW(nullptr, L"无法写入恢复事务日志；未移动任何文件。",
                                     kTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    for (auto& [entry, destination, status] : journal) {
        if (status != L"pending") continue;
        const std::filesystem::path source(entry.source);
        if (!FileExists(source)) {
            status = L"missing";
            ++result.missing;
        } else if (MoveWithoutReplacement(source, destination)) {
            status = L"restored";
            ++result.restored;
        } else {
            status = L"failed";
            ++result.failed;
        }
        (void)WriteUtf8Atomic(journalPath, BuildJournal(journal));
    }

    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    const std::filesystem::path report = library /
        (L"恢复结果-" + std::to_wstring(static_cast<long long>(now)) + L".txt");
    (void)WriteUtf8Atomic(report, WideToUtf8(ResultText(result)));
    if (interactive) MessageBoxW(nullptr, ResultText(result).c_str(), kTitle,
                                 MB_OK | MB_ICONINFORMATION);
    return true;
}

bool RunSelfTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        (L"GravityDesktopRecoveryTest-" +
         std::to_wstring(static_cast<unsigned long long>(GetTickCount64())));
    const std::filesystem::path desktop = root / L"desktop";
    const std::filesystem::path source = root / L"已托管文件" / L"sample.txt";
    std::error_code error;
    std::filesystem::create_directories(desktop, error);
    std::filesystem::create_directories(source.parent_path(), error);
    if (error) return false;
    {
        std::ofstream test(source, std::ios::binary);
        test << "sample";
    }
    const std::string manifest = "{\"schema_version\":1,\"cores\":[{\"files\":["
        "{\"file_stable_id\":1,\"original_path\":" +
        EscapeJson((desktop / L"sample.txt").wstring()) +
        ",\"managed_path\":" + EscapeJson(source.wstring()) +
        ",\"storage_mode\":\"managed\"}]}]}";
    const bool written = WriteUtf8Atomic(root / L"恢复清单.json", manifest);
    std::vector<ManagedEntry> entries;
    const std::filesystem::path restored = desktop / L"sample.txt";
    const std::vector<std::tuple<ManagedEntry, std::filesystem::path,
                                 std::wstring>> journal{
        {ManagedEntry{1, source.wstring(), restored.wstring()}, restored,
         L"restored"}};
    const bool journalWritten = WriteUtf8Atomic(
        root / L"恢复事务.json", BuildJournal(journal));
    const auto previous = ReadPreviousRecoveryJournal(root / L"恢复事务.json");
    const bool parsed = written && journalWritten &&
        ReadManagedEntries(root / L"恢复清单.json", entries) &&
        entries.size() == 1 && entries.front().id == 1 &&
        previous.size() == 1 && previous.begin()->second.destination == restored &&
        previous.begin()->second.state == L"restored";
    std::filesystem::remove_all(root, error);
    return parsed;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    if (commandLine != nullptr && wcsstr(commandLine, L"--self-test") != nullptr) {
        return RunSelfTest() ? 0 : 2;
    }
    const HRESULT com = OleInitialize(nullptr);
    if (FAILED(com)) return 1;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kRecoveryMutex);
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex != nullptr) CloseHandle(mutex);
        OleUninitialize();
        return 0;
    }
    HANDLE main = OpenMutexW(SYNCHRONIZE, FALSE, kMainMutex);
    if (main != nullptr) {
        CloseHandle(main);
        MessageBoxW(nullptr,
                    L"引力桌面仍在运行。请先安全退出引力桌面，再执行紧急恢复。\n"
                    L"未移动任何文件。",
                    kTitle, MB_OK | MB_ICONWARNING);
        CloseHandle(mutex);
        OleUninitialize();
        return 0;
    }
    RecoveryResult result;
    (void)RunRecovery(RunningLibraryPath(), true, result);
    CloseHandle(mutex);
    OleUninitialize();
    return 0;
}
