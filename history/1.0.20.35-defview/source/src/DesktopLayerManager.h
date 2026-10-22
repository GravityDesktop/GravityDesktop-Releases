#pragma once

#include <algorithm>
#include <cstdint>
#include <cwchar>

// A read-only view of the current Explorer desktop band and Gravity's two
// presentation surfaces. Events request a new observation; event HWNDs are
// never used as the desktop anchor.
struct DesktopLayerSnapshot {
    HWND foreground = nullptr;
    HWND host = nullptr;
    HWND layerHost = nullptr;
    HWND visual = nullptr;
    HWND interaction = nullptr;
    HWND divider = nullptr;
    HWND visualParent = nullptr;
    HWND visualOwner = nullptr;
    HWND interactionParent = nullptr;
    HWND interactionOwner = nullptr;
    int hostRank = -1;
    int layerRank = -1;
    int visualRank = -1;
    int interactionRank = -1;
    bool visualVisible = false;
    bool interactionVisible = false;
    bool dividerVisible = false;
    bool visualCloaked = false;
    bool visualTopmost = false;
    bool ordinaryBelowVisual = false;
    bool layerSplit = false;
    RECT visualRect{};
    RECT interactionRect{};
    RECT dividerRect{};
    std::uint64_t zorderFingerprint = 1469598103934665603ULL;
};

