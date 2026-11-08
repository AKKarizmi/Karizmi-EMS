// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdhdrs.h"
#include "winvnc.h"
#include "vncserver.h"
#include "vsocket.h"
#include "SettingsManager.h"
#include "vnckeymap.h"
#include "karizmi/integration.h"

extern HINSTANCE hAppInstance;
extern HINSTANCE hInstResDLL;
extern DWORD mainthreadId;
extern bool fShutdownOrdered;
namespace {
class ExamSocket : public VSocket {
public: explicit ExamSocket(SOCKET marker) { sock = marker; }
};
int available(void*) { return settings->getEnableRemoteInputs() ? 1 : 0; }
int attach(void* context, uintptr_t marker) {
    if (!KarizmiAuthenticated(marker)) return 0;
    auto* server = static_cast<vncServer*>(context);
    server->KillAuthClients(); server->KillUnauthClients();
    server->WaitUntilAuthEmpty(); server->WaitUntilUnauthEmpty();
    return server->AddClient(new ExamSocket(SOCKET(marker)), TRUE, FALSE, FALSE) >= 0;
}
void detach(void* context, uintptr_t) {
    auto* server = static_cast<vncServer*>(context); server->KillAuthClients(); server->KillUnauthClients();
}
void control(void*, uintptr_t, int) { /* Per-message input permission is checked in vncclient. */ }
void release(void*, uint32_t key) { vncKeymap::keyEvent(key, false, settings->getEnableJapInput(), settings->getEnableUnicodeInput()); }
}
int KarizmiStudentMain(HINSTANCE instance) {
    int result; if (KarizmiHelper(1, &result)) return result;
    hAppInstance = hInstResDLL = instance; mainthreadId = GetCurrentThreadId();
    settings = SettingsManager::getInstance();
    // No legacy INI/network startup is run: credentials and connection ownership
    // belong to the exam runtime. Keep the established capture/input engine.
    settings->setIPV6(FALSE); settings->setEnableConnections(false);
    settings->setHTTPConnect(false); settings->setEnableTLS(FALSE);
    settings->setCloudEnabled(FALSE); settings->setUseBridge(FALSE);
    settings->setUseDSMPlugin(FALSE); settings->setRequireMSLogon(FALSE);
    settings->setRequireMSLogonIII(FALSE); settings->setAuthRequired(TRUE);
    settings->setEnableFileTransfer(FALSE); settings->setDisableLocalInputs(FALSE);
    settings->setEnableBlankMonitor(FALSE); settings->setDisableTrayIcon(TRUE);
    settings->setScPrompt(FALSE); settings->setNotification(FALSE);
    settings->setQuerySetting(2); settings->setMaxViewers(1);
    settings->setHook(FALSE); settings->setDriver(FALSE); settings->setddEngine(FALSE);
    settings->setPollFullScreen(TRUE);
    VSocketSystem networking; if (!networking.Initialised()) return 1;
    vncServer server; server.SetName("Karizmi Student");
    KarizmiEngineCallbacks callbacks{&server, available, attach, detach, control, release};
    result = KarizmiRun(instance, 1, &callbacks);
    fShutdownOrdered = true; detach(&server,0); server.WaitUntilAuthEmpty(); server.WaitUntilUnauthEmpty();
    return result;
}
