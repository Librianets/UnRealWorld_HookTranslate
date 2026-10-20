#include "Shared.h"
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <cstdarg>
#include <algorithm>

static std::vector<wchar_t> GameEnvironment() {
    // A debugger launch otherwise leaves the expensive Windows debug heap
    // enabled even after detach. Keep normal heap behavior, child only.
    std::vector<std::wstring> entries;
    wchar_t* block=GetEnvironmentStringsW();
    if(block) {
        for(const wchar_t* p=block;*p;p+=wcslen(p)+1)
            if(_wcsnicmp(p,L"_NO_DEBUG_HEAP=",15)!=0)entries.emplace_back(p);
        FreeEnvironmentStringsW(block);
    }
    entries.emplace_back(L"_NO_DEBUG_HEAP=1");
    std::sort(entries.begin(),entries.end(),[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> result;
    for(const auto& entry:entries){result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}
    result.push_back(0);return result;
}

// Wide CRT printf uses the current C locale, even with a UTF-8 console code
// page. Write Unicode directly to a console, and UTF-8 to redirected output.
static std::wstring g_outputText;
static void Print(const wchar_t* format,...) {
    DWORD error=GetLastError();va_list args;va_start(args,format);
    int count=_vscwprintf(format,args);va_end(args);
    if(count>0) {
        std::vector<wchar_t> buffer((size_t)count+1);
        va_start(args,format);vswprintf_s(buffer.data(),buffer.size(),format,args);va_end(args);
        g_outputText.append(buffer.data(),count);
        if(g_outputText.size()>16000)g_outputText.erase(0,g_outputText.size()-16000);
        HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);DWORD mode=0,done=0;
        if(GetConsoleMode(output,&mode))WriteConsoleW(output,buffer.data(),(DWORD)count,&done,nullptr);
        else {
            int bytes=WideCharToMultiByte(CP_UTF8,0,buffer.data(),count,nullptr,0,nullptr,nullptr);
            std::vector<char> encoded((size_t)bytes);
            if(bytes>0){WideCharToMultiByte(CP_UTF8,0,buffer.data(),count,encoded.data(),bytes,nullptr,nullptr);WriteFile(output,encoded.data(),(DWORD)bytes,&done,nullptr);}
        }
    }
    SetLastError(error);
}

static const char* kExpected = "e0b15bbbe16ffcd95d4ab61f08fe95dea4b95ebe1b3de428dae1d9477a9710d9";
static std::wstring LastMessage(DWORD code=GetLastError()) {
    wchar_t* text=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,(wchar_t*)&text,0,nullptr);
    std::wstring result=L"Win32 "+std::to_wstring(code)+L": "+(text?text:L"Unknown error");if(text)LocalFree(text);
    while(!result.empty()&&(result.back()==L'\r'||result.back()==L'\n'))result.pop_back();return result;
}
static bool IndependentHandle(HANDLE& owned,HANDLE debuggerOwned) {
    if(owned!=debuggerOwned)return true;
    HANDLE copy=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),owned,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS))return false;
    // Windows owns the event handle. Do not close it, even if CreateProcess
    // returned the same numeric handle. The duplicate belongs to this launcher.
    owned=copy;return true;
}
static bool RemoteWrite(HANDLE process,void* address,const void* data,SIZE_T size) {
    SIZE_T done=0;return WriteProcessMemory(process,address,data,size,&done)&&done==size;
}
static bool CodeByte(HANDLE process,DWORD address,BYTE value) {
    DWORD old=0,ignored=0;if(!VirtualProtectEx(process,(void*)address,1,PAGE_EXECUTE_READWRITE,&old))return false;
    bool ok=RemoteWrite(process,(void*)address,&value,1);VirtualProtectEx(process,(void*)address,1,old,&ignored);FlushInstructionCache(process,(void*)address,1);return ok;
}
// Stop at the EXE entry, after the Windows loader has released its lock.
// This avoids installing DLL hooks under the loader lock or guessing that
// system DLL addresses in this process equal those in the child process.
static bool StopAtEntry(PROCESS_INFORMATION& pi,DWORD entryRva,DWORD* stoppedAddress=nullptr) {
    DWORD entry=0;BYTE saved=0;bool armed=false;ULONGLONG deadline=GetTickCount64()+30000;
    while(GetTickCount64()<deadline) {
        DEBUG_EVENT event={};if(!WaitForDebugEvent(&event,1000)){
            if(GetLastError()!=ERROR_SEM_TIMEOUT){Print(L"[BOOT] WaitForDebugEvent: %s\n",LastMessage().c_str());return false;}
            continue;
        }
        DWORD disposition=DBG_CONTINUE;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
            bool handles=IndependentHandle(pi.hProcess,event.u.CreateProcessInfo.hProcess)&&IndependentHandle(pi.hThread,event.u.CreateProcessInfo.hThread);
            entry=(DWORD)event.u.CreateProcessInfo.lpBaseOfImage+entryRva;SIZE_T n=0;
            bool ok=handles&&ReadProcessMemory(pi.hProcess,(void*)entry,&saved,1,&n)&&n==1&&CodeByte(pi.hProcess,entry,0xCC);
            if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
            // Process/thread handles from debug events are tracked and closed
            // by Windows on exit/detach. Closing them here leaves stale entries
            // whose handle numbers can be reused by unrelated handles.
            if(!ok){Print(L"[BOOT] Entry breakpoint setup: %s\n",LastMessage().c_str());return false;}armed=true;
            Print(L"[BOOT] Entry breakpoint: 0x%08lX\n",entry);fflush(stdout);
        } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
            if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
        } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
            auto& exception=event.u.Exception.ExceptionRecord;
            if(exception.ExceptionCode==EXCEPTION_BREAKPOINT&&armed&&(DWORD)exception.ExceptionAddress==entry&&event.dwThreadId==pi.dwThreadId) {
                if(!CodeByte(pi.hProcess,entry,saved)){Print(L"[BOOT] Restore entry: %s\n",LastMessage().c_str());return false;}
                CONTEXT context={};context.ContextFlags=CONTEXT_CONTROL;
                if(!GetThreadContext(pi.hThread,&context)){Print(L"[BOOT] Get context: %s\n",LastMessage().c_str());return false;}context.Eip=entry;
                if(!SetThreadContext(pi.hThread,&context)||SuspendThread(pi.hThread)==(DWORD)-1){Print(L"[BOOT] Suspend at entry: %s\n",LastMessage().c_str());return false;}
                if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE)){Print(L"[BOOT] Continue at entry: %s\n",LastMessage().c_str());return false;}
                bool detached=DebugActiveProcessStop(pi.dwProcessId)!=FALSE;
                if(!detached)Print(L"[BOOT] Detach at entry: %s\n",LastMessage().c_str());
                else {if(stoppedAddress)*stoppedAddress=entry;Print(L"[BOOT] Detached; primary thread held at entry.\n");}
                return detached;
            }
            if(exception.ExceptionCode!=EXCEPTION_BREAKPOINT){Print(L"[BOOT] Exception 0x%08lX at 0x%08lX, first chance %lu\n",exception.ExceptionCode,(DWORD)exception.ExceptionAddress,event.u.Exception.dwFirstChance);fflush(stdout);disposition=DBG_EXCEPTION_NOT_HANDLED;}
        } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) {
            Print(L"[BOOT] Process exited before entry: 0x%08lX\n",event.u.ExitProcess.dwExitCode);fflush(stdout);
            ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE);return false;
        }
        if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,disposition))return false;
    }
    Print(L"[BOOT] Timeout waiting for EXE entry.\n");fflush(stdout);return false;
}
static DWORD RemoteModule(DWORD pid,const std::wstring& name) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W entry={};entry.dwSize=sizeof(entry);DWORD base=0;
    if(Module32FirstW(snapshot,&entry))do {if(_wcsicmp(entry.szModule,name.c_str())==0){base=(DWORD)entry.modBaseAddr;break;}}while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);return base;
}
static void* RemoteLoadLibrary(DWORD pid) {
    FARPROC local=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HMODULE owner=nullptr;
    if(!local||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)local,&owner))return nullptr;
    std::wstring path=ModulePath(owner);size_t slash=path.find_last_of(L"\\/");
    DWORD remote=RemoteModule(pid,path.substr(slash==std::wstring::npos?0:slash+1));
    return remote?(void*)(remote+(DWORD)local-(DWORD)owner):nullptr;
}
static bool RemoteCall(HANDLE process,void* procedure,const void* input,SIZE_T size,DWORD& result) {
    void* remote=VirtualAllocEx(process,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!remote)return false;
    if(!RemoteWrite(process,remote,input,size)){VirtualFreeEx(process,remote,0,MEM_RELEASE);return false;}
    HANDLE thread=CreateRemoteThread(process,nullptr,0,(LPTHREAD_START_ROUTINE)procedure,remote,0,nullptr);
    bool ok=thread&&WaitForSingleObject(thread,30000)==WAIT_OBJECT_0&&GetExitCodeThread(thread,&result);
    if(thread)CloseHandle(thread);
    // On timeout the caller terminates this newly created child. Do not free
    // an argument block that the still-running remote thread may be reading.
    if(ok)VirtualFreeEx(process,remote,0,MEM_RELEASE);return ok;
}
static BOOL WINAPI ConsoleControl(DWORD type){return type==CTRL_C_EVENT||type==CTRL_BREAK_EVENT;}
static DWORD g_outputCodePage=1251;
static int Launch() {
    std::wstring root=Directory(ModulePath()),ini=root+L"\\URWTrace.ini";wchar_t setting[32768];
    GetPrivateProfileStringW(L"Hook",L"GameExe",L"game\\urw.exe",setting,32768,ini.c_str());
    std::wstring game=setting;if(game.find(L':')==std::wstring::npos&&game.rfind(L"\\\\",0)!=0)game=root+L"\\"+game;
    std::wstring dll=root+L"\\URWTraceHook.dll";
    Print(L"URW translation hook 0.20\nGame: %s\nChecking clean 3.89 SHA256...\n",game.c_str());
    if(Sha256File(game)!=kExpected){Print(L"ERROR: expected the clean English urw.exe 3.89. No game started.\n");return 2;}
    std::ifstream stream(game,std::ios::binary);std::vector<char> image((std::istreambuf_iterator<char>(stream)),{});
    if(image.size()<4096)return 2;
    auto* dos=(IMAGE_DOS_HEADER*)image.data();auto* nt=(IMAGE_NT_HEADERS32*)(image.data()+dos->e_lfanew);DWORD entryRva=nt->OptionalHeader.AddressOfEntryPoint;
    HMODULE local=LoadLibraryExW(dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!local){Print(L"ERROR: cannot inspect URWTraceHook.dll: %s\n",LastMessage().c_str());return 3;}
    FARPROC init=GetProcAddress(local,"InitializeTrace");DWORD initOffset=init?(DWORD)init-(DWORD)local:0;FreeLibrary(local);
    if(!initOffset){Print(L"ERROR: missing InitializeTrace export.\n");return 3;}
    STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION pi={};std::wstring command=L"\""+game+L"\"";
    auto environment=GameEnvironment();
    if(!CreateProcessW(game.c_str(),&command[0],nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_UNICODE_ENVIRONMENT,environment.data(),Directory(game).c_str(),&startup,&pi)){
        Print(L"ERROR: cannot start game: %s\n",LastMessage().c_str());return 4;
    }
    bool ready=false;
    do {
        if(!StopAtEntry(pi,entryRva)){Print(L"ERROR: failed to stop at game entry.\n");break;}
        void* loader=RemoteLoadLibrary(pi.dwProcessId);DWORD module=0;
        if(!loader||!RemoteCall(pi.hProcess,loader,dll.c_str(),(dll.size()+1)*sizeof(wchar_t),module)||!module){Print(L"ERROR: cannot load diagnostic DLL: %s\n",LastMessage().c_str());break;}
        InitConfig config={sizeof(InitConfig),GetCurrentProcessId(),g_outputCodePage};DWORD status=0;
        if(!RemoteCall(pi.hProcess,(void*)(module+initOffset),&config,sizeof(config),status)||status!=1){Print(L"ERROR: hook initialization failed. Check logs if created.\n");break;}
        if(ResumeThread(pi.hThread)==(DWORD)-1){Print(L"ERROR: cannot resume game.\n");break;}
        ready=true;
    }while(false);
    if(!ready){TerminateProcess(pi.hProcess,0xE0770002);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 5;}
    CloseHandle(pi.hThread);
    Print(L"Game PID: %lu. Capture active. Close the game normally when finished.\n",pi.dwProcessId);fflush(stdout);
    WaitForSingleObject(pi.hProcess,INFINITE);DWORD exitCode=0;GetExitCodeProcess(pi.hProcess,&exitCode);CloseHandle(pi.hProcess);
    Print(L"Game exited: 0x%08lX. Logs are in the logs folder.\n",exitCode);return exitCode==0?0:6;
}
// A bounded regression test for debugger detach, without executing game code
// past its entry or loading the diagnostic DLL. Every child is reaped here.
static int BootstrapSelfTest() {
    const unsigned attempts=24;unsigned passed=0;
    std::wstring root=Directory(ModulePath()),game=root+L"\\game\\urw.exe";
    if(Sha256File(game)!=kExpected){Print(L"ERROR: bootstrap test requires clean 3.89.\n");return 2;}
    std::ifstream stream(game,std::ios::binary);std::vector<char> image((std::istreambuf_iterator<char>(stream)),{});
    auto* dos=(IMAGE_DOS_HEADER*)image.data();auto* nt=(IMAGE_NT_HEADERS32*)(image.data()+dos->e_lfanew);
    for(unsigned i=0;i<attempts;++i) {
        // Vary surrounding handle allocations to exercise handle-number reuse.
        std::vector<HANDLE> pressure;
        for(unsigned n=0;n<(i%8)*7;++n)pressure.push_back(CreateEventW(nullptr,FALSE,FALSE,nullptr));
        PROCESS_INFORMATION pi={};STARTUPINFOW si={};si.cb=sizeof(si);
        std::wstring command=L"\""+game+L"\"";DWORD entry=0;
        auto environment=GameEnvironment();
        bool ok=CreateProcessW(game.c_str(),&command[0],nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_UNICODE_ENVIRONMENT,environment.data(),Directory(game).c_str(),&si,&pi)!=FALSE;
        if(ok) {
            ok=StopAtEntry(pi,nt->OptionalHeader.AddressOfEntryPoint,&entry);
            BOOL attached=TRUE;CONTEXT context={};context.ContextFlags=CONTEXT_CONTROL;
            ok=ok&&GetProcessId(pi.hProcess)==pi.dwProcessId&&GetThreadId(pi.hThread)==pi.dwThreadId&&
                CheckRemoteDebuggerPresent(pi.hProcess,&attached)&&!attached&&GetThreadContext(pi.hThread,&context)&&context.Eip==entry;
            // Suspend count must be exactly one; leave the game suspended.
            if(ok) {
                DWORD previous=SuspendThread(pi.hThread);
                ok=previous==1;
                if(previous!=(DWORD)-1)ok=(ResumeThread(pi.hThread)==previous+1)&&ok;
            }
            // Includes failure paths: never leave a test game behind.
            bool killed=TerminateProcess(pi.hProcess,0xE0770004)!=FALSE;
            if(killed)ok=(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0)&&ok;
            else ok=false;
            CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        }
        for(HANDLE handle:pressure)if(handle)CloseHandle(handle);
        if(ok)++passed;
        Print(L"%s bootstrap %u/%u: detached, owned handles valid, EIP held, child closed\n",ok?L"PASS":L"FAIL",i+1,attempts);
        if(!ok)break;
    }
    Print(L"UNICODE_CHECK: Русский текст ошибок\n");
    std::ofstream report(root+L"\\BOOTSTRAP_TEST.json",std::ios::binary);
    report<<"{\"version\":\"0.20\",\"launcher_sha256\":\""<<Sha256File(ModulePath())<<"\",\"attempts\":"<<attempts<<",\"passed\":"<<passed<<",\"failed\":"<<(attempts-passed)<<"}\n";
    return passed==attempts?0:1;
}
int wmain(int argc,wchar_t** argv) {
    int result=0;
    if(argc==2&&wcscmp(argv[1],L"--bootstrap-self-test")==0)return BootstrapSelfTest();
    std::wstring ini=Directory(ModulePath())+L"\\URWTrace.ini";
    wchar_t language[16];GetPrivateProfileStringW(L"Hook",L"Language",L"ru",language,16,ini.c_str());
    std::wstring selected=language;selected=selected.substr(0,selected.find_first_of(L";#"));
    size_t first=selected.find_first_not_of(L" \t\r\n"),last=selected.find_last_not_of(L" \t\r\n");
    selected=first==std::wstring::npos?L"":selected.substr(first,last-first+1);wcsncpy_s(language,selected.c_str(),_TRUNCATE);
    if(argc==3&&wcscmp(argv[1],L"--language")==0)wcsncpy_s(language,argv[2],_TRUNCATE);
    else if(argc!=1){MessageBoxW(nullptr,L"Usage: URWTrace.exe [--language ru|fi]",L"URWTrace",MB_OK|MB_ICONERROR);return 2;}
    if(wcscmp(language,L"ru")&&wcscmp(language,L"fi")){MessageBoxW(nullptr,L"Language must be ru or fi in URWTrace.ini.",L"URWTrace",MB_OK|MB_ICONERROR);return 2;}
    g_outputCodePage=wcscmp(language,L"fi")==0?1252:1251;
    bool console=GetPrivateProfileIntW(L"Hook",L"ConsoleMode",0,ini.c_str())!=0;
    if(console){if(!GetConsoleWindow())AllocConsole();SetConsoleOutputCP(CP_UTF8);SetConsoleCtrlHandler(ConsoleControl,TRUE);}
    try{result=Launch();}catch(...){Print(L"ERROR: launcher exception.\n");result=9;}
    // Only failure needs acknowledgement; successful runs close with the game.
    if(result){if(console){Print(L"Press Enter to close.\n");getwchar();}
        else MessageBoxW(nullptr,g_outputText.c_str(),L"UnReal World — перевод: ошибка запуска",MB_OK|MB_ICONERROR);}
    return result;
}
