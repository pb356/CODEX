class DropReceiver final : public IDropTarget {
    LONG references_ = 1;
    std::shared_ptr<View *> life_;
    View *owner() {
        auto v = *life_;
        return v && IsWindow(v->hwnd) ? v : nullptr;
    }
    bool accepts(IDataObject *object) {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return object && SUCCEEDED(object->QueryGetData(&format));
    }
    void track(POINTL point) {
        auto v = owner();
        if (!v)
            return;
        POINT p{point.x, point.y};
        ScreenToClient(v->hwnd, &p);
        RECT r;
        GetClientRect(v->hwnd, &r);
        v->dragScrollDirection = p.y < v->px(v->contentY() + 26) ? -1
                                 : p.y > r.bottom - v->px(26)    ? 1
                                                                 : 0;
        if (v->dragScrollDirection)
            SetTimer(v->hwnd, 15, 50, nullptr);
        else
            KillTimer(v->hwnd, 15);
    }

  public:
    explicit DropReceiver(std::shared_ptr<View *> life) : life_(std::move(life)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&references_);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *object, DWORD, POINTL p,
                                        DWORD *effect) override {
        if (!effect)
            return E_POINTER;
        auto v = owner();
        bool ok = v && !v->data().through && !v->data().collapsed &&
                  v->data().kind == box::Kind::Normal && accepts(object);
        *effect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (v) {
            v->dropFeedback = ok ? 1 : -1;
            v->draw();
            if (ok)
                track(p);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL p, DWORD *effect) override {
        if (!effect)
            return E_POINTER;
        auto v = owner();
        *effect = v && v->dropFeedback > 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (v && v->dropFeedback > 0)
            track(p);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (auto v = owner()) {
            v->dropFeedback = 0;
            v->dragScrollDirection = 0;
            KillTimer(v->hwnd, 15);
            v->draw();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *object, DWORD, POINTL, DWORD *effect) override {
        if (!effect)
            return E_POINTER;
        *effect = DROPEFFECT_NONE;
        auto v = owner();
        if (!v || v->data().kind != box::Kind::Normal || v->data().through)
            return S_OK;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (FAILED(object->GetData(&format, &medium)))
            return S_OK;
        std::vector<std::string> paths;
        bool valid = false;
        size_t bytes = GlobalSize(medium.hGlobal);
        auto data = (BYTE *)GlobalLock(medium.hGlobal);
        if (data && bytes >= sizeof(DROPFILES) + 4 && bytes <= box::MaxConfig) {
            auto drop = (DROPFILES *)data;
            if (drop->fWide && drop->pFiles >= sizeof(DROPFILES) && drop->pFiles < bytes - 3 &&
                drop->pFiles % 2 == 0) {
                auto chars = (wchar_t *)(data + drop->pFiles);
                size_t n = (bytes - drop->pFiles) / 2;
                for (size_t i = 1; i < n; ++i)
                    if (!chars[i] && !chars[i - 1]) {
                        valid = true;
                        break;
                    }
            }
        }
        if (data)
            GlobalUnlock(medium.hGlobal);
        if (valid) {
            UINT count = DragQueryFileW((HDROP)medium.hGlobal, 0xffffffff, nullptr, 0);
            size_t total = 0;
            for (UINT k = 0; k < count; ++k) {
                UINT n = DragQueryFileW((HDROP)medium.hGlobal, k, nullptr, 0);
                if (!n || n > 32760)
                    continue;
                total += (n + 1) * sizeof(wchar_t);
                if (total > box::MaxConfig)
                    break;
                std::wstring p(n + 1, 0);
                DragQueryFileW((HDROP)medium.hGlobal, k, p.data(), n + 1);
                p.resize(n);
                try {
                    paths.push_back(canonical(p));
                } catch (...) {
                    paths.push_back({});
                }
            }
        }
        ReleaseStgMedium(&medium);
        DragLeave();
        if (valid) {
            v->acceptPaths(std::move(paths));
            *effect = DROPEFFECT_COPY;
        } else
            v->flash(L"无法读取这次拖入的文件列表");
        return S_OK;
    }
};