class DesktopLayerManager {
public:
    static DesktopLayerSnapshot Observe(HWND host, HWND visual,
                                        HWND interaction, HWND divider) {
        DesktopLayerSnapshot result;
        result.foreground = GetForegroundWindow();
        result.host = host;
        result.layerHost = host;
        result.visual = visual;
        result.interaction = interaction;
        result.divider = divider;
        const HWND desktop = GetDesktopWindow();
        if (IsWindow(visual)) {
            // GetParent returns the owner of a top-level popup.  Inspect the
            // actual parent separately so an owned companion is not mistaken
            // for a child of Explorer.
            result.visualParent = GetAncestor(visual, GA_PARENT);
            if (result.visualParent == desktop) result.visualParent = nullptr;
            result.visualOwner = GetWindow(visual, GW_OWNER);
            result.visualVisible = IsWindowVisible(visual) != FALSE;
            result.visualTopmost =
                (GetWindowLongPtrW(visual, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
            (void)GetWindowRect(visual, &result.visualRect);
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(visual, DWMWA_CLOAKED,
                                                &cloaked, sizeof(cloaked))))
                result.visualCloaked = cloaked != 0;
        }
        if (IsWindow(interaction)) {
            result.interactionParent = GetAncestor(interaction, GA_PARENT);
            if (result.interactionParent == desktop)
                result.interactionParent = nullptr;
            result.interactionOwner = GetWindow(interaction, GW_OWNER);
            result.interactionVisible = IsWindowVisible(interaction) != FALSE;
            (void)GetWindowRect(interaction, &result.interactionRect);
        }
        if (IsWindow(divider)) {
            result.dividerVisible = IsWindowVisible(divider) != FALSE;
            (void)GetWindowRect(divider, &result.dividerRect);
        }
        DWORD explorerPid = 0;
        if (IsWindow(host)) GetWindowThreadProcessId(host, &explorerPid);
        int rank = 0;
        for (HWND current = GetTopWindow(nullptr); current && rank < 4096;
             current = GetWindow(current, GW_HWNDNEXT), ++rank) {
            result.zorderFingerprint ^=
                static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(current));
            result.zorderFingerprint *= 1099511628211ULL;
            if (current == host) result.hostRank = rank;
            if (current == visual) result.visualRank = rank;
            if (current == interaction) result.interactionRank = rank;
            if (current == host ||
                IsVisibleExplorerDesktopBand(current, explorerPid,
                                             result.visualRect)) {
                if (result.layerRank < 0 || rank < result.layerRank) {
                    result.layerRank = rank;
                    result.layerHost = current;
                }
            }
        }
        result.layerSplit = result.visualRank >= result.layerRank &&
                            result.interactionRank >= 0 &&
                            result.interactionRank < result.layerRank &&
                            result.layerRank >= 0;
        if (result.visualRank >= 0 && result.layerRank > result.visualRank) {
            for (HWND current = GetWindow(visual, GW_HWNDNEXT);
                 current && current != result.layerHost;
                 current = GetWindow(current, GW_HWNDNEXT)) {
                if (current == interaction || current == divider) continue;
                DWORD pid = 0;
                GetWindowThreadProcessId(current, &pid);
                if (pid == GetCurrentProcessId() ||
                    IsWindowVisible(current) == FALSE || IsIconic(current) ||
                    (GetWindowLongPtrW(current, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
                    continue;
                DWORD cloaked = 0;
                if (SUCCEEDED(DwmGetWindowAttribute(current, DWMWA_CLOAKED,
                                                    &cloaked, sizeof(cloaked))) &&
                    cloaked) continue;
                RECT rect{}, overlap{};
                if (GetWindowRect(current, &rect) &&
                    IntersectRect(&overlap, &rect, &result.visualRect)) {
                    result.ordinaryBelowVisual = true;
                    break;
                }
            }
        }
        return result;
    }

    static HWND InsertionAnchor(const DesktopLayerSnapshot& snapshot) {
        if (snapshot.layerRank < 0 || !IsWindow(snapshot.layerHost))
            return nullptr;
        HWND anchor = GetWindow(snapshot.layerHost, GW_HWNDPREV);
        while (anchor == snapshot.visual || anchor == snapshot.interaction ||
               anchor == snapshot.divider)
            anchor = GetWindow(anchor, GW_HWNDPREV);
        // If Explorer is the first normal window, this inserts at the top of
        // the normal band. It never makes the Visual topmost.
        if (!anchor ||
            (GetWindowLongPtrW(anchor, GWL_EXSTYLE) & WS_EX_TOPMOST))
            return HWND_NOTOPMOST;
        return anchor;
    }

    static const char* Failure(const DesktopLayerSnapshot& state,
                               bool expectedVisible, bool expectedDivider,
                               int interactionOverhang) {
        if (!expectedVisible) return nullptr;
        if (state.hostRank < 0 || state.layerRank < 0)
            return "desktop_layer_host_missing";
        if (state.visualRank < 0 || state.visualParent ||
            state.visualOwner != state.host)
            return "desktop_layer_visual_topology";
        if (state.visualTopmost) return "desktop_layer_visual_topmost";
        if (!state.visualVisible || state.visualCloaked)
            return "desktop_layer_visual_hidden";
        if (state.visualRect.right <= state.visualRect.left ||
            state.visualRect.bottom <= state.visualRect.top)
            return "desktop_layer_visual_rect";
        if (state.visualRank >= state.layerRank)
            return "desktop_layer_visual_behind_shell";
        if (state.ordinaryBelowVisual)
            return "desktop_layer_visual_over_app";
        // Both popups are owned by the selected desktop host, so a shell
        // reorder cannot strand the input behind its still-visible Visual.
        // Their relative Z-order must still leave input above Visual.
        if (state.interactionRank < 0 ||
            state.interactionParent || state.interactionOwner != state.host ||
            !state.interactionVisible ||
            state.interactionRank >= state.layerRank ||
            state.interactionRank <= state.visualRank)
            return "desktop_layer_interaction";
        if (state.interactionRect.left !=
                state.visualRect.left - interactionOverhang ||
            state.interactionRect.top != state.visualRect.top ||
            state.interactionRect.right != state.visualRect.right ||
            state.interactionRect.bottom != state.visualRect.bottom)
            return "desktop_layer_interaction_rect";
        if (expectedDivider &&
            (!state.dividerVisible || state.dividerRect.right <= state.dividerRect.left ||
             state.dividerRect.bottom <= state.dividerRect.top))
            return "desktop_layer_divider_missing";
        if (!expectedDivider && state.dividerVisible)
            return "desktop_layer_divider_unexpected";
        return nullptr;
    }

private:
    static bool IsVisibleExplorerDesktopBand(HWND candidate, DWORD explorerPid,
                                              const RECT& visualRect) {
        if (!explorerPid || !IsWindowVisible(candidate)) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(candidate, &pid);
        if (pid != explorerPid) return false;
        wchar_t name[32]{};
        if (!GetClassNameW(candidate, name, static_cast<int>(std::size(name))) ||
            (wcscmp(name, L"Progman") && wcscmp(name, L"WorkerW")))
            return false;
        RECT rect{}, overlap{};
        return GetWindowRect(candidate, &rect) &&
               IntersectRect(&overlap, &rect, &visualRect);
    }
};
