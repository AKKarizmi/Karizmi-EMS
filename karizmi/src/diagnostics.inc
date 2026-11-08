// Termination stack addresses only. Test dumps are explicitly enabled by env var.
void writeTerminationDiagnostics() {
        wchar_t folder[MAX_PATH]{};
        if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,folder))) {
            std::wstring directory=std::wstring(folder)+L"\\Karizmi"; CreateDirectoryW(directory.c_str(),nullptr);
            directory+=L"\\Diagnostics"; CreateDirectoryW(directory.c_str(),nullptr);
            auto path=directory+L"\\terminate-"+std::to_wstring(GetCurrentProcessId())+L".txt";
            HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE) {
                auto write=[&](const std::string& text) { DWORD bytes; WriteFile(file,text.data(),DWORD(text.size()),&bytes,nullptr); };
                write("Karizmi terminate stack; no credentials logged\r\n");
                try { if(auto exception=std::current_exception()) std::rethrow_exception(exception); }
                catch(const std::system_error& error) { write("system_error="+std::to_string(error.code().value())+"\r\n"); }
                catch(...) { write("active exception\r\n"); }
                void* frames[64]{}; auto count=CaptureStackBackTrace(0,64,frames,nullptr);
                for(USHORT index=0;index<count;++index) { std::ostringstream line; line<<frames[index]<<"\r\n"; write(line.str()); }
                CloseHandle(file);
            }
            wchar_t enabled[2]{};
            if(GetEnvironmentVariableW(L"KARIZMI_CAPTURE_DUMP",enabled,2)==1 && enabled[0]==L'1') {
                auto dump=directory+L"\\terminate-"+std::to_wstring(GetCurrentProcessId())+L".dmp";
                HANDLE output=CreateFileW(dump.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(output!=INVALID_HANDLE_VALUE) { MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),output,MiniDumpNormal,nullptr,nullptr,nullptr); CloseHandle(output); }
            }
        }
}
void installTerminationDiagnostics() {
    std::signal(SIGABRT, [](int) { writeTerminationDiagnostics(); std::signal(SIGABRT,SIG_DFL); });
    std::set_terminate([] { writeTerminationDiagnostics(); std::abort(); });
}
