// MSAA exposes virtual children without creating one HWND per file.
class BoxAccessible final : public IAccessible {
    LONG references_ = 1;
    std::shared_ptr<View *> life_;
    View *view() const {
        auto v = *life_;
        return v && IsWindow(v->hwnd) ? v : nullptr;
    }
    int base(View *v) const {
        return v->data().kind == box::Kind::Inbox && !v->data().collapsed ? 5 : 2;
    }
    int count(View *v) const {
        return base(v) + (v->data().collapsed ? 0 : (int)v->filtered.size());
    }
    int number(VARIANT child) const {
        return child.vt == VT_I4 ? child.lVal : -1;
    }
    int item(View *v, int child) const {
        int pos = child - base(v) - 1;
        if (pos < 0 || pos >= (int)v->filtered.size())
            return -1;
        int n = v->filtered[pos];
        return n >= 0 && n < (int)v->data().items.size() ? n : -1;
    }
    HRESULT string(const std::wstring &s, BSTR *out) {
        if (!out)
            return E_POINTER;
        *out = SysAllocStringLen(s.data(), (UINT)s.size());
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    RECT bounds(View *v, int child) const {
        RECT r{};
        GetClientRect(v->hwnd, &r);
        int width = v->dip(r.right);
        if (!child)
            return r;
        if (child == 1)
            return {v->px(width - 80), v->px(5), v->px(width - 48), v->px(37)};
        if (child == 2)
            return {v->px(width - 44), v->px(5), v->px(width - 12), v->px(37)};
        if (base(v) == 5 && child >= 3 && child <= 5) {
            int x = child == 3 ? 14 : child == 4 ? 96 : width - 110;
            return {v->px(x), v->px(44), v->px(x + (child == 5 ? 100 : 76)), v->px(72)};
        }
        int pos = child - base(v) - 1, cw = design::cellWidth(v->data()),
            ch = design::cellHeight(v->data()), cols = std::max(1, (width - 2 * Gap) / cw);
        int x = Gap + (pos % cols) * cw,
            y = v->contentY() + 12 + (pos / cols - v->data().scroll) * ch;
        return {v->px(x), v->px(y - 3), v->px(x + cw - 8), v->px(y + ch - 5)};
    }

  public:
    explicit BoxAccessible(std::shared_ptr<View *> life) : life_(std::move(life)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDispatch || id == IID_IAccessible) {
            *out = static_cast<IAccessible *>(this);
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
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *n) override {
        if (!n)
            return E_POINTER;
        *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **out) override {
        if (out)
            *out = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *,
                                     EXCEPINFO *, UINT *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        auto v = view();
        return v ? CreateStdAccessibleObject(v->hwnd, OBJID_WINDOW, IID_IDispatch, (void **)out)
                 : CO_E_OBJNOTCONNECTED;
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long *out) override {
        if (!out)
            return E_POINTER;
        auto v = view();
        *out = v ? count(v) : 0;
        return v ? S_OK : CO_E_OBJNOTCONNECTED;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR *out) override {
        auto v = view();
        int n = number(child);
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        if (n < 0 || n > count(v))
            return E_INVALIDARG;
        if (!n)
            return string(wide(v->data().name), out);
        if (n == 1)
            return string(v->data().collapsed ? L"展开收纳筐" : L"折叠收纳筐", out);
        if (n == 2)
            return string(L"更多操作", out);
        if (base(v) == 5 && n <= 5)
            return string(n == 3   ? L"未分类桌面项目"
                          : n == 4 ? L"全部桌面文件"
                                   : L"查看新收件",
                          out);
        int index = item(v, n);
        return index >= 0 ? string(wide(box::nameOf(v->data().items[index].path)), out)
                          : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR *out) override {
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        int i = item(v, number(child));
        return string(i >= 0 ? wide(v->data().items[i].path) : L"", out);
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR *out) override {
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        int i = item(v, number(child));
        if (i >= 0) {
            auto found = v->health.find(v->data().items[i].path);
            if (found != v->health.end() && found->second == 2)
                return string(v->data().kind == box::Kind::Inbox
                                  ? L"项目暂不可用，可刷新收件箱"
                                  : L"文件失效，可重新定位或移除引用",
                              out);
        }
        return string(L"", out);
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        int n = number(child);
        if (n < 0 || n > count(v))
            return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = !n             ? ROLE_SYSTEM_LIST
                    : n <= base(v) ? ROLE_SYSTEM_PUSHBUTTON
                                   : ROLE_SYSTEM_LISTITEM;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        int n = number(child);
        if (n < 0 || n > count(v))
            return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = STATE_SYSTEM_FOCUSABLE;
        if (v->data().through)
            out->lVal |= STATE_SYSTEM_UNAVAILABLE;
        if (!IsWindowVisible(v->hwnd))
            out->lVal |= STATE_SYSTEM_INVISIBLE;
        int selected = item(v, n);
        if (selected >= 0) {
            out->lVal |= STATE_SYSTEM_SELECTABLE;
            if (selected == v->selected)
                out->lVal |= STATE_SYSTEM_SELECTED;
            RECT c;
            GetClientRect(v->hwnd, &c);
            auto r = bounds(v, n);
            RECT intersection;
            if (!IntersectRect(&intersection, &r, &c) || r.bottom <= v->px(v->contentY()))
                out->lVal |= STATE_SYSTEM_OFFSCREEN;
        }
        int focused = v->focusPart == 1 ? 1 : v->focusPart == 2 ? 2 : 0;
        if (v->focusPart == 3) {
            auto it = std::find(v->filtered.begin(), v->filtered.end(), v->selected);
            if (it != v->filtered.end())
                focused = base(v) + 1 + (int)(it - v->filtered.begin());
        }
        if (v->focused && n == focused)
            out->lVal |= STATE_SYSTEM_FOCUSED;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR *out) override {
        return string(L"单击选择，双击或 Enter 打开。Tab 切换按钮与文件列表，方向键浏览。", out);
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *file, VARIANT, long *topic) override {
        if (file)
            *file = nullptr;
        if (topic)
            *topic = 0;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR *out) override {
        return string(number(child) > 2 ? L"Enter" : L"Space", out);
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        if (!v->focused)
            return S_FALSE;
        out->vt = VT_I4;
        out->lVal = v->focusPart == 1 ? 1 : v->focusPart == 2 ? 2 : 0;
        if (v->focusPart == 3) {
            auto it = std::find(v->filtered.begin(), v->filtered.end(), v->selected);
            if (it != v->filtered.end())
                out->lVal = base(v) + 1 + (int)(it - v->filtered.begin());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        auto it = std::find(v->filtered.begin(), v->filtered.end(), v->selected);
        if (it == v->filtered.end())
            return S_FALSE;
        out->vt = VT_I4;
        out->lVal = base(v) + 1 + (int)(it - v->filtered.begin());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR *out) override {
        int n = number(child);
        return string(n == 1 ? L"折叠或展开" : n == 2 ? L"打开菜单" : L"打开", out);
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT child) override {
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        if (v->data().through)
            return E_ACCESSDENIED;
        int n = number(child);
        if (n < 1 || n > count(v))
            return E_INVALIDARG;
        if (flags & SELFLAG_TAKEFOCUS) {
            SetForegroundWindow(v->hwnd);
            SetFocus(v->hwnd);
            v->focused = true;
        }
        if (n <= 2)
            v->focusPart = n;
        else {
            int i = item(v, n);
            if (i >= 0) {
                v->selected = i;
                v->focusPart = 3;
                v->ensureVisible(i);
            }
        }
        v->draw();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(long *x, long *y, long *w, long *h,
                                          VARIANT child) override {
        if (!x || !y || !w || !h)
            return E_POINTER;
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        int n = number(child);
        if (n < 0 || n > count(v))
            return E_INVALIDARG;
        auto r = bounds(v, n);
        POINT p{r.left, r.top};
        ClientToScreen(v->hwnd, &p);
        *x = p.x;
        *y = p.y;
        *w = r.right - r.left;
        *h = r.bottom - r.top;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT start, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        int n = number(start);
        if (direction == NAVDIR_FIRSTCHILD)
            n = 1;
        else if (direction == NAVDIR_LASTCHILD)
            n = count(v);
        else if (direction == NAVDIR_NEXT || direction == NAVDIR_RIGHT || direction == NAVDIR_DOWN)
            ++n;
        else if (direction == NAVDIR_PREVIOUS || direction == NAVDIR_LEFT || direction == NAVDIR_UP)
            --n;
        else
            return S_FALSE;
        if (n < 1 || n > count(v))
            return S_FALSE;
        out->vt = VT_I4;
        out->lVal = n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        POINT p{x, y};
        ScreenToClient(v->hwnd, &p);
        RECT c;
        GetClientRect(v->hwnd, &c);
        if (!PtInRect(&c, p))
            return S_FALSE;
        out->vt = VT_I4;
        out->lVal = CHILDID_SELF;
        for (int k = 1; k <= base(v); ++k) {
            auto r = bounds(v, k);
            if (PtInRect(&r, p)) {
                out->lVal = k;
                return S_OK;
            }
        }
        int i = v->hit(p);
        if (i >= 0) {
            auto it = std::find(v->filtered.begin(), v->filtered.end(), i);
            out->lVal = base(v) + 1 + (int)(it - v->filtered.begin());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        auto v = view();
        if (!v)
            return CO_E_OBJNOTCONNECTED;
        if (v->data().through)
            return E_ACCESSDENIED;
        int n = number(child);
        if (n == 1) {
            v->toggle();
            return S_OK;
        }
        if (n == 2) {
            RECT r;
            GetWindowRect(v->hwnd, &r);
            POINT p{r.right - v->px(24), r.top + v->px(42)};
            v->menu(p);
            return S_OK;
        }
        if (base(v) == 5 && n >= 3 && n <= 5) {
            v->data().inboxAll = n != 3;
            if (n == 5) {
                for (auto &i : v->data().items)
                    if (i.fresh && v->jumpPath.empty())
                        v->jumpPath = i.path;
                for (auto &i : v->data().items)
                    i.fresh = false;
                v->app->updateInbox();
            }
            v->changed();
            return S_OK;
        }
        int i = item(v, n);
        if (i >= 0) {
            v->open(i);
            return S_OK;
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override {
        return E_NOTIMPL;
    }
};
