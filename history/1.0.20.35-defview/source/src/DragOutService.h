#pragma once

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string_view>

namespace gravitydesktop::dragout {

enum class Result {
    Completed,
    Cancelled,
    SourceUnavailable,
    Failed,
};

// Starts a standard OLE file drag on the calling OLE/STA UI thread.  The
// service only exposes a real source path and always advertises COPY.
Result StartDrag(HWND owner, std::wstring_view path);

// Creates the same IDataObject used by StartDrag.  This is also used by the
// isolated self-test; ownership is transferred to the caller.
HRESULT CreateDataObject(std::wstring_view path, IDataObject** result);

// Gravity's own private marker lets its IDropTarget reject a drag that is
// being returned to the same workspace without affecting other applications.
bool IsGravityDragData(IDataObject* data);

} // namespace gravitydesktop::dragout
