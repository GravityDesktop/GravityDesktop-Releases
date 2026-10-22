#include "DragOutService.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace gravitydesktop::dragout {
namespace {

constexpr wchar_t kDragMarkerName[] =
    L"GravityDesktop.ExternalFileDragOut.1";

UINT DragMarkerFormat() {
    static const UINT format = RegisterClipboardFormatW(kDragMarkerName);
    return format;
}

HGLOBAL DuplicateGlobal(HGLOBAL source) {
    if (source == nullptr) return nullptr;
    const SIZE_T bytes = GlobalSize(source);
    if (bytes == 0) return nullptr;
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (copy == nullptr) return nullptr;
    void* sourceBytes = GlobalLock(source);
    void* destinationBytes = GlobalLock(copy);
    if (sourceBytes == nullptr || destinationBytes == nullptr) {
        if (destinationBytes != nullptr) GlobalUnlock(copy);
        if (sourceBytes != nullptr) GlobalUnlock(source);
        GlobalFree(copy);
        return nullptr;
    }
    std::memcpy(destinationBytes, sourceBytes, bytes);
    GlobalUnlock(copy);
    GlobalUnlock(source);
    return copy;
}

HGLOBAL CreateFileDropData(std::wstring_view path) {
    if (path.empty() || path.find(L'\0') != std::wstring_view::npos) {
        return nullptr;
    }
    const std::size_t bytes = sizeof(DROPFILES) +
        (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (memory == nullptr) return nullptr;
    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        return nullptr;
    }
    auto* drop = static_cast<DROPFILES*>(locked);
    drop->pFiles = sizeof(DROPFILES);
    drop->pt = POINT{0, 0};
    drop->fNC = FALSE;
    drop->fWide = TRUE;
    auto* names = reinterpret_cast<wchar_t*>(
        static_cast<std::byte*>(locked) + sizeof(DROPFILES));
    std::memcpy(names, path.data(), path.size() * sizeof(wchar_t));
    names[path.size()] = L'\0';
    names[path.size() + 1] = L'\0';
    GlobalUnlock(memory);
    return memory;
}

HGLOBAL CreateMarkerData() {
    HGLOBAL marker = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
    if (marker == nullptr) return nullptr;
    void* locked = GlobalLock(marker);
    if (locked == nullptr) {
        GlobalFree(marker);
        return nullptr;
    }
    *static_cast<unsigned char*>(locked) = 1;
    GlobalUnlock(marker);
    return marker;
}

bool IsSupportedFormat(const FORMATETC& format, UINT markerFormat) {
    if (format.ptd != nullptr || format.dwAspect != DVASPECT_CONTENT ||
        format.lindex != -1 || (format.tymed & TYMED_HGLOBAL) == 0) {
        return false;
    }
    return format.cfFormat == CF_HDROP ||
        format.cfFormat == static_cast<CLIPFORMAT>(
            RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)) ||
        (markerFormat != 0 && format.cfFormat ==
            static_cast<CLIPFORMAT>(markerFormat));
}

