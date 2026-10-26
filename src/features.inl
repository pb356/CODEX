void App::invalidateInbox() {
    if (auto b = inboxData())
        if (auto v = view(b->id)) {
            v->filterDirty = true;
            if (IsWindowVisible(v->hwnd))
                v->draw();
        }
}
void App::updateInbox(bool fresh) {
    if (!inbox)
        return;
    InboxRequest r;
    r.enabled = inboxData() != nullptr;
    r.markNewOnScan = fresh;
    r.paused = state.inboxPaused;
    r.revision = ++inboxRevision;
    if (auto b = inboxData())
        r.prior = b->items;
    inbox->update(std::move(r));
}
void App::createInbox() {
    if (inboxData()) {
        if (auto v = view(inboxData()->id)) {
            ShowWindow(v->hwnd, SW_SHOWNOACTIVATE);
            v->draw();
        }
        return;
    }
    if (state.boxes.size() >= box::MaxBoxes || state.nextId == UINT32_MAX)
        return;
    box::Box b;
    b.id = state.nextId++;
    b.name = "桌面收件箱";
    b.kind = box::Kind::Inbox;
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    b.x = mi.rcWork.right - 420;
    b.y = mi.rcWork.top + 40;
    state.boxes.push_back(b);
    createView(b.id);
    if (hidden)
        ShowWindow(views.back()->hwnd, SW_HIDE);
    updateInbox(false);
    dirty();
}
RECT View::scrollThumb() {
    RECT out{};
    if (data().collapsed || loading)
        return out;
    RECT client;
    GetClientRect(hwnd, &client);
    int width = dip(client.right), height = dip(client.bottom), top = contentY();
    int cols = std::max(1, (width - 2 * Gap) / design::cellWidth(data())),
        rows = std::max(1, (height - top - 12) / design::cellHeight(data()));
    int total = ((int)filtered.size() + cols - 1) / cols, maximum = std::max(0, total - rows);
    if (!maximum)
        return out;
    int track = std::max(1, height - top - 18), thumb = std::max(28, track * rows / total);
    int y = top + 8 + MulDiv(track - thumb, data().scroll, maximum);
    out = {px(width - 10), px(y), px(width - 4), px(y + thumb)};
    return out;
}
void View::ensureVisible(int item) {
    auto &list = visible();
    auto it = std::find(list.begin(), list.end(), item);
    if (it == list.end())
        return;
    RECT client;
    GetClientRect(hwnd, &client);
    int cols = std::max(1, (dip(client.right) - 2 * Gap) / design::cellWidth(data())),
        rows = std::max(1, (dip(client.bottom) - contentY() - 12) / design::cellHeight(data()));
    int row = int(it - list.begin()) / cols;
    if (row < data().scroll)
        data().scroll = row;
    else if (row >= data().scroll + rows)
        data().scroll = row - rows + 1;
    draw();
}
void View::tooltipText(POINT p) {
    if (!tooltip)
        return;
    RECT client;
    GetClientRect(hwnd, &client);
    std::wstring next;
    if (dip(p.y) < Title) {
        if (dip(p.x) > dip(client.right) - 44)
            next = L"更多操作";
        else if (dip(p.x) > dip(client.right) - 88)
            next = data().collapsed ? L"展开收纳筐" : L"折叠收纳筐";
        else
            next = wide(data().name);
    } else if (int item = hit(p); item >= 0 && item < (int)data().items.size()) {
        next = wide(box::nameOf(data().items[item].path)) + L"\n" + wide(data().items[item].path);
        auto status = health.find(data().items[item].path);
        if (status != health.end() && status->second == 2)
            next += L"\n文件已失效，可右键重新定位或移除引用。";
    }
    if (next == tooltipBuffer)
        return;
    tooltipBuffer = std::move(next);
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.hwnd = hwnd;
    info.uId = 1;
    info.lpszText = tooltipBuffer.empty() ? (LPWSTR)L" " : tooltipBuffer.data();
    GetClientRect(hwnd, &info.rect);
    SendMessageW(tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&info);
    SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&info);
}
void View::consumeWheel(int delta) {
    if (data().collapsed || data().through)
        return;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0)
        return;
    if (lines == WHEEL_PAGESCROLL) {
        RECT client;
        GetClientRect(hwnd, &client);
        lines = std::max(1, (dip(client.bottom) - contentY() - 12) / design::cellHeight(data()));
    }
    scrollRemainder -= float(delta) * lines / WHEEL_DELTA;
    int step = (int)scrollRemainder;
    scrollRemainder -= step;
    if (step) {
        data().scroll = std::max(0, data().scroll + step);
        draw();
        app->dirty();
    }
}
