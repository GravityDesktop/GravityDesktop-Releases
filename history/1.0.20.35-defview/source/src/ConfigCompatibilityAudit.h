#pragma once

// Included after the product model/serializer definitions, inside their
// anonymous namespace. This offline entry never constructs GravityApp, loads
// icons/shortcuts, resolves user references, or calls LoadState/SaveState.
namespace compatibility_audit {

inline bool ExactObject(HANDLE handle, const std::filesystem::path& path, bool regularFile) {
    BY_HANDLE_FILE_INFORMATION info{};
    wchar_t finalPath[32768]{};
    const DWORD length = GetFinalPathNameByHandleW(handle, finalPath, 32768, FILE_NAME_NORMALIZED);
    if (!length || length >= 32768 || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    std::wstring name(finalPath, length);
    if (name.starts_with(L"\\\\?\\")) name.erase(0, 4);
    return PathEquals(name, path.wstring()) && (regularFile
        ? info.nNumberOfLinks == 1 && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        : (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

struct Locks {
    std::vector<HANDLE> handles;
    ~Locks() { for (HANDLE handle : handles) CloseHandle(handle); }
    HANDLE Open(const std::filesystem::path& path, bool regularFile) {
        HANDLE handle = CreateFileW(path.c_str(), regularFile ? GENERIC_READ : FILE_READ_ATTRIBUTES,
            regularFile ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | (regularFile ? 0 : FILE_FLAG_BACKUP_SEMANTICS), nullptr);
        if (handle == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
        if (!ExactObject(handle, path, regularFile)) { CloseHandle(handle); return INVALID_HANDLE_VALUE; }
        handles.push_back(handle); // No DELETE sharing; inputs also deny writes.
        return handle;
    }
    bool Ancestors(const std::filesystem::path& path) {
        if (!path.is_absolute() || path != path.lexically_normal()) return false;
        std::vector<std::filesystem::path> paths;
        for (auto ancestor = path; !ancestor.empty();) {
            paths.push_back(ancestor);
            const auto parent = ancestor.parent_path();
            if (parent == ancestor) break;
            ancestor = parent;
        }
        for (auto it = paths.rbegin(); it != paths.rend(); ++it)
            if (Open(*it, false) == INVALID_HANDLE_VALUE) return false;
        return true;
    }
};

inline std::optional<std::string> ReadHandle(HANDLE file) {
    LARGE_INTEGER size{}, zero{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024 ||
        !SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return std::nullopt;
    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < text.size()) {
        DWORD read = 0;
        if (!ReadFile(file, text.data() + offset,
                static_cast<DWORD>(text.size() - offset), &read, nullptr) || !read) return std::nullopt;
        offset += read;
    }
    return text;
}

inline bool WriteNew(const std::filesystem::path& path, const std::string& bytes,
                     std::string* readback = nullptr) {
    // Direct evidence output may remain partial on failure; it must never
    // truncate, replace, clean up, or "roll back" an object owned by another writer.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = ExactObject(file, path, true);
    std::size_t offset = 0;
    while (ok && offset < bytes.size()) {
        DWORD written = 0;
        ok = WriteFile(file, bytes.data() + offset,
            static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024)), &written, nullptr) && written;
        offset += written;
    }
    ok = ok && FlushFileBuffers(file);
    const auto verified = ok ? ReadHandle(file) : std::nullopt;
    ok = verified && *verified == bytes && ExactObject(file, path, true);
    if (ok && readback) *readback = *verified;
    return CloseHandle(file) != FALSE && ok;
}

inline bool JsonMatches(const JsonValue& expected, const JsonValue& actual,
                        const std::string& path, std::vector<std::string>& differences,
                        std::size_t& expiredHistory, std::int64_t historyCutoff,
                        bool allowHistoryRetention = false) {
    const auto mismatch = [&] {
        if (differences.size() < 64) differences.push_back(path);
        return false;
    };
    if (const auto* object = expected.AsObject()) {
        const auto* other = actual.AsObject();
        if (!other || object->size() != other->size()) return mismatch();
        bool ok = true;
        for (const auto& [key, value] : *object) {
            const auto found = other->find(key);
            if (found == other->end()) { ok = mismatch(); continue; }
            if (!JsonMatches(value, found->second, path + "." + key, differences,
                    expiredHistory, historyCutoff, allowHistoryRetention)) ok = false;
        }
        return ok;
    }
    if (const auto* array = expected.AsArray()) {
        const auto* other = actual.AsArray();
        if (!other) return mismatch();
        const JsonValue::Array* source = array;
        JsonValue::Array retained;
        if (allowHistoryRetention && path.starts_with("state.cores[") &&
            path.find(".files[") != std::string::npos && path.ends_with(".recent_opens")) {
            for (const auto& item : *array) {
                const auto* timestamp = item.AsNumber();
                if (!timestamp || !std::isfinite(*timestamp) || std::floor(*timestamp) != *timestamp)
                    return mismatch();
                if (*timestamp >= static_cast<double>(historyCutoff)) retained.push_back(item);
            }
            if (retained.size() > 64) retained.erase(retained.begin(), retained.end() - 64);
            expiredHistory += array->size() - retained.size();
            source = &retained;
        }
        if (source->size() != other->size()) return mismatch();
        bool ok = true;
        for (std::size_t i = 0; i < source->size(); ++i)
            if (!JsonMatches((*source)[i], (*other)[i], path + "[" + std::to_string(i) + "]",
                    differences, expiredHistory, historyCutoff, allowHistoryRetention)) ok = false;
        return ok;
    }
    if (const auto* text = expected.AsString())
        return (actual.AsString() && *text == *actual.AsString()) || mismatch();
    if (const auto* number = expected.AsNumber())
        return (actual.AsNumber() && std::isfinite(*number) && *number == *actual.AsNumber()) || mismatch();
    if (const auto* boolean = expected.AsBool())
        return (actual.AsBool() && *boolean == *actual.AsBool()) || mismatch();
    return (!actual.AsObject() && !actual.AsArray() && !actual.AsString() &&
            !actual.AsNumber() && !actual.AsBool()) || mismatch();
}

struct KnownNormalization {
    std::string path;
    double before = 0;
    double after = 0;
};

inline JsonValue ExistingEmptySystemCoreRules(const JsonValue& original,
                                             std::vector<KnownNormalization>& changes) {
    // This is the exact pre-existing 1.0.20.4 DeserializeState clamp, not a
    // DefView schema migration. Only the empty, invisible system Core can use
    // this allowance; all user Core layout fields remain byte-value strict.
    const auto* object = original.AsObject();
    if (!object) return original;
    const auto* value = FindMember(*object, "cores");
    const auto* source = value ? value->AsArray() : nullptr;
    if (!source) return original;
    auto root = *object;
    auto cores = *source;
    for (std::size_t i = 0; i < cores.size(); ++i) {
        const auto* raw = cores[i].AsObject();
        if (!raw || NumberMember(*raw, "id", 0) != kUnclassifiedCoreId) continue;
        const auto* filesValue = FindMember(*raw, "files");
        const auto* files = filesValue ? filesValue->AsArray() : nullptr;
        const auto* innerValue = FindMember(*raw, "inner_radius");
        const auto* middleValue = FindMember(*raw, "middle_radius");
        const auto* inner = innerValue ? innerValue->AsNumber() : nullptr;
        const auto* middle = middleValue ? middleValue->AsNumber() : nullptr;
        if (!files || !files->empty() || !inner || !middle ||
            !std::isfinite(*inner) || !std::isfinite(*middle)) continue;
        auto core = *raw;
        const float normalizedInner = std::clamp(static_cast<float>(*inner), 60.0f, 320.0f);
        const float normalizedMiddle = std::clamp(static_cast<float>(*middle), normalizedInner + 36.0f, 420.0f);
        const auto allow = [&](const char* key, double before, float after) {
            if (static_cast<float>(before) == after) return;
            core[key] = JsonValue(static_cast<double>(after));
            changes.push_back({"state.cores[" + std::to_string(i) + "]." + key, before, after});
        };
        allow("inner_radius", *inner, normalizedInner);
        allow("middle_radius", *middle, normalizedMiddle);
        cores[i] = JsonValue(std::move(core));
    }
    root["cores"] = JsonValue(std::move(cores));
    return JsonValue(std::move(root));
}

inline bool Run(const std::filesystem::path& root) {
    // Caller supplies only a task directory, never an arbitrary input/output
    // path. Fixed filenames, single-link copies and a new output directory
    // ensure this entry cannot overwrite the original configuration.
    Locks locks;
    if (root.filename().wstring().rfind(L"V2-DEFVIEW-PRODUCTIZATION-", 0) != 0 ||
        !PathEquals(root.parent_path().wstring(), L"C:\\Gravity-P0\\out\\validation") ||
        !locks.Ancestors(root) || locks.Open(root / L"config-copy", false) == INVALID_HANDLE_VALUE) return false;
    const std::array<std::filesystem::path, 3> files{{root / L"config-copy" / L"state.json",
        root / L"config-copy" / L"relationships.json", root / L"config-copy" / L"preferences.json"}};
    std::array<std::string, 3> inputs;
    std::array<HANDLE, 3> inputHandles{};
    std::array<std::array<std::uint8_t, 32>, 3> hashes{};
    for (std::size_t i = 0; i < files.size(); ++i) {
        inputHandles[i] = locks.Open(files[i], true);
        if (inputHandles[i] == INVALID_HANDLE_VALUE) return false;
        const auto text = ReadHandle(inputHandles[i]);
        const auto hash = text ? Sha256Bytes(text->data(), text->size()) : std::nullopt;
        if (!text || !hash) return false;
        inputs[i] = *text;
        hashes[i] = *hash;
    }
    const auto output = root / L"compatibility-result";
    std::error_code error;
    if (!std::filesystem::create_directory(output, error) || error ||
        locks.Open(output, false) == INVALID_HANDLE_VALUE) return false;
    std::vector<std::string> differences;
    std::vector<KnownNormalization> normalizations;
    std::size_t trimmedHistory = 0;
    const auto cutoff = UnixTimeNow() - 30LL * 24 * 60 * 60;
    JsonValue rawState, rawRelationships, rawPreferences, savedState, expectedRelationships;
    WorkspaceState parsed, reloaded;
    RelationshipRecoveryManifest relationshipFormat;
    bool passed = JsonParser(inputs[0]).Parse(rawState) && rawState.AsObject() &&
        JsonParser(inputs[1]).Parse(rawRelationships) && rawRelationships.AsObject() &&
        JsonParser(inputs[2]).Parse(rawPreferences) && rawPreferences.AsObject() &&
        DeserializeState(inputs[0], parsed) && ValidateRelationState(parsed).IsValid() &&
        DeserializeRelationshipRecoveryManifest(inputs[1], relationshipFormat);
    if (passed) {
        const auto serialized = SerializeState(parsed);
        passed = JsonParser(serialized).Parse(savedState) &&
            JsonParser(SerializeRelationshipManifest(parsed)).Parse(expectedRelationships);
        if (passed) {
            // Raw comparison, not a comparison of two normalized models:
            // silently skipped entries/unknown fields cannot make this PASS.
            const auto expectedState = ExistingEmptySystemCoreRules(rawState, normalizations);
            const bool stateMatches = JsonMatches(expectedState, savedState, "state", differences,
                trimmedHistory, cutoff, true);
            std::size_t unused = 0;
            const bool relationshipsMatch = JsonMatches(rawRelationships, expectedRelationships,
                "relationships", differences, unused, cutoff);
            passed = stateMatches && relationshipsMatch;
        }
        if (passed) {
            std::string saved;
            passed = WriteNew(output / L"state-roundtrip.json", serialized, &saved);
            passed = passed && DeserializeState(saved, reloaded) &&
                ValidateRelationState(reloaded).IsValid() && SerializeState(reloaded) == serialized &&
                WriteNew(output / L"relationships-expected.json", SerializeRelationshipManifest(parsed));
        }
    }
    bool inputsUnchanged = true;
    for (std::size_t i = 0; i < files.size(); ++i)
        inputsUnchanged = inputsUnchanged && ExactObject(inputHandles[i], files[i], true) &&
            ReadHandle(inputHandles[i]) == std::optional<std::string>(inputs[i]);
    passed = passed && inputsUnchanged;
    std::size_t entries = 0;
    for (const auto& core : parsed.cores) entries += core.files.size();
    std::ostringstream report;
    report << "{\n  \"passed\": " << (passed ? "true" : "false")
           << ",\n  \"cores\": " << parsed.cores.size() << ",\n  \"entries\": " << entries
           << ",\n  \"history_entries_trimmed_by_existing_30_day_64_item_policy\": " << trimmedHistory
           << ",\n  \"inputs_unchanged\": " << (inputsUnchanged ? "true" : "false")
           << ",\n  \"reference_targets_opened\": false,\n  \"gui_run\": false,\n"
           << "  \"preferences_check\": \"syntax_and_bytes_only_not_GUI_semantics\",\n  \"input_sha256\": [";
    for (std::size_t i = 0; i < hashes.size(); ++i)
        report << (i ? ", " : "") << '"' << Sha256Hex(hashes[i]) << '"';
    report << "],\n  \"different_json_paths\": [";
    for (std::size_t i = 0; i < differences.size(); ++i)
        report << (i ? ", " : "") << EscapeJson(Utf8ToWide(differences[i]));
    report << "],\n  \"existing_parser_normalizations\": [";
    for (std::size_t i = 0; i < normalizations.size(); ++i) {
        const auto& change = normalizations[i];
        report << (i ? ", " : "") << "{\"path\": " << EscapeJson(Utf8ToWide(change.path))
               << ", \"before\": " << std::setprecision(9) << change.before << ", \"after\": " << change.after
               << ", \"baseline_commit\": \"798982c540e3e4503410d3d1589205119c6cdedc\"}";
    }
    report << "]\n}\n";
    return WriteNew(output / L"audit.json", report.str()) && passed;
}

} // namespace compatibility_audit
