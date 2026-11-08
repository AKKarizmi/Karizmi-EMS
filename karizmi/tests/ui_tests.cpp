#include "../src/app.cpp"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void saveWindow(HWND window, const std::filesystem::path& path) {
    RECT bounds{}; GetWindowRect(window,&bounds);
    const int width = bounds.right-bounds.left, height = bounds.bottom-bounds.top;
    HDC source = GetDC(window), memory = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source,width,height);
    auto previous = SelectObject(memory,bitmap);
    PatBlt(memory,0,0,width,height,WHITENESS);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    SelectObject(memory,previous);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    std::vector<char> pixels(size_t(width)*height*4);
    require(GetDIBits(memory,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS) != 0,"Cannot read screenshot");
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header)+sizeof(info.bmiHeader); header.bfSize = header.bfOffBits+DWORD(pixels.size());
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header),sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    output.write(pixels.data(),pixels.size());
    require(output.good(),"Cannot save screenshot");
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window,source);
}
void verify(App& application) {
    RECT client{}; GetClientRect(application.window,&client);
    std::vector<std::pair<int,RECT>> occupied;
    for (int identifier = Code; identifier <= Shortcut; ++identifier) {
        HWND control = application.child(identifier);
        if (!control || !IsWindowVisible(control)) continue;
        RECT rectangle{}; GetWindowRect(control,&rectangle);
        MapWindowPoints(nullptr,application.window,reinterpret_cast<POINT*>(&rectangle),2);
        require(rectangle.left >= 0 && rectangle.top >= 0 && rectangle.right <= client.right && rectangle.bottom <= client.bottom,"Control outside client area");
        require(rectangle.right > rectangle.left && rectangle.bottom > rectangle.top,"Control has no usable size");
        for (const auto& existing : occupied) {
            RECT intersection{};
            if (IntersectRect(&intersection,&rectangle,&existing.second)) {
                std::cerr << "DPI " << application.dpi << ", controls " << identifier << " and " << existing.first << '\n';
                throw std::runtime_error("Visible controls overlap");
            }
        }
        occupied.push_back({identifier,rectangle});
        if (identifier == Logo) continue;
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));
        LOGFONTW description{}; require(GetObjectW(font,sizeof(description),&description) != 0,"Missing font");
        const int logicalHeight = identifier == Title ? 22 : identifier == Code ? 26 : 14;
        require(description.lfHeight == -application.scale(logicalHeight),"Font scaled incorrectly");
        wchar_t label[4096]{}; GetWindowTextW(control,label,4096);
        if (!*label) continue;
        HDC context = GetDC(control); auto previous = SelectObject(context,font);
        RECT content{}; GetClientRect(control,&content);
        wchar_t kind[32]{}; GetClassNameW(control,kind,32);
        const bool button = wcscmp(kind,L"Button") == 0;
        const int availableWidth = content.right - (button ? application.scale(16 + ((identifier == Setup || identifier == Remove) ? 20 : 0)) : 0);
        RECT measured{0,0,availableWidth,0};
        DrawTextW(context,label,-1,&measured,DT_CALCRECT | (button ? DT_SINGLELINE : DT_WORDBREAK));
        SelectObject(context,previous); ReleaseDC(control,context);
        if (measured.right > availableWidth || measured.bottom > content.bottom) {
            std::cerr << "DPI " << application.dpi << ", control " << identifier << ", measured " << measured.right << 'x' << measured.bottom << ", available " << availableWidth << 'x' << content.bottom << '\n';
            throw std::runtime_error("Text does not fit control");
        }
    }
}
}
int main() try {
    {
        TeacherKeyFilter filter;
        auto event=[](UINT type,WPARAM key,LPARAM flags=0){MSG message{};message.message=type;message.wParam=key;message.lParam=flags;return message;};
        require(filter.process(event(WM_KEYDOWN,VK_CONTROL),true,false,false,1).forward.empty(),"Ctrl leaked before shortcut classification");
        require(filter.process(event(WM_KEYDOWN,VK_SHIFT),true,true,false,1).forward.empty(),"Shift leaked before shortcut classification");
        auto shortcut=filter.process(event(WM_KEYDOWN,VK_F12),true,true,false,1);
        require(shortcut.toggle && shortcut.forward.empty(),"Control shortcut not consumed");
        require(!filter.process(event(WM_KEYDOWN,VK_F12,LPARAM(1)<<30),true,true,false,1).toggle,"Auto-repeat toggled control");
        filter.focusChanged();
        require(filter.process(event(WM_KEYUP,VK_F12),true,true,false,2).forward.empty(),"Shortcut F12 release leaked after focus transfer");
        require(filter.process(event(WM_KEYUP,VK_SHIFT),true,false,false,2).forward.empty(),"Shortcut Shift release leaked");
        require(filter.process(event(WM_KEYUP,VK_CONTROL),false,false,false,2).forward.empty(),"Shortcut Ctrl release leaked");
        filter.reset();filter.process(event(WM_KEYDOWN,VK_CONTROL),true,false,false,1);
        filter.process(event(WM_KEYDOWN,VK_SHIFT),true,true,false,1);
        auto ordinary=filter.process(event(WM_KEYDOWN,'S'),true,true,false,1);
        require(ordinary.forward.size()==3 && ordinary.forward[0].wParam==VK_CONTROL && ordinary.forward[1].wParam==VK_SHIFT && ordinary.forward[2].wParam=='S' && !ordinary.toggle,"Ordinary Ctrl/Shift shortcut changed");
        require(filter.process(event(WM_KEYUP,'S'),true,true,false,1).forward.size()==1,"Ordinary key-up lost");
        filter.reset();filter.process(event(WM_KEYDOWN,VK_CONTROL),true,false,false,1);filter.process(event(WM_KEYDOWN,VK_SHIFT),true,true,false,1);
        auto fullscreen=filter.process(event(WM_KEYDOWN,VK_F12),true,true,true,1);
        require(!fullscreen.toggle && fullscreen.forward.size()==3,"Alt shortcut incorrectly treated as control toggle");
        std::cout<<"Teacher shortcut filtering checks passed\n";
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW definition{}; definition.hInstance = instance; definition.lpfnWndProc = windowProc;
    definition.lpszClassName = L"KarizmiLayoutTest"; definition.hCursor = LoadCursor(nullptr,IDC_ARROW);
    require(RegisterClassW(&definition) != 0,"Cannot register test window");
    std::filesystem::create_directories("ui-screenshots");
    unsigned checked = 0;
    for (auto role : {Role::Teacher,Role::Student}) {
        App application(role,instance,{});
        HWND window = CreateWindowExW(0,definition.lpszClassName,role == Role::Teacher ? L"Karizmi Teacher" : L"Karizmi Student",MainStyle,0,0,620,610,nullptr,nullptr,instance,&application);
        require(window != nullptr,"Cannot create test window"); KillTimer(window,1);
        ShowWindow(window,SW_SHOWNOACTIVATE);
        for (UINT dpi : {96u,120u,144u,192u,240u}) {
            for (bool confirming : {false,true}) {
                application.confirming = confirming;
                application.text(Code,L"00001234");
                application.text(Phrase,L"Compare on both screens:\r\namber birch cedar dawn elm fern");
                for (int width : {MinimumWidth,DefaultWidth,720}) {
                    RECT rectangle{0,0,MulDiv(width,dpi,96),MulDiv(application.minimumHeight(),dpi,96)};
                    AdjustWindowRectExForDpi(&rectangle,MainStyle,FALSE,0,GetDpiForWindow(window));
                    rectangle.right -= rectangle.left; rectangle.bottom -= rectangle.top; rectangle.left = 0; rectangle.top = 0;
                    SendMessageW(window,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&rectangle));
                    verify(application);
                    ++checked;
                    if (width == DefaultWidth && !confirming) {
                        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                        saveWindow(window,std::filesystem::path("ui-screenshots") / (std::string(role == Role::Teacher ? "Teacher-" : "Student-")+std::to_string(dpi)+"dpi.bmp"));
                    }
                }
            }
        }
        DestroyWindow(window); application.app.shutdown(); application.engine.dispatch();
        DeleteObject(application.font); DeleteObject(application.large); DeleteObject(application.codeFont); DeleteObject(application.background);
    }
    definition.lpszClassName=L"KarizmiMain";require(RegisterClassW(&definition)!=0,"Cannot register session test class");
    definition.lpfnWndProc=classroomProc; definition.lpszClassName=L"KarizmiClassroomTest";
    require(RegisterClassW(&definition)!=0,"Cannot register classroom test window");
    {
        ClassroomWindow classroom{instance,{},96,{}, {}};
        classroom.restoreSavedApprovals=false;
        HWND window=CreateWindowExW(0,definition.lpszClassName,L"Karizmi Classroom layout",MainStyle,0,0,1000,800,nullptr,nullptr,instance,&classroom);
        KillTimer(window,1); ShowWindow(window,SW_SHOWNOACTIVATE);
        struct FocusMap { HWND a{},b{}; } mapping;
        mapping.a=CreateWindowW(L"STATIC",L"Mock viewer A",WS_OVERLAPPED,0,0,200,100,nullptr,nullptr,instance,nullptr);
        mapping.b=CreateWindowW(L"STATIC",L"Mock viewer B",WS_OVERLAPPED,0,0,200,100,nullptr,nullptr,instance,nullptr);
        HWND viewerChild=CreateWindowW(L"STATIC",L"Viewer content",WS_CHILD,0,0,20,20,mapping.a,nullptr,instance,nullptr);
        classroom.callbacks.context=&mapping;
        classroom.callbacks.window_peer=+[](void* context,void* source)->uintptr_t {
            auto map=static_cast<FocusMap*>(context);auto window=static_cast<HWND>(source);
            if(window==map->a || IsChild(map->a,window))return 10001;
            if(window==map->b || IsChild(map->b,window))return 10002;
            return UINTPTR_MAX;
        };
        classroom.peers.push_back({{},classroom.makeSession()});classroom.peers.push_back({{},classroom.makeSession()});classroom.selected=1;
        peerRuntimes[10001]=&classroom.peers[0].ui->app;peerRuntimes[10002]=&classroom.peers[1].ui->app;
        require(classroom.focusTarget(viewerChild)==&classroom.peers[0].ui->app,"Focused viewer incorrectly routed to selected tile");
        require(classroom.focusTarget(classroom.peers[0].ui->window)==&classroom.peers[0].ui->app,"Session-details target incorrect");
        require(classroom.focusTarget(window)==&classroom.peers[1].ui->app,"Grid selection fallback incorrect");
        require(classroom.focusTarget(nullptr)==nullptr,"Unrelated focus obtained a student target");
        peerRuntimes.erase(10001);peerRuntimes.erase(10002);
        DestroyWindow(mapping.a);DestroyWindow(mapping.b);
        for(auto& peer:classroom.peers)DestroyWindow(peer.ui->window);classroom.peers.clear();classroom.selected=-1;
        classroom.callbacks={};
        for(UINT dpi:{96u,120u,144u,192u}) for(int width:{900,1200}) {
            RECT bounds{0,0,MulDiv(width,dpi,96),MulDiv(720,dpi,96)};
            AdjustWindowRectExForDpi(&bounds,MainStyle,FALSE,0,GetDpiForWindow(window));
            bounds.right-=bounds.left; bounds.bottom-=bounds.top; bounds.left=bounds.top=0;
            SendMessageW(window,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&bounds));
            RECT client{}; GetClientRect(window,&client); std::vector<RECT> occupied;
            for(int id=200;id<230;++id) {
                HWND control=classroom.child(id); if(!control) continue;
                RECT rect{}; GetWindowRect(control,&rect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
                require(rect.left>=0 && rect.top>=0 && rect.right<=client.right && rect.bottom<=client.bottom,"Classroom control outside client");
                for(auto existing:occupied) { RECT intersection{}; require(!IntersectRect(&intersection,&rect,&existing),"Classroom controls overlap"); }
                occupied.push_back(rect);
            }
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
            saveWindow(window,std::filesystem::path("ui-screenshots")/("Classroom-"+std::to_string(dpi)+"-"+std::to_string(width)+".bmp")); ++checked;
        }
        DestroyWindow(window);
    }
    std::cout << "Verified " << checked << " role/DPI/state/size combinations; screenshots saved in ui-screenshots.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
