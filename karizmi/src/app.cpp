// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/runtime.h"
#include "karizmi/integration.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dbghelp.h>
#include <algorithm>
#include <future>
#include <iomanip>
#include <sstream>
#include <csignal>

namespace {
#include "diagnostics.inc"
#include "teacher_keys.inc"
using namespace karizmi;
std::mutex registryMutex;
std::map<uintptr_t, std::shared_ptr<SecureConnection>> registry;
std::atomic_bool controlAllowed = false;
std::atomic<uint64_t> observedKeyEvents=0;
std::recursive_mutex inputMutex;
std::set<uint32_t> keys;
unsigned buttons = 0;
std::atomic<Runtime*> runtime = nullptr;
std::map<uintptr_t,Runtime*> peerRuntimes;
std::set<uintptr_t> peerControl;
Runtime* controlOwner = nullptr;
Runtime* pendingControlOwner = nullptr;
void selectControl(Runtime* selected,bool enabled) {
    if(enabled && (selected->display().session.state!=State::Connected || !selected->display().session.controlAvailable))return;
    std::vector<Runtime*> peers;
    { std::lock_guard lock(registryMutex); controlOwner=nullptr; pendingControlOwner=enabled?selected:nullptr; peerControl.clear();
      for(auto [marker,peer]:peerRuntimes) if(peer!=selected) peers.push_back(peer); }
    for(auto peer:peers) peer->requestControl(false);
    if (!enabled) selected->requestControl(false);
}
void toggleControl(Runtime* selected) {
    bool enabled;
    {std::lock_guard lock(registryMutex);enabled=!(pendingControlOwner==selected || controlOwner==selected);}
    selectControl(selected,enabled);
}
void dispatchControlHandoff() {
    Runtime* next = nullptr;
    { std::lock_guard lock(registryMutex);
      if (!pendingControlOwner) return;
      bool present=false;
      for (auto [marker,peer]:peerRuntimes) {
          if (peer==pendingControlOwner) present=true;
          else if(peer->controlRevocationPending()) return;
      }
      if(present) next=pendingControlOwner;
      pendingControlOwner=nullptr; controlOwner=next;
    }
    if(next) next->requestControl(true);
}
std::shared_ptr<SecureConnection> lookup(uintptr_t socket) {
    std::lock_guard lock(registryMutex); auto it = registry.find(socket); return it == registry.end() ? nullptr : it->second;
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!size) return L"Invalid UTF-8 text";
    std::wstring result(size, L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size); return result;
}
class UiEngine final : public Engine {
public:
    explicit UiEngine(KarizmiEngineCallbacks callbacks) : callbacks_(callbacks) {}
    Runtime* owner = nullptr;
    bool teacher = false;
    void showRemote(bool fullscreen=false) { if (callbacks_.show) callbacks_.show(callbacks_.context,marker_,fullscreen); }
    bool focusScope(HWND window){return callbacks_.focus_scope && callbacks_.focus_scope(callbacks_.context,marker_,window);}
    void thumbnail(HDC dc,int width,int height) { if (callbacks_.thumbnail && marker_ != INVALID_SOCKET) callbacks_.thumbnail(callbacks_.context,marker_,dc,width,height); }
    bool inputAvailable() const override { return callbacks_.input_available && callbacks_.input_available(callbacks_.context); }
    void attach(std::shared_ptr<SecureConnection> stream, const std::string&) override {
        const auto generation=++generation_;
        std::lock_guard lock(mutex_); commands_.push_back([this, stream,generation] {
            if(generation!=generation_) { stream->stop(); return; }
            // The descriptor is only an engine identifier, never a plaintext socket.
            auto marker = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (marker == INVALID_SOCKET) { stream->stop(); return; }
            { std::lock_guard guard(registryMutex); registry[marker] = stream; if (teacher) peerRuntimes[marker] = owner; }
            marker_ = marker;
            if (!callbacks_.attach || !callbacks_.attach(callbacks_.context, marker)) { stream->stop(); }
        });
    }
    void detach() override {
        ++generation_;const SOCKET detached=marker_.exchange(INVALID_SOCKET);
        if (!teacher) controlAllowed = false;
        std::lock_guard lock(mutex_); commands_.push_back([this,detached] {
            if(detached==INVALID_SOCKET)return;
            if (callbacks_.detach) callbacks_.detach(callbacks_.context,detached);
            // The legacy engine owns and closes its marker descriptor.
            std::lock_guard guard(registryMutex); registry.erase(detached); peerRuntimes.erase(detached); peerControl.erase(detached); if(controlOwner==owner) controlOwner=nullptr; if(pendingControlOwner==owner) pendingControlOwner=nullptr; if(marker_==detached)marker_ = INVALID_SOCKET;
        });
    }
    void control(bool enabled) override {
        if (!teacher) { std::lock_guard guard(inputMutex); controlAllowed = enabled; }
        const auto generation=generation_.load();
        std::lock_guard lock(mutex_); commands_.push_back([this, enabled,generation] {
            if(generation!=generation_)return;
            bool permitted=enabled;
            if (teacher) { std::lock_guard guard(registryMutex); permitted=enabled && controlOwner==owner; if (permitted) peerControl.insert(marker_); else peerControl.erase(marker_); }
            if (callbacks_.control) callbacks_.control(callbacks_.context, marker_, permitted);
        });
    }
    void releaseInput() override {
        if (teacher) { std::lock_guard guard(registryMutex); peerControl.erase(marker_); return; }
        std::lock_guard guard(inputMutex);
        controlAllowed = false;
        std::set<uint32_t> released; unsigned mask;
        released.swap(keys); mask = buttons; buttons = 0;
        for (auto key : released) if (callbacks_.release_key) callbacks_.release_key(callbacks_.context, key);
        for (auto pair : {std::pair<unsigned,DWORD>{1, MOUSEEVENTF_LEFTUP}, {2, MOUSEEVENTF_MIDDLEUP}, {4, MOUSEEVENTF_RIGHTUP}}) {
            if (mask & pair.first) { INPUT event{}; event.type = INPUT_MOUSE; event.mi.dwFlags = pair.second; SendInput(1, &event, sizeof(event)); }
        }
    }
    void dispatch() {
        std::deque<std::function<void()>> commands;
        { std::lock_guard lock(mutex_); commands.swap(commands_); }
        for (auto& command : commands) command();
    }
private:
    KarizmiEngineCallbacks callbacks_; std::mutex mutex_;
    std::deque<std::function<void()>> commands_; std::atomic<SOCKET> marker_{INVALID_SOCKET};std::atomic<uint64_t> generation_=0;
};
enum : int { Code = 101, Connect, Disconnect, Toggle, Retry, Setup, Remove, Exit, Confirm, Decline, Status, Identity, Phrase, Advanced, Title, Expiration, CodeLabel, Subtitle, Disclosure, Logo, StatusLabel, Shortcut };
constexpr UINT TrayMessage = WM_APP + 7;
const UINT TaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
constexpr DWORD MainStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr int MinimumWidth = 520, DefaultWidth = 560, DefaultHeight = 508;
constexpr COLORREF BackgroundColor = RGB(248,246,251), TextColor = RGB(38,29,48), BrandColor = RGB(92,44,125);
struct App {
    Role role; HINSTANCE instance; UiEngine engine; Runtime app;
    HWND window = nullptr, indicator = nullptr;
    HFONT font = nullptr, large = nullptr, codeFont = nullptr; HBRUSH background = nullptr;
    NOTIFYICONDATAW tray{}; std::future<void> firewallTask;
    bool quitting = false, warningShown = false;
    State lastState = State::Ready;
    UINT dpi = 96;
    bool confirming = false;
    bool sessionWindow = false;
    HANDLE serviceStop = nullptr;
    App(Role role_, HINSTANCE instance_, KarizmiEngineCallbacks callbacks, std::shared_ptr<ClassroomResources> resources = {}) : role(role_), instance(instance_), engine(callbacks), app(role_, engine,resources) { engine.owner = &app; engine.teacher = role == Role::Teacher; }
    ~App() { if (serviceStop) CloseHandle(serviceStop); }
    bool restoreTray() {
        tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        tray.hIcon = LoadIconW(instance,MAKEINTRESOURCEW(49001)); tray.uCallbackMessage = TrayMessage;
        Shell_NotifyIconW(NIM_DELETE,&tray);
        const bool registered = Shell_NotifyIconW(NIM_ADD,&tray) != FALSE;
        SetPropW(window,L"KarizmiTrayRegistered",reinterpret_cast<HANDLE>(INT_PTR(registered)));
        if (registered) return true;
        ShowWindow(window,SW_SHOW); return false;
    }
    std::wstring trayStatus() {
        const auto state = app.display().session;
        return state.state == State::Connected ? (state.control ? L"Monitored — Control on" : L"Monitored — View only") : state.state == State::Recovering ? L"Interrupted" : state.state == State::Confirming ? L"Pairing confirmation required" : L"Ready";
    }
    int scale(int value) const { return MulDiv(value, dpi, 96); }
    HWND child(int id) { return GetDlgItem(window, id); }
    void text(int id, const std::wstring& value) {
        wchar_t previous[4096]{};
        GetWindowTextW(child(id),previous,4096);
        if (value != previous) SetWindowTextW(child(id),value.c_str());
    }
    int minimumHeight() const { return DefaultHeight + (confirming ? 100 : 0); }
    RECT outerBounds(int width, int height) const {
        RECT bounds{0,0,scale(width),scale(height)};
        AdjustWindowRectExForDpi(&bounds,MainStyle,FALSE,0,dpi);
        return bounds;
    }
    void updateFonts() {
        auto makeFont = [&](int pixels, int weight) {
            LOGFONTW description{};
            description.lfHeight = -MulDiv(pixels,static_cast<int>(dpi),96);
            description.lfWeight = weight;
            description.lfCharSet = DEFAULT_CHARSET;
            description.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(description.lfFaceName,L"Segoe UI");
            return CreateFontIndirectW(&description);
        };
        HFONT nextFont = makeFont(14,FW_NORMAL), nextLarge = makeFont(22,FW_SEMIBOLD), nextCode = makeFont(26,FW_SEMIBOLD);
        if (!nextFont || !nextLarge || !nextCode) {
            if (nextFont) DeleteObject(nextFont); if (nextLarge) DeleteObject(nextLarge); if (nextCode) DeleteObject(nextCode);
            throw std::runtime_error("Cannot create interface fonts");
        }
        HFONT oldFont = font, oldLarge = large, oldCode = codeFont;
        font = nextFont; large = nextLarge; codeFont = nextCode;
        for (int id = Code; id <= Shortcut; ++id)
            if (auto handle = child(id)) SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(id == Title ? large : id == Code ? codeFont : font),TRUE);
        if (oldFont) DeleteObject(oldFont); if (oldLarge) DeleteObject(oldLarge); if (oldCode) DeleteObject(oldCode);
    }
    void add(const wchar_t* kind, const wchar_t* text, int id, DWORD style = 0) {
        auto handle = CreateWindowExW(wcscmp(kind, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, kind, text,
            WS_CHILD | WS_VISIBLE | style, 0,0,0,0,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
        if (!handle) throw std::runtime_error("Cannot create an interface control");
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(id == Title ? large : id == Code ? codeFont : font), TRUE);
    }
    void layout() {
        if (!child(Title)) return;
        RECT client{}; GetClientRect(window,&client);
        const int width = MulDiv(client.right,96,dpi), height = MulDiv(client.bottom,96,dpi);
        const int content = width - 48, column = (content - 12) / 2;
        auto move = [&](int id, int x, int y, int w, int h) { MoveWindow(child(id), scale(x),scale(y),scale(w),scale(h),TRUE); };
        move(Logo,24,20,32,32); move(Title,68,18,content-44,32); move(Subtitle,24,60,content,22);
        move(CodeLabel,24,96,content,20); move(Code,24,120,column,40); move(Expiration,36+column,120,column,44);
        move(StatusLabel,24,178,content,20); move(Status,24,202,content,54); move(Identity,24,262,content,22);
        const int extra = confirming ? 100 : 0;
        move(Phrase,24,294,content,42); move(Confirm,24,344,column,34); move(Decline,36+column,344,column,34);
        ShowWindow(child(Phrase),confirming ? SW_SHOW : SW_HIDE); ShowWindow(child(Confirm),confirming ? SW_SHOW : SW_HIDE); ShowWindow(child(Decline),confirming ? SW_SHOW : SW_HIDE);
        if (role == Role::Teacher) {
            move(Connect,24,298+extra,column,34); move(Disconnect,36+column,298+extra,column,34);
            move(Toggle,24,342+extra,column,34); move(Retry,36+column,342+extra,column,34);
            move(Shortcut,24,382+extra,content,20);
        } else {
            move(Disclosure,24,296+extra,content,44); move(Disconnect,24,350+extra,column,34);
        }
        move(Setup,24,height-90,column,34); move(Remove,36+column,height-90,column,34);
        move(Advanced,24,height-44,column,28); move(Exit,width-124,height-44,100,28);
    }
    void create() {
        dpi = GetDpiForWindow(window); if (!dpi) dpi = 96;
        updateFonts();
        background = CreateSolidBrush(BackgroundColor);
        add(L"STATIC",L"",Logo,SS_ICON | SS_REALSIZECONTROL);
        SendMessageW(child(Logo),STM_SETICON,reinterpret_cast<WPARAM>(LoadIconW(instance,MAKEINTRESOURCEW(49001))),0);
        add(L"STATIC",role == Role::Student ? L"Karizmi Student" : L"Karizmi Teacher",Title);
        add(L"STATIC",role == Role::Student ? L"Share your screen with your paired teacher." : L"Connect to a student on your classroom network.",Subtitle);
        add(L"STATIC",role == Role::Student ? L"Your &pairing code" : L"Student &pairing code",CodeLabel);
        add(L"EDIT",L"",Code,WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | (role == Role::Student ? ES_READONLY : 0)); SendMessageW(child(Code),EM_SETLIMITTEXT,8,0);
        if (role == Role::Teacher) SendMessageW(child(Code),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"8 digits"));
        add(L"STATIC",L"Enter all eight digits,\nincluding leading zeros.",Expiration);
        add(L"STATIC",L"Connection status",StatusLabel);
        add(L"STATIC",L"Ready",Status,SS_EDITCONTROL | SS_ENDELLIPSIS); add(L"STATIC",L"No paired device",Identity,SS_ENDELLIPSIS); add(L"STATIC",L"",Phrase,SS_NOPREFIX);
        add(L"STATIC",L"The paired teacher can view your screen and request control.\nUse Disconnect or Exit to stop monitoring.",Disclosure);
        add(L"STATIC",L"Control shortcut: Ctrl+Shift+F12",Shortcut);
        for (auto item : {std::pair<int,const wchar_t*>{Connect,L"&Connect"},{Disconnect,L"&Disconnect"},{Toggle,L"Enable &control"},{Confirm,L"Con&firm phrase"},{Decline,L"Dec&line"},{Retry,L"&Reconnect now"},{Setup,L"&Set up exam connection"},{Remove,L"Re&move access"},{Advanced,L"&Advanced settings"},{Exit,L"E&xit"}}) add(L"BUTTON",item.second,item.first,WS_TABSTOP | BS_PUSHBUTTON);
        if (role == Role::Student) { ShowWindow(child(Connect),SW_HIDE); ShowWindow(child(Toggle),SW_HIDE); ShowWindow(child(Retry),SW_HIDE); ShowWindow(child(Shortcut),SW_HIDE); }
        else ShowWindow(child(Disclosure),SW_HIDE);
        if(sessionWindow) { ShowWindow(child(Setup),SW_HIDE); ShowWindow(child(Remove),SW_HIDE); }
        SendMessageW(child(Setup), BCM_SETSHIELD, 0, TRUE); SendMessageW(child(Remove), BCM_SETSHIELD, 0, TRUE);
        layout(); SetTimer(window,1,100,nullptr);
    }
    void show() { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
    void tick() {
        if (serviceStop && WaitForSingleObject(serviceStop,0) == WAIT_OBJECT_0) { command(Exit); return; }
        engine.dispatch(); auto state = app.display();
        if(role==Role::Student)SetPropW(window,L"KarizmiRemoteKeyEvents",reinterpret_cast<HANDLE>(UINT_PTR(observedKeyEvents.load())));
        SetPropW(window,L"KarizmiSessionGeneration",reinterpret_cast<HANDLE>(UINT_PTR(state.session.generation)));
        SetPropW(window,L"KarizmiControlOn",reinterpret_cast<HANDLE>(UINT_PTR(state.session.control)));
        if (confirming != state.awaitingConfirmation) {
            confirming = state.awaitingConfirmation;
            RECT client{}; GetClientRect(window,&client);
            auto bounds = outerBounds(std::max(MinimumWidth,MulDiv(client.right,96,dpi)),minimumHeight());
            SetWindowPos(window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            if (confirming && role == Role::Student) show();
        }
        if (role == Role::Student) {
            text(Code,wide(state.code));
            auto seconds = state.expires > now() ? (state.expires - now() + 999) / 1000 : 0;
            text(Expiration,state.code.empty() ? L"Pairing reserved" : (L"Expires in " + std::to_wstring(seconds) + L" seconds"));
        }
        std::wstring statusText = wide(state.status);
        if (state.session.state == State::Recovering) {
            statusText += L"\r\nSTALE — monitoring unavailable";
            if (state.session.lastFrame) statusText += L"; last update " + std::to_wstring((now() - state.session.lastFrame) / 1000) + L" seconds ago";
        }
        text(Status,statusText);
        text(Identity,state.session.peer.empty() ? (role == Role::Student ? L"Teacher: not paired" : L"Student: not paired") : ((role == Role::Student ? L"Teacher device: " : L"Student device: ") + wide(state.session.peer)));
        text(Phrase,state.awaitingConfirmation ? L"Compare on both screens:\r\n" + wide(state.phrase) : L"");
        text(Toggle,state.session.control ? L"Switch to &view only" : L"Enable &control");
        EnableWindow(child(Confirm),state.awaitingConfirmation); EnableWindow(child(Decline),state.awaitingConfirmation);
        EnableWindow(child(Connect),state.session.state == State::Ready || state.session.state == State::Ended);
        EnableWindow(child(Toggle),state.session.state == State::Connected && state.session.controlAvailable);
        EnableWindow(child(Retry),state.session.state == State::Recovering || state.session.state==State::Connected);
        if (firewallTask.valid() && firewallTask.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { firewallTask.get(); EnableWindow(child(Setup),TRUE); EnableWindow(child(Remove),TRUE); }
        if (role == Role::Teacher && !sessionWindow) {
            wcsncpy_s(tray.szTip, statusText.c_str(), _TRUNCATE); Shell_NotifyIconW(NIM_MODIFY,&tray);
            if (state.session.state == State::Recovering && lastState != State::Recovering) {
                tray.uFlags |= NIF_INFO; wcscpy_s(tray.szInfoTitle,L"Student connection interrupted"); wcscpy_s(tray.szInfo,L"Monitoring is unavailable. Retrying until Disconnect or Exit."); tray.dwInfoFlags = NIIF_WARNING; Shell_NotifyIconW(NIM_MODIFY,&tray); tray.uFlags &= ~NIF_INFO;
            }
        }
        lastState = state.session.state;
        if (role == Role::Student) {
            const auto tooltip = L"Karizmi Student — " + trayStatus();
            wcsncpy_s(tray.szTip,tooltip.c_str(),_TRUNCATE); Shell_NotifyIconW(NIM_MODIFY,&tray);
        }
    }
    void command(int id) {
        try {
            switch (id) {
            case Connect: {
                wchar_t code[9]{}; GetWindowTextW(child(Code),code,9); std::string value;
                for (auto digit : std::wstring(code)) { if (digit < L'0' || digit > L'9') throw std::runtime_error("Enter eight decimal digits."); value += static_cast<char>(digit); }
                app.connectTeacher(value); break;
            }
            case Disconnect: app.disconnect(); break;
            case Toggle: if(role==Role::Teacher) toggleControl(&app); break;
            case Retry: app.reconnectNow(); break;
            case Confirm: app.confirm(true); break;
            case Decline: app.confirm(false); break;
            case Setup: case Remove:
                if (!firewallTask.valid()) {
                    EnableWindow(child(Setup),FALSE); EnableWindow(child(Remove),FALSE);
                    firewallTask = std::async(std::launch::async,[this,id] { app.setupFirewall(window,id == Remove); });
                } break;
            case Exit: if(sessionWindow) { app.disconnect(); ShowWindow(window,SW_HIDE); } else { app.suspend(); quitting = true; DestroyWindow(window); } break;
            case Advanced: {
                std::wstring diagnostic=L"Karizmi uses the local IPv4 LAN/WLAN.\n\nDiscovery: UDP 45900\nPairing/recovery: TCP 45901\nEncrypted screen/control: TCP 45902\n\nSet up firewall access on both PCs. Wi-Fi client isolation and managed policy may prevent access.\n\nBuilt on UltraVNC (GPL-3.0-or-later) and Botan (BSD-2-Clause). See packaged licenses.";
                diagnostic+=L"\n\nRecent recovery stages (credentials excluded):\n";
                for(const auto& event:app.display().recoveryTrace)diagnostic+=wide(event)+L"\n";
                MessageBoxW(window,diagnostic.c_str(),L"Karizmi - Advanced / About",MB_OK);break;
            }
            }
        } catch (const std::exception& e) { MessageBoxW(window,wide(e.what()).c_str(),L"Karizmi",MB_OK | MB_ICONERROR); }
    }
};
LRESULT CALLBACK indicatorProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) { auto create = reinterpret_cast<CREATESTRUCTW*>(l); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams)); }
    if (app && (message == WM_LBUTTONUP || message == WM_KEYUP)) { app->show(); return 0; }
    if (message == WM_CLOSE) return 0;
    if (message == WM_PAINT && app) { PAINTSTRUCT paint; auto dc = BeginPaint(window,&paint); RECT rect; GetClientRect(window,&rect); SetDCBrushColor(dc,BrandColor); FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255)); auto previous = SelectObject(dc,app->font); wchar_t text[256]; GetWindowTextW(window,text,256); DrawTextW(dc,text,-1,&rect,DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc,previous); EndPaint(window,&paint); return 0; }
    if (message == WM_SETTEXT) { auto result = DefWindowProcW(window,message,w,l); InvalidateRect(window,nullptr,TRUE); return result; }
    return DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE) { auto create = reinterpret_cast<CREATESTRUCTW*>(l); app = static_cast<App*>(create->lpCreateParams); app->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if (!app) return DefWindowProcW(window,message,w,l);
    if (message == TaskbarCreated) { if(!app->sessionWindow) app->restoreTray(); return 0; }
    switch (message) {
    case WM_CREATE: app->create(); return 0;
    case WM_COMMAND: if (HIWORD(w) == BN_CLICKED) app->command(LOWORD(w)); return 0;
    case WM_SIZE: if (w != SIZE_MINIMIZED) app->layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto limits = reinterpret_cast<MINMAXINFO*>(l); auto bounds = app->outerBounds(MinimumWidth,app->minimumHeight());
        limits->ptMinTrackSize = {bounds.right-bounds.left,bounds.bottom-bounds.top}; return 0;
    }
    case WM_TIMER: app->tick(); return 0;
    case WM_CLOSE: ShowWindow(window,SW_HIDE); return 0;
    case WM_QUERYENDSESSION: app->app.disconnect(); return TRUE;
    case WM_ENDSESSION: if (w) { app->quitting = true; DestroyWindow(window); } return 0;
    case WM_DESTROY:
        KillTimer(window,1); if (app->indicator) DestroyWindow(app->indicator); Shell_NotifyIconW(NIM_DELETE,&app->tray);
        if (!app->sessionWindow) PostQuitMessage(0); return 0;
    case WM_DPICHANGED:
        app->dpi = HIWORD(w); { auto rect = reinterpret_cast<RECT*>(l); SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER); } app->updateFonts(); app->layout(); return 0;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(l) == app->child(Code)) { SetTextColor(reinterpret_cast<HDC>(w),TextColor); SetBkColor(reinterpret_cast<HDC>(w),RGB(255,255,255)); return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH)); }
        SetTextColor(reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l) == app->child(Title) ? BrandColor : TextColor); SetBkColor(reinterpret_cast<HDC>(w),BackgroundColor); return reinterpret_cast<LRESULT>(app->background);
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(w),TextColor); SetBkColor(reinterpret_cast<HDC>(w),RGB(255,255,255)); return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_ERASEBKGND: { RECT rect; GetClientRect(window,&rect); FillRect(reinterpret_cast<HDC>(w),&rect,app->background); return 1; }
    case TrayMessage:
        if (l == WM_LBUTTONDBLCLK) app->show();
        if (l == WM_RBUTTONUP) { HMENU menu = CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,app->role == Role::Student ? L"Open Karizmi Student" : L"Open Karizmi Teacher"); AppendMenuW(menu,MF_STRING | MF_DISABLED,0,app->role == Role::Student ? app->trayStatus().c_str() : wide(app->app.display().status).c_str()); AppendMenuW(menu,MF_STRING,Disconnect,L"Disconnect"); AppendMenuW(menu,MF_STRING,Exit,L"Exit"); POINT point; GetCursorPos(&point); SetForegroundWindow(window); int choice = TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,window,nullptr); DestroyMenu(menu); if (choice == 1) app->show(); else if (choice) app->command(choice); } return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
