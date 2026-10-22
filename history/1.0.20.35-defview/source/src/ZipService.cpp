#include "ZipService.h"

#include <windows.h>
#include <objbase.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <optional>
#include <string_view>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

extern "C" {
#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"
}

namespace gravitydesktop::zip {
namespace {

constexpr DWORD kReparsePointAttribute = FILE_ATTRIBUTE_REPARSE_POINT;
constexpr std::size_t kCopyBufferSize = 1024 * 1024;

struct Entry {
    std::filesystem::path sourcePath;
    std::string archivePath;
    bool directory = false;
};

[[nodiscard]] bool IsCancelled(const Request& request) {
    return request.cancellation != nullptr &&
           request.cancellation->load(std::memory_order_relaxed);
}

[[nodiscard]] std::wstring FormatWin32Error(DWORD error) {
    PWSTR buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<PWSTR>(&buffer), 0, nullptr);
    std::wstring text = count != 0 && buffer != nullptr
        ? std::wstring(buffer, count) : L"Win32 error " + std::to_wstring(error);
    if (buffer != nullptr) LocalFree(buffer);
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    return text;
}

[[nodiscard]] std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

[[nodiscard]] std::wstring FileName(const std::filesystem::path& path) {
    std::wstring name = path.filename().wstring();
    while (!name.empty() && (name.back() == L'\\' || name.back() == L'/')) name.pop_back();
    return name.empty() ? L"Archive" : name;
}

[[nodiscard]] std::filesystem::path CreateTemporaryPath(
    const std::filesystem::path& target) {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t text[40]{};
    if (StringFromGUID2(guid, text, static_cast<int>(std::size(text))) <= 0) return {};
    std::wstring suffix(text);
    suffix.erase(std::remove(suffix.begin(), suffix.end(), L'{'), suffix.end());
    suffix.erase(std::remove(suffix.begin(), suffix.end(), L'}'), suffix.end());
    return target.parent_path() /
        (L"." + target.filename().wstring() + L".gravitydesktop-" + suffix + L".tmp");
}

[[nodiscard]] bool IsDirectory(DWORD attributes) {
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

[[nodiscard]] Result MakeResult(ResultCode code, std::wstring detail = {}) {
    Result result;
    result.code = code;
    result.detail = std::move(detail);
    return result;
}

[[nodiscard]] std::string ChildArchivePath(
    std::string_view parent, std::wstring_view child, bool directory) {
    std::string result(parent);
    if (!result.empty() && result.back() != '/') result.push_back('/');
    result += ToUtf8(child);
    if (directory && (result.empty() || result.back() != '/')) result.push_back('/');
    return result;
}

bool EnumerateDirectory(const Request& request, const std::filesystem::path& directory,
                        std::string_view archiveDirectory, std::vector<Entry>* entries,
                        Result* failure) {
    if (IsCancelled(request)) {
        *failure = MakeResult(ResultCode::Cancelled);
        return false;
    }

    const std::wstring pattern = (directory / L"*").wstring();
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        *failure = MakeResult(ResultCode::ReadFailure, FormatWin32Error(GetLastError()));
        return false;
    }

    bool completed = true;
    do {
        if (IsCancelled(request)) {
            *failure = MakeResult(ResultCode::Cancelled);
            completed = false;
            break;
        }
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        const auto childPath = directory / std::wstring(name);
        const bool childDirectory = IsDirectory(data.dwFileAttributes);
        // Reparse points are never followed or written into an archive. This
        // prevents a junction or link from escaping the selected tree.
        if ((data.dwFileAttributes & kReparsePointAttribute) != 0) continue;
        const std::string childArchive = ChildArchivePath(
            archiveDirectory, name, childDirectory);
        if (childArchive.empty()) {
            *failure = MakeResult(ResultCode::ReadFailure, L"Invalid UTF-8 file name");
            completed = false;
            break;
        }
        entries->push_back({childPath, childArchive, childDirectory});
        if (childDirectory && !EnumerateDirectory(
                request, childPath, childArchive, entries, failure)) {
            completed = false;
            break;
        }
    } while (FindNextFileW(find, &data) != FALSE);

    const DWORD lastError = GetLastError();
    FindClose(find);
    if (completed && lastError != ERROR_NO_MORE_FILES) {
        *failure = MakeResult(ResultCode::ReadFailure, FormatWin32Error(lastError));
        return false;
    }
    return completed;
}

[[nodiscard]] bool BuildEntries(const Request& request, std::vector<Entry>* entries,
                                Result* failure) {
    const DWORD attributes = GetFileAttributesW(request.sourcePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        *failure = MakeResult(ResultCode::SourceUnavailable, FormatWin32Error(GetLastError()));
        return false;
    }
    if ((attributes & kReparsePointAttribute) != 0) {
        *failure = MakeResult(ResultCode::SourceUnavailable, L"Reparse point sources are not supported");
        return false;
    }

    const std::wstring rootName = FileName(request.sourcePath);
    const std::string rootArchive = ToUtf8(rootName);
    if (rootArchive.empty()) {
        *failure = MakeResult(ResultCode::SourceUnavailable, L"Invalid UTF-8 source name");
        return false;
    }
    if (IsDirectory(attributes)) {
        const std::string directoryName = rootArchive + "/";
        entries->push_back({request.sourcePath, directoryName, true});
        return EnumerateDirectory(request, request.sourcePath, directoryName, entries, failure);
    }
    entries->push_back({request.sourcePath, rootArchive, false});
    return true;
}

[[nodiscard]] bool AddDirectory(void* writer, const Entry& entry) {
    mz_zip_file info{};
    info.filename = entry.archivePath.c_str();
    info.filename_size = static_cast<uint16_t>(entry.archivePath.size());
    info.flag = MZ_ZIP_FLAG_UTF8;
    info.compression_method = MZ_COMPRESS_METHOD_STORE;
    info.external_fa = FILE_ATTRIBUTE_DIRECTORY;
    info.zip64 = MZ_ZIP64_AUTO;
    return mz_zip_writer_entry_open(writer, &info) == MZ_OK &&
           mz_zip_writer_entry_close(writer) == MZ_OK;
}

[[nodiscard]] Result AddFile(void* writer, const Entry& entry, const Request& request) {
    HANDLE source = CreateFileW(entry.sourcePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        return MakeResult(ResultCode::ReadFailure, FormatWin32Error(GetLastError()));
    }

    mz_zip_file info{};
    info.filename = entry.archivePath.c_str();
    info.filename_size = static_cast<uint16_t>(entry.archivePath.size());
    info.flag = MZ_ZIP_FLAG_UTF8;
    info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    info.zip64 = MZ_ZIP64_AUTO;
    if (mz_zip_writer_entry_open(writer, &info) != MZ_OK) {
        CloseHandle(source);
        return MakeResult(ResultCode::WriteFailure, L"Unable to open ZIP entry");
    }

    // Keep the 1 MiB transfer buffer off the worker's stack. A Win32 thread
    // may have a small stack and a ZIP write must never overflow it.
    std::vector<std::byte> buffer(kCopyBufferSize);
    Result result = MakeResult(ResultCode::Success);
    for (;;) {
        if (IsCancelled(request)) {
            result = MakeResult(ResultCode::Cancelled);
            break;
        }
        DWORD bytesRead = 0;
        if (ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()),
                     &bytesRead, nullptr) == FALSE) {
            result = MakeResult(ResultCode::ReadFailure, FormatWin32Error(GetLastError()));
            break;
        }
        if (bytesRead == 0) break;
        if (mz_zip_writer_entry_write(writer, buffer.data(),
                                      static_cast<int32_t>(bytesRead)) !=
            static_cast<int32_t>(bytesRead)) {
            result = MakeResult(ResultCode::WriteFailure, L"Unable to write ZIP entry");
            break;
        }
    }
    CloseHandle(source);
    if (mz_zip_writer_entry_close(writer) != MZ_OK && result.Succeeded()) {
        result = MakeResult(ResultCode::WriteFailure, L"Unable to finalize ZIP entry");
    }
    return result;
}

