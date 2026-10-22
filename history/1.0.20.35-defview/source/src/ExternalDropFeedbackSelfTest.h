#pragma once

// Included alongside the existing offline self-tests, not a product startup.
bool RunExternalDropFeedbackSelfTest() {
    const SelfTestModeScope selfTestMode;
    const auto root = SelfTestTempRoot() / (L"GravityDesktop-drop-feedback-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!ValidateSelfTestPath(root)) return false;
    std::error_code error;
    if (!std::filesystem::create_directory(root, error) || error) return false;
    const auto library = root / L"library";
    const auto desktop = root / L"desktop";
    std::filesystem::create_directory(library, error);
    if (error) return false;
    std::filesystem::create_directory(desktop, error);
    if (error) return false;
    const auto source = root / L"source.txt", second = root / L"second.txt";
    if (!WriteUtf8Atomic(source, "reference source\n") ||
        !WriteUtf8Atomic(second, "second source\n")) return false;
    const auto sourceHash = Sha256File(source), secondHash = Sha256File(second);
    if (!sourceHash || !secondHash) return false;
    unsigned passed = 0, failed = 0;
    std::ostringstream results;
    const auto check = [&](const char* name, bool value) {
        results << (value ? "PASS " : "FAIL ") << name << '\n';
        value ? ++passed : ++failed;
    };
    check("COPY accepted", gravity::drop::Effect(DROPEFFECT_COPY, true) == DROPEFFECT_COPY);
    check("LINK preferred", gravity::drop::Effect(DROPEFFECT_COPY | DROPEFFECT_LINK, true) == DROPEFFECT_LINK);
    check("MOVE-only rejected", gravity::drop::Effect(DROPEFFECT_MOVE, true) == DROPEFFECT_NONE);
    check("no offered operation rejected", gravity::drop::Effect(0, true) == DROPEFFECT_NONE);
    check("rejected input has no effect", gravity::drop::Effect(DROPEFFECT_COPY | DROPEFFECT_LINK, false) == DROPEFFECT_NONE);
    GravityApp app;
    app.stateFile_ = root / L"state.json";
    app.desktopPathOverride_ = desktop;
    app.workspaceRecoveryRootOverride_ = library;
    app.state_.managedLibraryPath = library.wstring();
    app.state_.nextId = 3;
    Core unclassified;
    unclassified.id = kUnclassifiedCoreId;
    unclassified.name = L"未分类";
    app.state_.cores.push_back(unclassified);
    Core core;
    core.id = 2;
    core.name = L"drop result fixture";
    core.position = D2D1::Point2F(300, 300);
    app.state_.cores.push_back(core);
    check("accepted reference reports success", app.CommitExternalDropPaths({source.wstring()}, 2, 0, core.position));
    check("accepted reference exists once", app.FindCore(2)->files.size() == 1 && app.FindItemByPath(source.wstring()));
    WorkspaceState disk;
    const auto text = ReadUtf8File(app.stateFile_);
    const bool loaded = text && DeserializeState(*text, disk);
    check("saved state readable before OLE acknowledgement", loaded);
    check("saved relationship model is valid", loaded && ValidateRelationState(disk).IsValid());
    check("both system and user Core retained", loaded && disk.cores.size() == 2);
    const auto diskCore = std::find_if(disk.cores.begin(), disk.cores.end(),
        [](const Core& value) { return value.id == 2; });
    const auto diskSystemCore = std::find_if(disk.cores.begin(), disk.cores.end(),
        [](const Core& value) { return value.id == kUnclassifiedCoreId; });
    check("system Core remains empty", diskSystemCore != disk.cores.end() && diskSystemCore->files.empty());
    check("saved target contains the accepted source exactly once", diskCore != disk.cores.end() &&
        diskCore->files.size() == 1 && PathEquals(diskCore->files[0].path, source.wstring()));
    check("saved source remains a reference not a managed move", diskCore != disk.cores.end() &&
        diskCore->files.size() == 1 && diskCore->files[0].storageMode == FileStorageMode::Reference &&
        diskCore->files[0].managedPath.empty());
    const auto before = SerializeState(app.state_);
    const auto stateHash = Sha256File(app.stateFile_);
    check("duplicate reports no effect", !app.CommitExternalDropPaths({source.wstring()}, 2, 0, core.position));
    check("duplicate does not alter saved state", stateHash && Sha256File(app.stateFile_) == stateHash && SerializeState(app.state_) == before);
    check("empty input rejected", !app.CommitExternalDropPaths({}, 2, 0, core.position));
    check("absent target rejected", !app.CommitExternalDropPaths({second.wstring()}, 999, 0, core.position));
    check("protected library import rejected", !app.CommitExternalDropPaths({library.wstring()}, 2, 0, core.position));
    const auto blocker = root / L"save-blocker";
    if (!WriteUtf8Atomic(blocker, "not a directory")) return false;
    app.stateFile_ = blocker / L"state.json";
    check("actual save failure reports rejection", !app.CommitExternalDropPaths({second.wstring()}, 2, 0, core.position));
    check("save failure restores complete model", SerializeState(app.state_) == before && !app.FindItemByPath(second.wstring()));
    check("all rejection paths preserve sources", Sha256File(source) == sourceHash && Sha256File(second) == secondHash);
    app.stateFile_ = root / L"state.json";
    check("mixed duplicate/new batch accepts actual addition", app.CommitExternalDropPaths({source.wstring(), second.wstring()}, 2, 0, core.position));
    check("mixed batch contains exactly two references", app.FindCore(2)->files.size() == 2);
    const auto unchangedModel = SerializeState(app.state_);
    const auto unchangedDisk = Sha256File(app.stateFile_);
    app.dirty_ = false;
    app.status_ = L"preserve prior user feedback";
    app.CheckReferences(std::nullopt, true);
    check("healthy automatic check is silent", app.status_ == L"preserve prior user feedback");
    check("healthy automatic check changes neither model nor disk", !app.dirty_ &&
        SerializeState(app.state_) == unchangedModel && unchangedDisk && Sha256File(app.stateFile_) == unchangedDisk);
    app.CheckReferences();
    check("explicit healthy check still reports its result", app.status_ == app.T("status.references_check_ok"));
    app.FindCore(2)->files[0].missing = true;
    app.status_ = L"before restored reference";
    app.CheckReferences(std::nullopt, true);
    check("restored reference is corrected and save is scheduled", !app.FindCore(2)->files[0].missing && app.dirty_);
    check("real automatic state change is still reported", app.status_ == app.T("status.references_check_ok"));
    FileItem missingFixture;
    missingFixture.id = 100;
    missingFixture.path = (root / L"actually-absent.txt").wstring();
    app.FindCore(2)->files.push_back(missingFixture);
    app.dirty_ = false;
    app.CheckReferences(std::nullopt, true);
    check("actual absent reference still becomes missing", app.FindCore(2)->files.back().missing && app.dirty_);
    check("automatic missing reference is not silent", app.status_ != app.T("status.references_check_ok"));
    check("inaccessible automatic result still needs notice (policy only)",
        GravityApp::ReferenceCheckNeedsNotice(true, 0, 0, 1));

    GravityApp bindingOwner;
    const HWND fakeInput = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x123));
    bindingOwner.interactionWindow_ = fakeInput;
    bindingOwner.defViewInputGeneration_ = 7;
    auto* binding = new GravityApp::InteractionDropTarget(&bindingOwner, fakeInput, 7);
    check("matching OLE receipt token (no OLE call)", binding->MatchesBinding(&bindingOwner, fakeInput, 7));
    check("stale OLE receipt generation rejected", !binding->MatchesBinding(&bindingOwner, fakeInput, 6));
    check("foreign owner receipt rejected", !binding->MatchesBinding(&app, fakeInput, 7));
    bindingOwner.defViewInputGeneration_ = 8;
    check("recreated input cannot borrow old receipt", !binding->MatchesBinding(&bindingOwner, fakeInput, 7));
    binding->Detach();
    check("detached drop target cannot authorize reuse", !binding->MatchesBinding(&bindingOwner, fakeInput, 7));
    binding->Release();
    bindingOwner.interactionWindow_ = nullptr; // Synthetic token was never a HWND.
    check("no source deleted or changed", Sha256File(source) == sourceHash && Sha256File(second) == secondHash);
    check("self-test containment intact", !gSelfTestPathViolation);
    results << "passed=" << passed << " failed=" << failed << " GUI=NOT_RUN OLE_CALLBACK=NOT_RUN\n";
    return WriteUtf8Atomic(root / L"results.txt", results.str()) && failed == 0;
}
