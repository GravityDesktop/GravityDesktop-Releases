#pragma once
#include "SafeLibraryShortcut.h"
#include <aclapi.h>
#include <sddl.h>
#include <memory>
#include <algorithm>
#pragma comment(lib, "advapi32.lib")

namespace gravity::shortcut {
// Local NTFS, same-directory, no-replace transaction. It deliberately keeps
// verified old objects as rollback evidence. It is not a power-loss guarantee.
enum class UpdateFault { None, Prepared, BackedUp, Publish, Verify, Race, RollbackRace, ProcessCrashAfterBackup };
inline thread_local Result* ownedCloseReport = nullptr;
struct CloseReportScope {
    Result* previous;
    explicit CloseReportScope(Result& report) : previous(ownedCloseReport) { ownedCloseReport = &report; }
    ~CloseReportScope() { ownedCloseReport = previous; }
};
struct LockedFile {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit LockedFile(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    LockedFile(const LockedFile&) = delete;
    LockedFile& operator=(const LockedFile&) = delete;
    ~LockedFile() { const DWORD error = Close(); if (error && ownedCloseReport) ownedCloseReport->cleanupCloseError = error; }
    DWORD Close() {
        if (value == INVALID_HANDLE_VALUE) return ERROR_SUCCESS;
        HANDLE handle = value; value = INVALID_HANDLE_VALUE;
        return CloseHandle(handle) ? ERROR_SUCCESS : GetLastError();
    }
};
struct OwnedParents {
    std::vector<HANDLE> handles;
    DWORD error = 0;
    explicit OwnedParents(const std::filesystem::path& path) {
        if (!path.is_absolute() || path.filename().empty() || path.filename().wstring().find(L':') != std::wstring::npos) { error = ERROR_INVALID_NAME; return; }
        auto ancestor = path.root_path();
        for (const auto& part : path.parent_path().relative_path()) {
            if (part == L"." || part == L".." || part.wstring().find(L':') != std::wstring::npos) { error = ERROR_INVALID_NAME; return; }
            ancestor /= part;
            HANDLE handle = CreateFileW(ancestor.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) { error = GetLastError(); return; }
            handles.push_back(handle); FILE_ATTRIBUTE_TAG_INFO info{};
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info))) { error = GetLastError(); return; }
            if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { error = ERROR_REPARSE_TAG_INVALID; return; }
        }
    }
    DWORD Close() {
        DWORD result = 0;
        for (HANDLE handle : handles) if (!CloseHandle(handle) && !result) result = GetLastError();
        handles.clear(); return result;
    }
    ~OwnedParents() { const DWORD result = Close(); if (result && ownedCloseReport) ownedCloseReport->directoryCloseError = result; }
};
inline Result Failure(const char* stage, DWORD error, Status status = Status::Failed) {
    return {status, HRESULT_FROM_WIN32(error), error, stage};
}
inline DWORD DecodeFields(const std::vector<unsigned char>& bytes, Fields& fields, HRESULT* comResult = nullptr) {
    Microsoft::WRL::ComPtr<IStream> stream; Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistStream> persist;
    HRESULT hr = MemoryStream(bytes, stream);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = link.As(&persist);
    if (SUCCEEDED(hr)) hr = persist->Load(stream.Get());
    if (SUCCEEDED(hr)) hr = ReadFields(link.Get(), fields);
    if (comResult) *comResult = hr;
    return SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
inline std::filesystem::path UniqueSibling(const std::filesystem::path& path, const wchar_t* prefix) {
    GUID id{}; wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, 40)) return {};
    return path.parent_path() / (std::wstring(prefix) + text + L".tmp");
}
struct Transaction {
    FILE_ID_INFO oldId{}, newId{}, journalId{}, parentId{};
    std::filesystem::path destination, backup, next, journal;
    std::vector<unsigned char> oldBytes, newBytes;
};
inline void AppendBlob(std::vector<unsigned char>& output, const void* data, size_t size) {
    const auto* first = static_cast<const unsigned char*>(data); output.insert(output.end(), first, first + size);
}
inline std::vector<unsigned char> EncodeTransaction(const Transaction& tx) {
    std::vector<unsigned char> bytes; const char magic[] = "Gravity.SafeLibrary.Transaction.v1";
    AppendBlob(bytes, magic, sizeof(magic)); AppendBlob(bytes, &tx.oldId, sizeof(tx.oldId)); AppendBlob(bytes, &tx.newId, sizeof(tx.newId));
    AppendBlob(bytes, &tx.journalId, sizeof(tx.journalId)); AppendBlob(bytes, &tx.parentId, sizeof(tx.parentId));
    for (const auto& path : {tx.destination, tx.backup, tx.next, tx.journal}) {
        const auto text = path.wstring(); const DWORD count = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        AppendBlob(bytes, &count, sizeof(count)); AppendBlob(bytes, text.data(), count);
    }
    for (const auto* content : {&tx.oldBytes, &tx.newBytes}) {
        const DWORD count = static_cast<DWORD>(content->size()); AppendBlob(bytes, &count, sizeof(count)); AppendBlob(bytes, content->data(), count);
    }
    return bytes;
}
inline bool TransactionEncodingAllowed(const Transaction& tx, const std::vector<unsigned char>& encoded) {
    // Leave ample bounded headroom for DPAPI overhead under the 2MiB reader.
    return !tx.oldBytes.empty() && !tx.newBytes.empty() && tx.oldBytes.size() <= 1024 * 1024 &&
        tx.newBytes.size() <= 1024 * 1024 && encoded.size() <= 1024 * 1024;
}
inline bool DecodeTransaction(const std::vector<unsigned char>& bytes, Transaction& tx) {
    size_t offset = 0;
    const auto take = [&](void* output, size_t count) {
        if (count > bytes.size() - offset) return false;
        std::memcpy(output, bytes.data() + offset, count); offset += count; return true;
    };
    const char magic[] = "Gravity.SafeLibrary.Transaction.v1"; char found[sizeof(magic)]{};
    if (!take(found, sizeof(found)) || std::memcmp(found, magic, sizeof(magic)) || !take(&tx.oldId, sizeof(tx.oldId)) || !take(&tx.newId, sizeof(tx.newId)) || !take(&tx.journalId, sizeof(tx.journalId)) || !take(&tx.parentId, sizeof(tx.parentId))) return false;
    for (auto* path : {&tx.destination, &tx.backup, &tx.next, &tx.journal}) {
        DWORD count = 0; if (!take(&count, sizeof(count)) || !count || count > 65534 || count % sizeof(wchar_t)) return false;
        std::wstring text(count / sizeof(wchar_t), L'\0'); if (!take(text.data(), count) || text.find(L'\0') != std::wstring::npos) return false;
        *path = text;
    }
    for (auto* content : {&tx.oldBytes, &tx.newBytes}) {
        DWORD count = 0; if (!take(&count, sizeof(count)) || !count || count > 1024 * 1024) return false;
        content->resize(count); if (!take(content->data(), count)) return false;
    }
    return offset == bytes.size();
}
inline DWORD VerifyLocked(HANDLE file, const std::filesystem::path& path, const FILE_ID_INFO& id,
                          const std::vector<unsigned char>& bytes, HANDLE* receipt = nullptr) {
    std::wstring actual; DWORD error = HandlePath(file, actual); if (error) return error;
    if (_wcsicmp(actual.c_str(), path.c_str())) return ERROR_INVALID_NAME;
    FILE_ID_INFO found{}; std::vector<unsigned char> read;
    error = HandleBytes(file, read, found); if (error) return error;
    if (std::memcmp(&id, &found, sizeof(id)) || bytes != read) return ERROR_INVALID_DATA;
    return Ownership(file, path, false, receipt);
}
inline HANDLE OpenOwned(const std::filesystem::path& path) {
    return CreateFileW(path.c_str(), GENERIC_READ | DELETE | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}
inline DWORD SecurityText(HANDLE file, std::wstring& text) {
    PSECURITY_DESCRIPTOR descriptor = nullptr; LPWSTR converted = nullptr;
    const SECURITY_INFORMATION requested = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    DWORD error = GetSecurityInfo(file, SE_FILE_OBJECT, requested, nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (!error) {
        if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1, requested, &converted, nullptr)) error = GetLastError();
        else text = converted;
    }
    if (converted) LocalFree(converted); if (descriptor) LocalFree(descriptor);
    return error;
}
inline DWORD JournalBytes(HANDLE file, std::vector<unsigned char>& bytes, bool write) {
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()}, output{};
    DWORD error = 0;
    if (write) {
        if (!CryptProtectData(&input, L"GravityDesktop.SafeLibrary.Transaction.v1", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return GetLastError();
        DWORD count = 0;
        if (output.cbData > 2 * 1024 * 1024) error = ERROR_FILE_TOO_LARGE;
        else if (!WriteFile(file, output.pbData, output.cbData, &count, nullptr)) error = GetLastError();
        else if (count != output.cbData) error = ERROR_WRITE_FAULT;
        else if (!FlushFileBuffers(file)) error = GetLastError();
    } else {
        std::vector<unsigned char> sealed; FILE_ID_INFO id{};
        error = HandleBytes(file, sealed, id);
        if (!error) {
            input = {static_cast<DWORD>(sealed.size()), sealed.data()};
            if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) error = GetLastError();
            else bytes.assign(output.pbData, output.pbData + output.cbData);
        }
    }
    if (output.pbData) LocalFree(output.pbData);
    return error;
}
inline bool TransactionName(const std::filesystem::path& path, const wchar_t* prefix) {
    const auto name = path.filename().wstring(); const size_t length = std::wcslen(prefix);
    if (name.size() != length + 38 + 4 || name.substr(0, length) != prefix || name.substr(length + 38) != L".tmp") return false;
    GUID id{}; return SUCCEEDED(CLSIDFromString(name.substr(length, 38).c_str(), &id));
}
inline Result UpdateOwnedImpl(const std::filesystem::path& path, const Fields& expected, UpdateFault fault,
                              void (*race)(const std::filesystem::path&)) {
    OwnedParents parents(path); if (parents.error) return Failure("owned_parent", parents.error);
    LockedFile old(OpenOwned(path));
    if (old.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return CreateOrValidate(path, expected, Fault::None, nullptr, true);
        return Failure("owned_open", error);
    }
    FILE_ID_INFO oldId{}; std::vector<unsigned char> oldBytes; DWORD error = HandleBytes(old.value, oldBytes, oldId);
    if (error) return Failure("owned_inspect", error, Status::PreservedConflict);
    Fields actual; HRESULT decodeResult = S_OK; error = DecodeFields(oldBytes, actual, &decodeResult);
    if (error) return {Status::PreservedConflict, decodeResult, error, "existing_corrupt"};
    LockedFile receipt;
    error = Ownership(old.value, path, false, &receipt.value);
    // A legacy exact match is usable, but is never adopted or rewritten.
    if (error) return {Equal(actual, expected) ? Status::Valid : Status::PreservedConflict, S_OK, error, "unowned_preserved"};
    if (Equal(actual, expected)) return {Status::Valid, S_OK, 0, "owned_unchanged"};
    FILE_BASIC_INFO originalMetadata{};
    if (!GetFileInformationByHandleEx(old.value, FileBasicInfo, &originalMetadata, sizeof(originalMetadata))) return Failure("owned_metadata", GetLastError());
    if (originalMetadata.FileAttributes & FILE_ATTRIBUTE_READONLY) return Failure("owned_readonly_preserved", ERROR_ACCESS_DENIED, Status::PreservedConflict);
    // A user edit invalidates the receipt before this point, including params.
    const auto nextPath = UniqueSibling(path, L".gravity-next-");
    const auto backupPath = UniqueSibling(path, L".gravity-backup-");
    const auto journalPath = UniqueSibling(path, L".gravity-tx-");
    if (nextPath.empty() || backupPath.empty() || journalPath.empty()) return Failure("transaction_name", ERROR_INVALID_NAME);
    const auto made = CreateOrValidate(nextPath, expected, Fault::None, nullptr, true);
    if (made.status != Status::Created) return made;
    LockedFile next(OpenOwned(nextPath)); if (next.value == INVALID_HANDLE_VALUE) return Failure("prepared_open", GetLastError());
    // Never replace an entry with inherited permissions that weaken a user's
    // custom ACL/owner/group. Do not silently rewrite the original security.
    std::wstring oldSecurity, newSecurity;
    error = SecurityText(old.value, oldSecurity);
    if (!error) error = SecurityText(next.value, newSecurity);
    if (error) return Failure("security_query", error);
    if (oldSecurity != newSecurity) return Failure("custom_security_preserved", ERROR_ACCESS_DENIED, Status::PreservedConflict);
    Transaction tx; tx.destination = path; tx.next = nextPath; tx.backup = backupPath; tx.journal = journalPath; tx.oldId = oldId; tx.oldBytes = oldBytes;
    error = HandleBytes(next.value, tx.newBytes, tx.newId);
    if (!error && std::memcmp(&tx.newId, &made.publishedIdentity, sizeof(tx.newId))) error = ERROR_INVALID_DATA;
    if (!error) error = VerifyLocked(next.value, nextPath, tx.newId, tx.newBytes);
    if (error) return Failure("prepared_verify", error);
    LockedFile journal(CreateFileW(journalPath.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (journal.value == INVALID_HANDLE_VALUE) return Failure("journal_create", GetLastError());
    if (!GetFileInformationByHandleEx(journal.value, FileIdInfo, &tx.journalId, sizeof(tx.journalId))) return Failure("journal_identity", GetLastError());
    if (parents.handles.empty() || !GetFileInformationByHandleEx(parents.handles.back(), FileIdInfo, &tx.parentId, sizeof(tx.parentId))) return Failure("journal_parent_identity", parents.handles.empty() ? ERROR_INVALID_NAME : GetLastError());
    auto record = EncodeTransaction(tx);
    if (!TransactionEncodingAllowed(tx, record)) return Failure("transaction_encoding_limit_old_preserved", ERROR_FILE_TOO_LARGE);
    error = JournalBytes(journal.value, record, true);
    std::vector<unsigned char> readRecord;
    if (!error) error = JournalBytes(journal.value, readRecord, false);
    if (!error && record != readRecord) error = ERROR_INVALID_DATA;
    if (error) return Failure("transaction_record", error);
    // No mutation of the old object has happened before this durable receipt.
    if (fault == UpdateFault::Prepared) return Failure("simulated_prepared_interruption", ERROR_OPERATION_ABORTED);
    error = VerifyLocked(old.value, path, oldId, oldBytes);
    if (error) return Failure("old_precommit_verify", error);
    error = RenameNoReplace(old.value, backupPath);
    if (error) return Failure("backup_no_replace", error);
    error = VerifyLocked(old.value, backupPath, oldId, oldBytes);
    if (error) return Failure("backup_verify", error);
    if (fault == UpdateFault::ProcessCrashAfterBackup) ExitProcess(73); // Isolated test subprocess only; production always None.
    if (fault == UpdateFault::BackedUp) return Failure("simulated_backup_interruption", ERROR_OPERATION_ABORTED);
    if ((fault == UpdateFault::Race || fault == UpdateFault::RollbackRace) && race) race(path);
    error = fault == UpdateFault::Publish || fault == UpdateFault::RollbackRace ? ERROR_WRITE_FAULT : RenameNoReplace(next.value, path);
    if (error) {
        // Roll back only our held old object, and never displace a race winner.
        const DWORD restored = RenameNoReplace(old.value, path);
        if (!restored && !VerifyLocked(old.value, path, oldId, oldBytes)) return Failure("publish_failed_old_restored", error);
        Result result = Failure("publish_failed_backup_retained", error);
        result.publicationEvidence = "rollback_error=" + std::to_string(restored) + " backup=" + PathUnits(backupPath.wstring()); return result;
    }
    error = fault == UpdateFault::Verify ? ERROR_INVALID_DATA : VerifyLocked(next.value, path, tx.newId, tx.newBytes);
    if (error) return Failure("updated_verify_backup_retained", error);
    FILE_BASIC_INFO newMetadata{}; newMetadata.FileAttributes = originalMetadata.FileAttributes;
    if (!SetFileInformationByHandle(next.value, FileBasicInfo, &newMetadata, sizeof(newMetadata))) return Failure("updated_attributes_backup_retained", GetLastError());
    // Closing failures are surfaced. Verified backup is retained for recovery,
    // not removed by a guessed name. No background scanning/retry loop exists.
    const DWORD receiptClose = receipt.Close(), oldClose = old.Close(), nextClose = next.Close(), journalClose = journal.Close(), parentClose = parents.Close();
    if (receiptClose || oldClose || nextClose || journalClose || parentClose) return Failure("updated_close", receiptClose ? receiptClose : oldClose ? oldClose : nextClose ? nextClose : journalClose ? journalClose : parentClose);
    Result result{Status::Updated, S_OK, 0, "updated_verified_backup_retained"};
    result.publishedIdentity = tx.newId; result.publishedPath = path.wstring(); result.publicationEvidence = "backup=" + PathUnits(backupPath.wstring()); return result;
}
// A v1 receipt is an authenticated edge, not a commit certificate. Its state
// is established from pinned objects; neither time nor enumeration order is
// used. All receipts remain locked until the decision/rename is complete.
enum class TransactionPhase { PreparedOrRecovered, Committed, BackedUp };
struct RecoveryRecord {
    Transaction tx;
    std::unique_ptr<LockedFile> journal, backup, next, backupReceipt, nextReceipt;
    TransactionPhase phase = TransactionPhase::PreparedOrRecovered;
};
inline bool SameVersion(const FILE_ID_INFO& a, const std::vector<unsigned char>& ab,
                        const FILE_ID_INFO& b, const std::vector<unsigned char>& bb) {
    return !std::memcmp(&a, &b, sizeof(a)) && ab == bb;
}
inline Result RecoverTransactionContext(const std::filesystem::path& requested,
                                        const std::filesystem::path* requiredRecord = nullptr) {
    OwnedParents parents(requested); if (parents.error) return Failure("recovery_parent", parents.error);
    FILE_ID_INFO parentId{};
    if (parents.handles.empty() || !GetFileInformationByHandleEx(parents.handles.back(), FileIdInfo, &parentId, sizeof(parentId))) return Failure("recovery_parent_id", parents.handles.empty() ? ERROR_INVALID_NAME : GetLastError());
    std::vector<RecoveryRecord> records;
    std::error_code iteratorError; size_t count = 0, contextBytes = 0; bool requiredFound = !requiredRecord;
    std::vector<std::filesystem::path> directoryBefore;
    for (std::filesystem::directory_iterator it(requested.parent_path(), iteratorError), end;
         !iteratorError && it != end; it.increment(iteratorError)) {
        if (++count > 4096) return Failure("recovery_scan_limit", ERROR_MORE_DATA);
        const auto path = it->path(); directoryBefore.push_back(path);
        if (!TransactionName(path, L".gravity-tx-")) continue;
        RecoveryRecord record; record.journal = std::make_unique<LockedFile>(OpenOwned(path));
        if (record.journal->value == INVALID_HANDLE_VALUE) return Failure("recovery_record_open", GetLastError());
        FILE_ID_INFO id{};
        if (!GetFileInformationByHandleEx(record.journal->value, FileIdInfo, &id, sizeof(id))) return Failure("recovery_record_id", GetLastError());
        std::wstring actual; DWORD error = HandlePath(record.journal->value, actual);
        std::vector<unsigned char> bytes; if (!error) error = JournalBytes(record.journal->value, bytes, false);
        auto& tx = record.tx;
        if (error || _wcsicmp(actual.c_str(), path.c_str()) || !DecodeTransaction(bytes, tx) ||
            std::memcmp(&tx.journalId, &id, sizeof(id)) || std::memcmp(&tx.parentId, &parentId, sizeof(parentId)) ||
            tx.destination.parent_path() != requested.parent_path() || tx.destination.filename().wstring().find(L':') != std::wstring::npos ||
            _wcsicmp(tx.destination.extension().c_str(), L".lnk") || tx.backup.parent_path() != requested.parent_path() ||
            tx.next.parent_path() != requested.parent_path() || !TransactionName(tx.backup, L".gravity-backup-") ||
            !TransactionName(tx.next, L".gravity-next-") || tx.journal != path || !std::memcmp(&tx.oldId, &tx.newId, sizeof(id)))
            return Failure("recovery_record_invalid", error ? error : ERROR_INVALID_DATA, Status::PreservedConflict);
        contextBytes += bytes.size();
        if (contextBytes > 32 * 1024 * 1024) return Failure("recovery_context_byte_limit", ERROR_NOT_ENOUGH_MEMORY);
        Fields oldFields, newFields;
        HRESULT oldHr = S_OK, newHr = S_OK;
        if (DecodeFields(tx.oldBytes, oldFields, &oldHr) || DecodeFields(tx.newBytes, newFields, &newHr))
            return {Status::PreservedConflict, FAILED(oldHr) ? oldHr : newHr, ERROR_INVALID_DATA, "recovery_record_fields"};
        if (requiredRecord && !_wcsicmp(requiredRecord->c_str(), path.c_str())) requiredFound = true;
        for (bool next : {false, true}) {
            const auto& objectPath = next ? tx.next : tx.backup;
            auto& file = next ? record.next : record.backup;
            file = std::make_unique<LockedFile>(OpenOwned(objectPath));
            if (file->value == INVALID_HANDLE_VALUE) {
                error = GetLastError(); file.reset();
                if (error != ERROR_FILE_NOT_FOUND) return Failure("recovery_object_open", error);
            } else {
                auto& receipt = next ? record.nextReceipt : record.backupReceipt;
                receipt = std::make_unique<LockedFile>();
                error = VerifyLocked(file->value, objectPath, next ? tx.newId : tx.oldId, next ? tx.newBytes : tx.oldBytes, &receipt->value);
                if (error) return Failure("recovery_object_invalid", error, Status::PreservedConflict);
            }
        }
        records.push_back(std::move(record));
    }
    if (iteratorError) return Failure("recovery_scan", static_cast<DWORD>(iteratorError.value()));
    if (!requiredFound) return Failure("recovery_required_record_absent", ERROR_FILE_NOT_FOUND);
    for (const auto& path : directoryBefore) {
        if (!TransactionName(path, L".gravity-backup-")) continue;
        bool referenced = false;
        for (const auto& record : records) if (!_wcsicmp(record.tx.backup.c_str(), path.c_str())) referenced = true;
        if (!referenced) return Failure("recovery_orphan_backup", ERROR_INVALID_DATA, Status::PreservedConflict);
    }
    // Authenticate the entire context before any mutation. Different localized
    // targets are separate chains; at most one may need recovery per operation.
    std::vector<std::filesystem::path> targets;
    for (const auto& record : records) {
        bool found = false;
        for (const auto& path : targets) if (!_wcsicmp(path.c_str(), record.tx.destination.c_str())) found = true;
        if (!found) targets.push_back(record.tx.destination);
    }
    RecoveryRecord* pending = nullptr;
    std::vector<std::unique_ptr<LockedFile>> destinations, destinationReceipts;
    for (const auto& target : targets) {
        auto destination = std::make_unique<LockedFile>(OpenOwned(target));
        auto destinationReceipt = std::make_unique<LockedFile>();
        FILE_ID_INFO destinationId{}; std::vector<unsigned char> destinationBytes;
        DWORD error = 0;
        if (destination->value == INVALID_HANDLE_VALUE) {
            error = GetLastError(); destination.reset();
            if (error != ERROR_FILE_NOT_FOUND) return Failure("recovery_destination_open", error);
        } else {
            error = HandleBytes(destination->value, destinationBytes, destinationId);
            if (!error) error = Ownership(destination->value, target, false, &destinationReceipt->value);
            if (error) return Failure("recovery_destination_foreign", error, Status::PreservedConflict);
        }
        std::vector<RecoveryRecord*> chain;
        const auto witnessed = [&](const FILE_ID_INFO& id, const std::vector<unsigned char>& bytes) {
            if (destination && SameVersion(id, bytes, destinationId, destinationBytes)) return true;
            for (const auto& record : records)
                if (!_wcsicmp(record.tx.destination.c_str(), target.c_str()) && record.backup &&
                    SameVersion(id, bytes, record.tx.oldId, record.tx.oldBytes)) return true;
            return false;
        };
        for (auto& record : records) {
            if (_wcsicmp(record.tx.destination.c_str(), target.c_str())) continue;
            if (!record.backup) {
                // Prepared, failed publish rollback, or verified prior recovery:
                // not a committed edge. Its old version must still be witnessed.
                if (!record.next || !witnessed(record.tx.oldId, record.tx.oldBytes))
                    return Failure("recovery_unproven_legacy_state", ERROR_INVALID_DATA, Status::PreservedConflict);
                record.phase = TransactionPhase::PreparedOrRecovered;
                continue;
            }
            if (record.next) {
                if (pending) return Failure("recovery_multiple_pending", ERROR_INVALID_DATA, Status::PreservedConflict);
                // A race-winning/foreign target must never be displaced.
                if (destination) return Failure("recovery_pending_destination_exists", ERROR_ALREADY_EXISTS, Status::PreservedConflict);
                record.phase = TransactionPhase::BackedUp; pending = &record;
            } else {
                if (!witnessed(record.tx.newId, record.tx.newBytes))
                    return Failure("recovery_commit_unproven", ERROR_INVALID_DATA, Status::PreservedConflict);
                record.phase = TransactionPhase::Committed;
            }
            chain.push_back(&record);
        }
        // One connected linear chain, with exact File ID AND byte continuity.
        // Duplicate edges, forks, missing middle receipts, and cycles fail closed.
        size_t roots = 0, leaves = 0; RecoveryRecord* leaf = nullptr;
        for (auto* record : chain) {
            size_t previous = 0, following = 0;
            for (auto* other : chain) {
                if (other == record) continue;
                if (SameVersion(other->tx.newId, other->tx.newBytes, record->tx.oldId, record->tx.oldBytes)) ++previous;
                if (SameVersion(record->tx.newId, record->tx.newBytes, other->tx.oldId, other->tx.oldBytes)) ++following;
                if (!std::memcmp(&record->tx.oldId, &other->tx.oldId, sizeof(FILE_ID_INFO)) ||
                    !std::memcmp(&record->tx.newId, &other->tx.newId, sizeof(FILE_ID_INFO)))
                    return Failure("recovery_duplicate_or_fork", ERROR_INVALID_DATA, Status::PreservedConflict);
            }
            if (previous > 1 || following > 1) return Failure("recovery_branch", ERROR_INVALID_DATA, Status::PreservedConflict);
            if (!previous) ++roots;
            if (!following) { ++leaves; leaf = record; }
        }
        if (!chain.empty() && (roots != 1 || leaves != 1)) return Failure("recovery_disconnected_chain", ERROR_INVALID_DATA, Status::PreservedConflict);
        if (leaf) {
            // Walking predecessors ensures no disconnected cycle accompanies it.
            size_t visited = 1; auto* node = leaf;
            for (;;) {
                RecoveryRecord* previous = nullptr;
                for (auto* other : chain) if (other != node && SameVersion(other->tx.newId, other->tx.newBytes, node->tx.oldId, node->tx.oldBytes)) previous = other;
                if (!previous) break;
                node = previous; if (++visited > chain.size()) return Failure("recovery_cycle", ERROR_INVALID_DATA, Status::PreservedConflict);
            }
            if (visited != chain.size()) return Failure("recovery_disconnected_chain", ERROR_INVALID_DATA, Status::PreservedConflict);
        }
        if (leaf && leaf->phase != TransactionPhase::BackedUp && (!destination || !SameVersion(leaf->tx.newId, leaf->tx.newBytes, destinationId, destinationBytes)))
            return Failure("recovery_current_version_unproven", ERROR_INVALID_DATA, Status::PreservedConflict);
        destinations.push_back(std::move(destination));
        destinationReceipts.push_back(std::move(destinationReceipt));
    }
    // This is explicitly a non-atomic membership recheck. Pinned identities and
    // the no-replace rename supply protection even after this bounded check.
    std::vector<std::filesystem::path> directoryAfter; iteratorError.clear(); count = 0;
    for (std::filesystem::directory_iterator it(requested.parent_path(), iteratorError), end;
         !iteratorError && it != end; it.increment(iteratorError)) {
        if (++count > 4096) return Failure("recovery_scan_limit", ERROR_MORE_DATA);
        directoryAfter.push_back(it->path());
    }
    if (iteratorError) return Failure("recovery_rescan", static_cast<DWORD>(iteratorError.value()));
    std::sort(directoryBefore.begin(), directoryBefore.end()); std::sort(directoryAfter.begin(), directoryAfter.end());
    if (directoryBefore != directoryAfter) return Failure("recovery_context_changed", ERROR_RETRY);
    if (!pending) return {Status::Valid, S_OK, 0, "transaction_context_verified"};
    auto& tx = pending->tx;
    // All old/new witnesses and receipts are still locked. Publish by held
    // handle with no replacement; concurrent occupation is an explicit failure.
    DWORD error = RenameNoReplace(pending->backup->value, tx.destination);
    if (error) return Failure("recovery_restore_no_replace", error);
    error = VerifyLocked(pending->backup->value, tx.destination, tx.oldId, tx.oldBytes);
    if (error) return Failure("recovery_restore_verify", error);
    Result result{Status::Restored, S_OK, 0, "unique_chain_old_restored_verified"};
    result.publishedPath = tx.destination.wstring(); result.publishedIdentity = tx.oldId;
    result.publicationEvidence = "state=RECOVERED actual_old_identity_and_bytes_verified";
    return result;
}
inline Result RecoverOwnedTransactionImpl(const std::filesystem::path& recordPath, const std::filesystem::path& requested) {
    return RecoverTransactionContext(requested, &recordPath);
}
inline Result FinishOwnedResult(Result result, const Result& close) {
    if (close.cleanupCloseError) result.cleanupCloseError = close.cleanupCloseError;
    if (close.directoryCloseError) result.directoryCloseError = close.directoryCloseError;
    if (close.cleanupCloseError || close.directoryCloseError) {
        const DWORD error = close.cleanupCloseError ? close.cleanupCloseError : close.directoryCloseError;
        result.status = Status::Failed; result.stage = "owned_close_failed";
        result.error = error; result.hr = HRESULT_FROM_WIN32(error);
    }
    return result;
}
inline Result RecoverOwnedTransaction(const std::filesystem::path& recordPath, const std::filesystem::path& requested) {
    Result close, result;
    { CloseReportScope scope(close); result = RecoverOwnedTransactionImpl(recordPath, requested); }
    return FinishOwnedResult(std::move(result), close);
}
inline Result EnsureOwnedLibraryShortcutImpl(const std::filesystem::path& path, const Fields& expected,
                                         UpdateFault fault, void (*race)(const std::filesystem::path&)) {
    // Only a bounded one-time directory check. No polling or user data scans.
    OwnedParents parents(path); if (parents.error) return Failure("ensure_parent", parents.error);
    const auto context = RecoverTransactionContext(path);
    if (context.status != Status::Valid) return context;
    return UpdateOwnedImpl(path, expected, fault, race);
}
inline Result EnsureOwnedLibraryShortcut(const std::filesystem::path& path, const Fields& expected,
                                         UpdateFault fault = UpdateFault::None,
                                         void (*race)(const std::filesystem::path&) = nullptr) {
    Result close, result;
    { CloseReportScope scope(close); result = EnsureOwnedLibraryShortcutImpl(path, expected, fault, race); }
    return FinishOwnedResult(std::move(result), close);
}
}