class FormatEnumerator final : public IEnumFORMATETC {
public:
    explicit FormatEnumerator(std::vector<FORMATETC> formats,
                               ULONG index = 0)
        : formats_(std::move(formats)), index_(index) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IEnumFORMATETC) {
            *object = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Next(
        ULONG count, FORMATETC* elements, ULONG* fetched) override {
        if (elements == nullptr) return E_POINTER;
        if (fetched == nullptr && count != 1) return E_POINTER;
        ULONG copied = 0;
        while (copied < count && index_ < formats_.size()) {
            elements[copied] = formats_[index_++];
            ++copied;
        }
        if (fetched != nullptr) *fetched = copied;
        return copied == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const std::size_t remaining = formats_.size() -
            std::min<std::size_t>(index_, formats_.size());
        index_ += std::min<std::size_t>(count, remaining);
        return count <= remaining ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override {
        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** result) override {
        if (result == nullptr) return E_POINTER;
        *result = new (std::nothrow) FormatEnumerator(
            formats_, static_cast<ULONG>(index_));
        return *result == nullptr ? E_OUTOFMEMORY : S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    std::vector<FORMATETC> formats_;
    std::size_t index_ = 0;
};

class FileDataObject final : public IDataObject {
public:
    explicit FileDataObject(std::wstring_view path)
        : files_(CreateFileDropData(path)),
          preferredEffect_(CreatePreferredEffectData()),
          marker_(CreateMarkerData()), markerFormat_(DragMarkerFormat()) {
        FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1,
                        TYMED_HGLOBAL};
        FORMATETC effect{
            static_cast<CLIPFORMAT>(
                RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)),
            nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        FORMATETC marker{static_cast<CLIPFORMAT>(markerFormat_), nullptr,
                         DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        formats_.push_back(files);
        formats_.push_back(effect);
        formats_.push_back(marker);
    }

    bool IsValid() const {
        return files_ != nullptr && preferredEffect_ != nullptr &&
            marker_ != nullptr && markerFormat_ != 0;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDataObject) {
            *object = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetData(
        FORMATETC* format, STGMEDIUM* medium) override {
        if (format == nullptr || medium == nullptr) return E_POINTER;
        if (!IsSupportedFormat(*format, markerFormat_)) {
            return DV_E_FORMATETC;
        }
        HGLOBAL source = nullptr;
        if (format->cfFormat == CF_HDROP) {
            source = files_;
        } else if (format->cfFormat == static_cast<CLIPFORMAT>(
                       RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT))) {
            source = preferredEffect_;
        } else if (format->cfFormat == static_cast<CLIPFORMAT>(markerFormat_)) {
            source = marker_;
        }
        HGLOBAL copy = DuplicateGlobal(source);
        if (copy == nullptr) return E_OUTOFMEMORY;
        *medium = STGMEDIUM{TYMED_HGLOBAL, {}, nullptr};
        medium->hGlobal = copy;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(
        FORMATETC*, STGMEDIUM*) override {
        return DATA_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (format == nullptr) return E_POINTER;
        return IsSupportedFormat(*format, markerFormat_)
            ? S_OK : DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(
        FORMATETC* formatIn, FORMATETC* formatOut) override {
        if (formatIn == nullptr || formatOut == nullptr) return E_POINTER;
        *formatOut = *formatIn;
        formatOut->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(
        FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(
        DWORD direction, IEnumFORMATETC** result) override {
        if (result == nullptr) return E_POINTER;
        *result = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        *result = new (std::nothrow) FormatEnumerator(formats_);
        return *result == nullptr ? E_OUTOFMEMORY : S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(
        FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** result) override {
        if (result != nullptr) *result = nullptr;
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    static HGLOBAL CreatePreferredEffectData() {
        HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                     sizeof(DWORD));
        if (effect == nullptr) return nullptr;
        void* locked = GlobalLock(effect);
        if (locked == nullptr) {
            GlobalFree(effect);
            return nullptr;
        }
        *static_cast<DWORD*>(locked) = DROPEFFECT_COPY;
        GlobalUnlock(effect);
        return effect;
    }

    ~FileDataObject() {
        if (files_ != nullptr) GlobalFree(files_);
        if (preferredEffect_ != nullptr) GlobalFree(preferredEffect_);
        if (marker_ != nullptr) GlobalFree(marker_);
    }

    std::atomic<ULONG> references_{1};
    HGLOBAL files_ = nullptr;
    HGLOBAL preferredEffect_ = nullptr;
    HGLOBAL marker_ = nullptr;
    UINT markerFormat_ = 0;
    std::vector<FORMATETC> formats_;
};

class FileDropSource final : public IDropSource {
public:
    explicit FileDropSource(HWND owner) : owner_(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropSource) {
            *object = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(
        BOOL escapePressed, DWORD keyState) override {
        if (escapePressed != FALSE || (keyState & MK_RBUTTON) != 0) {
            return DRAGDROP_S_CANCEL;
        }
        // Once the pointer has genuinely left Gravity's visual workspace,
        // returning to that workspace must cancel the external operation.
        // The desktop child is intentionally hit-test transparent, so
        // WorkerW/Progman can otherwise become the effective drop target and
        // create a copy before Gravity's own IDropTarget gets a chance to
        // reject its private marker format.
        if (owner_ != nullptr) {
            POINT cursor{};
            RECT ownerRect{};
            if (GetCursorPos(&cursor) != FALSE &&
                GetWindowRect(owner_, &ownerRect) != FALSE) {
                const bool inside = PtInRect(&ownerRect, cursor) != FALSE;
                if (!leftOwner_) {
                    leftOwner_ = !inside;
                } else if (inside) {
                    return DRAGDROP_S_CANCEL;
                }
            }
        }
        return (keyState & MK_LBUTTON) == 0
            ? DRAGDROP_S_DROP : S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    std::atomic<ULONG> references_{1};
    HWND owner_ = nullptr;
    bool leftOwner_ = false;
};

} // namespace

HRESULT CreateDataObject(std::wstring_view path, IDataObject** result) {
    if (result == nullptr) return E_POINTER;
    *result = nullptr;
    auto* object = new (std::nothrow) FileDataObject(path);
    if (object == nullptr) return E_OUTOFMEMORY;
    if (!object->IsValid()) {
        object->Release();
        return E_INVALIDARG;
    }
    *result = object;
    return S_OK;
}

bool IsGravityDragData(IDataObject* data) {
    if (data == nullptr) return false;
    const UINT marker = DragMarkerFormat();
    if (marker == 0) return false;
    FORMATETC format{static_cast<CLIPFORMAT>(marker), nullptr,
                     DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data->QueryGetData(&format) == S_OK;
}

Result StartDrag(HWND owner, std::wstring_view path) {
    if (path.empty() || GetFileAttributesW(std::wstring(path).c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
        return Result::SourceUnavailable;
    }
    IDataObject* data = nullptr;
    const HRESULT dataResult = CreateDataObject(path, &data);
    if (FAILED(dataResult) || data == nullptr) return Result::Failed;
    IDropSource* source = new (std::nothrow) FileDropSource(owner);
    if (source == nullptr) {
        data->Release();
        return Result::Failed;
    }
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT result = DoDragDrop(
        data, source, DROPEFFECT_COPY, &effect);
    source->Release();
    data->Release();
    if (result == DRAGDROP_S_CANCEL) return Result::Cancelled;
    return SUCCEEDED(result) && effect == DROPEFFECT_COPY
        ? Result::Completed : Result::Failed;
}

} // namespace gravitydesktop::dragout
