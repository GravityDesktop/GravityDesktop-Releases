#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <climits>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace gravity::defview {

inline constexpr char kBuild[] = "120-035-DEFVIEW-PRODUCT-LOCALTEST-R2";
inline constexpr char kVersion[] = "1.0.20.35";
// Presentation and data isolation are different policies. Production uses
// the same DefView scene; only the explicitly compiled fixture build redirects
// user data and restricts file references to its validated test root.
#if defined(GRAVITY_DEFVIEW_PRESENTATION) || defined(GRAVITY_DEFVIEW_LOCALTEST)
inline constexpr bool kPresentationCompiled = true;
#else
inline constexpr bool kPresentationCompiled = false;
#endif
#ifdef GRAVITY_DEFVIEW_LOCALTEST
inline constexpr bool kCompiled = true;
#else
inline constexpr bool kCompiled = false;
#endif

inline bool IdentityAllowed(std::string_view channel, std::string_view build,
                            std::string_view version, bool packaged) {
    return !packaged && channel == "LOCALTEST" && build == kBuild &&
           version == kVersion;
}

inline bool LocalTestArgument(std::wstring_view arguments) {
    // WinMain receives the raw tail. PowerShell Start-Process can append a
    // separator after its final argument. Ignore only surrounding whitespace;
    // do not accept another token, quoted alternatives, or partial matches.
    const auto first = arguments.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return false;
    const auto last = arguments.find_last_not_of(L" \t\r\n");
    return arguments.substr(first, last - first + 1) == L"--defview-localtest";
}

inline bool EntryAllowed(bool compiled, std::string_view channel,
                          std::string_view build, std::string_view version,
                          bool packaged, std::wstring_view arguments) {
    if (compiled) return IdentityAllowed(channel, build, version, packaged) &&
        LocalTestArgument(arguments);
    // A reserved request/identity with the compile gate missing must NOT fall
    // through to ordinary production startup and real user data.
    return arguments.find(L"--defview-localtest") == std::wstring_view::npos &&
        !build.starts_with("120-031-DEFVIEW-PRODUCT-LOCALTEST-") &&
        !build.starts_with("120-032-DEFVIEW-PRODUCT-LOCALTEST-") &&
        !build.starts_with("120-033-DEFVIEW-PRODUCT-LOCALTEST-") &&
        !build.starts_with("120-034-DEFVIEW-PRODUCT-LOCALTEST-") &&
        !build.starts_with("120-035-DEFVIEW-PRODUCT-LOCALTEST-");
}

// Set only after the fail-closed startup gate. Never infer a real user path.
inline std::filesystem::path isolatedRoot;

struct RebuildWork {
    unsigned teardownDepth = 0;
    bool wanted = false;
    bool posted = false;
    bool Dispatchable() const { return wanted && !teardownDepth; }
};

// Receipt for the computed hit region, not the empty bootstrap region.
// Invalidate it before a geometry/region change and record only after the
// actual SetWindowRgn succeeds for the same live Input lifetime.
struct InputRegionReceipt {
    HWND window = nullptr;
    std::uint64_t generation = 0;
    void Clear() { window = nullptr; generation = 0; }
    void Record(HWND input, std::uint64_t lifetime) { window = input; generation = lifetime; }
    bool Matches(HWND input, std::uint64_t lifetime) const {
        return input && lifetime && window == input && generation == lifetime;
    }
};

inline bool FrameSubmitted(bool renderSucceeded, HRESULT presentResult,
                           std::uint64_t before, std::uint64_t after) {
    return renderSucceeded && SUCCEEDED(presentResult) && after > before;
}

// Replacement-only publication lease. Bootstrap startup keeps its existing
// paint flow. Every replacement reserves a new lifetime BEFORE CreateWindow
// (which can pump messages), and cannot borrow a prior HWND's submitted frame.
struct ReconnectFrameGate {
    std::uint64_t lifetime = 0;
    std::uint64_t verifiedLifetime = 0;
    HWND surface = nullptr;

    bool Begin() {
        surface = nullptr;
        verifiedLifetime = 0;
        if (lifetime == UINT64_MAX) return false;
        ++lifetime;
        return true;
    }
    bool Bind(HWND window, std::uint64_t expected) {
        if (!window || !lifetime || lifetime != expected || surface || verifiedLifetime) return false;
        surface = window;
        return true;
    }
    bool Verify(HWND window, std::uint64_t expected, bool submitted) {
        if (!submitted || !window || window != surface || !lifetime || lifetime != expected) return false;
        verifiedLifetime = lifetime;
        return true;
    }
    bool CanPublish(HWND window) const {
        return window && (!lifetime ||
            (surface == window && verifiedLifetime == lifetime));
    }
    void Retired(HWND window) {
        if (window == surface) {
            surface = nullptr;
            verifiedLifetime = 0;
        }
    }
};