#include "classroom_ui.inc"
}
extern "C" {
int KarizmiPeerControlAllowed(uintptr_t socket) { std::lock_guard lock(registryMutex); return peerControl.count(socket) != 0; }
void KarizmiObserveKeyEvent(){++observedKeyEvents;}
void KarizmiPeerToggleControl(uintptr_t socket) {
    Runtime* selected=nullptr;
    {std::lock_guard lock(registryMutex);auto found=peerRuntimes.find(socket);if(found!=peerRuntimes.end())selected=found->second;}
    if(selected)toggleControl(selected);
}
void KarizmiPeerDisableControl(uintptr_t socket) {
    Runtime* selected = nullptr;
    { std::lock_guard lock(registryMutex); peerControl.erase(socket); auto found = peerRuntimes.find(socket); if (found != peerRuntimes.end()) selected = found->second; }
    if (selected) selected->requestControl(false);
}
void KarizmiPeerFrameReceived(uintptr_t socket) { std::lock_guard lock(registryMutex); auto found = peerRuntimes.find(socket); if (found != peerRuntimes.end()) found->second->frameReceived(); }
void KarizmiPeerViewerClosed(uintptr_t socket) { if(auto channel=lookup(socket)) channel->stop(); }
int KarizmiHelper(int student, int* result) { auto value = karizmi::firewallHelperMain(student ? karizmi::Role::Student : karizmi::Role::Teacher); if (!value) return 0; *result = *value; return 1; }
int KarizmiRead(uintptr_t socket, void* data, int size) {
    auto channel=lookup(socket); if(!channel) return 0;
    const int count=channel->readRfbBlocking(data,size);return count<0?0:count;
}
int KarizmiWrite(uintptr_t socket, const void* data, int size) { auto channel = lookup(socket); return size >= 0 && channel && channel->writeRfb(data,size) ? size : -1; }
int KarizmiReadable(uintptr_t socket, unsigned timeout) { auto channel = lookup(socket); return channel && channel->rfbReadable(karizmi::now()+timeout); }
int KarizmiAuthenticated(uintptr_t socket) { auto channel = lookup(socket); return channel && channel->alive(); }
int KarizmiControlAllowed() { return controlAllowed; }
void KarizmiLockInput() { inputMutex.lock(); }
void KarizmiUnlockInput() { inputMutex.unlock(); }
void KarizmiTrackKey(uint32_t key, int down) { std::lock_guard lock(inputMutex); if (down) keys.insert(key); else keys.erase(key); }
void KarizmiTrackButtons(unsigned mask) { std::lock_guard lock(inputMutex); buttons = mask & 7; }
void KarizmiToggleControl() { if (auto app = runtime.load()) app->requestControl(!app->display().session.control); }
void KarizmiDisableControl() { if (auto app = runtime.load()) app->requestControl(false); }
void KarizmiFrameReceived() { if (auto app = runtime.load()) app->frameReceived(); }
void KarizmiDisconnect() { if (auto app = runtime.load()) app->disconnect(); }
int KarizmiRun(void* instance, int student, const KarizmiEngineCallbacks* callbacks) {
    installTerminationDiagnostics();
    try {
        const auto role = student ? karizmi::Role::Student : karizmi::Role::Teacher;
        auto helper = karizmi::firewallHelperMain(role); if (helper) return *helper;
        if (!student) return runClassroom(static_cast<HINSTANCE>(instance),*callbacks);
        HANDLE singleton = CreateMutexW(nullptr,TRUE,student ? L"Local\\KarizmiStudent" : L"Local\\KarizmiTeacher");
        if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) { if (singleton) CloseHandle(singleton); MessageBoxW(nullptr,L"Karizmi is already running in this Windows session.",L"Karizmi",MB_OK); return 1; }
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        if (student && MessageBoxW(nullptr,L"During this exam, the paired teacher can view your screen and, when control is enabled, use your mouse and keyboard.\n\nKarizmi stays in the system tray. Open its tray menu to see monitoring status, Disconnect, or Exit. Interrupted sessions reconnect until you disconnect or exit.\n\nContinue? Open Student from its tray icon to see your pairing code.",L"Karizmi — Exam monitoring notice",MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) { CloseHandle(singleton); return 0; }
        App app(role,static_cast<HINSTANCE>(instance),*callbacks); runtime = &app.app;
        if (student) {
            int count{}; auto arguments = CommandLineToArgvW(GetCommandLineW(),&count);
            if (!arguments) throw std::runtime_error("Cannot read Student startup arguments");
            const bool fromService = count == 3 && wcscmp(arguments[1],L"--service-agent") == 0;
            if (fromService) app.serviceStop = OpenEventW(SYNCHRONIZE,FALSE,arguments[2]);
            LocalFree(arguments);
            if (fromService && !app.serviceStop) throw std::runtime_error("The Karizmi Student service is unavailable");
        }
        WNDCLASSW wc{}; wc.hInstance = app.instance; wc.lpfnWndProc = windowProc; wc.lpszClassName = L"KarizmiMain"; wc.hCursor = LoadCursor(nullptr,IDC_ARROW); wc.hIcon = LoadIconW(app.instance,MAKEINTRESOURCEW(49001)); RegisterClassW(&wc);
        wc.lpfnWndProc = indicatorProc; wc.lpszClassName = L"KarizmiIndicator"; RegisterClassW(&wc);
        app.window = CreateWindowExW(0,L"KarizmiMain",student ? L"Karizmi Student" : L"Karizmi Teacher",MainStyle,CW_USEDEFAULT,CW_USEDEFAULT,620,610,nullptr,nullptr,app.instance,&app);
        if (!app.window) throw std::runtime_error("Cannot create Karizmi window");
        auto bounds = app.outerBounds(DefaultWidth,DefaultHeight);
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST),&monitor);
        SetWindowPos(app.window,nullptr,monitor.rcWork.left + std::max<LONG>(0,(monitor.rcWork.right-monitor.rcWork.left-(bounds.right-bounds.left))/2),monitor.rcWork.top + std::max<LONG>(0,(monitor.rcWork.bottom-monitor.rcWork.top-(bounds.bottom-bounds.top))/2),bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER);
        if (student) {
            app.app.startStudent();
        }
        {
            app.tray.cbSize = sizeof(app.tray); app.tray.hWnd = app.window; app.tray.uID = 1; app.tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            app.tray.hIcon = wc.hIcon; app.tray.uCallbackMessage = TrayMessage; wcscpy_s(app.tray.szTip,student ? L"Karizmi Student — Ready" : L"Karizmi Teacher — Ready");
            if (!app.restoreTray()) ShowWindow(app.window,SW_SHOW);
        }
        if (!student) ShowWindow(app.window,SW_SHOW);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) {
            if(!IsDialogMessageW(app.window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        app.app.shutdown(); runtime = nullptr; app.engine.dispatch();
        if (app.firewallTask.valid()) app.firewallTask.wait();
        CloseHandle(singleton); return 0;
    } catch (const std::exception& e) { runtime = nullptr; MessageBoxW(nullptr,wide(e.what()).c_str(),L"Karizmi startup failed",MB_OK | MB_ICONERROR); return 1; }
}
}
