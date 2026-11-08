// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/approvals.h"
#include <sddl.h>
#include <wincrypt.h>
#include <algorithm>
namespace karizmi {
namespace {
std::wstring name(const InstanceId& id) {
    std::wstring result; const wchar_t digits[]=L"0123456789abcdef";
    for(auto byte:id) { result+=digits[byte>>4]; result+=digits[byte&15]; } return result;
}
template<size_t N> void readArray(Reader& reader,std::array<uint8_t,N>& result) { auto bytes=reader.bytes(N); std::copy(bytes.begin(),bytes.end(),result.begin()); erase(bytes); }
}
ApprovalStore::ApprovalStore(Role role,std::wstring testNamespace):role_(role) {
    if(!testNamespace.empty() && (testNamespace.size()>64 || testNamespace.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")!=std::wstring::npos)) return;
    HANDLE token{}; TOKEN_STATISTICS statistics{}; DWORD bytes{};
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return;
    bool valid=GetTokenInformation(token,TokenStatistics,&statistics,sizeof(statistics),&bytes)!=FALSE;
    CloseHandle(token); if(!valid)return;
    static_assert(sizeof(statistics.AuthenticationId)==8); memcpy(logon_.data(),&statistics.AuthenticationId,8);
    std::wstring path=L"Software\\Karizmi\\SessionApprovals-"+std::to_wstring(statistics.AuthenticationId.HighPart)+L"-"+std::to_wstring(statistics.AuthenticationId.LowPart)+L"\\"+(role==Role::Teacher?L"Teacher":L"Student");
    if(!testNamespace.empty()) path+=L"-Test-"+testNamespace;
    DWORD sidBytes=0;
    // Current-user ownership plus explicit SYSTEM/admin rights. No other users.
    HANDLE current{}; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&current))return;
    GetTokenInformation(current,TokenUser,nullptr,0,&sidBytes); std::vector<uint8_t> user(sidBytes);
    valid=GetTokenInformation(current,TokenUser,user.data(),sidBytes,&sidBytes)!=FALSE; CloseHandle(current); if(!valid)return;
    wchar_t* sid{}; if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid))return;
    std::wstring sddl=L"D:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KA;;;"+std::wstring(sid)+L")"; LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor{}; if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,REG_OPTION_VOLATILE,KEY_READ|KEY_WRITE,&attributes,&key_,nullptr);
    LocalFree(descriptor);
}
ApprovalStore::~ApprovalStore(){if(key_)RegCloseKey(key_);erase(logon_);}
std::vector<Approval> ApprovalStore::load() const {
    std::vector<Approval> result; if(!key_)return result;
    for(DWORD index=0;index<10;++index) {
        wchar_t valueName[40]{}; DWORD length=40,type=0,size=4096; std::vector<uint8_t> encrypted(size);
        auto error=RegEnumValueW(key_,index,valueName,&length,nullptr,&type,encrypted.data(),&size);
        if(error==ERROR_NO_MORE_ITEMS)break;
        if(error!=ERROR_SUCCESS || type!=REG_BINARY || size>4096)continue;
        DATA_BLOB source{size,encrypted.data()},plain{};
        if(!CryptUnprotectData(&source,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain))continue;
        try {
            Reader reader({plain.pbData,plain.cbData});
            if(reader.u32()!=1 || reader.u16()!=ProtocolVersion || reader.u16()!=uint16_t(role_))throw 0;
            if(!equalSecret(reader.bytes(8),logon_))throw 0;
            Approval entry;entry.role=role_;readArray(reader,entry.local);readArray(reader,entry.peer);
            entry.label=reader.text();entry.address=reader.text(15);entry.bootstrap=reader.u16();entry.stream=reader.u16();
            readArray(reader,entry.current);readArray(reader,entry.pending);readArray(reader,entry.previous);
            auto pending=reader.u16(),input=reader.u16();entry.previousDeadline=reader.u64();reader.end();
            if(pending>1 || input>1 || !validDeviceLabel(entry.label) || !parseAddress(entry.address) || !entry.bootstrap || !entry.stream || name(entry.peer)!=valueName)throw 0;
            entry.hasPending=pending!=0;entry.inputAvailable=input!=0;result.push_back(entry);
        } catch(...) { /* Corrupt/foreign records never authorize a connection. */ }
        SecureZeroMemory(plain.pbData,plain.cbData);LocalFree(plain.pbData);
    }
    return result;
}
bool ApprovalStore::save(const Approval& entry) {
    if(!key_ || entry.role!=role_ || !validDeviceLabel(entry.label) || !parseAddress(entry.address))return false;
    DWORD count{};if(RegQueryInfoKeyW(key_,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS)return false;
    auto valueName=name(entry.peer);DWORD existing{};
    if(count>=(role_==Role::Teacher?10u:1u) && RegQueryValueExW(key_,valueName.c_str(),nullptr,nullptr,nullptr,&existing)!=ERROR_SUCCESS)return false;
    Writer writer;writer.u32(1);writer.u16(ProtocolVersion);writer.u16(uint16_t(role_));writer.bytes(logon_);
    writer.bytes(entry.local);writer.bytes(entry.peer);writer.text(entry.label);writer.text(entry.address,15);writer.u16(entry.bootstrap);writer.u16(entry.stream);
    writer.bytes(entry.current);writer.bytes(entry.pending);writer.bytes(entry.previous);writer.u16(entry.hasPending);writer.u16(entry.inputAvailable);writer.u64(entry.previousDeadline);
    DATA_BLOB source{DWORD(writer.data.size()),writer.data.data()},encrypted{};
    bool success=CryptProtectData(&source,L"Karizmi current sign-in approval",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted)!=FALSE;
    erase(writer.data);
    if(success) { success=RegSetValueExW(key_,valueName.c_str(),0,REG_BINARY,encrypted.pbData,encrypted.cbData)==ERROR_SUCCESS;LocalFree(encrypted.pbData); }
    return success;
}
void ApprovalStore::remove(const InstanceId& peer){if(key_)RegDeleteValueW(key_,name(peer).c_str());}
void ApprovalStore::clear(){if(key_)RegDeleteTreeW(key_,nullptr);}
}
