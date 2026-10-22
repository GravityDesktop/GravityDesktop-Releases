#pragma once
#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

namespace gravity::shortcut {
enum class Status { Created, Valid, Updated, Restored, PreservedConflict, Failed };
struct Result {
    Status status = Status::Failed;
    HRESULT hr = S_OK;
    DWORD error = 0;
    const char* stage = "begin";
    DWORD cleanupDeleteError = 0, cleanupCloseError = 0, directoryCloseError = 0;
    std::string publicationEvidence;
    FILE_ID_INFO publishedIdentity{};
    std::wstring publishedPath;
};
struct Fields {
    std::wstring target, icon, description, arguments, workingDirectory;
    int iconIndex = 0, showCommand = SW_SHOWNORMAL;
};
// Test seams are explicit inputs; production always uses None / nullptr.
enum class Fault { None, Icon, Save, Interrupted, Verify, Publish,
                   WrongName, Identity, Missing, ReadBack, Query, Cleanup };
inline HRESULT ReadFields(IShellLinkW* link, Fields& fields) {
    HRESULT hr = S_OK;
    std::array<wchar_t, 32768> text{};
    if (SUCCEEDED(hr)) hr = link->GetPath(text.data(), static_cast<int>(text.size()), nullptr, SLGP_RAWPATH);
    if (SUCCEEDED(hr)) fields.target = text.data();
    if (SUCCEEDED(hr)) hr = link->GetIconLocation(text.data(), static_cast<int>(text.size()), &fields.iconIndex);
    if (SUCCEEDED(hr)) fields.icon = text.data();
    if (SUCCEEDED(hr)) hr = link->GetDescription(text.data(), static_cast<int>(text.size()));
    if (SUCCEEDED(hr)) fields.description = text.data();
    if (SUCCEEDED(hr)) hr = link->GetArguments(text.data(), static_cast<int>(text.size()));
    if (SUCCEEDED(hr)) fields.arguments = text.data();
    if (SUCCEEDED(hr)) hr = link->GetWorkingDirectory(text.data(), static_cast<int>(text.size()));
    if (SUCCEEDED(hr)) fields.workingDirectory = text.data();
    if (SUCCEEDED(hr)) hr = link->GetShowCmd(&fields.showCommand);
    return hr;
}
inline HRESULT Read(const std::filesystem::path& path, Fields& fields) {
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(link.GetAddressOf()));
    Microsoft::WRL::ComPtr<IPersistFile> persist;
    if (SUCCEEDED(hr)) hr = link.As(&persist);
    if (SUCCEEDED(hr)) hr = persist->Load(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE);
    return SUCCEEDED(hr) ? ReadFields(link.Get(), fields) : hr;
}
inline HRESULT MemoryStream(const std::vector<unsigned char>& bytes,
                            Microsoft::WRL::ComPtr<IStream>& stream) {
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, stream.ReleaseAndGetAddressOf());
    ULONG written = 0;
    if (SUCCEEDED(hr)) hr = stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written);
    if (SUCCEEDED(hr) && written != bytes.size()) hr = E_FAIL;
    LARGE_INTEGER start{};
    if (SUCCEEDED(hr)) hr = stream->Seek(start, STREAM_SEEK_SET, nullptr);
    return hr;
}
inline bool Equal(const Fields& a, const Fields& b) {
    return _wcsicmp(a.target.c_str(), b.target.c_str()) == 0 &&
           _wcsicmp(a.icon.c_str(), b.icon.c_str()) == 0 &&
           a.iconIndex == b.iconIndex && a.description == b.description &&
           a.arguments == b.arguments &&
           _wcsicmp(a.workingDirectory.c_str(), b.workingDirectory.c_str()) == 0 &&
           a.showCommand == b.showCommand;
}
inline const char* Name(Status status) {
    switch (status) {
    case Status::Created: return "CREATED";
    case Status::Valid: return "VALID";
    case Status::Updated: return "UPDATED";
    case Status::Restored: return "RESTORED";
    case Status::PreservedConflict: return "PRESERVED_CONFLICT";
    default: return "FAILED";
    }
}
inline std::string PathUnits(const std::wstring& value) {
    // Bounded UTF-16 evidence also handles malformed names without losing
    // invalid surrogate code units during a UTF-8 conversion.
    std::ostringstream text;
    text << "length=" << value.size() << ":" << std::hex << std::setfill('0');
    const size_t start = value.size() > 512 ? value.size() - 512 : 0;
    if (start) text << "tail:";
    for (size_t index = start; index < value.size(); ++index)
        text << std::setw(4) << static_cast<unsigned int>(static_cast<unsigned short>(value[index])) << '-';
    return text.str();
}
inline std::string IdentityUnits(const FILE_ID_INFO& value) {
    std::ostringstream text;
    text << std::hex << value.VolumeSerialNumber << ':' << std::setfill('0');
    for (auto byte : value.FileId.Identifier) text << std::setw(2) << static_cast<unsigned int>(byte);
    return text.str();
}
// This is provenance against accidental adoption, not a security boundary
// against a malicious process running as the same Windows user. DPAPI binds
// the receipt to that user; file identity + exact bytes reject copies/edits.
inline DWORD HandleBytes(HANDLE file, std::vector<unsigned char>& bytes, FILE_ID_INFO& identity) {
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{}, zero{};
    if (!GetFileInformationByHandle(file, &info)) return GetLastError();
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks != 1)
        return ERROR_INVALID_DATA;
    if (!GetFileInformationByHandleEx(file, FileIdInfo, &identity, sizeof(identity))) return GetLastError();
    if (!GetFileSizeEx(file, &size)) return GetLastError();
    if (size.QuadPart <= 0 || size.QuadPart > 2 * 1024 * 1024) return ERROR_INVALID_DATA;
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return GetLastError();
    bytes.resize(static_cast<size_t>(size.QuadPart)); DWORD count = 0;
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) return GetLastError();
    return count == bytes.size() ? ERROR_SUCCESS : ERROR_READ_FAULT;
}
inline DWORD HandlePath(HANDLE file, std::wstring& path) {
    std::vector<wchar_t> buffer(32768);
    const DWORD count = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count) return GetLastError();
    if (count >= buffer.size()) return ERROR_INSUFFICIENT_BUFFER;
    path.assign(buffer.data(), count);
    if (path.find(L"\\\\?\\") == 0) path.erase(0, 4);
    return ERROR_SUCCESS;
}
inline DWORD RenameNoReplace(HANDLE file, const std::filesystem::path& destination) {
    const auto name = destination.wstring();
    const DWORD length = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO, FileName) + length + sizeof(wchar_t), 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists = FALSE; info->FileNameLength = length;
    std::memcpy(info->FileName, name.data(), length);
    if (!SetFileInformationByHandle(file, FileRenameInfo, info, static_cast<DWORD>(storage.size()))) return GetLastError();
    std::wstring actual; const DWORD error = HandlePath(file, actual);
    if (error) return error;
    return _wcsicmp(actual.c_str(), name.c_str()) == 0 ? ERROR_SUCCESS : ERROR_INVALID_NAME;
}
inline DWORD SealedStream(const std::filesystem::path& path, const wchar_t* streamName,
                          std::vector<unsigned char>& plain, bool write, const FILE_ID_INFO* identity = nullptr,
                          HANDLE* retained = nullptr) {
    DATA_BLOB input{static_cast<DWORD>(plain.size()), plain.data()}, output{};
    if (write && !CryptProtectData(&input, L"GravityDesktop.SafeLibraryShortcut.v1", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return GetLastError();
    const auto streamPath = path.wstring() + streamName;
    HANDLE stream = CreateFileW(streamPath.c_str(), write ? GENERIC_WRITE : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, write ? CREATE_NEW : OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    DWORD error = 0;
    if (stream == INVALID_HANDLE_VALUE) error = GetLastError();
    else {
        FILE_ID_INFO streamIdentity{};
        if (!GetFileInformationByHandleEx(stream, FileIdInfo, &streamIdentity, sizeof(streamIdentity))) error = GetLastError();
        else if (identity && std::memcmp(identity, &streamIdentity, sizeof(streamIdentity))) error = ERROR_INVALID_DATA;
    }
    if (stream != INVALID_HANDLE_VALUE && !error && write) {
        DWORD count = 0;
        if (!WriteFile(stream, output.pbData, output.cbData, &count, nullptr)) error = GetLastError();
        else if (count != output.cbData) error = ERROR_WRITE_FAULT;
        else if (!FlushFileBuffers(stream)) error = GetLastError();
    } else if (stream != INVALID_HANDLE_VALUE && !error) {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(stream, &size)) error = GetLastError();
        else if (size.QuadPart <= 0 || size.QuadPart > 3 * 1024 * 1024) error = ERROR_INVALID_DATA;
        else {
            std::vector<unsigned char> sealed(static_cast<size_t>(size.QuadPart)); DWORD count = 0;
            if (!ReadFile(stream, sealed.data(), static_cast<DWORD>(sealed.size()), &count, nullptr)) error = GetLastError();
            else if (count != sealed.size()) error = ERROR_READ_FAULT;
            else {
                input = {static_cast<DWORD>(sealed.size()), sealed.data()};
                if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) error = GetLastError();
                else plain.assign(output.pbData, output.pbData + output.cbData);
            }
        }
    }
    if (!error && retained && !write) { *retained = stream; stream = INVALID_HANDLE_VALUE; }
    if (stream != INVALID_HANDLE_VALUE && !CloseHandle(stream) && !error) error = GetLastError();
    if (output.pbData) LocalFree(output.pbData);
    return error;
}
inline DWORD Ownership(HANDLE file, const std::filesystem::path& path, bool write, HANDLE* retained = nullptr) {
    FILE_ID_INFO id{}; std::vector<unsigned char> bytes;
    DWORD error = HandleBytes(file, bytes, id); if (error) return error;
    std::wstring actual; error = HandlePath(file, actual); if (error) return error;
    if (_wcsicmp(actual.c_str(), path.c_str()) != 0) return ERROR_INVALID_NAME;
    const char magic[] = "Gravity.SafeLibrary.Owner.v1";
    std::vector<unsigned char> expected(sizeof(magic) + sizeof(id) + bytes.size());
    std::memcpy(expected.data(), magic, sizeof(magic));
    std::memcpy(expected.data() + sizeof(magic), &id, sizeof(id));
    std::memcpy(expected.data() + sizeof(magic) + sizeof(id), bytes.data(), bytes.size());
    auto receipt = expected;
    error = SealedStream(path, L":GravityDesktop.SafeLibrary.Owner.v1", receipt, write, &id, write ? nullptr : retained);
    if (error) return error;
    if (write) { receipt.clear(); error = SealedStream(path, L":GravityDesktop.SafeLibrary.Owner.v1", receipt, false, &id); }
    if (error) return error;
    return receipt == expected ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
// No existing link is modified, even when its name/target looks program-owned.
// VALID is a field validation result, NOT an ownership claim. Automatic updates
// require a separately designed identity-locked ownership protocol.
inline Result CreateOrValidateImpl(const std::filesystem::path& destination,
                               const Fields& expected, Fault fault,
                               void (*beforePublish)(const std::filesystem::path&), Result& cleanupReport, bool owned) {
    // Pin every ancestor without delete sharing. Reject reparse points rather
    // than following a junction to an unintended directory. Handles remain
    // live through validation and publication; this is not a power-loss claim.
    struct DirectoryPins {
        std::vector<HANDLE> handles;
        Result& report;
        ~DirectoryPins() { for (HANDLE handle : handles) {
            if (!CloseHandle(handle)) { const DWORD error = GetLastError(); report.directoryCloseError = error; }
        } }
    } pins{{}, cleanupReport};
    if (!destination.is_absolute() || destination.filename().empty())
        return {Status::Failed, E_INVALIDARG, 0, "destination_path"};
    std::filesystem::path ancestor = destination.root_path();
    for (const auto& component : destination.parent_path().relative_path()) {
        if (component == L".." || component == L".")
            return {Status::Failed, E_INVALIDARG, 0, "noncanonical_path"};
        ancestor /= component;
        HANDLE directory = CreateFileW(ancestor.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (directory == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            return {Status::Failed, HRESULT_FROM_WIN32(error), error, "pin_parent"};
        }
        pins.handles.push_back(directory);
        FILE_ATTRIBUTE_TAG_INFO info{};
        if (!GetFileInformationByHandleEx(directory, FileAttributeTagInfo, &info, sizeof(info))) {
            const DWORD error = GetLastError();
            return {Status::Failed, HRESULT_FROM_WIN32(error), error, "inspect_parent"};
        }
        if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return {Status::Failed, E_INVALIDARG, 0, "unsafe_parent"};
    }
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return {Status::PreservedConflict, S_OK, 0, "existing_nonregular"};
        Fields existing;
        const HRESULT hr = Read(destination, existing);
        return {SUCCEEDED(hr) && Equal(existing, expected) ? Status::Valid : Status::PreservedConflict,
                hr, 0, "existing_validation"};
    }
    const DWORD inspectError = GetLastError();
    if (inspectError != ERROR_FILE_NOT_FOUND)
        return {Status::Failed, HRESULT_FROM_WIN32(inspectError), inspectError, "inspect_destination"};
    GUID id{};
    HRESULT hr = CoCreateGuid(&id);
    if (FAILED(hr)) return {Status::Failed, hr, 0, "unique_name"};
    std::array<wchar_t, 40> guid{};
    if (!StringFromGUID2(id, guid.data(), static_cast<int>(guid.size())))
        return {Status::Failed, E_FAIL, 0, "unique_name_format"};
    const auto temporary = destination.parent_path() /
        (std::wstring(L".gravity-link-") + guid.data() + L".tmp.lnk");
    // Read sharing permits independent path verification without relinquishing
    // write/delete exclusivity. Other writers and renamers remain blocked.
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return {Status::Failed, HRESULT_FROM_WIN32(error), error, "reserve_temp"};
    }
    struct Cleanup { HANDLE file; Result& report; Fault fault; bool keep = false;
        ~Cleanup() {
            if (!keep) {
                FILE_DISPOSITION_INFO disposition{TRUE};
                if (fault == Fault::Cleanup) report.cleanupDeleteError = ERROR_ACCESS_DENIED;
                else if (!SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition))) {
                    const DWORD error = GetLastError(); report.cleanupDeleteError = error;
                }
            }
            if (!CloseHandle(file)) { const DWORD error = GetLastError(); report.cleanupCloseError = error; }
        }
    } cleanup{file, cleanupReport, fault};
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    const char* stage = "create_com";
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()));
    if (SUCCEEDED(hr)) { stage = "set_target"; hr = link->SetPath(expected.target.c_str()); }
    if (SUCCEEDED(hr)) { stage = "set_icon"; hr = fault == Fault::Icon ? E_INVALIDARG : link->SetIconLocation(expected.icon.c_str(), expected.iconIndex); }
    if (SUCCEEDED(hr)) { stage = "set_description"; hr = link->SetDescription(expected.description.c_str()); }
    if (SUCCEEDED(hr)) { stage = "set_arguments"; hr = link->SetArguments(expected.arguments.c_str()); }
    if (SUCCEEDED(hr)) { stage = "set_working_directory"; hr = link->SetWorkingDirectory(expected.workingDirectory.c_str()); }
    if (SUCCEEDED(hr)) { stage = "set_show_command"; hr = link->SetShowCmd(expected.showCommand); }
    Microsoft::WRL::ComPtr<IPersistStream> persist;
    Microsoft::WRL::ComPtr<IStream> stream;
    if (SUCCEEDED(hr)) { stage = "query_persist_stream"; hr = link.As(&persist); }
    if (SUCCEEDED(hr)) { stage = "create_memory_stream"; hr = CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()); }
    if (SUCCEEDED(hr)) { stage = "com_save_memory"; hr = fault == Fault::Save ? E_FAIL : persist->Save(stream.Get(), TRUE); }
    if (FAILED(hr)) return {Status::Failed, hr, 0, stage};
    STATSTG stat{};
    hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr) || stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > 1024 * 1024)
        return {Status::Failed, FAILED(hr) ? hr : E_FAIL, 0, "stream_size"};
    std::vector<unsigned char> encoded(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{}; ULONG read = 0;
    hr = stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (SUCCEEDED(hr)) hr = stream->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &read);
    if (FAILED(hr) || read != encoded.size()) return {Status::Failed, FAILED(hr) ? hr : E_FAIL, 0, "stream_read"};
    DWORD written = 0;
    const DWORD requested = static_cast<DWORD>(fault == Fault::Interrupted ? encoded.size() / 2 : encoded.size());
    if (!WriteFile(file, encoded.data(), requested, &written, nullptr)) {
        const DWORD error = GetLastError();
        return {Status::Failed, HRESULT_FROM_WIN32(error), error, "write_temp"};
    }
    if (written != requested) return {Status::Failed, E_FAIL, 0, "short_write_temp"};
    if (!FlushFileBuffers(file)) {
        const DWORD error = GetLastError();
        return {Status::Failed, HRESULT_FROM_WIN32(error), error, "flush_temp"};
    }
    if (fault == Fault::Interrupted) { cleanup.keep = true; return {Status::Failed, E_ABORT, 0, "simulated_interruption"}; }
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "seek_temp"};
    }
    std::vector<unsigned char> roundtrip(encoded.size()); DWORD actualBytes = 0;
    if (!ReadFile(file, roundtrip.data(), static_cast<DWORD>(roundtrip.size()), &actualBytes, nullptr)) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "read_temp"};
    }
    if (actualBytes != roundtrip.size() || roundtrip != encoded)
        return {Status::Failed, E_FAIL, 0, "verify_bytes"};
    Microsoft::WRL::ComPtr<IStream> verifiedStream;
    Microsoft::WRL::ComPtr<IShellLinkW> verifiedLink;
    Microsoft::WRL::ComPtr<IPersistStream> verifiedPersist;
    hr = MemoryStream(roundtrip, verifiedStream);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(verifiedLink.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = verifiedLink.As(&verifiedPersist);
    if (SUCCEEDED(hr)) hr = verifiedPersist->Load(verifiedStream.Get());
    Fields actual;
    if (SUCCEEDED(hr)) hr = ReadFields(verifiedLink.Get(), actual);
    if (FAILED(hr) || !Equal(actual, expected) || fault == Fault::Verify || fault == Fault::Cleanup)
        return {Status::Failed, FAILED(hr) ? hr : E_FAIL, 0, "verify_temp"};
    if (beforePublish) beforePublish(destination);
    if (fault == Fault::Publish) return {Status::Failed, E_FAIL, 0, "injected_publish_failure"};
    FILE_ID_INFO beforeIdentity{};
    if (!GetFileInformationByHandleEx(file, FileIdInfo, &beforeIdentity, sizeof(beforeIdentity))) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "identity_before_publish"};
    }
    const std::wstring requestedName = std::filesystem::absolute(destination).wstring();
    cleanupReport.publicationEvidence = "requested_utf16=" + PathUnits(requestedName) + " source_id=" + IdentityUnits(beforeIdentity);
    const std::wstring fullName = requestedName + (fault == Fault::WrongName ? L".unexpected" : L"");
    const DWORD nameBytes = static_cast<DWORD>(fullName.size() * sizeof(wchar_t));
    // FileNameLength excludes the terminator, while the Win32 FileName contract
    // also describes a NUL-terminated string. Provide both bounded length and
    // an explicit terminator; never let a consumer scan past this allocation.
    std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t), 0);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr; rename->FileNameLength = nameBytes;
    std::memcpy(rename->FileName, fullName.data(), nameBytes);
    // Handle-based, identity-checked source; never replace a race winner.
    if (!SetFileInformationByHandle(file, FileRenameInfo, rename, static_cast<DWORD>(storage.size()))) {
        const DWORD error = GetLastError();
        return {error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? Status::PreservedConflict : Status::Failed,
                HRESULT_FROM_WIN32(error), error, "publish_no_replace"};
    }
    cleanup.keep = true;
    // Once rename succeeds never delete a mismatched published object. Retain
    // it for evidence; do not remove anything through the requested path.
    std::vector<wchar_t> finalPath(32768);
    if (fault == Fault::Query)
        return {Status::Failed, HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), ERROR_INVALID_HANDLE, "published_path_query"};
    const DWORD pathLength = GetFinalPathNameByHandleW(file, finalPath.data(), static_cast<DWORD>(finalPath.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!pathLength) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "published_path_query"};
    }
    if (pathLength >= finalPath.size()) return {Status::Failed, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER), ERROR_INSUFFICIENT_BUFFER, "published_path_query"};
    std::wstring observedName(finalPath.data(), pathLength);
    if (observedName.find(L"\\\\?\\") == 0) observedName.erase(0, 4);
    cleanupReport.publicationEvidence += " observed_utf16=" + PathUnits(observedName);
    if (_wcsicmp(observedName.c_str(), requestedName.c_str()) != 0)
        return {Status::Failed, E_FAIL, 0, "published_path_mismatch"};
    FILE_ID_INFO afterIdentity{};
    if (!GetFileInformationByHandleEx(file, FileIdInfo, &afterIdentity, sizeof(afterIdentity))) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "identity_after_publish"};
    }
    cleanupReport.publicationEvidence += " after_id=" + IdentityUnits(afterIdentity);
    if (fault == Fault::Identity || std::memcmp(&beforeIdentity, &afterIdentity, sizeof(beforeIdentity)) != 0)
        return {Status::Failed, E_FAIL, 0, "published_identity_mismatch"};
    const auto openPath = destination.wstring() + (fault == Fault::Missing ? L".missing" : L"");
    HANDLE reopened = CreateFileW(openPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (reopened == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "published_path_open"};
    }
    struct ReadHandle { HANDLE value; Result& report;
        ~ReadHandle() { if (!CloseHandle(value)) { const DWORD error = GetLastError(); report.cleanupCloseError = error; } }
    } reader{reopened, cleanupReport};
    FILE_ID_INFO reopenedIdentity{};
    if (!GetFileInformationByHandleEx(reopened, FileIdInfo, &reopenedIdentity, sizeof(reopenedIdentity))) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "published_reopen_identity"};
    }
    cleanupReport.publicationEvidence += " reopened_id=" + IdentityUnits(reopenedIdentity);
    if (std::memcmp(&beforeIdentity, &reopenedIdentity, sizeof(beforeIdentity)) != 0)
        return {Status::Failed, E_FAIL, 0, "published_reopen_identity_mismatch"};
    LARGE_INTEGER finalSize{};
    if (!GetFileSizeEx(reopened, &finalSize)) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "published_size_query"};
    }
    if (finalSize.QuadPart != static_cast<LONGLONG>(encoded.size()))
        return {Status::Failed, E_FAIL, 0, "published_size_mismatch"};
    std::vector<unsigned char> publishedBytes(encoded.size()); DWORD publishedCount = 0;
    if (fault == Fault::ReadBack)
        return {Status::Failed, HRESULT_FROM_WIN32(ERROR_READ_FAULT), ERROR_READ_FAULT, "published_read"};
    if (!ReadFile(reopened, publishedBytes.data(), static_cast<DWORD>(publishedBytes.size()), &publishedCount, nullptr)) {
        const DWORD error = GetLastError(); return {Status::Failed, HRESULT_FROM_WIN32(error), error, "published_read"};
    }
    if (publishedCount != publishedBytes.size() || publishedBytes != encoded)
        return {Status::Failed, E_FAIL, 0, "published_content_mismatch"};
    Microsoft::WRL::ComPtr<IStream> publishedStream;
    hr = MemoryStream(publishedBytes, publishedStream);
    if (SUCCEEDED(hr)) hr = verifiedPersist->Load(publishedStream.Get());
    Fields publishedFields;
    if (SUCCEEDED(hr)) hr = ReadFields(verifiedLink.Get(), publishedFields);
    if (FAILED(hr) || !Equal(publishedFields, expected))
        return {Status::Failed, FAILED(hr) ? hr : E_FAIL, 0, "published_fields_mismatch"};
    cleanupReport.publishedIdentity = beforeIdentity;
    if (owned) {
        const DWORD error = Ownership(file, destination, true);
        if (error) return {Status::Failed, HRESULT_FROM_WIN32(error), error, "ownership_receipt"};
    }
    return {Status::Created, S_OK, 0, "published_verified"};
}
inline Result CreateOrValidate(const std::filesystem::path& destination, const Fields& expected,
                               Fault fault = Fault::None,
                               void (*beforePublish)(const std::filesystem::path&) = nullptr, bool owned = false) {
    Result cleanupReport;
    Result outcome = CreateOrValidateImpl(destination, expected, fault, beforePublish, cleanupReport, owned);
    outcome.cleanupDeleteError = cleanupReport.cleanupDeleteError;
    outcome.cleanupCloseError = cleanupReport.cleanupCloseError;
    outcome.directoryCloseError = cleanupReport.directoryCloseError;
    outcome.publicationEvidence = std::move(cleanupReport.publicationEvidence);
    outcome.publishedIdentity = cleanupReport.publishedIdentity;
    if (outcome.status == Status::Created) outcome.publishedPath = destination.wstring();
    if (outcome.cleanupCloseError || outcome.directoryCloseError) {
        const DWORD error = outcome.cleanupCloseError ? outcome.cleanupCloseError : outcome.directoryCloseError;
        outcome.status = Status::Failed; outcome.hr = HRESULT_FROM_WIN32(error);
        outcome.error = error; outcome.stage = "close_handle_failed";
    }
    return outcome;
}
}