[[nodiscard]] bool ValidateArchive(const std::filesystem::path& archive,
                                   std::size_t expectedEntries) {
    const std::string archiveUtf8 = ToUtf8(archive.wstring());
    if (archiveUtf8.empty()) return false;
    void* reader = mz_zip_reader_create();
    if (reader == nullptr) return false;
    bool valid = mz_zip_reader_open_file(reader, archiveUtf8.c_str()) == MZ_OK;
    std::size_t count = 0;
    if (valid) {
        int32_t result = mz_zip_reader_goto_first_entry(reader);
        while (result == MZ_OK) {
            ++count;
            if (mz_zip_reader_entry_open(reader) != MZ_OK) {
                valid = false;
                break;
            }
            std::array<std::byte, 64 * 1024> buffer{};
            for (;;) {
                const int32_t read = mz_zip_reader_entry_read(
                    reader, buffer.data(), static_cast<int32_t>(buffer.size()));
                if (read == 0) break;
                if (read < 0) { valid = false; break; }
            }
            const bool closed = mz_zip_reader_entry_close(reader) == MZ_OK;
            if (!valid || !closed) { valid = false; break; }
            result = mz_zip_reader_goto_next_entry(reader);
        }
        valid = valid && result == MZ_END_OF_LIST && count == expectedEntries;
    }
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    return valid;
}

