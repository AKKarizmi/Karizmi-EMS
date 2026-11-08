// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/firewall.h"
#include <netfw.h>
#include <sddl.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <cwchar>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace karizmi {
namespace {
struct Failure { HRESULT code; };
void check(HRESULT result) { if (FAILED(result)) throw Failure{result}; }
template<class T> struct Com {
    T* p = nullptr; ~Com() { if (p) p->Release(); }
    T** address() { return &p; } T* operator->() const { return p; }
};
struct Bstr { BSTR p; explicit Bstr(const std::wstring& s) : p(SysAllocStringLen(s.data(), UINT(s.size()))) { if (!p) throw Failure{E_OUTOFMEMORY}; } ~Bstr() { SysFreeString(p); } operator BSTR() const { return p; } };
struct Handle { HANDLE h = nullptr; ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
std::wstring executable(HANDLE process = nullptr) {
    std::wstring path(32768, L'\0'); DWORD size = DWORD(path.size());
    if (process) { if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) throw Failure{HRESULT_FROM_WIN32(GetLastError())}; }
    else { size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size())); if (!size || size >= path.size()) throw Failure{E_FAIL}; }
    path.resize(size); return path;
}
std::wstring wide(const std::string& text) { return {text.begin(), text.end()}; }
std::wstring identity(const std::wstring& path, DWORD session) {
    std::wstring lower = path; CharLowerBuffW(lower.data(), DWORD(lower.size()));
    lower += L"|" + std::to_wstring(session);
    BCRYPT_ALG_HANDLE algorithm = nullptr; check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    std::array<uint8_t, 32> digest{};
    auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(lower.data()), ULONG(lower.size() * sizeof(wchar_t)), digest.data(), ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0); check(status);
    std::wostringstream out; out << L"Karizmi.Exam.";
    for (auto b : digest) out << std::hex << std::setw(2) << std::setfill(L'0') << unsigned(b);
    return out.str();
}
std::wstring subnetScope() {
    std::wstring out; for (const auto& adapter : adapters()) { if (!out.empty()) out += L","; out += wide(adapter.subnet); }
    if (out.empty()) throw Failure{HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE)}; return out;
}
std::wstring localScope() {
    std::wstring out; for (const auto& adapter : adapters()) { if (!out.empty()) out += L","; out += wide(adapter.address); }
    if (out.empty()) throw Failure{HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE)}; return out;
}
struct RuleSpec { std::wstring suffix, addresses, locals, port; long protocol; };
// Firewall may return an exact address with an explicit /32 mask.
std::vector<std::pair<uint32_t, uint32_t>> addressSet(const std::wstring& value) {
    std::vector<std::pair<uint32_t, uint32_t>> result;
    std::wistringstream stream(value); std::wstring entry;
    while (std::getline(stream, entry, L',')) {
        const auto slash = entry.find(L'/');
        const auto host = entry.substr(0, slash);
        IN_ADDR address{}, mask{}; uint32_t bits = 0xffffffff;
        if (InetPtonW(AF_INET, host.c_str(), &address) != 1) throw Failure{E_INVALIDARG};
        if (slash != std::wstring::npos) {
            auto suffix = entry.substr(slash + 1);
            if (suffix.find(L'.') != std::wstring::npos) {
                if (InetPtonW(AF_INET, suffix.c_str(), &mask) != 1) throw Failure{E_INVALIDARG};
                bits = ntohl(mask.S_un.S_addr);
            } else {
                size_t used = 0; auto count = std::stoul(suffix, &used);
                if (used != suffix.size() || count > 32) throw Failure{E_INVALIDARG};
                bits = count ? 0xffffffffu << (32 - count) : 0;
            }
        }
        result.emplace_back(ntohl(address.S_un.S_addr) & bits, bits);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
class Firewall {
public:
    Firewall(Role role, DWORD session) : role_(role), path_(executable()), prefix_(identity(path_, session)) {
        check(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), reinterpret_cast<void**>(policy_.address())));
        check(policy_->get_Rules(rules_.address()));
        auto lockName = L"Local\\" + prefix_;
        guard_.h = CreateMutexW(nullptr, FALSE, lockName.c_str());
        if (!guard_.h || WaitForSingleObject(guard_.h, 0) != WAIT_OBJECT_0) throw Failure{HRESULT_FROM_WIN32(ERROR_BUSY)};
    }
    ~Firewall() { try { remove(); } catch (...) {} if (guard_.h) ReleaseMutex(guard_.h); }
    FirewallResult apply(const FirewallRequest& request) {
        FirewallResult result{};
        if (!validateFirewallRequest(request)) { result.error = ERROR_INVALID_PARAMETER; wcscpy_s(result.message, L"Invalid firewall request."); return result; }
        try {
            if (request.mode == AccessMode::Remove) { remove(); current_.clear(); wcscpy_s(result.message, L"Karizmi access removed. Other firewall rules are unchanged."); return result; }
            std::vector<RuleSpec> desired;
            auto locals = localScope(); auto subnets = subnetScope();
            if (role_ == Role::Teacher) {
                desired.push_back({L"Discovery", subnets, locals, L"*", NET_FW_IP_PROTOCOL_UDP});
            } else {
                if (request.mode == AccessMode::Bootstrap || request.mode == AccessMode::Recovery) {
                    desired.push_back({L"Discovery", subnets, locals, std::to_wstring(request.discovery), NET_FW_IP_PROTOCOL_UDP});
                    desired.push_back({L"Bootstrap", subnets, locals, std::to_wstring(request.bootstrap), NET_FW_IP_PROTOCOL_TCP});
                }
                if (request.mode == AccessMode::Session) desired.push_back({L"Stream", wide(request.peer), locals, std::to_wstring(request.stream), NET_FW_IP_PROTOCOL_TCP});
            }
            const auto previous = current_;
            try {
                for (const auto& spec : desired) put(spec, request);
                for (const auto* suffix : {L"Discovery", L"Bootstrap", L"Stream"}) {
                    if (std::none_of(desired.begin(), desired.end(), [&](const auto& s) { return s.suffix == suffix; })) eraseRule(suffix);
                }
                current_ = desired; currentRequest_ = request;
            } catch (...) {
                auto failure = std::current_exception();
                // Restore the last verified scope, never broaden it on failure.
                try {
                    remove();
                    for (const auto& spec : previous) put(spec, currentRequest_);
                    current_ = previous;
                } catch (...) { try { remove(); } catch (...) {} current_.clear(); }
                std::rethrow_exception(failure);
            }
            std::wstring text = L"Rule configured. Connection has not been tested.";
            long profiles = 0; check(policy_->get_CurrentProfileTypes(&profiles));
            for (auto profile : {NET_FW_PROFILE2_DOMAIN, NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC}) {
                if (!(profiles & profile)) continue;
                VARIANT_BOOL enabled = VARIANT_FALSE, blocked = VARIANT_FALSE;
                check(policy_->get_FirewallEnabled(profile, &enabled)); check(policy_->get_BlockAllInboundTraffic(profile, &blocked));
                if (enabled != VARIANT_TRUE) text += L" Windows Firewall is disabled for an active profile.";
                if (blocked == VARIANT_TRUE) text += L" An active profile blocks all inbound connections.";
            }
            NET_FW_MODIFY_STATE modify{};
            if (SUCCEEDED(policy_->get_LocalPolicyModifyState(&modify)) && modify != NET_FW_MODIFY_STATE_OK) text += L" Managed policy may prevent local rules from taking effect.";
            text += diagnose();
            wcsncpy_s(result.message, text.c_str(), _TRUNCATE);
        } catch (const Failure& e) { result.error = uint32_t(e.code); wcscpy_s(result.message, L"Firewall operation failed. Check administrator permissions, the Windows Firewall service, and managed policy. Partial new access was removed where possible."); }
        catch (...) { result.error = ERROR_GEN_FAILURE; wcscpy_s(result.message, L"Firewall setup failed."); }
        return result;
    }
private:
    void put(const RuleSpec& spec, const FirewallRequest& request) {
        Com<INetFwRule> rule;
        check(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule), reinterpret_cast<void**>(rule.address())));
        const auto name = prefix_ + L"." + spec.suffix;
        check(rule->put_Name(Bstr(name))); check(rule->put_Grouping(Bstr(L"Karizmi Exam temporary access")));
        check(rule->put_Description(Bstr(L"Temporary access owned by the running Karizmi application. Removed on Exit.")));
        check(rule->put_ApplicationName(Bstr(path_))); check(rule->put_Protocol(spec.protocol));
        check(rule->put_LocalPorts(Bstr(spec.port)));
        if (role_ == Role::Teacher) check(rule->put_RemotePorts(Bstr(std::to_wstring(request.discovery))));
        check(rule->put_LocalAddresses(Bstr(spec.locals))); check(rule->put_RemoteAddresses(Bstr(spec.addresses)));
        check(rule->put_Direction(NET_FW_RULE_DIR_IN)); check(rule->put_Action(NET_FW_ACTION_ALLOW));
        check(rule->put_Profiles(NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC));
        check(rule->put_EdgeTraversal(VARIANT_FALSE)); check(rule->put_Enabled(VARIANT_TRUE));
        check(rules_->Add(rule.p));
        Com<INetFwRule> readback; check(rules_->Item(Bstr(name), readback.address()));
        auto stringValue = [&](auto getter) {
            BSTR actual = nullptr; check((readback.p->*getter)(&actual));
            std::wstring value = actual ? actual : L""; SysFreeString(actual); return value;
        };
        VARIANT_BOOL enabled = VARIANT_FALSE; check(readback->get_Enabled(&enabled));
        if (enabled != VARIANT_TRUE) throw Failure{E_FAIL};
        if (_wcsicmp(stringValue(&INetFwRule::get_ApplicationName).c_str(), path_.c_str()) ||
            stringValue(&INetFwRule::get_LocalPorts) != spec.port ||
            addressSet(stringValue(&INetFwRule::get_RemoteAddresses)) != addressSet(spec.addresses) ||
            addressSet(stringValue(&INetFwRule::get_LocalAddresses)) != addressSet(spec.locals) ||
            stringValue(&INetFwRule::get_RemotePorts) != (role_ == Role::Teacher ? std::to_wstring(request.discovery) : L"*")) throw Failure{E_FAIL};
        long protocol = 0, profiles = 0; NET_FW_RULE_DIRECTION direction{}; NET_FW_ACTION action{};
        VARIANT_BOOL edge = VARIANT_TRUE;
        check(readback->get_Protocol(&protocol)); check(readback->get_Profiles(&profiles));
        check(readback->get_Direction(&direction)); check(readback->get_Action(&action)); check(readback->get_EdgeTraversal(&edge));
        if (protocol != spec.protocol || profiles != (NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC) ||
            direction != NET_FW_RULE_DIR_IN || action != NET_FW_ACTION_ALLOW || edge != VARIANT_FALSE) throw Failure{E_FAIL};
    }
    void eraseRule(const std::wstring& suffix) {
        auto hr = rules_->Remove(Bstr(prefix_ + L"." + suffix));
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) check(hr);
    }
    void remove() { for (const auto* suffix : {L"Discovery", L"Bootstrap", L"Stream"}) eraseRule(suffix); }
    std::wstring diagnose() {
        Com<IUnknown> unknown; check(rules_->get__NewEnum(unknown.address())); Com<IEnumVARIANT> iterator;
        check(unknown->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(iterator.address())));
        bool allow = false, block = false;
        VARIANT value; VariantInit(&value); ULONG fetched = 0;
        while (iterator->Next(1, &value, &fetched) == S_OK) {
            Com<INetFwRule> rule;
            if (value.vt == VT_DISPATCH && SUCCEEDED(value.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(rule.address())))) {
                VARIANT_BOOL enabled = VARIANT_FALSE; NET_FW_RULE_DIRECTION direction{}; BSTR program = nullptr, name = nullptr;
                rule->get_Enabled(&enabled); rule->get_Direction(&direction); rule->get_ApplicationName(&program); rule->get_Name(&name);
                if (enabled == VARIANT_TRUE && direction == NET_FW_RULE_DIR_IN && (!program || !*program || _wcsicmp(program, path_.c_str()) == 0) && (!name || std::wstring(name).find(prefix_) != 0)) {
                    NET_FW_ACTION action{}; rule->get_Action(&action); if (action == NET_FW_ACTION_BLOCK) block = true; else allow = true;
                }
                SysFreeString(program); SysFreeString(name);
            }
            VariantClear(&value);
        }
        std::wstring text;
        if (allow) text += L" Other inbound allow rules may permit broader access; review Windows Firewall advanced settings.";
        if (block) text += L" Existing inbound block rules may conflict; this setup does not override them.";
        return text;
    }
    Role role_; std::wstring path_, prefix_; Com<INetFwPolicy2> policy_; Com<INetFwRules> rules_;
    Handle guard_; std::vector<RuleSpec> current_; FirewallRequest currentRequest_{};
};
bool transfer(HANDLE pipe, void* data, DWORD length, bool write, HANDLE parent = nullptr) {
    Handle event; event.h = CreateEventW(nullptr, TRUE, FALSE, nullptr); OVERLAPPED operation{}; operation.hEvent = event.h;
    DWORD count = 0; BOOL ok = write ? WriteFile(pipe, data, length, &count, &operation) : ReadFile(pipe, data, length, &count, &operation);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        HANDLE waits[]{event.h, parent}; DWORD result = WaitForMultipleObjects(parent ? 2 : 1, waits, FALSE, write ? 5000 : (parent ? INFINITE : 10000));
        if (result != WAIT_OBJECT_0) { CancelIoEx(pipe, &operation); GetOverlappedResult(pipe, &operation, &count, TRUE); return false; }
        ok = GetOverlappedResult(pipe, &operation, &count, FALSE);
    }
    return ok && count == length;
}
}
bool validateFirewallRequest(const FirewallRequest& r) {
    if (r.version != 1 || uint32_t(r.mode) > uint32_t(AccessMode::Recovery)) return false;
    if (r.discovery < 1024 || r.discovery > 65535 || r.bootstrap < 1024 || r.bootstrap > 65535 || r.stream < 1024 || r.stream > 65535 || r.bootstrap == r.stream) return false;
    if (!memchr(r.peer, 0, sizeof(r.peer))) return false;
    return r.mode != AccessMode::Session || parseAddress(r.peer).has_value();
}
std::optional<int> firewallHelperMain(Role role) {
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    if (argc < 2 || wcscmp(argv[1], L"--karizmi-firewall-helper")) { LocalFree(argv); return {}; }
    std::vector<std::wstring> args; for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]); LocalFree(argv);
    if (args.size() != 4 || args[3].size() != 32 || args[3].find_first_not_of(L"0123456789abcdef") != std::wstring::npos) return ERROR_INVALID_PARAMETER;
    wchar_t* end = nullptr; unsigned long parentId = wcstoul(args[2].c_str(), &end, 10);
    if (!parentId || !end || *end) return ERROR_INVALID_PARAMETER;
    Handle parent; parent.h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentId);
    if (!parent.h) return ERROR_ACCESS_DENIED;
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); if (FAILED(com)) return int(com);
    int result = 0;
    try {
        DWORD ownSession = 0, parentSession = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) || !ProcessIdToSessionId(parentId, &parentSession) || ownSession != parentSession || _wcsicmp(executable().c_str(), executable(parent.h).c_str())) throw Failure{E_ACCESSDENIED};
        auto pipeName = L"\\\\.\\pipe\\Karizmi." + args[3];
        Handle pipe; pipe.h = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe.h == INVALID_HANDLE_VALUE) throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        ULONG server = 0; if (!GetNamedPipeServerProcessId(pipe.h, &server) || server != parentId) throw Failure{E_ACCESSDENIED};
        Network network; Firewall firewall(role, ownSession);
        while (WaitForSingleObject(parent.h, 0) == WAIT_TIMEOUT) {
            FirewallRequest request{}; if (!transfer(pipe.h, &request, sizeof(request), false, parent.h)) break;
            auto response = firewall.apply(request); if (!transfer(pipe.h, &response, sizeof(response), true, parent.h)) break;
        }
    } catch (const Failure& e) { result = int(e.code); } catch (...) { result = ERROR_GEN_FAILURE; }
    CoUninitialize(); return result;
}
void FirewallBroker::start(HWND owner) {
    std::array<uint8_t, 16> random; randomBytes(random); std::wostringstream token;
    for (auto b : random) token << std::hex << std::setw(2) << std::setfill(L'0') << unsigned(b);
    auto name = L"\\\\.\\pipe\\Karizmi." + token.str();
    Handle processToken; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken.h)) throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    DWORD needed = 0; GetTokenInformation(processToken.h, TokenUser, nullptr, 0, &needed); std::vector<uint8_t> bytes(needed);
    if (!GetTokenInformation(processToken.h, TokenUser, bytes.data(), needed, &needed)) throw Failure{E_ACCESSDENIED};
    LPWSTR sid = nullptr; if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) throw Failure{E_ACCESSDENIED};
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + std::wstring(sid) + L")"; LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) throw Failure{E_ACCESSDENIED};
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    pipe_ = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 8192, 8192, 0, &attributes);
    LocalFree(descriptor); if (pipe_ == INVALID_HANDLE_VALUE) throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    Handle event; event.h = CreateEventW(nullptr, TRUE, FALSE, nullptr); OVERLAPPED connection{}; connection.hEvent = event.h;
    ConnectNamedPipe(pipe_, &connection);
    auto args = L"--karizmi-firewall-helper " + std::to_wstring(GetCurrentProcessId()) + L" " + token.str(); auto path = executable();
    SHELLEXECUTEINFOW info{sizeof(info)}; info.hwnd = owner; info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas"; info.lpFile = path.c_str(); info.lpParameters = args.c_str(); info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) { auto error = GetLastError(); CancelIoEx(pipe_, &connection); DWORD unused; GetOverlappedResult(pipe_, &connection, &unused, TRUE); CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; throw Failure{HRESULT_FROM_WIN32(error)}; }
    process_ = info.hProcess; HANDLE waits[]{event.h, process_}; auto waited = WaitForMultipleObjects(2, waits, FALSE, 30000);
    DWORD unused = 0;
    if (waited != WAIT_OBJECT_0) { CancelIoEx(pipe_, &connection); GetOverlappedResult(pipe_, &connection, &unused, TRUE); CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; throw Failure{E_FAIL}; }
    if (!GetOverlappedResult(pipe_, &connection, &unused, FALSE)) throw Failure{E_FAIL};
    ULONG client = 0; if (!GetNamedPipeClientProcessId(pipe_, &client) || client != GetProcessId(process_)) throw Failure{E_ACCESSDENIED};
}
FirewallResult FirewallBroker::configure(HWND owner, const FirewallRequest& request) {
    std::lock_guard lock(mutex_); FirewallResult result{};
    if (!validateFirewallRequest(request)) { result.error = ERROR_INVALID_PARAMETER; wcscpy_s(result.message, L"Invalid firewall request."); return result; }
    try {
        if (pipe_ == INVALID_HANDLE_VALUE) start(owner);
        auto copy = request;
        if (!transfer(pipe_, &copy, sizeof(copy), true) || !transfer(pipe_, &result, sizeof(result), false)) throw Failure{HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE)};
    } catch (const Failure& e) {
        result.error = uint32_t(e.code);
        if (e.code == HRESULT_FROM_WIN32(ERROR_CANCELLED)) wcscpy_s(result.message, L"Administrator approval was cancelled. Access was not configured.");
        else wcscpy_s(result.message, L"Firewall helper unavailable. Check administrator permissions and Windows Firewall policy/service.");
        if (pipe_ != INVALID_HANDLE_VALUE) { CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; }
        if (process_) { CloseHandle(process_); process_ = nullptr; }
    }
    return result;
}
FirewallBroker::~FirewallBroker() {
    if (pipe_ != INVALID_HANDLE_VALUE) { CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; }
    if (process_) CloseHandle(process_);
}
}
