// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdhdrs.h"
#include "VNCviewerApp32.h"
#include "ClientConnection.h"
#include "Exception.h"
#include "karizmi/integration.h"

namespace {
int available(void*) { return 1; }
int attach(void* context, uintptr_t marker) {
    if (!KarizmiAuthenticated(marker)) return 0;
    auto* app = static_cast<VNCviewerApp32*>(context);
    app->m_options.m_ViewOnly = true;
    app->NewConnection(false, SOCKET(marker));
    app->KarizmiShowConnection(INVALID_SOCKET);
    return app->KarizmiHasConnection(SOCKET(marker));
}
void detach(void* context, uintptr_t marker) { static_cast<VNCviewerApp32*>(context)->KarizmiCloseConnection(SOCKET(marker)); }
void control(void* context, uintptr_t marker, int enabled) { static_cast<VNCviewerApp32*>(context)->KarizmiSetControl(SOCKET(marker),enabled != 0); }
void show(void* context, uintptr_t marker, int fullscreen) { static_cast<VNCviewerApp32*>(context)->KarizmiShowConnection(SOCKET(marker),fullscreen != 0); }
void thumbnail(void* context, uintptr_t marker, void* dc, int width, int height) { static_cast<VNCviewerApp32*>(context)->KarizmiThumbnail(SOCKET(marker),static_cast<HDC>(dc),width,height); }
void release(void*, uint32_t) {}
uintptr_t windowPeer(void* context,void* window){return static_cast<VNCviewerApp32*>(context)->KarizmiWindowPeer(static_cast<HWND>(window));}
int focusScope(void* context,uintptr_t marker,void* window){return static_cast<VNCviewerApp32*>(context)->KarizmiFocusScope(SOCKET(marker),static_cast<HWND>(window));}
}
int KarizmiTeacherMain(HINSTANCE instance) {
    wchar_t arguments[] = L"";
    VNCviewerApp32 app(instance, arguments);
    app.m_options.m_listening = false;
    app.m_options.m_ViewOnly = true;
    app.m_options.m_DisableClipboard = true;
    app.m_options.m_NoStatus = true;
    app.m_options.m_ipv6 = false;
    app.m_options.m_Directx = false; // Classroom thumbnails use the decoded DIB.
    KarizmiEngineCallbacks callbacks{&app, available, attach, detach, control, release, show, thumbnail, windowPeer, focusScope};
    return KarizmiRun(instance, 0, &callbacks);
}