inline bool MonitorIdentifier(std::wstring_view text) {
    // MONITORINFOEX::szDevice is display metadata, not a file reference. Only
    // the complete numeric DISPLAY device spelling is exempt from path checks.
    constexpr std::wstring_view prefix = L"\\\\.\\DISPLAY";
    if (!text.starts_with(prefix) || text.size() == prefix.size()) return false;
    return std::all_of(text.begin() + prefix.size(), text.end(),
        [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
}

inline bool JsonReferenceNeedsContainment(std::string_view key, bool rooted,
                                          bool translationDictionary) {
    // Recovery Tools copies its UI dictionaries into the isolated library.
    // Their key names (e.g. dialog.full_path.title) are not file references.
    // Absolute/device/UNC values still require containment even in this one
    // directory; every other JSON keeps the original conservative key check.
    return rooted || (!translationDictionary && key.find("path") != std::string_view::npos);
}

// Runtime drop containment, not a general user-file management policy. This
// exact LOCALTEST candidate accepts only ordinary non-linked fixture objects.
inline bool FixturePathAllowed(const std::filesystem::path& candidate,
                               const std::filesystem::path& root) {
    if (root.empty() || !candidate.is_absolute()) return false;
    const auto path = candidate.lexically_normal();
    auto prefix = root.lexically_normal().wstring();
    if (prefix.empty()) return false;
    if (prefix.back() != L'\\') prefix.push_back(L'\\');
    const auto text = path.wstring();
    if (text.find(L':', 2) != std::wstring::npos) return false; // No ADS fixture paths.
    if (text.size() < prefix.size() || CompareStringOrdinal(text.c_str(),
        static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) != CSTR_EQUAL)
        return false;
    if (_wcsicmp(path.extension().c_str(), L".lnk") == 0) return false;
    auto ancestor = path;
    while (!ancestor.empty()) {
        const DWORD attributes = GetFileAttributesW(ancestor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return true;
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    const bool allowed = file != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return allowed;
}

inline bool ClassIs(HWND window, const wchar_t* expected) {
    wchar_t actual[128]{};
    return IsWindow(window) && GetClassNameW(window, actual, 128) &&
           wcscmp(actual, expected) == 0;
}

inline std::optional<FILETIME> ProcessCreation(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return std::nullopt;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool ok = GetProcessTimes(process, &created, &exited, &kernel, &user) &&
                   WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return ok ? std::optional<FILETIME>(created) : std::nullopt;
}

struct Host {
    HWND shell = nullptr;
    HWND root = nullptr;       // Progman/WorkerW; NOT the scene parent.
    HWND defView = nullptr;    // All three product surfaces are siblings here.
    HWND list = nullptr;
    DWORD pid = 0;
    DWORD tid = 0;
    FILETIME processCreated{};

    bool Valid() const {
        DWORD actualPid = 0;
        const DWORD actualTid = GetWindowThreadProcessId(defView, &actualPid);
        DWORD rootPid = 0, listPid = 0, shellPid = 0;
        GetWindowThreadProcessId(root, &rootPid);
        GetWindowThreadProcessId(list, &listPid);
        GetWindowThreadProcessId(shell, &shellPid);
        const auto created = ProcessCreation(pid);
        RECT client{};
        return pid && actualPid == pid && rootPid == pid && listPid == pid &&
            shellPid == pid && tid == actualTid && shell == GetShellWindow() &&
            (ClassIs(root, L"Progman") || ClassIs(root, L"WorkerW")) &&
            ClassIs(defView, L"SHELLDLL_DefView") &&
            ClassIs(list, L"SysListView32") && GetParent(defView) == root &&
            GetAncestor(root, GA_ROOT) == root && GetParent(list) == defView &&
            created && CompareFileTime(&*created, &processCreated) == 0 &&
            GetClientRect(defView, &client) && client.right > client.left &&
            client.bottom > client.top;
    }

    bool Same(const Host& other) const {
        return shell == other.shell && root == other.root && defView == other.defView &&
            list == other.list && pid == other.pid && tid == other.tid &&
            CompareFileTime(&processCreated, &other.processCreated) == 0;
    }

    // Identity comparison only, NOT a replacement for Valid(). Explorer may
    // move this same DefView between its Progman/WorkerW roots. A caller must
    // validate the new complete Host and the live product surfaces before
    // adopting its root; the old cached Host is necessarily invalid then.
    bool SameScene(const Host& other) const {
        return shell && defView && list && pid && tid &&
            (processCreated.dwLowDateTime || processCreated.dwHighDateTime) &&
            shell == other.shell && defView == other.defView && list == other.list &&
            pid == other.pid && tid == other.tid &&
            CompareFileTime(&processCreated, &other.processCreated) == 0;
    }

    bool ReadyForScene() const {
        // A hidden parent cannot display our children. Do not confuse this
        // Shell transition with a failed Win32 create or OLE registration.
        return Valid() && IsWindowVisible(root) && IsWindowVisible(defView);
    }
};

inline std::optional<Host> FindHost() {
    struct Search { Host found; unsigned matches = 0; } search;
    EnumWindows([](HWND root, LPARAM context) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(context);
        if (!ClassIs(root, L"Progman") && !ClassIs(root, L"WorkerW")) return TRUE;
        for (HWND view = FindWindowExW(root, nullptr, L"SHELLDLL_DefView", nullptr);
             view; view = FindWindowExW(root, view, L"SHELLDLL_DefView", nullptr)) {
            Host candidate;
            candidate.shell = GetShellWindow();
            candidate.root = root;
            candidate.defView = view;
            candidate.list = FindWindowExW(view, nullptr, L"SysListView32", nullptr);
            candidate.tid = GetWindowThreadProcessId(view, &candidate.pid);
            const auto created = ProcessCreation(candidate.pid);
            if (!created) continue;
            candidate.processCreated = *created;
            if (!candidate.Valid()) continue;
            result.found = candidate;
            ++result.matches;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    // Never guess among duplicate/transitional desktop views.
    if (search.matches != 1 || !search.found.Valid()) return std::nullopt;
    return search.found;
}

inline bool ChildStyleValid(LONG_PTR style, LONG_PTR exstyle,
                            bool visual) {
    const LONG_PTR required = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
        (visual ? WS_EX_TRANSPARENT : 0);
    return (style & WS_CHILD) && !(style & WS_POPUP) &&
        (exstyle & required) == required &&
        !(exstyle & (WS_EX_TOPMOST | WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP)) &&
        (visual || !(exstyle & WS_EX_TRANSPARENT));
}

inline bool IsSurface(HWND surface, const Host& host, bool visual) {
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(surface, &pid);
    return host.Valid() && pid == GetCurrentProcessId() &&
        tid == GetCurrentThreadId() && GetParent(surface) == host.defView &&
        GetWindow(surface, GW_OWNER) == nullptr &&
        ChildStyleValid(GetWindowLongPtrW(surface, GWL_STYLE),
                        GetWindowLongPtrW(surface, GWL_EXSTYLE), visual);
}

// Finite direct-child traversal only. Native icon ListView must be below the
// whole group. Top-level window ranks are irrelevant to this child route.
inline bool SiblingOrder(int visual, int input, int divider, int list) {
    return visual >= 0 && input >= 0 && divider >= 0 && list >= 0 &&
        divider < visual && visual < input && input < list;
}

inline bool OrderValid(const Host& host, HWND visual, HWND input, HWND divider) {
    int v = -1, i = -1, d = -1, l = -1, count = 0;
    for (HWND child = GetWindow(host.defView, GW_CHILD); child && count < 4096;
         child = GetWindow(child, GW_HWNDNEXT), ++count) {
        if (child == visual) v = count;
        if (child == input) i = count;
        if (child == divider) d = count;
        if (child == host.list) l = count;
    }
    return count < 4096 && SiblingOrder(v, i, d, l);
}

inline std::optional<POINT> ClientOrigin(POINT screen, POINT parentScreen) {
    const auto x = static_cast<std::int64_t>(screen.x) - parentScreen.x;
    const auto y = static_cast<std::int64_t>(screen.y) - parentScreen.y;
    if (x < LONG_MIN || x > LONG_MAX || y < LONG_MIN || y > LONG_MAX)
        return std::nullopt;
    return POINT{static_cast<LONG>(x), static_cast<LONG>(y)};
}

inline bool ScreenToParent(HWND parent, POINT& point) {
    POINT parentScreen{};
    if (!IsWindow(parent) || !ClientToScreen(parent, &parentScreen)) return false;
    const auto client = ClientOrigin(point, parentScreen);
    if (!client) return false;
    point = *client;
    return true;
}

} // namespace gravity::defview
