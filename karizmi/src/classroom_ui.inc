// Included inside app.cpp's private namespace: shared Student/session UI is reused.
struct ClassroomWindow {
    HINSTANCE instance; HWND window{}; UINT dpi=96; HFONT font{};
    KarizmiEngineCallbacks callbacks;
    bool restoreSavedApprovals=true;
    std::shared_ptr<ClassroomResources> resources = std::make_shared<ClassroomResources>();
    std::vector<Candidate> discoveries;
    struct Peer { InstanceId id; std::unique_ptr<App> ui; HBITMAP frame{}; Tick captured{}; std::string label; };
    std::vector<Peer> peers;
    NOTIFYICONDATAW tray{};
    std::future<FirewallResult> firewallTask;
    Tick nextPaint=0; int selected=-1;
    TeacherKeyFilter keys;HWND previousForeground{};
    Runtime* focusTarget(HWND source) {
        if(callbacks.window_peer) {
            auto marker=callbacks.window_peer(callbacks.context,source);
            std::lock_guard lock(registryMutex);auto found=peerRuntimes.find(marker);if(found!=peerRuntimes.end())return found->second;
        }
        for(auto& peer:peers)if(source==peer.ui->window || IsChild(peer.ui->window,source))return &peer.ui->app;
        if((source==window || IsChild(window,source)) && selected>=0 && selected<int(peers.size()))return &peers[selected].ui->app;
        return nullptr;
    }
    void checkControlFocus() {
        HWND foreground=GetForegroundWindow();
        if(foreground!=previousForeground){keys.focusChanged();previousForeground=foreground;}
        Runtime* owner;{std::lock_guard lock(registryMutex);owner=controlOwner?controlOwner:pendingControlOwner;}
        if(!owner)return;
        for(auto& peer:peers)if(&peer.ui->app==owner) {
            if(peer.ui->engine.focusScope(foreground) || focusTarget(foreground)==owner)return;
            selectControl(owner,false);return;
        }
    }
    int scale(int value) const { return MulDiv(value,dpi,96); }
    HWND child(int id) { return GetDlgItem(window,id); }
    void status(const std::wstring& text) { SetWindowTextW(child(207),text.c_str()); }
    void layout() {
        RECT area{}; GetClientRect(window,&area); int width=MulDiv(area.right,96,dpi), height=MulDiv(area.bottom,96,dpi);
        auto move=[&](int id,int x,int y,int w,int h) { MoveWindow(child(id),scale(x),scale(y),scale(w),scale(h),TRUE); };
        move(200,20,16,width-40,32); move(201,20,62,260,height-280);
        move(202,20,height-176,130,32); move(203,160,height-176,120,32);
        move(204,20,height-132,125,32); move(205,155,height-132,125,32);
        move(206,20,height-88,260,32); move(207,20,height-46,width-130,36); move(208,width-100,height-46,80,30);
        move(209,20,height-210,260,24);
        const int columns=width>=1150 ? 3 : 2, rows=(10+columns-1)/columns;
        const int cellWidth=(width-320)/columns,cellHeight=(height-140)/rows;
        for(int i=0;i<10;++i) move(220+i,300+(i%columns)*cellWidth,62+(i/columns)*cellHeight,cellWidth-10,cellHeight-10);
    }
    void fonts() {
        HFONT replacement=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(int id=200;id<230;++id) if(child(id)) SendMessageW(child(id),WM_SETFONT,reinterpret_cast<WPARAM>(replacement),TRUE);
        if(font) DeleteObject(font); font=replacement;
    }
    bool addTray() {
        tray.cbSize=sizeof(tray); tray.hWnd=window; tray.uID=1; tray.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE;
        tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(49001)); tray.uCallbackMessage=TrayMessage;
        wcscpy_s(tray.szTip,L"Karizmi Teacher — Classroom");
        Shell_NotifyIconW(NIM_DELETE,&tray);
        const bool registered=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;
        SetPropW(window,L"KarizmiTrayRegistered",reinterpret_cast<HANDLE>(INT_PTR(registered)));
        if(registered) return true;
        ShowWindow(window,SW_SHOW); return false;
    }
    void create() {
        dpi=GetDpiForWindow(window);
        auto add=[&](const wchar_t* kind,const wchar_t* label,int id,DWORD style=0) {
            CreateWindowExW(0,kind,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
        };
        add(L"STATIC",L"Karizmi Teacher — Classroom (up to 10 students)",200);
        add(L"LISTBOX",L"",201,WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY);
        add(L"EDIT",L"",202,WS_TABSTOP|WS_BORDER|ES_NUMBER|ES_CENTER);
        SendMessageW(child(202),EM_SETLIMITTEXT,8,0); SendMessageW(child(202),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"8-digit code"));
        add(L"BUTTON",L"&Pair selected",203,WS_TABSTOP);
        add(L"BUTTON",L"&Scan now",204,WS_TABSTOP); add(L"BUTTON",L"&End exam",205,WS_TABSTOP);
        add(L"BUTTON",L"Set up exam &connection",206,WS_TABSTOP); SendMessageW(child(206),BCM_SETSHIELD,0,TRUE);
        add(L"STATIC",L"Scanning local network…",207); add(L"BUTTON",L"E&xit",208,WS_TABSTOP);
        add(L"STATIC",L"Selected student's pairing code:",209);
        for(int i=0;i<10;++i) add(L"BUTTON",L"",220+i,BS_OWNERDRAW|BS_NOTIFY|WS_TABSTOP);
        fonts(); layout(); addTray(); SetTimer(window,1,100,nullptr);
        ApprovalStore store(Role::Teacher);
        for(const auto& record:restoreSavedApprovals?store.load():std::vector<Approval>{}) {
            auto ui=makeSession();ui->app.restoreApproval(record);
            peers.push_back({record.peer,std::move(ui),{},0,record.label});
        }
    }
    std::unique_ptr<App> makeSession() {
        auto ui=std::make_unique<App>(Role::Teacher,instance,callbacks,resources);ui->sessionWindow=true;
        ui->window=CreateWindowExW(0,L"KarizmiMain",L"Karizmi Teacher — Student session",MainStyle,CW_USEDEFAULT,CW_USEDEFAULT,620,610,window,nullptr,instance,ui.get());
        if(!ui->window)throw std::runtime_error("Cannot create student session window");
        auto bounds=ui->outerBounds(DefaultWidth,DefaultHeight);
        SetWindowPos(ui->window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER);
        return ui;
    }
    void endExam() { for(auto& peer:peers) peer.ui->app.disconnect(); }
    void pair() {
        int index=int(SendMessageW(child(201),LB_GETCURSEL,0,0));
        if(index<0 || index>=int(discoveries.size())) throw std::runtime_error("Select a discovered Student first.");
        if(!resources->ready()) throw std::runtime_error("Use Set up exam connection before pairing.");
        auto target=discoveries[index]; wchar_t code[9]{}; GetWindowTextW(child(202),code,9);
        std::string value; for(auto c:std::wstring(code)) value+=char(c);
        if(!validCode(value)) throw std::runtime_error("Enter eight decimal digits.");
        for(auto& peer:peers) if(peer.id==target.instance) {
            peer.ui->app.connectTeacher(target,value); return;
        }
        if(peers.size()==10) {
            auto ended=std::find_if(peers.begin(),peers.end(),[](auto& peer){return peer.ui->app.display().session.state==State::Ended;});
            if(ended==peers.end()) throw std::runtime_error("Ten students are already reserved. Disconnect a student before pairing another.");
            ended->ui->app.shutdown(); ended->ui->engine.dispatch(); DestroyWindow(ended->ui->window);
            if(ended->frame) DeleteObject(ended->frame); peers.erase(ended);
        }
        auto ui=std::make_unique<App>(Role::Teacher,instance,callbacks,resources); ui->sessionWindow=true;
        ui->window=CreateWindowExW(0,L"KarizmiMain",L"Karizmi Teacher — Student session",MainStyle,CW_USEDEFAULT,CW_USEDEFAULT,620,610,window,nullptr,instance,ui.get());
        auto bounds=ui->outerBounds(DefaultWidth,DefaultHeight);
        SetWindowPos(ui->window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER);
        ui->app.connectTeacher(target,value);
        peers.push_back({target.instance,std::move(ui)}); SetWindowTextW(child(202),L"");
    }
    void tick() {
        checkControlFocus();
        dispatchControlHandoff();
        for(auto& peer:peers) {
            peer.ui->engine.dispatch();
            if(peer.ui->app.display().awaitingConfirmation && !IsWindowVisible(peer.ui->window)) peer.ui->show();
        }
        if(firewallTask.valid() && firewallTask.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            auto result=firewallTask.get(); status(result.message); EnableWindow(child(206),TRUE);
        }
        if(now()<nextPaint) return; nextPaint=now()+1000;
        auto found=resources->candidates();
        bool changed=found.size()!=discoveries.size();
        if(!changed) for(size_t i=0;i<found.size();++i) if(found[i].instance!=discoveries[i].instance || found[i].address!=discoveries[i].address || found[i].label!=discoveries[i].label) changed=true;
        if(changed) {
            int old=int(SendMessageW(child(201),LB_GETCURSEL,0,0)); std::optional<InstanceId> selection;
            if(old>=0 && old<int(discoveries.size())) selection=discoveries[old].instance;
            discoveries=std::move(found); SendMessageW(child(201),LB_RESETCONTENT,0,0);
            for(size_t i=0;i<discoveries.size();++i) {
                auto label=wide(discoveries[i].label+" — "+discoveries[i].address+" (Unverified)");
                SendMessageW(child(201),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                if(selection && *selection==discoveries[i].instance) SendMessageW(child(201),LB_SETCURSEL,i,0);
            }
        }
        for(size_t i=0;i<peers.size();++i) {
            auto state=peers[i].ui->app.display();
            if(!state.session.peer.empty()) peers[i].label=state.session.peer;
            if(state.session.state==State::Connected && state.session.lastFrame>peers[i].captured) {
                HDC screen=GetDC(window),memory=CreateCompatibleDC(screen);
                if(!peers[i].frame) peers[i].frame=CreateCompatibleBitmap(screen,320,180);
                auto old=SelectObject(memory,peers[i].frame); PatBlt(memory,0,0,320,180,BLACKNESS);
                peers[i].ui->engine.thumbnail(memory,320,180);
                SelectObject(memory,old); DeleteDC(memory); ReleaseDC(window,screen); peers[i].captured=state.session.lastFrame;
            }
        }
        for(int i=0;i<10;++i) InvalidateRect(child(220+i),nullptr,TRUE);
    }
    void draw(DRAWITEMSTRUCT* item) {
        int index=int(item->CtlID)-220; if(index<0 || index>=10) return;
        FillRect(item->hDC,&item->rcItem,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        FrameRect(item->hDC,&item->rcItem,static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,TextColor); auto old=SelectObject(item->hDC,font);
        RECT text=item->rcItem; InflateRect(&text,-scale(8),-scale(6));
        if(index>=int(peers.size())) DrawTextW(item->hDC,L"No paired student",-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        else {
            auto& peer=peers[index]; auto state=peer.ui->app.display();
            auto label=wide(peer.label.empty()?"Pairing…":peer.label); RECT heading=text; heading.bottom=heading.top+scale(20);
            DrawTextW(item->hDC,label.c_str(),-1,&heading,DT_SINGLELINE|DT_END_ELLIPSIS);
            RECT footer=text; footer.top=footer.bottom-scale(36);
            std::wstring detail=state.session.state==State::Connected ? (state.session.control?L"CONTROL ON":L"View only") : state.session.state==State::Recovering?L"STALE — Interrupted":wide(state.status);
            if(peer.captured) { if(state.session.state!=State::Connected && state.session.state!=State::Recovering) detail=L"STALE — Disconnected"; detail+=L"\nLast frame "+std::to_wstring((now()-peer.captured)/1000)+L"s ago"; }
            DrawTextW(item->hDC,detail.c_str(),-1,&footer,DT_WORDBREAK|DT_END_ELLIPSIS);
            int top=heading.bottom+scale(4),height=footer.top-top-scale(4),width=text.right-text.left;
            if(peer.frame && height>0) { HDC memory=CreateCompatibleDC(item->hDC); auto previous=SelectObject(memory,peer.frame); StretchBlt(item->hDC,text.left,top,width,height,memory,0,0,320,180,SRCCOPY); SelectObject(memory,previous); DeleteDC(memory); }
        }
        SelectObject(item->hDC,old); if(item->itemState&ODS_FOCUS) DrawFocusRect(item->hDC,&item->rcItem);
    }
    void command(int id,int notification) {
        try {
            if(id>=240 && id<250 && id-240<int(peers.size())) {
                selected=id-240; peers[selected].ui->engine.showRemote(true); return;
            }
            if(id>=260 && id<270 && id-260<int(peers.size())) {selected=id-260;toggleControl(&peers[selected].ui->app);return;}
            if(id>=280 && id<290 && id-280<int(peers.size())) {selected=id-280;peers[selected].ui->app.reconnectNow();status(wide(peers[selected].ui->app.display().status));return;}
            if(id>=220 && id<230 && id-220<int(peers.size())) {
                selected=id-220;
                if(notification==BN_DOUBLECLICKED) peers[selected].ui->engine.showRemote();
                else status(L"Double-click to view. Right-click for control, reconnect, disconnect, and session details.");
            } else if(id==203) pair();
            else if(id==204) { resources->scanNow(); status(wide(resources->diagnostic())); }
            else if(id==205) { endExam(); status(L"Exam ended. Fresh pairing is required to connect again."); }
            else if(id==206 && !firewallTask.valid()) { EnableWindow(child(206),FALSE); firewallTask=std::async(std::launch::async,[this]{return resources->setup(window,false);}); }
            else if(id==208) { for(auto& peer:peers)peer.ui->app.suspend(); DestroyWindow(window); }
        } catch(const std::exception& error) { status(wide(error.what())); }
    }
    ~ClassroomWindow() {
        for(auto& peer:peers) { peer.ui->app.shutdown(); peer.ui->engine.dispatch(); if(IsWindow(peer.ui->window)) DestroyWindow(peer.ui->window); if(peer.frame) DeleteObject(peer.frame); }
        if(firewallTask.valid()) firewallTask.wait(); if(font) DeleteObject(font);
    }
};
LRESULT CALLBACK classroomProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto app=reinterpret_cast<ClassroomWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) { app=static_cast<ClassroomWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if(!app) return DefWindowProcW(window,message,w,l);
    if(message==TaskbarCreated) { app->addTray(); return 0; }
    switch(message) {
    case WM_CREATE: app->create(); return 0;
    case WM_SIZE: app->layout(); return 0;
    case WM_TIMER: app->tick(); return 0;
    case WM_COMMAND: app->command(LOWORD(w),HIWORD(w)); return 0;
    case WM_DRAWITEM: app->draw(reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_CONTEXTMENU: {
        int index=GetDlgCtrlID(reinterpret_cast<HWND>(w))-220;
        if(index<0 || index>=int(app->peers.size())) return 0;
        app->selected=index; auto& peer=app->peers[index]; HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,1,L"Open screen"); AppendMenuW(menu,MF_STRING,6,L"Open &full screen"); AppendMenuW(menu,MF_STRING,2,L"Pairing / session details");
        auto state=peer.ui->app.display().session;
        AppendMenuW(menu,MF_STRING | ((state.state==State::Connected && state.controlAvailable)?0:MF_GRAYED),3,L"Toggle control / view only");
        bool retry=state.state==State::Connected || state.state==State::Recovering;
        AppendMenuW(menu,MF_STRING | (retry?0:MF_GRAYED),4,retry?L"Reconnect now":L"Reconnect unavailable — pair this student first"); AppendMenuW(menu,MF_STRING,5,L"Disconnect");
        POINT point; GetCursorPos(&point); SetForegroundWindow(window);
        int choice=TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,window,nullptr); DestroyMenu(menu);
        if(choice==1) peer.ui->engine.showRemote(); if(choice==2) peer.ui->show();
        if(choice==6) app->command(240+index,0);
        if(choice==3) app->command(260+index,0);
        if(choice==4) app->command(280+index,0); if(choice==5) peer.ui->app.disconnect(); return 0;
    }
    case WM_DPICHANGED: { app->dpi=HIWORD(w); auto bounds=reinterpret_cast<RECT*>(l); SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER); app->fonts(); app->layout(); return 0; }
    case WM_GETMINMAXINFO: { RECT bounds{0,0,app->scale(900),app->scale(700)}; AdjustWindowRectExForDpi(&bounds,MainStyle,FALSE,0,app->dpi); auto limits=reinterpret_cast<MINMAXINFO*>(l); limits->ptMinTrackSize={bounds.right-bounds.left,bounds.bottom-bounds.top}; return 0; }
    case WM_CLOSE: ShowWindow(window,SW_HIDE); return 0;
    case WM_QUERYENDSESSION: app->endExam(); return TRUE;
    case WM_ENDSESSION: if(w) DestroyWindow(window); return 0;
    case WM_DESTROY: KillTimer(window,1); for(auto& peer:app->peers)peer.ui->app.suspend(); Shell_NotifyIconW(NIM_DELETE,&app->tray); PostQuitMessage(0); return 0;
    case TrayMessage:
        if(l==WM_LBUTTONDBLCLK) { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
        if(l==WM_RBUTTONUP) { HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"Open Karizmi Teacher"); AppendMenuW(menu,MF_STRING,205,L"End exam"); AppendMenuW(menu,MF_STRING,208,L"Exit"); POINT point; GetCursorPos(&point); SetForegroundWindow(window); int choice=TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,window,nullptr); DestroyMenu(menu); if(choice==1) ShowWindow(window,SW_RESTORE); else app->command(choice,0); } return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
