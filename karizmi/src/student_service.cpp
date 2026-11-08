// SPDX-License-Identifier: GPL-3.0-or-later
// Session-0 supervisor. Capture, pairing, and UI run under the signed-in user.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <sddl.h>
#include <bcrypt.h>
#include <string>
#include <set>
#include <stdexcept>

namespace {
constexpr wchar_t ServiceName[] = L"KarizmiStudent";
SERVICE_STATUS_HANDLE statusHandle{};
HANDLE stopping{};
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
void report(DWORD state, DWORD error = NO_ERROR) {
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = error;
    status.dwWaitHint = state == SERVICE_STOP_PENDING ? 10000 : 0;
    SetServiceStatus(statusHandle,&status);
}
DWORD WINAPI control(DWORD code, DWORD, void*, void*) {
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) SetEvent(stopping);
    return NO_ERROR;
}
void logError(const wchar_t* operation) {
    const auto error = GetLastError();
    Handle source(RegisterEventSourceW(nullptr,ServiceName));
    if (source.value) {
        std::wstring text = std::wstring(operation) + L" failed; Windows error " + std::to_wstring(error);
        const wchar_t* strings[] = {text.c_str()};
        ReportEventW(source.value,EVENTLOG_ERROR_TYPE,0,1,nullptr,1,0,strings,nullptr);
        DeregisterEventSource(source.value); source.value = nullptr;
    }
}
std::wstring executable() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr,path,32768);
    if (!length || length >= 32768) throw std::runtime_error("service path");
    std::wstring result(path,length);
    return result.substr(0,result.find_last_of(L"\\/") + 1) + L"KarizmiStudent.exe";
}
void supervise() {
    const auto application = executable();
    const auto directory = application.substr(0,application.find_last_of(L"\\/"));
    Handle job(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
        throw std::runtime_error("service job");
    unsigned char random[16]{};
    if (BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("service event randomness");
    std::wstring eventName = L"Global\\KarizmiStudentStop-";
    const wchar_t hex[] = L"0123456789abcdef";
    for (auto byte : random) { eventName += hex[byte >> 4]; eventName += hex[byte & 15]; }
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100000;;;AU)",SDDL_REVISION_1,&descriptor,nullptr))
        throw std::runtime_error("service event ACL");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    Handle agentStop(CreateEventW(&attributes,TRUE,FALSE,eventName.c_str()));
    DWORD eventError = GetLastError(); LocalFree(descriptor);
    if (!agentStop.value || eventError == ERROR_ALREADY_EXISTS) throw std::runtime_error("service event");
    Handle process;
    DWORD childSession = MAXDWORD;
    std::set<unsigned long long> launchedLogons;
    ULONGLONG nextAttempt = 0;
    while (WaitForSingleObject(stopping,1000) == WAIT_TIMEOUT) {
        const DWORD session = WTSGetActiveConsoleSessionId();
        if (process.value && (session != childSession || WaitForSingleObject(process.value,0) == WAIT_OBJECT_0)) {
            SetEvent(agentStop.value);
            if (WaitForSingleObject(process.value,5000) == WAIT_TIMEOUT) TerminateJobObject(job.value,ERROR_CANCELLED);
            CloseHandle(process.value); process.value = nullptr;
        }
        if (process.value || session == MAXDWORD || session == 0 || GetTickCount64() < nextAttempt) continue;
        nextAttempt = GetTickCount64() + 30000;
        Handle token;
        if (!WTSQueryUserToken(session,&token.value)) continue; // No signed-in console user yet.
        TOKEN_STATISTICS statistics{}; DWORD bytes{};
        if (!GetTokenInformation(token.value,TokenStatistics,&statistics,sizeof(statistics),&bytes)) { logError(L"Query session token"); continue; }
        const auto logon = (static_cast<unsigned long long>(static_cast<DWORD>(statistics.AuthenticationId.HighPart)) << 32) | statistics.AuthenticationId.LowPart;
        if (launchedLogons.count(logon)) continue; // Respect Exit/declined notice until next logon or service restart.
        void* environment{};
        if (!CreateEnvironmentBlock(&environment,token.value,FALSE)) { logError(L"Create user environment"); continue; }
        ResetEvent(agentStop.value);
        std::wstring command = L"\"" + application + L"\" --service-agent " + eventName;
        STARTUPINFOW startup{sizeof(startup)}; wchar_t desktop[] = L"winsta0\\default"; startup.lpDesktop = desktop;
        PROCESS_INFORMATION child{};
        BOOL created = CreateProcessAsUserW(token.value,application.c_str(),&command[0],nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,environment,directory.c_str(),&startup,&child);
        DWORD createError = GetLastError(); DestroyEnvironmentBlock(environment); SetLastError(createError);
        if (!created) { logError(L"Start Karizmi Student"); continue; }
        Handle thread(child.hThread); process.value = child.hProcess;
        if (!AssignProcessToJobObject(job.value,process.value) || ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
            logError(L"Attach Karizmi Student to service job"); TerminateProcess(process.value,ERROR_CANCELLED);
            CloseHandle(process.value); process.value = nullptr; continue;
        }
        childSession = session; launchedLogons.insert(logon);
    }
    report(SERVICE_STOP_PENDING);
    SetEvent(agentStop.value);
    if (process.value) WaitForSingleObject(process.value,5000);
    // Closing the job also contains failures/crashes and never kills unrelated applications.
}
void WINAPI serviceMain(DWORD, wchar_t**) {
    Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr)); stopping = stop.value;
    statusHandle = RegisterServiceCtrlHandlerExW(ServiceName,control,nullptr);
    if (!statusHandle) return;
    if (!stopping) { report(SERVICE_STOPPED,GetLastError()); return; }
    report(SERVICE_RUNNING);
    try { supervise(); report(SERVICE_STOPPED); }
    catch (...) { logError(L"Karizmi service initialization"); report(SERVICE_STOPPED,ERROR_SERVICE_SPECIFIC_ERROR); }
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,wchar_t*,int) {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(ServiceName),serviceMain},{nullptr,nullptr}};
    return StartServiceCtrlDispatcherW(table) ? 0 : static_cast<int>(GetLastError());
}