void DeleteIfPresent(const std::filesystem::path& path) {
    if (!path.empty()) DeleteFileW(path.c_str());
}

bool WriteSelfTestFile(const std::filesystem::path& path,
                       std::string_view contents) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool result = WriteFile(handle, contents.data(),
        static_cast<DWORD>(contents.size()), &written, nullptr) != FALSE &&
        written == contents.size();
    CloseHandle(handle);
    return result;
}

bool HashSelfTestFile(const std::filesystem::path& path,
                      std::array<std::byte, 32>* digest) {
    if (digest == nullptr) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD returned = 0;
    bool success = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
            &returned, 0) == 0;
    std::vector<std::byte> object(objectBytes);
    if (success) success = BCryptCreateHash(algorithm, &hash,
        reinterpret_cast<PUCHAR>(object.data()), objectBytes, nullptr, 0, 0) == 0;
    std::array<std::byte, 64 * 1024> buffer{};
    while (success) {
        DWORD read = 0;
        if (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                     &read, nullptr) == FALSE) { success = false; break; }
        if (read == 0) break;
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), read, 0) != 0) {
            success = false;
            break;
        }
    }
    if (success) success = BCryptFinishHash(hash,
        reinterpret_cast<PUCHAR>(digest->data()), static_cast<ULONG>(digest->size()), 0) == 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return success;
}

bool ExtractSelfTestEntry(const std::filesystem::path& archive,
                          std::string_view archivePath,
                          const std::filesystem::path& destination) {
    const std::string archiveUtf8 = ToUtf8(archive.wstring());
    const std::string destinationUtf8 = ToUtf8(destination.wstring());
    if (archiveUtf8.empty() || destinationUtf8.empty()) return false;
    void* reader = mz_zip_reader_create();
    if (reader == nullptr) return false;
    const bool success = mz_zip_reader_open_file(reader, archiveUtf8.c_str()) == MZ_OK &&
        mz_zip_reader_locate_entry(reader, std::string(archivePath).c_str(), 0) == MZ_OK &&
        mz_zip_reader_entry_save_file(reader, destinationUtf8.c_str()) == MZ_OK;
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    return success;
}