int runClassroom(HINSTANCE instance,KarizmiEngineCallbacks callbacks) {
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\KarizmiTeacher");
    if(!singleton || GetLastError()==ERROR_ALREADY_EXISTS) { if(singleton) CloseHandle(singleton); return 1; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW definition{}; definition.hInstance=instance; definition.hCursor=LoadCursor(nullptr,IDC_ARROW); definition.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(49001)); definition.hbrBackground=static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    definition.lpfnWndProc=windowProc; definition.lpszClassName=L"KarizmiMain"; RegisterClassW(&definition);
    definition.lpfnWndProc=classroomProc; definition.lpszClassName=L"KarizmiClassroom"; RegisterClassW(&definition);
    {
        ClassroomWindow app{instance,{},96,{},callbacks};
        HWND window=CreateWindowExW(0,L"KarizmiClassroom",L"Karizmi Teacher — Classroom",MainStyle,CW_USEDEFAULT,CW_USEDEFAULT,1000,800,nullptr,nullptr,instance,&app);
        RECT bounds{0,0,app.scale(1000),app.scale(720)}; AdjustWindowRectExForDpi(&bounds,MainStyle,FALSE,0,app.dpi);
        SetWindowPos(window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER); ShowWindow(window,SW_SHOW);
        MSG message{};
        auto dispatch=[&](MSG& event) {
            for(auto& peer:app.peers) if((event.hwnd==peer.ui->window || IsChild(peer.ui->window,event.hwnd)) && IsDialogMessageW(peer.ui->window,&event)) return;
            if(!IsDialogMessageW(window,&event)) { TranslateMessage(&event); DispatchMessageW(&event); }
        };
        while(GetMessageW(&message,nullptr,0,0)>0) {
            Runtime* target=app.focusTarget(message.hwnd);
            auto filtered=app.keys.process(message,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_MENU)&0x8000)!=0,reinterpret_cast<uintptr_t>(target));
            if(filtered.toggle && target)toggleControl(target);
            for(auto& forwarded:filtered.forward)dispatch(forwarded);
        }
    }
    CloseHandle(singleton); return 0;
}
