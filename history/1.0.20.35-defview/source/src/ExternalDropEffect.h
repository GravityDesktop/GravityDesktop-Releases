#pragma once

#include <windows.h>
#include <oleidl.h>

namespace gravity::drop {

// External files are referenced or explicitly collected by our own confirmed
// transaction. Never tell an OLE source to remove its original with MOVE.
inline DWORD Effect(DWORD offered, bool accepted) {
    if (!accepted) return DROPEFFECT_NONE;
    if (offered & DROPEFFECT_LINK) return DROPEFFECT_LINK;
    if (offered & DROPEFFECT_COPY) return DROPEFFECT_COPY;
    return DROPEFFECT_NONE;
}

} // namespace gravity::drop
