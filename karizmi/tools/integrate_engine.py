"""Apply the guarded Karizmi engine hooks once, preserving upstream build paths.

This migration is intentionally exact-match and fails on an unexpected source
revision. Original files are copied to ignored build/originals before changing.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
changed = {}

def read(name):
    if name not in changed:
        changed[name] = (ROOT / name).read_bytes().decode('utf-8-sig')
    return changed[name]

def replace(name, old, new, count=1):
    text = read(name)
    # Normalize only in memory; restore CRLF on writing when appropriate.
    text = text.replace('\r\n', '\n')
    if text.count(old) != count:
        raise RuntimeError(f'{name}: expected {count} occurrences of {old[:80]!r}, got {text.count(old)}')
    changed[name] = text.replace(old, new)

def include(name):
    text = read(name).replace('\r\n', '\n')
    anchor = re.search(r'^#include[^\n]*\n', text, re.M)
    if not anchor:
        raise RuntimeError(f'No include in {name}')
    changed[name] = text[:anchor.end()] + '#ifdef KARIZMI_EXAM\n#include "karizmi/integration.h"\n#endif\n' + text[anchor.end():]

if 'KARIZMI_EXAM' in (ROOT / 'winvnc/winvnc/winvnc.cpp').read_text(encoding='utf-8-sig'):
    raise SystemExit('Karizmi hooks already applied; do not apply this migration twice.')

server_main = 'winvnc/winvnc/winvnc.cpp'
replace(server_main, 'int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine2, int iCmdShow)\n{',
'''#ifdef KARIZMI_EXAM
extern int KarizmiStudentMain(HINSTANCE instance);
#endif
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine2, int iCmdShow)
{
#ifdef KARIZMI_EXAM
    return KarizmiStudentMain(hInstance);
#endif''')
viewer_main = 'vncviewer/vncviewer.cpp'
include(viewer_main)
replace(viewer_main, 'int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR szCmdLine, int nCmdShow)\n{',
'''#ifdef KARIZMI_EXAM
extern int KarizmiTeacherMain(HINSTANCE instance);
#endif
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR szCmdLine, int nCmdShow)
{
#ifdef KARIZMI_EXAM
    int helperResult;
    if (KarizmiHelper(0, &helperResult)) return helperResult;
    wchar_t examArguments[] = L"";
    szCmdLine = examArguments;
#endif''')
replace(viewer_main, '\t\tVNCviewerApp32 app(hInstance, szCmdLine);',
'''#ifdef KARIZMI_EXAM
        return KarizmiTeacherMain(hInstance);
#endif
        VNCviewerApp32 app(hInstance, szCmdLine);''')

socket = 'winvnc/winvnc/vsocket.cpp'
include(socket)
for signature, expression in [
    ('VSocket::ReadSock(char *buff, const VCard bufflen, SOCKET allsock)', 'KarizmiRead(allsock, buff, bufflen)'),
    ('VSocket::ReadIPV4(char *buff, const VCard bufflen)', 'KarizmiRead(sock, buff, bufflen)'),
    ('VSocket::ReadSelectSock(VCard to, SOCKET allsock)', 'KarizmiReadable(allsock, to)'),
    ('sendall(SOCKET RemoteSocket,char *buff,unsigned int bufflen,int dummy)', 'KarizmiWrite(RemoteSocket, buff, bufflen) == int(bufflen)'),
]:
    replace(socket, signature+'\n{', signature+'\n{\n#ifdef KARIZMI_EXAM\n    return '+expression+';\n#endif')
replace(socket, 'VSocket::ReadSelectIPV4(VCard to)\n {', 'VSocket::ReadSelectIPV4(VCard to)\n {\n#ifdef KARIZMI_EXAM\n    return KarizmiReadable(sock, to);\n#endif')
replace('winvnc/winvnc/vsocket.h', '  // GetPeerName\n', '  // GetPeerName\n', 1) # verifies expected upstream interface
replace('winvnc/winvnc/vsocket.h', '  // GetPeerName\n', '''#ifdef KARIZMI_EXAM
  bool KarizmiAuthenticatedSocket() const { return KarizmiAuthenticated(sock) != 0; }
#endif
  // GetPeerName
''')
include('winvnc/winvnc/vsocket.h')
for signature in ['VSocket::GetPeerName(bool naam)', 'VSocket::GetSockName()']:
    replace(socket, signature+'\n{', signature+'\n{\n#ifdef KARIZMI_EXAM\n    return const_cast<char*>("Karizmi authenticated peer");\n#endif')

server = 'winvnc/winvnc/vncserver.cpp'
include(server)
replace(server, '\tomni_mutex_lock l(m_clientsLock, 13);', '''\tomni_mutex_lock l(m_clientsLock, 13);
#ifdef KARIZMI_EXAM
    if (!socket->KarizmiAuthenticatedSocket() || !m_authClients.empty() || !m_unauthClients.empty()) { delete socket; return -1; }
#endif''')
replace(server, 'vncServer::EnableConnections(BOOL On)\n{', 'vncServer::EnableConnections(BOOL On)\n{\n#ifdef KARIZMI_EXAM\n    if (On) return FALSE;\n#endif')

client = 'winvnc/winvnc/vncclient.cpp'
include(client)
replace(client, '\t\t\t\t\tif (m_client->m_keyboardenabled)\n', '''\t\t\t\t\tif (m_client->m_keyboardenabled
#ifdef KARIZMI_EXAM
                        && KarizmiControlAllowed()
#endif
                    )
''')
replace(client, '\t\t\t\t\t\tvncKeymap::keyEvent(msg.ke.key, (0 != msg.ke.down), m_client->m_jap, m_client->m_unicode);', '''#ifdef KARIZMI_EXAM
                        KarizmiTrackKey(msg.ke.key, msg.ke.down != 0);
#endif
                        vncKeymap::keyEvent(msg.ke.key, (0 != msg.ke.down), m_client->m_jap, m_client->m_unicode);''')
replace(client, '\t\t\t\tif (m_client->m_pointerenabled)\n', '''\t\t\t\tif (m_client->m_pointerenabled
#ifdef KARIZMI_EXAM
                    && KarizmiControlAllowed()
#endif
                )
''')
replace(client, '\t\t\t\tif (PreConnect) break;', '''\t\t\t\tif (PreConnect) break;
#ifdef KARIZMI_EXAM
                if (KarizmiControlAllowed() && m_client->m_pointerenabled) KarizmiTrackButtons(msg.pe.buttonMask);
#endif''')

rdr = 'rdr/FdInStream.cxx'
include(rdr)
replace(rdr, 'int FdInStream::checkReadable(int fd, int timeout)\n{', '''int FdInStream::checkReadable(int fd, int timeout)
{
#ifdef KARIZMI_EXAM
    return KarizmiReadable(uintptr_t(fd), timeout ? unsigned(timeout) : 30000);
#endif''')
replace(rdr, '\t\tn = ::read(fd, buf, len);', '''#ifdef KARIZMI_EXAM
        n = KarizmiRead(uintptr_t(fd), buf, len);
#else
        n = ::read(fd, buf, len);
#endif''')

viewer = 'vncviewer/ClientConnection.cpp'
include(viewer)
replace(viewer, '\thavetobekilled=false;\n\tforcedexit=false;', '''\thavetobekilled=false;
    forcedexit=false;
#ifdef KARIZMI_EXAM
    if (!KarizmiAuthenticated(m_sock)) throw QuietException(L"Authenticated Karizmi transport required");
    m_opts->m_ViewOnly = true;
    m_opts->m_DisableClipboard = true;
    m_autoReconnect = 0;
    m_reconnectcounter = 0;
    m_port = 45902;
#endif''')
replace(viewer, '\tSetDSMPluginStuff(); // The Plugin is now activated BEFORE the protocol negociation', '''#ifdef KARIZMI_EXAM
    m_pDSMPlugin->SetEnabled(false);
    m_fUsePlugin = false;
#else
    SetDSMPluginStuff();
#endif
    // The Plugin is now activated BEFORE the protocol negociation''')
for old, new in [
    ('aa=send(m_sock, buff, bufflen, 0)', 'aa=KarizmiWrite(m_sock, buff, bufflen)'),
    ('j = send(m_sock, m_QueueBuffer+i, m_nQueueBufferLength-i, 0);', 'j = KarizmiWrite(m_sock, m_QueueBuffer+i, m_nQueueBufferLength-i);'),
    ('sent = send(m_sock, m_QueueBuffer, G_SENDBUFFER, 0);', 'sent = KarizmiWrite(m_sock, m_QueueBuffer, G_SENDBUFFER);'),
]:
    if old.startswith('aa='):
        # The timeout path must avoid select() on the non-network marker.
        continue
    replace(viewer, old, '#ifdef KARIZMI_EXAM\n                '+new+'\n#else\n                '+old+'\n#endif')
replace(viewer, 'ClientConnection::Send(const char *buff, const unsigned int bufflen,int timeout)\n{', '''ClientConnection::Send(const char *buff, const unsigned int bufflen,int timeout)
{
#ifdef KARIZMI_EXAM
    return KarizmiWrite(m_sock, buff, bufflen);
#endif''')
replace(viewer, 'ClientConnection::SendPointerEvent(int x, int y, int buttonMask)\n{', '''ClientConnection::SendPointerEvent(int x, int y, int buttonMask)
{
#ifdef KARIZMI_EXAM
    if (!KarizmiControlAllowed()) return;
#endif''')
replace(viewer, 'ClientConnection::SendKeyEvent(CARD32 key, bool down)\n{', '''ClientConnection::SendKeyEvent(CARD32 key, bool down)
{
#ifdef KARIZMI_EXAM
    if (!KarizmiControlAllowed()) return;
#endif''')
replace(viewer, '\t\t\t\t\tcase ID_VIEWONLYTOGGLE:\n', '''\t\t\t\t\tcase ID_VIEWONLYTOGGLE:
#ifdef KARIZMI_EXAM
                        KarizmiToggleControl(); return 0;
#endif
''')
replace(viewer, '\t\t\tcase WM_KILLFOCUS:\n', '''\t\t\tcase WM_KILLFOCUS:
#ifdef KARIZMI_EXAM
                KarizmiDisableControl();
#endif
''', count=2)

appheader = 'vncviewer/VNCviewerApp.h'
replace(appheader, '\tVNCOptions m_options;', '''#ifdef KARIZMI_EXAM
    void KarizmiCloseConnection();
    void KarizmiSetControl(bool enabled);
    bool KarizmiHasConnection();
#endif
    VNCOptions m_options;''')
app = 'vncviewer/VNCviewerApp.cpp'
replace(app, '\t\t\tif ((m_clilist[0] == NULL) && (!pApp->m_options.m_listening))\n\t\t\t\tPostQuitMessage(0);', '''#ifndef KARIZMI_EXAM
            if ((m_clilist[0] == NULL) && (!pApp->m_options.m_listening))
                PostQuitMessage(0);
#endif''')
changed[app] += '''
#ifdef KARIZMI_EXAM
bool VNCviewerApp::KarizmiHasConnection() { return m_clilist[0] != NULL; }
void VNCviewerApp::KarizmiCloseConnection() {
    // Called only by the window thread; WM_CLOSE may deregister synchronously.
    if (m_clilist[0] && IsWindow(m_clilist[0]->m_hwndMain))
        SendMessage(m_clilist[0]->m_hwndMain, WM_CLOSE, 0, 1);
}
void VNCviewerApp::KarizmiSetControl(bool enabled) {
    if (m_clilist[0]) {
        m_clilist[0]->m_opts->m_ViewOnly = !enabled;
        SetWindowTextW(m_clilist[0]->m_hwndMain, enabled ? L"Karizmi Teacher — CONTROL ON" : L"Karizmi Teacher — VIEW ONLY");
    }
}
#endif
'''

for name, text in changed.items():
    path = ROOT / name
    original = path.read_bytes()
    backup = ROOT / 'karizmi/build/originals' / name
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists(): backup.write_bytes(original)
    text = text.replace('\r\n', '\n')
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8') if b'\r\n' in original else text.encode('utf-8'))
print(f'Integrated {len(changed)} engine files with KARIZMI_EXAM guards.')