std::filesystem::path ExtendedPath(const std::filesystem::path& path) {
    std::wstring value = path.wstring();
    if (value.rfind(L"\\\\?\\", 0) == 0) return path;
    const DWORD needed = GetFullPathNameW(value.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring absolute(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = GetFullPathNameW(value.c_str(), needed,
                                            absolute.data(), nullptr);
    if (written == 0 || written >= needed) return path;
    absolute.resize(written);
    return std::filesystem::path(L"\\\\?\\" + absolute);
}

}  // namespace

Result CreateArchive(const Request& request) {
    if (request.sourcePath.empty() || request.targetZipPath.empty()) {
        return MakeResult(ResultCode::InvalidTarget, L"Source and target are required");
    }
    if (IsCancelled(request)) return MakeResult(ResultCode::Cancelled);
    const auto parent = request.targetZipPath.parent_path();
    const DWORD parentAttributes = GetFileAttributesW(parent.c_str());
    if (!IsDirectory(parentAttributes)) {
        return MakeResult(ResultCode::TargetUnavailable, FormatWin32Error(GetLastError()));
    }
    const bool targetExists = GetFileAttributesW(request.targetZipPath.c_str()) !=
        INVALID_FILE_ATTRIBUTES;
    if (targetExists && !request.allowReplaceExisting) {
        return MakeResult(ResultCode::TargetExists);
    }

    std::vector<Entry> entries;
    Result failure;
    if (!BuildEntries(request, &entries, &failure)) return failure;
    const auto temporary = CreateTemporaryPath(request.targetZipPath);
    if (temporary.empty()) return MakeResult(ResultCode::TargetUnavailable, L"Unable to create temporary ZIP path");

    const std::string temporaryUtf8 = ToUtf8(temporary.wstring());
    if (temporaryUtf8.empty()) return MakeResult(ResultCode::TargetUnavailable, L"Invalid UTF-8 target path");
    void* writer = mz_zip_writer_create();
    if (writer == nullptr) return MakeResult(ResultCode::WriteFailure, L"Unable to create ZIP writer");

    Result result = MakeResult(ResultCode::Success);
    result.temporaryPath = temporary;
    if (mz_zip_writer_open_file(writer, temporaryUtf8.c_str(), 0, 0) != MZ_OK) {
        result = MakeResult(ResultCode::TargetUnavailable, L"Unable to create temporary ZIP");
        result.temporaryPath = temporary;
    } else {
        for (const Entry& entry : entries) {
            if (IsCancelled(request)) { result = MakeResult(ResultCode::Cancelled); break; }
            if (entry.directory) {
                if (!AddDirectory(writer, entry)) {
                    result = MakeResult(ResultCode::WriteFailure, L"Unable to write ZIP directory");
                    break;
                }
            } else {
                result = AddFile(writer, entry, request);
                if (!result.Succeeded()) break;
            }
        }
        const int32_t closeResult = mz_zip_writer_close(writer);
        if (result.Succeeded() && closeResult != MZ_OK) {
            result = MakeResult(ResultCode::WriteFailure, L"Unable to finalize ZIP");
        }
    }
    mz_zip_writer_delete(&writer);

    if (!result.Succeeded()) {
        DeleteIfPresent(temporary);
        result.temporaryPath = temporary;
        return result;
    }
    if (!ValidateArchive(temporary, entries.size())) {
        DeleteIfPresent(temporary);
        return MakeResult(ResultCode::ArchiveValidationFailed, L"ZIP validation failed");
    }

    DWORD moveFlags = MOVEFILE_WRITE_THROUGH;
    if (targetExists) moveFlags |= MOVEFILE_REPLACE_EXISTING;
    if (MoveFileExW(temporary.c_str(), request.targetZipPath.c_str(), moveFlags) == FALSE) {
        const std::wstring detail = FormatWin32Error(GetLastError());
        DeleteIfPresent(temporary);
        return MakeResult(ResultCode::WriteFailure, detail);
    }
    result.entryCount = entries.size();
    result.temporaryPath.clear();
    return result;
}

int RunSelfTest(const std::filesystem::path& testRoot, std::wstring* failure) {
    if (failure != nullptr) failure->clear();
    const auto source = testRoot / L"项目";
    const auto nested = source / L"日本語";
    const auto output = testRoot / L"archive.zip";
    CreateDirectoryW(testRoot.c_str(), nullptr);
    CreateDirectoryW(source.c_str(), nullptr);
    CreateDirectoryW(nested.c_str(), nullptr);
    const auto file = nested / L"Emoji😀.txt";
    if (!WriteSelfTestFile(file, "Gravity Desktop ZIP self-test") ||
        !WriteSelfTestFile(source / L"中文.docx", "document") ||
        !WriteSelfTestFile(source / L"Résumé.jpg", "image") ||
        !WriteSelfTestFile(source / L"website.url", "[InternetShortcut]\r\nURL=https://gravitydesktop.com\r\n") ||
        !WriteSelfTestFile(source / L"shortcut.lnk", "link payload") ||
        CreateDirectoryW((source / L"empty-folder").c_str(), nullptr) == FALSE) {
        if (failure != nullptr) *failure = L"Unable to write ZIP test source";
        return 1;
    }
    std::atomic_bool cancellation{false};
    const Result result = CreateArchive({source, output, false, &cancellation});
    // root + nested + empty folder + five files. Validation reopens and
    // reads every archive member rather than trusting writer completion.
    if (!result.Succeeded() || !ValidateArchive(output, 8)) {
        if (failure != nullptr) *failure = L"ZIP self-test archive validation failed";
        return 1;
    }
    const auto extracted = testRoot / L"extracted";
    const auto extractedNested = extracted / L"nested";
    CreateDirectoryW(extracted.c_str(), nullptr);
    CreateDirectoryW(extractedNested.c_str(), nullptr);
    const std::array<std::pair<std::filesystem::path, std::filesystem::path>, 5>
        hashes{{
            {source / L"中文.docx", extracted / L"中文.docx"},
            {source / L"Résumé.jpg", extracted / L"Résumé.jpg"},
            {source / L"website.url", extracted / L"website.url"},
            {source / L"shortcut.lnk", extracted / L"shortcut.lnk"},
            {file, extractedNested / L"Emoji😀.txt"},
        }};
    const std::array<std::string, 5> names{{
        ToUtf8(FileName(source)) + "/中文.docx",
        ToUtf8(FileName(source)) + "/Résumé.jpg",
        ToUtf8(FileName(source)) + "/website.url",
        ToUtf8(FileName(source)) + "/shortcut.lnk",
        ToUtf8(FileName(source)) + "/日本語/Emoji😀.txt",
    }};
    for (std::size_t index = 0; index < hashes.size(); ++index) {
        std::array<std::byte, 32> sourceHash{};
        std::array<std::byte, 32> extractedHash{};
        if (!ExtractSelfTestEntry(output, names[index], hashes[index].second) ||
            !HashSelfTestFile(hashes[index].first, &sourceHash) ||
            !HashSelfTestFile(hashes[index].second, &extractedHash) ||
            sourceHash != extractedHash) {
            if (failure != nullptr) *failure = L"ZIP self-test SHA-256 extraction failed";
            return 1;
        }
    }

    // Exercise a real > MAX_PATH Unicode directory through the exact Win32
    // extended path form used by the service. No path may be truncated.
    std::filesystem::path extendedLongSource = ExtendedPath(testRoot);
    for (int i = 0; i < 7; ++i) {
        extendedLongSource /= std::wstring(32, L'路');
        if (CreateDirectoryW(extendedLongSource.c_str(), nullptr) == FALSE &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            if (failure != nullptr) *failure = L"Unable to create long ZIP test path";
            return 1;
        }
    }
    const auto longFile = extendedLongSource / L"長い名前😀.txt";
    const auto longOutput = testRoot / L"long-path.zip";
    if (!WriteSelfTestFile(longFile, "long path")) {
        if (failure != nullptr) *failure = L"ZIP long-path self-test could not write source";
        return 1;
    }
    const Result longResult = CreateArchive(
        {extendedLongSource, longOutput, false, &cancellation});
    if (!longResult.Succeeded()) {
        if (failure != nullptr) *failure = L"ZIP long-path archive creation failed: " + longResult.detail;
        return 1;
    }
    if (!ValidateArchive(longOutput, 2)) {
        if (failure != nullptr) *failure = L"ZIP long-path archive validation failed";
        return 1;
    }

    // A pre-cancelled request proves the transaction never publishes a ZIP.
    cancellation.store(true, std::memory_order_relaxed);
    const auto cancelledOutput = testRoot / L"cancelled.zip";
    if (CreateArchive({source, cancelledOutput, false, &cancellation}).code !=
            ResultCode::Cancelled ||
        GetFileAttributesW(cancelledOutput.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (failure != nullptr) *failure = L"ZIP cancellation self-test failed";
        return 1;
    }
    cancellation.store(false, std::memory_order_relaxed);

    DeleteIfPresent(output);
    DeleteIfPresent(longOutput);
    DeleteIfPresent(cancelledOutput);
    DeleteFileW((source / L"中文.docx").c_str());
    DeleteFileW((source / L"Résumé.jpg").c_str());
    DeleteFileW((source / L"website.url").c_str());
    DeleteFileW((source / L"shortcut.lnk").c_str());
    DeleteFileW(file.c_str());
    for (const auto& hash : hashes) DeleteFileW(hash.second.c_str());
    RemoveDirectoryW(extractedNested.c_str());
    RemoveDirectoryW(extracted.c_str());
    RemoveDirectoryW((source / L"empty-folder").c_str());
    RemoveDirectoryW(nested.c_str());
    RemoveDirectoryW(source.c_str());
    std::error_code cleanupError;
    std::filesystem::remove_all(ExtendedPath(testRoot), cleanupError);
    if (cleanupError) {
        if (failure != nullptr) *failure = L"Unable to clean ZIP self-test";
        return 1;
    }
    return 0;
}

}  // namespace gravitydesktop::zip
