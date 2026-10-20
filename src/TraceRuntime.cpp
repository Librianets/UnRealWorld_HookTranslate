#include "Shared.h"
#include "TraceTestApi.h"
#include "../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <intrin.h>
#include <iterator>
#include "TranslationCatalog.h"
#include "TranslationScope.h"
#include "OutputSites.h"
#include "GeneratedPresentation.h"
#include "TextureOperands.h"
#include "TextureScope.h"
#include "EncyclopediaFile.h"
#include "MenuSourceSnapshot.h"
#include "NpcSourceIdentity.h"
#include "NfoQueryCase.h"
#include "RecoveredDialogueOutputSites014.h"
#include <new>
#include <cstdarg>

enum Kind : unsigned { Leaf=1, Dialogue=2, Bypass=4, WholeSource=8, SplitSource=16, TextureSource=64 };
struct HookSpec { DWORD rva; const char* name; unsigned kind; BYTE prefix[16], mask[16]; };
struct SiteSpec { DWORD rva; unsigned caller, target; };
#include "GeneratedProfile.h"
static const unsigned kHookCount = sizeof(kProfile)/sizeof(kProfile[0]);
static_assert(kHookCount >= 147, "Unexpected profile");

// Layout is shared by entry/return assembly bridges. A return address is
// replaced only on the current thread's stack. Translation may substitute
// the render text argument; original string storage and other arguments stay intact.
struct SavedRegs {
    DWORD edi, esi, ebp, saved_esp, ebx, edx, ecx, eax;
    DWORD flags, bridge_target, original_return;
};
static_assert(offsetof(SavedRegs, bridge_target)==36, "Bridge layout");
static_assert(offsetof(SavedRegs, original_return)==40, "Bridge layout");
struct Frame {
    unsigned id; DWORD entry_sp, original_return, return_site;
    unsigned caller, caller_origin; bool pending;
    DWORD uid, group, dialogue;
    unsigned direct_draws, direct_dialogues, route;
    char* localized;
    char* nfo_query; // private UI search argument; not translated output
    bool translated_view, view_locked, stream_view, message_ready;
    bool protected_user_note;
    char* view_backup;
    char* source_file;
    char* english_assembly;
    DWORD view_counter, english_token_end;
    urwtranslation::MenuSourceFrame* menu_source;
    char* armour_full_source;
    char* armour_expected_title;
    char* menu_original_title;
    char* menu_selection_expected;
    char* menu_selection_translation;
    std::vector<urwtranslation::NpcSourceIdentity>* npc_sources;
};
struct ThreadState { bool busy; unsigned depth; Frame frames[512]; urwtranslation::MenuSourceCache* menu_sources; };
static DWORD g_tls = TLS_OUT_OF_INDEXES;
static DWORD g_base;
static HMODULE g_dll;
static bool g_testing;
#include "VersionAddresses.h"
static volatile LONG g_state=0, g_uid=0;
static volatile LONG g_entered=0, g_left=0, g_texts=0, g_consumed=0, g_abandoned=0, g_failures=0;
static void* g_trampolines[kHookCount] = {};
static void* g_menuFormatOriginal=nullptr;
static DWORD g_menuDataBase=0,g_menuFormatTarget=0;
static bool g_installed[kHookCount] = {};
static CRITICAL_SECTION g_logLock;
static HANDLE g_log=INVALID_HANDLE_VALUE, g_console=INVALID_HANDLE_VALUE;
static std::wstring g_folder, g_ini, g_translationDirectory;
static std::wstring SelectedTranslationDirectory() {
    if(g_translationDirectory.find(L':')!=std::wstring::npos||g_translationDirectory.rfind(L"\\\\",0)==0)return g_translationDirectory;
    return Directory(ModulePath(g_dll))+L"\\"+g_translationDirectory;
}
static DWORD g_start;
static bool g_loggingEnabled=true;
static unsigned g_outputCodePage=1251;
static unsigned g_consoleMode=1, g_textLimit=4096, g_codepage=1252, g_repeatMs=500;
static unsigned g_segment=0, g_keep=8;
static uint64_t g_logBytes=0, g_logLimit=64ULL*1024*1024, g_seq=0;
static bool g_ioFailed=false;
struct Seen { uint64_t hash; DWORD tick; };
static Seen g_seen[1024] = {};
static urwtranslation::TranslationCatalog g_catalog;
static bool g_translationEnabled=false, g_translationTest=false;
static volatile LONG g_translated=0, g_catalogGeneration=0, g_badCatalogNotice=0;
static std::wstring g_fontPath;
using GameFopen=void* (__cdecl*)(const char*,const char*);
using GameWfopen=void* (__cdecl*)(const wchar_t*,const wchar_t*);
static GameFopen g_originalFopen=nullptr;
static GameWfopen g_originalWfopen=nullptr;
static char g_messageView[1024];
static char g_tokenView[650000]; // Same 5000 * 130 slot capacity as 55F4D0.
static char g_assemblyView[1024]; // Renderer scratch; never expose Russian scratch to game consumers.
static DWORD g_tokenCounter=0;
static CRITICAL_SECTION g_viewLock;
static std::wstring g_nfoPath;
static bool g_nfoReady=false;
static LONG g_nfoGeneration=0;

extern "C" void __cdecl TraceEnter(SavedRegs* r, unsigned id) noexcept;
extern "C" void __cdecl TraceLeave(SavedRegs* r) noexcept;
extern "C" __declspec(naked) void EntryBridge() {
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0FFFFFFF0h
    __asm fxsave [esp]
    __asm fninit
    __asm cld
    __asm push eax
    __asm push ebx
    __asm call TraceEnter
    __asm add esp, 8
    __asm fxrstor [esp]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}
extern "C" __declspec(naked) void ReturnBridge() {
    __asm push 0
    __asm pushfd
    __asm pushad
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0FFFFFFF0h
    __asm fxsave [esp]
    __asm fninit
    __asm cld
    __asm push ebx
    __asm call TraceLeave
    __asm add esp, 4
    __asm fxrstor [esp]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}
#include "GeneratedStubs.h"
// Most sprintf calls pass straight through this integer-only prefilter. Only
// DATA524 record starts reach the C++ read-only source monitor, without adding
// a trace frame or replacing the native continuation/arguments.
extern "C" void __cdecl CaptureMenuFormat(SavedRegs* r) noexcept;
extern "C" __declspec(naked) void MenuFormatBridge(){
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0FFFFFFF0h
    __asm fxsave [esp]
    __asm fninit
    __asm cld
    __asm push ebx
    __asm call CaptureMenuFormat
    __asm add esp, 4
    __asm fxrstor [esp]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}
extern "C" __declspec(naked) void MenuFormatMonitor(){
    __asm pushfd
    __asm push eax
    __asm mov eax, [esp+12]
    __asm sub eax, g_menuDataBase
    __asm cmp eax, 26724 //51*524
    __asm jae unobserved
    __asm pop eax
    __asm popfd
    __asm push 0
    __asm pushfd
    __asm pushad
    __asm jmp MenuFormatBridge
unobserved:
    __asm pop eax
    __asm popfd
    __asm jmp dword ptr [g_menuFormatOriginal]
}


static std::string Hex(DWORD value) { char s[16]; sprintf_s(s,"0x%08lX",value); return s; }
static std::string Utf8(const std::wstring& s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),&out[0],n,nullptr,nullptr); return out;
}
static std::wstring Wide(const std::string& s, unsigned cp=CP_UTF8) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(cp,0,s.data(),(int)s.size(),nullptr,0);
    std::wstring out(n,0); MultiByteToWideChar(cp,0,s.data(),(int)s.size(),&out[0],n); return out;
}
static std::string Quote(const std::string& value) {
    std::string out="\"";
    for(unsigned char c:value) {
        if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}
        else if(c<32){char x[8];sprintf_s(x,"\\u%04x",c);out+=x;}
        else out+=(char)c;
    }
    return out+'"';
}
static const char* Name(unsigned id) { return id<kHookCount?kProfile[id].name:"unknown"; }
static const char* Route(unsigned route) {
    return route==1?"dialogue_message":route==2?"bypass_draw":route==3?"dialogue_split_family":route==4?"dialogue_fragment":route==5?"texture_draw":route==6?"texture_fragment":"scope";
}
static bool WriteAll(HANDLE h,const char* bytes,size_t n) {
    while(n) { DWORD written=0; if(!WriteFile(h,bytes,(DWORD)n,&written,nullptr)||!written)return false;bytes+=written;n-=written; }
    return true;
}
static std::wstring SegmentPath(unsigned index) {
    wchar_t name[48];swprintf_s(name,L"\\trace_%04u.jsonl",index);return g_folder+name;
}
static bool Rotate() {
    if(g_log!=INVALID_HANDLE_VALUE)CloseHandle(g_log);
    ++g_segment;g_logBytes=0;
    g_log=CreateFileW(SegmentPath(g_segment).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(g_segment>g_keep)DeleteFileW(SegmentPath(g_segment-g_keep).c_str());
    return g_log!=INVALID_HANDLE_VALUE;
}
struct LogLock { LogLock(){EnterCriticalSection(&g_logLock);}~LogLock(){LeaveCriticalSection(&g_logLock);} };
static void Emit(const char* event,const std::string& fields,const std::string& human={},bool normalConsole=false,bool dedup=false) {
    if(!g_loggingEnabled&&!g_consoleMode)return;
    LogLock lock;
    DWORD tick=GetTickCount();
    std::string line="{\"seq\":"+std::to_string(++g_seq)+",\"ms\":"+std::to_string(tick-g_start)+",\"process\":"+std::to_string(GetCurrentProcessId())+",\"thread\":"+std::to_string(GetCurrentThreadId())+",\"event\":"+Quote(event)+fields+"}\n";
    if(g_loggingEnabled&&!g_ioFailed) {
        if(g_logBytes+line.size()>g_logLimit&&!Rotate())g_ioFailed=true;
        if(!g_ioFailed&&!WriteAll(g_log,line.data(),line.size()))g_ioFailed=true;
        if(!g_ioFailed)g_logBytes+=line.size();
    }
    if(g_ioFailed){g_consoleMode=2;OutputDebugStringA("rus_URWTrace: log write failed; game continues.\n");}
    bool display=g_consoleMode==2||(g_consoleMode==1&&normalConsole);
    if(display&&dedup&&g_repeatMs) {
        uint64_t hash=1469598103934665603ULL;for(unsigned char c:human){hash^=c;hash*=1099511628211ULL;}
        Seen& seen=g_seen[hash%1024];
        if(seen.hash==hash&&tick-seen.tick<g_repeatMs)display=false;
        else seen={hash,tick};
    }
    if(display&&g_console!=INVALID_HANDLE_VALUE) {
        std::wstring w=Wide(human.empty()?line:human+"\r\n");DWORD written=0;
        if(!WriteConsoleW(g_console,w.data(),(DWORD)w.size(),&written,nullptr)) {
            std::string bytes=human.empty()?line:human+"\r\n";WriteAll(g_console,bytes.data(),bytes.size());
        }
    }
}
static ThreadState* State() {
    auto* s=(ThreadState*)TlsGetValue(g_tls);
    if(!s){s=(ThreadState*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(ThreadState));if(s&&!TlsSetValue(g_tls,s)){HeapFree(GetProcessHeap(),0,s);s=nullptr;}}
    return s;
}
static std::string FrameFields(const Frame& f) {
    const char* origin=f.caller_origin==1?"direct_asm_call":f.caller_origin==2?"tail_transition":f.caller_origin==3?"active_instrumented_scope":"unresolved";
    return ",\"function\":"+Quote(Name(f.id))+",\"scope_id\":"+std::to_string(f.uid)+",\"group_id\":"+std::to_string(f.group)+",\"dialogue_id\":"+std::to_string(f.dialogue)+",\"caller\":"+Quote(Name(f.caller))+",\"caller_origin\":"+Quote(origin)+",\"call_va\":"+(f.caller_origin==1?Quote(Hex(f.return_site-5)):"null")+",\"return_va\":"+Quote(Hex(f.return_site))+",\"route\":"+Quote(Route(f.route));
}
static void Clear(Frame& f,const char* reason) {
    if(!f.pending)return;f.pending=false;InterlockedIncrement(&g_consumed);
    Emit("MARKER_CLEAR",FrameFields(f)+",\"marker_before\":1,\"marker_after\":0,\"reason\":"+Quote(reason),
         std::string("[MARKER] ")+Name(f.id)+"_on=0 ("+reason+")",true,true);
}
static bool SavedReturnPresent(DWORD sp) {
    __try{return *(DWORD*)sp==(DWORD)&ReturnBridge;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool StoreWord(DWORD address,DWORD value);
static bool CopyMemoryChecked(void*,const void*,size_t);
static void ReleaseTranslation(Frame& frame) {
    if(frame.nfo_query){HeapFree(GetProcessHeap(),0,frame.nfo_query);frame.nfo_query=nullptr;}
    delete frame.menu_source;frame.menu_source=nullptr;
    delete frame.npc_sources;frame.npc_sources=nullptr;
    if(frame.menu_original_title){HeapFree(GetProcessHeap(),0,frame.menu_original_title);frame.menu_original_title=nullptr;}
    if(frame.menu_selection_expected){HeapFree(GetProcessHeap(),0,frame.menu_selection_expected);frame.menu_selection_expected=nullptr;}
    if(frame.menu_selection_translation){HeapFree(GetProcessHeap(),0,frame.menu_selection_translation);frame.menu_selection_translation=nullptr;}
    if(frame.armour_full_source){HeapFree(GetProcessHeap(),0,frame.armour_full_source);frame.armour_full_source=nullptr;}
    if(frame.armour_expected_title){HeapFree(GetProcessHeap(),0,frame.armour_expected_title);frame.armour_expected_title=nullptr;}

    if(frame.localized){HeapFree(GetProcessHeap(),0,frame.localized);frame.localized=nullptr;}
    if(frame.source_file){HeapFree(GetProcessHeap(),0,frame.source_file);frame.source_file=nullptr;}
    if(frame.view_locked) {
        if(frame.view_backup) {
            memcpy(frame.stream_view?g_tokenView:g_messageView,frame.view_backup,frame.stream_view?sizeof(g_tokenView):sizeof(g_messageView));
            if(frame.stream_view)memcpy(g_assemblyView,frame.view_backup+sizeof(g_tokenView),sizeof(g_assemblyView));
            HeapFree(GetProcessHeap(),0,frame.view_backup);frame.view_backup=nullptr;
        }
        if(frame.stream_view) {
            g_tokenCounter=frame.view_counter;
            StoreWord(GameAddress(0xA622874),frame.english_token_end);
            // Match the native English postcondition for callers such as 4ABA60.
            if(frame.english_assembly)CopyMemoryChecked((void*)(GameAddress(0x65B8BA8)),frame.english_assembly,strlen(frame.english_assembly)+1);
        }
        frame.view_locked=false;LeaveCriticalSection(&g_viewLock);
    }
    if(frame.english_assembly){HeapFree(GetProcessHeap(),0,frame.english_assembly);frame.english_assembly=nullptr;}
    // Do not restore a popped stack argument here: RET n and ReturnBridge's
    // PUSHAD may already have reused that slot for a saved return register.
}
static void Prune(ThreadState* s,DWORD entrySp,DWORD returnAddress) {
    while(s->depth) {
        Frame& f=s->frames[s->depth-1];
        if((entrySp<f.entry_sp&&SavedReturnPresent(f.entry_sp))||(entrySp==f.entry_sp&&returnAddress==(DWORD)&ReturnBridge))break;
        Clear(f,"stack_unwind");
        Emit("ABANDON",FrameFields(f)+",\"reason\":\"stack_unwind_or_longjmp\"","[UNWIND] "+std::string(Name(f.id)),true);
        ReleaseTranslation(f);
        --s->depth;InterlockedIncrement(&g_abandoned);
    }
}
static unsigned Caller(DWORD returnVa,unsigned target) {
    if(!g_testing&&returnVa>=g_base+5) {
        DWORD rva=returnVa-g_base-5;
        auto* first=kSites;auto* end=kSites+sizeof(kSites)/sizeof(kSites[0]);
        auto* p=std::lower_bound(first,end,rva,[](const SiteSpec& s,DWORD a){return s.rva<a;});
        if(p!=end&&p->rva==rva&&p->target==target)return p->caller;
    }
    return kHookCount;
}
// No C++ objects in SEH helpers: unreadable pointers are diagnostic data,
// not a reason to prevent the game from receiving its original arguments.
static unsigned CopyText(const char* input,char* out,unsigned limit,bool* fault,bool* truncated) {
    unsigned n=0;*fault=false;*truncated=false;
    __try {
        if(!input){*fault=true;return 0;}
        for(;n<limit;++n){char c=input[n];if(!c)break;out[n]=c;}
        *truncated=n==limit;
    } __except(EXCEPTION_EXECUTE_HANDLER){*fault=true;}
    return n;
}
static DWORD ReadWord(DWORD pointer) { __try{return *(DWORD*)pointer;}__except(EXCEPTION_EXECUTE_HANDLER){return 0;} }
static bool CopyMemoryChecked(void* destination,const void* source,size_t bytes) {
    __try{memcpy(destination,source,bytes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool StoreWord(DWORD address,DWORD value) {
    __try{*(DWORD*)address=value;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool RecoveredDialogueSite014(DWORD caller,DWORD call,DWORD target) {
    for(const auto& allowed:kRecoveredDialogueOutputSites014)
        if(allowed.call_va==call&&allowed.caller_va==caller&&allowed.target_va==target)return true;
    return false;
}
bool TestRecoveredOutputSites014() {
    unsigned count=0;
    for(const auto& allowed:kRecoveredDialogueOutputSites014) {
        if(!RecoveredDialogueSite014(allowed.caller_va,allowed.call_va,allowed.target_va)||
            RecoveredDialogueSite014(allowed.caller_va,allowed.call_va+1,allowed.target_va)||
            RecoveredDialogueSite014(allowed.caller_va^1,allowed.call_va,allowed.target_va)||
            RecoveredDialogueSite014(allowed.caller_va,allowed.call_va,allowed.target_va^1))return false;
        ++count;
    }
    return count==91;
}
static bool TranslationSite(const Frame& f) {
    if(f.caller>=kHookCount)return false;
    DWORD caller=kProfile[f.caller].rva+0x400000,target=kProfile[f.id].rva+0x400000;
    if(g_testing&&g_translationTest) {
        if(target==0x50B690||target==0x50B810)for(const auto& allowed:kTextureTranslateSites)if(allowed.caller_va==caller&&allowed.target_va==target)return true;
        if(target==0x530DC0)for(DWORD allowed:kTranslateWholeMessageCallers)if(allowed==caller)return true;
        if(target==0x50B9D0)for(DWORD allowed:kTranslateWholeFieldCallers)if(allowed==caller)return true;
        return false;
    }
    if(f.caller_origin!=1||f.return_site<g_base+5)return false;
    DWORD call=CanonicalVa(f.return_site-5);
    for(const auto& allowed:kTranslateSites)
        if(allowed.call_va==call&&allowed.caller_va==caller&&allowed.target_va==target)return true;
    for(const auto& allowed:kAdditionalOutputSites)
        if(allowed.call_va==call&&allowed.caller_va==caller&&allowed.target_va==target)return true;
    if(RecoveredDialogueSite014(caller,call,target))return true;
    for(const auto& allowed:kTextureTranslateSites)
        if(allowed.call_va==call&&allowed.caller_va==caller&&allowed.target_va==target)return true;
    return false;
}
static DWORD TextSlot(const Frame& f,DWORD sp) {
    DWORD va=kProfile[f.id].rva+0x400000;
    return sp+((va==0x50B690||va==0x50B810)?8:4);
}
static bool UserInputContext(ThreadState* s) {
    for(unsigned i=0;i<s->depth;++i) {
        if(s->frames[i].protected_user_note)return true;
        DWORD va=kProfile[s->frames[i].id].rva+0x400000;
        if(va==0x50CD80||va==0x50C9E0)return true;
    }
    return false;
}
static bool Encoded(const std::string& utf8,std::string& bytes) {
    if(utf8.empty())return false;
    if(g_catalog.GetCodePage()!=g_outputCodePage)return false;
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),(int)utf8.size(),nullptr,0);
    if(count<=0)return false;
    std::wstring wide(count,0);
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),(int)utf8.size(),&wide[0],count))return false;
    BOOL replaced=FALSE;
    count=WideCharToMultiByte(g_outputCodePage,WC_NO_BEST_FIT_CHARS,wide.data(),(int)wide.size(),nullptr,0,nullptr,&replaced);
    if(count<=0||replaced)return false;
    bytes.resize(count);replaced=FALSE;
    return WideCharToMultiByte(g_outputCodePage,WC_NO_BEST_FIT_CHARS,wide.data(),(int)wide.size(),&bytes[0],count,nullptr,&replaced)>0&&!replaced&&bytes.find('\0')==std::string::npos;
}
static void RefreshEncyclopedia();
static std::string SourceFile(ThreadState*);
static bool LookupView(const std::string&,DWORD,DWORD,std::string&,std::string&,const std::vector<urwtranslation::NpcSourceIdentity>* =nullptr);
static void RefreshCatalog(bool force=false) {
    bool ok=g_catalog.Reload(force);
    auto stats=g_catalog.GetStats();
    if(!ok) {
        if(!InterlockedExchange(&g_badCatalogNotice,1))
            Emit("CATALOG_ERROR",",\"error\":"+Quote(g_catalog.LastError()),"[CATALOG] Update rejected; previous catalogue retained: "+g_catalog.LastError(),true);
    } else InterlockedExchange(&g_badCatalogNotice,0);
    if(InterlockedExchange(&g_catalogGeneration,(LONG)stats.generation)!=(LONG)stats.generation) {
        Emit("CATALOG_RELOADED",",\"generation\":"+std::to_string(stats.generation)+",\"exact\":"+std::to_string(stats.enabledExact)+",\"templates\":"+std::to_string(stats.enabledTemplates),
             "[CATALOG] Loaded generation "+std::to_string(stats.generation)+"; compact="+std::to_string(stats.compactUnits)+" legacy_exact="+std::to_string(stats.enabledExact)+" legacy_templates="+std::to_string(stats.enabledTemplates),true);
        if(!g_testing)RefreshEncyclopedia();
    }
}
static bool ApplyTranslation(ThreadState* s,Frame& f,SavedRegs* r,DWORD sp) {
    if(!g_translationEnabled||f.route==4||f.route==6||UserInputContext(s)||!TranslationSite(f))return false;
    // None of the routes below may see already translated dialogue fragments.
    for(unsigned i=0;i+1<s->depth;++i)if(s->frames[i].translated_view||s->frames[i].localized)return false;
    RefreshCatalog();
    if(g_catalog.GetCodePage()!=g_outputCodePage)return false;
    DWORD pointer=kProfile[f.id].kind==Leaf?ReadWord(TextSlot(f,sp)):r->ecx;
    char raw[4097];bool fault=false,truncated=false;
    unsigned size=CopyText((const char*)pointer,raw,4096,&fault,&truncated);
    if(fault||truncated||!size)return false;
    std::string original=Utf8(Wide(std::string(raw,size),g_codepage)),rule,bytes;
    DWORD caller=kProfile[f.caller].rva+0x400000,target=kProfile[f.id].rva+0x400000;
    bool recoveredArmour=false;
    if(target==0x50B9D0&&caller==0x512BF0){
        for(unsigned i=s->depth;i>0;--i){
            const auto& owner=s->frames[i-1];
            if(kProfile[owner.id].rva+0x400000!=0x4F9D50)continue;
            if(owner.armour_full_source&&owner.armour_expected_title&&std::string(raw,size)==owner.armour_expected_title){
                std::string full=Utf8(Wide(owner.armour_full_source,g_codepage));
                if(LookupView(full,0x4F9D50,0x4F9D50,bytes,rule)){
                    bytes=urwtranslation::NativeArmourClip(bytes);recoveredArmour=true;
                    Emit("ARMOUR_SOURCE_SNAPSHOT",FrameFields(f)+",\"virtual_target\":\"0x004F9D50\",\"full_title\":"+Quote(full)+",\"source_buffers_modified\":false","",false,true);
                }
            }
            break;
        }
    }
    if(!recoveredArmour){
        if(target==0x50B810) {
            std::string exact;
            if(!g_catalog.Translate(original,caller,target,exact,rule,true)||exact==original||!Encoded(exact,bytes))return false;
        } else if(!LookupView(original,caller,target,bytes,rule))return false;
    }
    std::string translated=Utf8(Wide(bytes,g_outputCodePage));
    const char* reason=nullptr;
    if(kProfile[f.id].kind==Leaf&&bytes.size()>299)reason="leaf_299_byte_limit";
    else if(target==0x50B690&&caller==0x500E90&&bytes.size()>14&&
       (f.return_site-g_base+0x400000==0x5011B4||f.return_site-g_base+0x400000==0x501269||
        f.return_site-g_base+0x400000==0x501301||f.return_site-g_base+0x400000==0x5013B6||f.return_site-g_base+0x400000==0x501447))reason="movement_caption_14_glyph_cell";
    else if(bytes.size()>4096)reason="dialogue_4096_byte_limit";
    // 530DC0 eventually passes each word/portion to the same 300-byte leaf.
    else if(kProfile[f.id].kind==Dialogue) {
        size_t run=0;
        for(size_t i=0;i<bytes.size();++i) {
            ++run;
            if(strchr(" .,!?",(unsigned char)bytes[i])&&(i+1==bytes.size()||bytes[i+1]!=' '))run=0;
            if(run>290){reason="dialogue_unsplittable_portion";break;}
        }
    }
    if(reason) {
        Emit("TRANSLATION_REJECTED",FrameFields(f)+",\"rule\":"+Quote(rule)+",\"reason\":"+Quote(reason),"[TRANSLATE] Kept English: "+std::string(reason),true,true);return false;
    }
    char* copy=(char*)HeapAlloc(GetProcessHeap(),0,bytes.size()+1);if(!copy)return false;
    memcpy(copy,bytes.data(),bytes.size());copy[bytes.size()]=0;
    if(kProfile[f.id].kind==Leaf) {
        if(!StoreWord(TextSlot(f,sp),(DWORD)copy)){HeapFree(GetProcessHeap(),0,copy);return false;}
        if(target==0x50B690&&(caller==0x510980||(caller==0x500E90&&
           (f.return_site-g_base+0x400000==0x501140||f.return_site-g_base+0x400000==0x501B5D||f.return_site-g_base+0x400000==0x501D48||f.return_site-g_base+0x400000==0x501F1D))))
            r->edx+=(DWORD)(4*((int)size-(int)bytes.size()));
    } else r->ecx=(DWORD)copy;
    f.localized=copy;f.translated_view=true;InterlockedIncrement(&g_translated);
    Emit("TRANSLATE",FrameFields(f)+",\"rule\":"+Quote(rule)+",\"source\":"+Quote(original)+",\"translation\":"+Quote(translated)+",\"encoding\":"+Quote("cp"+std::to_string(g_outputCodePage))+",\"original_pointer\":"+Quote(Hex(pointer)),
         "[TRANSLATED] "+std::string(Name(f.caller))+" "+original+" => "+translated,true,true);
    return true;
}
static void Capture(ThreadState* s,Frame& f,SavedRegs* r,DWORD entrySp) {
    char raw[65536];bool fault,truncated;
    DWORD pointer=kProfile[f.id].kind==Leaf?ReadWord(TextSlot(f,entrySp)):r->ecx;
    unsigned n=CopyText((const char*)pointer,raw,g_textLimit,&fault,&truncated);
    unsigned cp=g_codepage;
    // Only the already translated synopsis is CP1251. Its topic/key stays English.
    if(g_nfoReady&&f.caller<kHookCount&&kProfile[f.caller].rva+0x400000==0x43AF20&&
       f.return_site==GameAddress(0x43B11F))cp=g_outputCodePage;
    if(g_nfoReady&&f.caller<kHookCount&&kProfile[f.caller].rva+0x400000==0x43B1B0&&f.return_site==GameAddress(0x43B3C4))cp=g_outputCodePage;
    for(unsigned i=0;i+1<s->depth;++i)if(s->frames[i].translated_view||(g_nfoReady&&kProfile[s->frames[i].id].rva+0x400000==0x43BCB0)){cp=g_outputCodePage;break;}
    std::string bytes(raw,n),text=Utf8(Wide(bytes,cp));
    std::string fields=FrameFields(f)+",\"source_file\":"+Quote(SourceFile(s))+",\"pointer\":"+Quote(Hex(pointer))+",\"byte_count\":"+std::to_string(n)+",\"text\":"+Quote(text)+",\"read_fault\":"+(fault?"true":"false")+",\"truncated\":"+(truncated?"true":"false")+",\"marker\":1";
    if(kProfile[f.id].kind==Leaf) {
        bool overlay=kProfile[f.id].rva+0x400000==0x50B810;
        bool texture=overlay||kProfile[f.id].rva+0x400000==0x50B690;
        fields+=",\"x\":"+std::to_string((int)(texture?r->edx:r->ecx))+",\"y\":"+std::to_string((int)(texture?ReadWord(entrySp+4):r->edx))+",\"color\":"+Quote(Hex(ReadWord(entrySp+(texture?12:8))))+",\"outline\":"+std::to_string(overlay?0:ReadWord(entrySp+(texture?16:12)));
        if(texture)fields+=",\"renderer\":"+Quote(Hex(r->ecx));
    }
    if(std::any_of(bytes.begin(),bytes.end(),[](unsigned char c){return c>=128;})) {
        std::string hex;for(unsigned char c:bytes){char pair[3];sprintf_s(pair,"%02X",c);hex+=pair;}fields+=",\"raw_hex\":"+Quote(hex);
    }
    std::string human="[TEXT "+std::string(Route(f.route))+"] "+Name(f.id)+" <- "+Name(f.caller)+" group="+std::to_string(f.group)+" "+text;
    // Only the console omits duplicate leaf fragments already visible as an
    // incoming 530DC0 message. The JSONL always records all of them.
    Emit("TEXT",fields,human,f.route!=4,true);InterlockedIncrement(&g_texts);
    for(unsigned i=0;i+1<s->depth;++i) {
        Frame& owner=s->frames[i];unsigned kind=kProfile[owner.id].kind;
        if(kProfile[f.id].kind==Leaf&&(kind&Bypass))Clear(owner,"consumed_by_50B9D0");
        if(kProfile[f.id].kind==Leaf&&(kind&TextureSource))Clear(owner,"consumed_by_texture_draw");
        if(kProfile[f.id].kind==Dialogue&&(kind&(WholeSource|SplitSource)))Clear(owner,"consumed_by_530DC0");
    }
}
// Complete render inputs are translated before any width calculation or splitting.
static std::string CanonicalDisplay(const std::string& text) {
    std::string out;bool pending=false;
    for(size_t i=0;i<text.size();++i) {
        unsigned char c=text[i];
        if(c==' '||c=='\r'||c=='\n'||c=='\t'){pending=!out.empty();continue;}
        if(c=='<'&&!out.empty()&&out.back()!=' ')pending=true;
        if(pending&&!out.empty()&&out.back()!=' ')out+=' ';
        pending=false;out+=(char)c;
        if(c=='>')pending=true;
    }
    return out;
}
static DWORD Preferred(const Frame& f) {return kProfile[f.id].rva+0x400000;}
static bool ParentHasTranslation(ThreadState* s) {
    for(unsigned i=0;i+1<s->depth;++i)if(s->frames[i].translated_view)return true;
    return false;
}
static Frame* Ancestor(ThreadState* s,DWORD va) {
    for(unsigned i=s->depth-1;i>0;--i)if(Preferred(s->frames[i-1])==va)return &s->frames[i-1];
    return nullptr;
}
static std::string SourceFile(ThreadState* s) {
    for(unsigned i=s->depth;i>0;--i)if(s->frames[i-1].source_file)return s->frames[i-1].source_file;
    return "urw.exe";
}
static bool ReadString(DWORD,std::string&,unsigned);
static bool LookupView(const std::string& source,DWORD caller,DWORD target,std::string& bytes,std::string& rule,const std::vector<urwtranslation::NpcSourceIdentity>* identities) {
    // 482450's live addressing forms copy Source (max12 native bytes).
    // Bind its typed name parameter to that actual value, never interpret a
    // random descriptor such as "stranger" as a different player name.
    // Historical messages and preclipped virtual queries get no current state.
    std::vector<urwtranslation::NpcSourceIdentity> liveSources;
    if(target==0x530DC0||target==0x4F3990){
        if(identities)liveSources=*identities;
        std::string name;
        if(ReadString(GameAddress(0xA727C48),name,13)&&!name.empty())
            liveSources.push_back({Utf8(Wide(name,g_codepage)),"player_name"});
        if(!liveSources.empty())identities=&liveSources;
    }
    std::string result;
    auto accept=[&](const std::string& candidate,const std::string& original){
        if(candidate==original)return false;
        if(Encoded(candidate,bytes))return true;
        Emit("TRANSLATION_REJECTED",",\"caller\":"+Quote(Hex(caller))+",\"target\":"+Quote(Hex(target))+",\"source\":"+Quote(source)+",\"rule\":"+Quote(rule)+",\"reason\":\"not_encodable_selected_codepage\"","",false,true);
        return false;
    };
    bool matched=g_catalog.Translate(source,caller,target,result,rule,false,identities);
    auto reason=g_catalog.CompactDiagnostic();
    if(!reason.empty()&&reason!="no_compact_match")
        Emit("GRAMMAR_FALLBACK",",\"caller\":"+Quote(Hex(caller))+",\"target\":"+Quote(Hex(target))+",\"source\":"+Quote(source)+",\"reason\":"+Quote(reason)+",\"legacy_matched\":"+(matched?"true":"false"),"[GRAMMAR] "+reason+" | "+source,false,true);
    if(matched&&accept(result,source))return true;
    // Virtual source identities accept only the original, scoped full source.
    // Never reinterpret a name after whitespace/decoration canonicalization.
    if(target==0x4FCF90||target==0x4F9D50||target==0x4FD710||(caller==0x4FB720&&target==0x50BF70))return false;
    std::string canonical=CanonicalDisplay(source);
    if(canonical!=source&&g_catalog.Translate(canonical,caller,target,result,rule,false,identities)&&accept(result,canonical))return true;
    if(target==0x4F3990&&source.size()>1&&strchr("!#+:&=_>?",source[0])) {
        std::string inner=source.substr(1);
        if(g_catalog.Translate(inner,caller,target,result,rule,false,identities)&&result!=inner&&Encoded(source.substr(0,1)+result,bytes))return true;
    }
    // Render-only decoration: translate the complete inner field, preserve its frame.
    size_t begin=source.find_first_not_of(" \t\r\n"),end=source.find_last_not_of(" \t\r\n");
    if(begin==std::string::npos)return false;
    size_t a=begin,b=end+1;
    if(source[a]=='('&&source[b-1]==')'&&b>a+2){++a;--b;}
    if(a==0&&b==source.size())return false;
    std::string inner=source.substr(a,b-a);
    return g_catalog.Translate(inner,caller,target,result,rule,false,identities)&&result!=inner&&
           Encoded(source.substr(0,a)+result+source.substr(b),bytes);
}
static bool LookupButton(const std::string& source,DWORD caller,std::string& bytes,std::string& rule,DWORD target=0x511AC0) {
    std::string exact;
    size_t dot=source.find('.',1);
    if(g_catalog.Translate(source,caller,target,exact,rule,true)&&exact!=source&&Encoded(exact,bytes)&&
       (dot==std::string::npos||bytes.substr(0,dot+1)==source.substr(0,dot+1)))return true;
    // This is the exact first-dot split used by native 511AC0. The original key
    // bytes remain in place; only the separately drawn description is translated.
    if(dot!=std::string::npos&&dot<=32&&dot+1<source.size()) {
        std::string label=source.substr(dot+1),translated;
        if(LookupView(label,caller,target,translated,rule)) {
            bytes=source.substr(0,dot+1)+translated;return true;
        }
    }
    return LookupView(source,caller,target,bytes,rule);
}
static std::vector<std::string> Markup(const std::string& source,bool angles=true) {
    std::vector<std::string> result;
    for(size_t i=0;i<source.size();++i) {
        char close=source[i]=='['?']':angles&&source[i]=='<'?'>':0;
        if(!close)continue;
        size_t end=source.find(close,i+1);if(end==std::string::npos)continue;
        result.push_back(source.substr(i,end-i+1));i=end;
    }
    return result;
}
static bool ShortPortions(const std::string& bytes,size_t limit) {
    size_t run=0;
    for(unsigned char c:bytes) {
        if(c==' '||c=='.'||c==','||c=='!'||c=='?'||c=='\n'||c=='\r'||c=='\t')run=0;
        else if(++run>limit)return false;
    }
    return true;
}
static bool BracketSegmentsFit(const std::string& bytes) {
    size_t plain=0,buttonBytes=0;bool button=false;
    for(char c:bytes) {
        if(c=='['){if(button)return false;button=true;buttonBytes=0;continue;}
        if(c==']'){if(!button)return false;button=false;plain=0;continue;}
        if(button&&++buttonBytes>299)return false;
        if(!button&&++plain>299)return false;
    }
    return !button;
}
static void ViewEvent(Frame& f,const std::string& original,const std::string& bytes,const std::string& rule) {
    f.translated_view=true;InterlockedIncrement(&g_translated);
    Emit("PRESENTATION_TRANSLATE",FrameFields(f)+",\"source\":"+Quote(original)+",\"translation\":"+Quote(Utf8(Wide(bytes,g_outputCodePage)))+",\"rule\":"+Quote(rule)+",\"encoding\":"+Quote("cp"+std::to_string(g_outputCodePage))+"",
         "[TRANSLATED block] "+std::string(Name(f.id))+" "+Utf8(Wide(bytes,g_outputCodePage)),true,true);
}
static bool ReadString(DWORD pointer,std::string& text,unsigned limit=4096) {
    std::vector<char> raw(limit);bool fault=false,truncated=false;
    unsigned n=CopyText((const char*)pointer,raw.data(),limit,&fault,&truncated);
    if(fault||truncated)return false;text.assign(raw.data(),n);return true;
}

static bool ExactDirectCall(DWORD returnAddress,DWORD target){
    BYTE code[5]={};if(returnAddress<5||!CopyMemoryChecked(code,(void*)(returnAddress-5),5)||code[0]!=0xE8)return false;
    LONG relative=0;memcpy(&relative,code+1,4);return returnAddress+(DWORD)relative==target;
}
static DWORD MenuSourceOrigin(const SavedRegs* r,DWORD sp,DWORD& formatterSite){
    if(!ExactDirectCall(r->original_return,g_menuFormatTarget))return 0;
    formatterSite=CanonicalVa(r->original_return-5);
    DWORD helper=0;
    switch(formatterSite){
        case 0x4FD007:case 0x4FD044:case 0x4FD092:helper=0x4FCF90;break;
        case 0x4FD181:helper=0x4FD120;break;
        case 0x4FD2C0:helper=0x4FD1C0;break;
        case 0x4FD401:helper=0x4FD390;break;
    }
    if(!helper)return formatterSite;
    BYTE framePrefix[3]={};
    if(!CopyMemoryChecked(framePrefix,(void*)(GameAddress(helper)),3)||framePrefix[0]!=0x55||framePrefix[1]!=0x8B||framePrefix[2]!=0xEC)return 0;
    // All four unhooked native helpers have a verified EBP frame. Reject an
    // unexpected frame/outer CALL instead of falling back to a broad union.
    if(r->ebp<sp||r->ebp-sp>0x10000)return 0;
    DWORD parentReturn=ReadWord(r->ebp+4);
    if(!ExactDirectCall(parentReturn,GameAddress(helper)))return 0;
    return CanonicalVa(parentReturn-5);
}
extern "C" void __cdecl CaptureMenuFormat(SavedRegs* r) noexcept {
    r->bridge_target=(DWORD)g_menuFormatOriginal;
    DWORD error=GetLastError();ThreadState* state=nullptr;
    try{
        DWORD sp=(DWORD)&r->original_return,destination=ReadWord(sp+4);
        DWORD delta=destination-g_menuDataBase;
        if(delta>=urwtranslation::kDataMenuSlots*524||delta%524){SetLastError(error);return;}
        state=State();if(!state||state->busy){SetLastError(error);return;}state->busy=true;
        if(!state->menu_sources)state->menu_sources=new(std::nothrow)urwtranslation::MenuSourceCache;
        if(state->menu_sources){
            unsigned index=delta/524;state->menu_sources->Erase(index);
            DWORD formatterSite=0,origin=MenuSourceOrigin(r,sp,formatterSite);
            if(!origin){state->busy=false;SetLastError(error);return;}
            std::string format,label,full;DWORD labelPointer=0;
            if(ReadString(ReadWord(sp+8),format,24)){
                if(format=="%c.%s"){
                    unsigned char key=(unsigned char)ReadWord(sp+12);
                    if(key){full+=(char)key;full+='.';labelPointer=ReadWord(sp+16);}
                }else if(format=="TAB.%s"||format=="F1.%s"||format=="CTRL + s.%s"){
                    full=format.substr(0,format.size()-2);labelPointer=ReadWord(sp+12);
                }
                if(labelPointer&&ReadString(labelPointer,label,1023))state->menu_sources->Observe(index,full+label,origin,formatterSite);
            }
        }
    }catch(...){}
    if(state)state->busy=false;SetLastError(error);
}
static DWORD g_menuTestEntryError=0;
static __declspec(noinline) int __cdecl FakeMenuSourceFormat(char* destination,const char* format,...){
    g_menuTestEntryError=GetLastError();
    va_list args;va_start(args,format);
    int result=vsprintf_s(destination,1024,format,args);va_end(args);
    SetLastError(0x88776655);return result;
}
bool TestMenuSourceMonitor(){
    if(!g_testing)return false;
    ThreadState* state=State();if(!state||state->busy)return false;
    auto* oldCache=state->menu_sources;state->menu_sources=new(std::nothrow)urwtranslation::MenuSourceCache;
    if(!state->menu_sources){state->menu_sources=oldCache;return false;}
    std::vector<char> records(urwtranslation::kDataMenuSlots*524+1024,0);
    DWORD oldBase=g_menuDataBase,oldFormatTarget=g_menuFormatTarget;void* oldTarget=g_menuFormatOriginal;
    g_menuDataBase=(DWORD)records.data();g_menuFormatTarget=(DWORD)&FakeMenuSourceFormat;bool result=true;
    if(MH_CreateHook((void*)&FakeMenuSourceFormat,(void*)&MenuFormatMonitor,&g_menuFormatOriginal)!=MH_OK)result=false;
    else if(MH_EnableHook((void*)&FakeMenuSourceFormat)!=MH_OK)result=false;
    else{
        auto before=ReadTraceStats();char outside[1024]={};
        SetLastError(0x11223344);
        result=FakeMenuSourceFormat(outside,"%s","outside")==7&&std::string(outside)=="outside"&&g_menuTestEntryError==0x11223344&&GetLastError()==0x88776655;
        std::string label="Remove the extremely long crafted item to test exact source identity";
        char* native=records.data()+524;
        SetLastError(0x11223344);
        result=result&&FakeMenuSourceFormat(native,"%c.%s",82,label.c_str())==(int)label.size()+2&&g_menuTestEntryError==0x11223344&&GetLastError()==0x88776655;
        std::string full="R."+label,got;std::vector<urwtranslation::NativeMenuRecord> live(2);
        live[1]={urwtranslation::NativeMenuClip(full),82};
        auto frame=state->menu_sources->Freeze(0x1000,live);
        result=result&&frame.FullRecord(0x1000+232,live[1].bytes,82,got)&&got==full;
        result=result&&!frame.FullRecord(0x1001+232,live[1].bytes,82,got);
        FakeMenuSourceFormat(native,"%s","unrecognized replacement format");
        frame=state->menu_sources->Freeze(0x1000,live);
        result=result&&!frame.FullRecord(0x1000+232,live[1].bytes,82,got);
        auto after=ReadTraceStats();
        result=result&&before.entered==after.entered&&before.left==after.left&&before.depth==after.depth;
    }
    MH_DisableHook((void*)&FakeMenuSourceFormat);MH_RemoveHook((void*)&FakeMenuSourceFormat);
    g_menuDataBase=oldBase;g_menuFormatTarget=oldFormatTarget;g_menuFormatOriginal=oldTarget;
    delete state->menu_sources;state->menu_sources=oldCache;return result;
}
static void SnapshotDataMenu(ThreadState* state,Frame& frame){
    if(!state->menu_sources)return;
    DWORD count=ReadWord(GameAddress(0x65A6BF8));
    if(count>urwtranslation::kDataMenuSlots)return;
    count=std::min<DWORD>(count,urwtranslation::kDataMenuVisibleRows);
    std::vector<urwtranslation::NativeMenuRecord> records;
    for(DWORD i=0;i<count;++i){
        DWORD address=g_menuDataBase+i*524;std::string native;
        if(!ReadString(address,native,1023))return;
        records.push_back({native,ReadWord(address+200)});
    }
    frame.menu_source=new(std::nothrow)urwtranslation::MenuSourceFrame(state->menu_sources->Freeze(GameAddress(0x634C470),records));
}
static char* PrivateTextCopy(const std::string& text){
    auto* p=(char*)HeapAlloc(GetProcessHeap(),0,text.size()+1);
    if(p)memcpy(p,text.c_str(),text.size()+1);return p;
}
// All lookups here are strict source routes, including deliberately preserved
// names. A no-match is not proof that a fragment may safely be copied.
static bool KnownVirtualPart(const std::string& raw,DWORD caller,DWORD target,std::string& bytes,bool& changed){
    std::string source=Utf8(Wide(raw,g_codepage)),translated,rule;
    if(!g_catalog.Translate(source,caller,target,translated,rule)||!Encoded(translated,bytes))return false;
    changed=translated!=source;return true;
}
static void SnapshotMenuSelection(ThreadState* state,Frame& message,const SavedRegs* r){
    if(message.return_site!=GameAddress(0x4FDD10)||r->edi%524)return;
    Frame* menu=Ancestor(state,0x4FD710);
    unsigned index=r->edi/524;
    if(!menu||!menu->menu_source||!menu->menu_original_title||index>=menu->menu_source->count)return;
    const auto& row=menu->menu_source->rows[index];
    if(!row.valid||row.key==27||row.key==(std::uint32_t)-1)return;
    std::string stored,actual;
    DWORD address=g_menuDataBase+index*524;
    if(!ReadString(address,stored,1023)||stored!=row.stored||ReadWord(address+200)!=row.key||!ReadString(r->ecx,actual,1024))return;
    std::string expected,translated;
    bool preserveCase=(ReadWord(GameAddress(0x217C9B3))&255)==1;
    if(!urwtranslation::BuildMenuSelectionView(*menu->menu_source,index,menu->menu_original_title,stored,row.key,actual,preserveCase,
       [](const std::string& raw,std::uint32_t caller,std::uint32_t target,std::string& bytes,bool& changed){return KnownVirtualPart(raw,caller,target,bytes,changed);},expected,translated)||!ShortPortions(translated,128))return;
    message.menu_selection_expected=PrivateTextCopy(expected);
    message.menu_selection_translation=PrivateTextCopy(translated);
    if(message.menu_selection_expected&&message.menu_selection_translation)
        Emit("MENU_SELECTION_SOURCE_SNAPSHOT",FrameFields(message)+",\"selected_index\":"+std::to_string(index)+",\"source_call_va\":"+Quote(Hex(row.origin))+",\"full_record\":"+Quote(Utf8(Wide(row.full,g_codepage)))+",\"source_buffers_modified\":false","",false,true);
}
bool TestMenuSelectionComposition(){
    if(!g_testing)return false;
    urwtranslation::MenuSourceCache cache;
    const std::string full="K.Knife the extremely long named craftsman";
    cache.Observe(0,full,0x543C10);
    std::vector<urwtranslation::NativeMenuRecord> native={{urwtranslation::NativeMenuClip(full),75}};
    auto frame=cache.Freeze(0x1000,native);
    const std::string raw=native[0].bytes,original=urwtranslation::NativeMenuSelection("Trade",raw);
    std::string expected,translated;
    auto lookup=[](const std::string& text,std::uint32_t caller,std::uint32_t target,std::string& out,bool& changed){
        if(caller==0x4FD710&&target==0x4FD710&&text=="Trade"){out=text;changed=false;return true;}
        if(caller==0x543C10&&target==0x4FCF90&&text=="Knife the extremely long named craftsman"){
            out="Knife \xF0\xE5\xEC\xE5\xF1\xEB\xE5\xED\xED\xE8\xEA";changed=true;return true;}
        return false;
    };
    bool ok=urwtranslation::BuildMenuSelectionView(frame,0,"Trade",raw,75,original,false,lookup,expected,translated)&&
        expected==urwtranslation::NativeMenuSelectionView(original,false)&&translated=="#Trade: Knife \xF0\xE5\xEC\xE5\xF1\xEB\xE5\xED\xED\xE8\xEA";
    frame.rows[0].origin++;
    ok=ok&&!urwtranslation::BuildMenuSelectionView(frame,0,"Trade",raw,75,original,false,lookup,expected,translated);
    return ok&&native[0].bytes==raw&&frame.rows[0].full==full;
}
static bool SavedPlayerNote(const Frame& f,const SavedRegs* r) {
    if(Preferred(f)!=0x50BF70)return false;
    if(g_testing){if(f.caller>=kHookCount||kProfile[f.caller].rva+0x400000!=0x4FB720)return false;}
    else if(f.return_site!=GameAddress(0x4FBE30))return false;
    const DWORD begin=GameAddress(0x65A36F0);
    if(r->edi<begin||r->edi-begin>=70*100||(r->edi-begin)%100)return false;
    const unsigned index=(r->edi-begin)/100;
    std::string metadata,payload;
    if(!ReadString(r->edi,metadata,100)||!ReadString(GameAddress(0x65A7310)+index*1024,payload,1024))return false;
    return metadata.size()>=17&&metadata.compare(0,8,"(600060)")==0&&metadata.compare(14,3,"[\"]")==0&&payload.rfind("-- ",0)==0;
}
static bool LockView(Frame& f,bool stream) {
    EnterCriticalSection(&g_viewLock);f.view_locked=true;f.stream_view=stream;
    f.view_backup=(char*)HeapAlloc(GetProcessHeap(),0,stream?sizeof(g_tokenView)+sizeof(g_assemblyView):sizeof(g_messageView));
    if(!f.view_backup){f.view_locked=false;LeaveCriticalSection(&g_viewLock);return false;}
    memcpy(f.view_backup,stream?g_tokenView:g_messageView,stream?sizeof(g_tokenView):sizeof(g_messageView));
    if(stream)memcpy(f.view_backup+sizeof(g_tokenView),g_assemblyView,sizeof(g_assemblyView));
    f.view_counter=g_tokenCounter;return true;
}
static bool TokenizeView(const std::string& text) {
    std::vector<std::string> tokens;std::string current;
    auto flush=[&](){if(!current.empty()){tokens.push_back(current);current.clear();}};
    for(size_t i=0;i<text.size();++i) {
        unsigned char c=text[i];
        if(c=='<') {flush();size_t end=text.find('>',i);if(end==std::string::npos)return false;tokens.push_back(text.substr(i,end-i+1));i=end;}
        else if(c==' '||c=='\t'||c=='\r'||c=='\n'){flush();}
        else current+=(char)c;
    }
    flush();if(tokens.size()>=5000)return false;
    for(const auto& token:tokens)if(token.size()>128)return false;
    memset(g_tokenView,0,sizeof(g_tokenView));
    for(size_t i=0;i<tokens.size();++i)memcpy(g_tokenView+i*130,tokens[i].data(),tokens[i].size());
    return true;
}
static void SnapshotNpcSource(Frame& frame,const SavedRegs* r){
    // Actual native return and saved actor slot, not inferred visible words.
    // The alert producer proves that its resolved npc_defdesc and the saved
    // actor identify the same record. Other 4FB120 callers may pass a buffer
    // assembled for another actor; do not generalize by matching label words.
    // Never use current flags to reinterpret an older saved journal entry.
    if(frame.return_site!=GameAddress(0x4FB468)||r->ebp<0x810)return;
    DWORD actor=0,current=0,producer=0;
    if(!CopyMemoryChecked(&producer,(void*)(r->ebp+4),4)||producer!=GameAddress(0x4AAA49))return;
    if(!CopyMemoryChecked(&actor,(void*)(r->ebp-0x810),4)||
       !CopyMemoryChecked(&current,(void*)(GameAddress(0xA61FFAC)),4)||actor!=current)return;
    urwtranslation::NpcSourceIdentity proof;
    auto reader=[](uintptr_t address,void* data,size_t count){return CopyMemoryChecked(data,(void*)address,count);};
    if(!urwtranslation::ReadNpcFlags2Identity((int)actor,g_base,reader,proof))return;
    proof.label=Utf8(Wide(proof.label,g_codepage));
    frame.npc_sources=new(std::nothrow)std::vector<urwtranslation::NpcSourceIdentity>;
    if(!frame.npc_sources)return;
    frame.npc_sources->push_back(proof);
    Emit("NPC_SOURCE_IDENTITY",FrameFields(frame)+",\"record_id\":"+std::to_string((int)actor)+",\"branch\":"+Quote(proof.branch)+",\"label\":"+Quote(proof.label),"",false,true);
}
static void EnterPresentation(ThreadState* s,Frame& f,SavedRegs* r,DWORD sp) {
    if(!g_translationEnabled)return;
    DWORD va=Preferred(f);
    if(g_nfoReady&&(va==0x43C730||va==0x43CB10)) {
        std::string query,upper;
        if(ReadString(r->ecx,query,190)&&std::any_of(query.begin(),query.end(),[](unsigned char c){return c>=128;})&&
           UpperNfoQuery(query,g_outputCodePage,upper)&&upper!=query) {
            f.nfo_query=PrivateTextCopy(upper);if(f.nfo_query)r->ecx=(DWORD)f.nfo_query;
        }
        return;
    }
    if(urwtranslation::IsDataMenuScope(va)){
        SnapshotDataMenu(s,f);
        if(va==0x4FD710){std::string title;if(ReadString(r->ecx,title,1024)&&!title.empty())f.menu_original_title=PrivateTextCopy(title);}
        return;
    }
    if(va==0x4F9D50&&r->ecx&&r->edx){
        std::string title;if(ReadString(r->edx,title,500)){
            title=urwtranslation::NativeUpperCaption(title);
            f.armour_full_source=PrivateTextCopy(title);
            f.armour_expected_title=PrivateTextCopy(urwtranslation::NativeArmourClip(title));
        }
    }
    if(UserInputContext(s)&&va!=0x50BF70&&va!=0x50C400)return;
    if(va==0x4F3990) {LockView(f,false);SnapshotMenuSelection(s,f,r);SnapshotNpcSource(f,r);return;}
    if(va==0x4F2F80) {
        Frame* message=Ancestor(s,0x4F3990);
        if(!message||!message->view_locked||message->message_ready)return;
        message->message_ready=true;
        if(!CopyMemoryChecked(g_messageView,(void*)(GameAddress(0x65A5A18)),sizeof(g_messageView))) {g_messageView[0]=0;return;}
        g_messageView[1023]=0;
        std::string original=Utf8(Wide(g_messageView,g_codepage)),bytes,rule;
        Emit("PRESENTATION_TEXT",FrameFields(*message)+",\"source_file\":"+Quote(SourceFile(s))+",\"text\":"+Quote(original),"[TEXT complete_message] "+original,true,true);
        RefreshCatalog();
        if(message->menu_selection_expected&&message->menu_selection_translation&&
           std::string(g_messageView)==message->menu_selection_expected){
            bytes=message->menu_selection_translation;
            memcpy(g_messageView,bytes.c_str(),bytes.size()+1);
            ViewEvent(*message,original,bytes,"source_identity:menu_selection4FD710");return;
        }
        if(LookupView(original,message->caller<kHookCount?kProfile[message->caller].rva+0x400000:0,0x4F3990,bytes,rule,message->npc_sources)&&bytes.size()<sizeof(g_messageView)&&ShortPortions(bytes,128)) {
            // Color prefix belongs to the renderer; retain it if a legacy rule omitted it.
            if(!original.empty()&&strchr("!#+:&=_>?",original[0])&&(bytes.empty()||bytes[0]!=original[0]))bytes.insert(bytes.begin(),original[0]);
            if(bytes.size()<sizeof(g_messageView)) {memcpy(g_messageView,bytes.c_str(),bytes.size()+1);ViewEvent(*message,original,bytes,rule);}
        }
        return;
    }
    if(va==0x50BF70||va==0x50C400) {
        if(!LockView(f,true))return;
        f.english_token_end=ReadWord(GameAddress(0xA622874));
        if(!CopyMemoryChecked(g_tokenView,(void*)(GameAddress(0xA622878)),sizeof(g_tokenView))) {g_tokenView[0]=0;return;}
        unsigned count=0;std::string input;std::vector<std::string> englishTokens;
        for(;count<5000;++count) {
            std::string token;
            if(!ReadString((DWORD)(g_tokenView+count*130),token,130)){input.clear();break;}
            if(token.empty())break;
            englishTokens.push_back(token);
            if(!input.empty()&&token[0]!='.')input+=' ';
            input+=token;
        }
        f.english_token_end=count+1;
        std::string assembly;
        for(size_t i=0;i<englishTokens.size();++i) {
            std::string token=englishTokens[i];if(token[0]=='<')continue;
            if(i+1<englishTokens.size()&&englishTokens[i+1][0]!='.')token+=' ';
            if(assembly.size()+token.size()<1024)assembly+=token;
        }
        f.english_assembly=(char*)HeapAlloc(GetProcessHeap(),0,assembly.size()+1);
        if(f.english_assembly)memcpy(f.english_assembly,assembly.c_str(),assembly.size()+1);
        g_tokenView[sizeof(g_tokenView)-130]=0;
        // 4ABA60 uses this renderer in measurement mode to build logical names.
        // The private view stays English there; input slots are never rewritten.
        if(f.caller>=kHookCount||kProfile[f.caller].rva+0x400000==0x4ABA60)return;
        std::string original=Utf8(Wide(input,g_codepage)),bytes,rule;
        Emit("PRESENTATION_TEXT",FrameFields(f)+",\"source_file\":"+Quote(SourceFile(s))+",\"text\":"+Quote(original),"[TEXT complete_stream] "+original,true,true);
        RefreshCatalog();
        f.protected_user_note=SavedPlayerNote(f,r);
        if(f.protected_user_note)Emit("USER_NOTE_PRESERVED",FrameFields(f)+",\"source_file\":\"msglog\",\"reason\":\"native_note_record_kind\"","[NOTE] Personal journal entry preserved",false,true);
        bool userMarker=UserInputContext(s)||(va==0x50C400&&kProfile[f.caller].rva+0x400000==0x4E2110);
        if(!userMarker&&!ParentHasTranslation(s)&&input.size()<=4096&&LookupView(original,kProfile[f.caller].rva+0x400000,va,bytes,rule)&&Markup(input)==Markup(bytes)&&TokenizeView(bytes))ViewEvent(f,original,bytes,rule);
        return;
    }
    if(ParentHasTranslation(s))return;
    DWORD slot=0,pointer=0;size_t limit=1023,portion=128;bool ecx=false,button=false;
    Frame* owner=va==0x4D18F0?Ancestor(s,0x511E60):nullptr;
    if(va==0x50BB50||va==0x511AC0||va==0x522660){slot=sp+4;pointer=ReadWord(slot);button=va==0x511AC0;}
    else if(va==0x4CD210){slot=sp+12;pointer=ReadWord(slot);button=true;}
    else if(va==0x512160){slot=sp+8;pointer=ReadWord(slot);button=true;}
    else if(va==0x510860){ecx=true;pointer=r->edx;limit=96;}
    else if(va==0x50E990||va==0x512F10){slot=sp+12;pointer=ReadWord(slot);limit=299;}
    else if(va==0x50BD40){slot=sp+8;pointer=ReadWord(slot);}
    else if(va==0x531760){ecx=true;pointer=r->ecx;limit=4096;portion=98;}
    else if(va==0x4D18F0&&owner){ecx=true;pointer=r->ecx;}
    else return;
    std::string raw;if(!ReadString(pointer,raw))return;
    unsigned inputCodePage=g_codepage;
    if(g_nfoReady&&button&&f.caller<kHookCount&&kProfile[f.caller].rva+0x400000==0x43C730&&raw.find(".Back to \"")!=std::string::npos)inputCodePage=g_outputCodePage;
    std::string original=Utf8(Wide(raw,inputCodePage)),bytes,rule;
    Emit("PRESENTATION_TEXT",FrameFields(f)+",\"source_file\":"+Quote(SourceFile(s))+",\"text\":"+Quote(original),"[TEXT complete_input] "+std::string(Name(f.id))+" "+original,true,true);
    RefreshCatalog();bool found=false;
    if(va==0x511AC0){
        Frame* menu=nullptr;
        for(unsigned i=s->depth;i>0;--i){
            Frame& candidate=s->frames[i-1];
            if(urwtranslation::IsDataMenuScope(Preferred(candidate))){menu=&candidate;break;}
        }
        std::string full;std::uint32_t origin=0;bool wasClipped=false;
        if(menu&&menu->menu_source&&menu->menu_source->FullRecord(pointer,raw,ReadWord(pointer+200),full,&origin,&wasClipped)){
            // Prefix stays in original game bytes (including high-bit/control
            // keys). Only the complete bare label is decoded and translated.
            size_t dot=full.find('.',1);
            if(dot!=std::string::npos&&dot+1<full.size()){
                std::string label=Utf8(Wide(full.substr(dot+1),g_codepage)),translated;
                if(LookupView(label,origin,0x4FCF90,translated,rule)){
                    bytes=full.substr(0,dot+1)+translated;
                    if(wasClipped)bytes=urwtranslation::NativeMenuClip(bytes);
                    found=true;
                    Emit("MENU_SOURCE_SNAPSHOT",FrameFields(f)+",\"virtual_target\":\"0x004FCF90\",\"source_call_va\":"+Quote(Hex(origin))+",\"full_label\":"+Quote(label)+",\"source_buffers_modified\":false","",false,true);
                }
            }
        }
    }
    if(va==0x522660) {
        // The one direct caller passes one of eleven known animation rows.
        if(f.caller>=kHookCount||kProfile[f.caller].rva+0x400000!=0x522790)return;
        std::string block;int index=-1;
        for(unsigned i=0;i<11;++i){if(i)block+='\n';block+=kFirstWinterLines[i];if(original==kFirstWinterLines[i])index=(int)i;}
        std::string all,blockRule;
        if(index>=0&&LookupView(block,0x522790,0x522660,all,blockRule)) {
            std::vector<std::string> lines;size_t start=0;
            for(size_t i=0;i<=all.size();++i)if(i==all.size()||all[i]=='\n'){lines.push_back(all.substr(start,i-start));start=i+1;}
            if(lines.size()==11){bytes=lines[index];rule=blockRule;found=true;}
        }
        limit=96; // 768 pixels; centered by the same 8-pixel font geometry.
    }
    if(!found) {
        DWORD caller=f.caller<kHookCount?kProfile[f.caller].rva+0x400000:0;
        found=button?LookupButton(original,caller,bytes,rule,va):LookupView(original,caller,owner?0x511E60:va,bytes,rule);
    }
    if(!found||bytes.empty()||bytes.size()>limit||!ShortPortions(bytes,portion)||Markup(raw)!=Markup(bytes))return;
    if(va==0x50BB50&&!BracketSegmentsFit(bytes))return;
    if(va==0x50BD40&&bytes.size()>299) {
        int width=(int)ReadWord(sp+4);
        if(width<=0||width/8+1>299)return;
    }
    if(button) {
        if((va==0x511AC0&&bytes.size()>299)||(va!=0x511AC0&&!BracketSegmentsFit(bytes)))return;
        size_t dot=raw.find('.',1);
        if(dot!=std::string::npos&&bytes.substr(0,dot+1)!=raw.substr(0,dot+1))return;
        if(va==0x511AC0&&dot==std::string::npos&&bytes.size()>98)return;
    }
    char* copy=(char*)HeapAlloc(GetProcessHeap(),0,bytes.size()+1);if(!copy)return;
    memcpy(copy,bytes.c_str(),bytes.size()+1);
    if(va==0x510860)r->edx=(DWORD)copy;
    else if(ecx)r->ecx=(DWORD)copy;
    else if(!StoreWord(slot,(DWORD)copy)){HeapFree(GetProcessHeap(),0,copy);return;}
    f.localized=copy;ViewEvent(f,original,bytes,rule);
    if(owner)owner->translated_view=true;
    if(va==0x522660)r->ecx=400-(DWORD)(8*bytes.size()/2);
}
static std::vector<PresentationOperand> AllPresentationOperands() {
    std::vector<PresentationOperand> result(std::begin(kPresentationOperands),std::end(kPresentationOperands));
    result.insert(result.end(),std::begin(kTextureOperands),std::end(kTextureOperands));return result;
}
static bool InstallPresentationOperands() {
    auto operands=AllPresentationOperands();
    for(const auto& p:operands)if(ReadWord(GameAddress(p.va))!=GameAddress(p.expected))return false;
    for(const auto& p:operands) {
        DWORD address=GameAddress(p.va),old=0,ignored=0;
        DWORD replacement=p.buffer==1?(DWORD)g_messageView+p.delta:p.buffer==2?(DWORD)g_tokenView:p.buffer==4?(DWORD)g_assemblyView:(DWORD)&g_tokenCounter;
        if(!VirtualProtect((void*)address,4,PAGE_EXECUTE_READWRITE,&old))return false;
        bool okay=StoreWord(address,replacement);VirtualProtect((void*)address,4,old,&ignored);
        if(!okay)return false;FlushInstructionCache(GetCurrentProcess(),(void*)address,4);
    }
    Emit("PRESENTATION_VIEWS",",\"operands\":"+std::to_string(operands.size())+",\"source_buffers_modified\":false","[READY] Private message/stream/renderer scratch views; English source buffers retained",true);return true;
}
static void RefreshEncyclopedia() {
    if(g_catalog.GetCodePage()!=g_outputCodePage)return;
    auto docs=g_catalog.GetDocuments();if(docs.empty())return;
    LONG generation=(LONG)g_catalog.GetStats().generation;
    if(generation==g_nfoGeneration)return;
    std::string bytes,error;
    if(!urwtranslation::BuildEncyclopedia(docs,bytes,error,g_outputCodePage)) {Emit("ENCYCLOPEDIA_ERROR",",\"error\":"+Quote(error),"[GAME.NFO] Previous file retained: "+error,true);return;}
    std::wstring directory=SelectedTranslationDirectory()+L"\\generated";
    CreateDirectoryW(directory.c_str(),nullptr);g_nfoPath=directory+L"\\GAME.NFO";
    std::wstring temporary=g_nfoPath+L".tmp";HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    DWORD written=0;bool okay=file!=INVALID_HANDLE_VALUE&&WriteFile(file,bytes.data(),(DWORD)bytes.size(),&written,nullptr)&&written==bytes.size();
    if(file!=INVALID_HANDLE_VALUE){FlushFileBuffers(file);CloseHandle(file);}
    if(okay)okay=MoveFileExW(temporary.c_str(),g_nfoPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!okay){DeleteFileW(temporary.c_str());Emit("ENCYCLOPEDIA_ERROR",",\"error\":\"cannot_publish_file\"","[GAME.NFO] Cannot publish generated/GAME.NFO",true);return;}
    g_nfoReady=true;g_nfoGeneration=generation;
    Emit("ENCYCLOPEDIA_READY",",\"documents\":"+std::to_string(docs.size())+",\"bytes\":"+std::to_string(bytes.size()),"[GAME.NFO] Published "+std::to_string(docs.size())+" articles from the selected translation catalogue",true);
}

static __declspec(noinline) void* __cdecl FontFopen(const char* filename,const char* mode) {
    const DWORD callReturn=(DWORD)_ReturnAddress();
    if(filename&&mode&&mode[0]=='r') {
        auto* state=(ThreadState*)TlsGetValue(g_tls);
        if(state&&!state->busy)for(unsigned i=state->depth;i>0;--i) {
            Frame& owner=state->frames[i-1];DWORD va=Preferred(owner);
            if(va!=0x4F8800&&va!=0x4DDFE0&&va!=0x4FAF80&&va!=0x43C730&&va!=0x4108A0)continue;
            std::string path;
            if(ReadString((DWORD)filename,path,512)) {
                size_t slash=path.find_last_of("/\\");if(slash!=std::string::npos)path=path.substr(slash+1);
                char* copy=(char*)HeapAlloc(GetProcessHeap(),0,path.size()+1);
                if(copy){memcpy(copy,path.c_str(),path.size()+1);if(owner.source_file)HeapFree(GetProcessHeap(),0,owner.source_file);owner.source_file=copy;}
            }
            break;
        }
    }
    if(g_translationEnabled&&filename&&mode&&_stricmp(filename,"GAME.NFO")==0&&strcmp(mode,"rb")==0) {
        try {RefreshCatalog();}catch(...){}
        if(g_nfoReady) {
            void* file=g_originalWfopen(g_nfoPath.c_str(),L"rb");
            if(file){try{Emit("ENCYCLOPEDIA_OPEN",",\"file\":\"GAME.NFO\"","[GAME.NFO] Reading external translation",true);}catch(...){}return file;}
        }
    }
    if(callReturn==GameAddress(0x514F21)&&filename&&mode&&_stricmp(filename,"URW.FNT")==0&&strcmp(mode,"rb")==0) {
        // Use _wfopen from the SAME CRT as the game's fopen/fread/fclose.
        // The /MT CRT linked into this DLL has a different FILE ownership.
        void* file=g_originalWfopen(g_fontPath.c_str(),L"rb");DWORD error=GetLastError();
        try {Emit("FONT_OVERRIDE",",\"path\":"+Quote(Utf8(g_fontPath))+",\"loaded\":"+(file?"true":"false")+",\"encoding\":"+Quote("cp"+std::to_string(g_outputCodePage))+"",
                  file?"[FONT] Loaded translation folder URW.FNT (selected codepage)":"[FONT] Cannot read translation folder URW.FNT",true);}catch(...){}
        if(!file){g_translationEnabled=false;return g_originalFopen(filename,mode);}
        SetLastError(error);return file;
    }
    return g_originalFopen(filename,mode);
}
static bool PrepareTranslation() {
    std::wstring root=Directory(ModulePath(g_dll));
    g_translationEnabled=GetPrivateProfileIntW(L"Hook",L"TranslationEnabled",1,g_ini.c_str())!=0;
    if(!g_translationEnabled)return true;
    Emit("TRANSLATION_LOAD",",\"stage\":\"catalogue_begin\"","[CATALOG] Loading selected translation catalogue",true);
    if(!g_catalog.Initialize(L"", SelectedTranslationDirectory())||g_catalog.GetCodePage()!=g_outputCodePage) {
        Emit("TRANSLATION_ERROR",",\"error\":"+Quote(g_catalog.LastError()),"[ERROR] Invalid selected catalogue or encoding does not match selected language: "+g_catalog.LastError(),true);return false;
    }
    Emit("TRANSLATION_LOAD",",\"stage\":\"catalogue_ready\"","[CATALOG] Loaded",true);
    g_fontPath=SelectedTranslationDirectory()+L"\\URW.FNT";
    WIN32_FILE_ATTRIBUTE_DATA info={};
    if(!GetFileAttributesExW(g_fontPath.c_str(),GetFileExInfoStandard,&info)||info.nFileSizeHigh||info.nFileSizeLow!=20480) {
        Emit("TRANSLATION_ERROR",",\"reason\":\"missing_or_invalid_font\"","[ERROR] translation folder URW.FNT must contain 20480 bytes",true);return false;
    }
    void* target=(void*)ReadWord(GameAddress(0x589478));HMODULE owner=nullptr;
    if(!target||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)target,&owner)) {
        Emit("TRANSLATION_ERROR",",\"reason\":\"font_crt_owner\",\"target\":"+Quote(Hex((DWORD)target))+",\"error\":"+std::to_string(GetLastError()),"[ERROR] Cannot identify game's fopen module",true);return false;
    }
    g_originalWfopen=(GameWfopen)GetProcAddress(owner,"_wfopen");
    if(!g_originalWfopen) {
        Emit("TRANSLATION_ERROR",",\"reason\":\"font_wfopen_export\",\"owner\":"+Quote(Utf8(ModulePath(owner)))+",\"target\":"+Quote(Hex((DWORD)target))+",\"error\":"+std::to_string(GetLastError()),"[ERROR] Game CRT does not export _wfopen",true);return false;
    }
    MH_STATUS status=MH_CreateHook(target,(void*)&FontFopen,(void**)&g_originalFopen);
    if(status!=MH_OK){Emit("TRANSLATION_ERROR",",\"reason\":\"font_hook\"","[ERROR] Cannot redirect font loading",true);return false;}
    std::string nfoError;
    if(!g_catalog.GetDocuments().empty()&&!urwtranslation::InstallSynopsisTable(g_base,nfoError)) {
        Emit("TRANSLATION_ERROR",",\"error\":"+Quote(nfoError),"[GAME.NFO] Cannot reserve synopsis table: "+nfoError,true);return false;
    }
    if(!InstallPresentationOperands())return false;
    RefreshCatalog();return g_catalog.GetDocuments().empty()||g_nfoReady;
}
extern "C" void __cdecl TraceEnter(SavedRegs* r,unsigned id) noexcept {
    DWORD error=GetLastError();
    r->bridge_target=(DWORD)g_trampolines[id];
    ThreadState* s=nullptr;
    try {
        s=State();if(!s||s->busy){SetLastError(error);return;}s->busy=true;
        DWORD sp=(DWORD)&r->original_return, ret=r->original_return;
        Prune(s,sp,ret);
        if(s->depth==512){InterlockedIncrement(&g_failures);Emit("OVERFLOW",",\"frame_limit\":512","[WARNING] Frame limit; this invocation remains uninstrumented",true);s->busy=false;SetLastError(error);return;}
        unsigned caller=Caller(ret,id);
        unsigned callerOrigin=caller<kHookCount?1:0;
        if(caller==kHookCount&&s->depth){caller=s->frames[s->depth-1].id;callerOrigin=ret==(DWORD)&ReturnBridge?2:3;}
        Frame* owner=nullptr;Frame* dialogue=nullptr;
        for(unsigned j=s->depth;j>0;--j) {
            Frame& ancestor=s->frames[j-1];
            if(!owner&&ancestor.id==caller)owner=&ancestor;
            if(!dialogue&&kProfile[ancestor.id].kind==Dialogue)dialogue=&ancestor;
        }
        Frame& f=s->frames[s->depth++];memset(&f,0,sizeof(f));
        f.id=id;f.uid=(DWORD)InterlockedIncrement(&g_uid);f.entry_sp=sp;f.original_return=ret;f.return_site=ret;f.caller=caller;f.caller_origin=callerOrigin;f.pending=true;
        unsigned kind=kProfile[id].kind;
        if(kind==Dialogue) {
            f.route=caller<kHookCount&&(kProfile[caller].kind&SplitSource)?3:1;
            f.group=owner?owner->uid:f.uid;f.dialogue=f.uid;if(owner)++owner->direct_dialogues;
        } else if(kind==Leaf) {
            f.route=dialogue?4:2;f.dialogue=dialogue?dialogue->uid:0;
            DWORD target=kProfile[id].rva+0x400000;
            if(target==0x50B690||target==0x50B810) {
                f.route=5;
                for(unsigned j=s->depth-1;j>0;--j) {
                    Frame& parent=s->frames[j-1];DWORD source=kProfile[parent.id].rva+0x400000;
                    if(source==0x50C400||source==0x512160||source==0x4CD210) {
                        f.route=6;f.dialogue=parent.uid;dialogue=&parent;break;
                    }
                }
            }
            f.group=dialogue?dialogue->group:(owner?owner->uid:f.uid);if(owner)++owner->direct_draws;
        } else { f.group=f.uid;f.route=0; }
        // Commit the continuation before potentially allocating log strings.
        r->original_return=(DWORD)&ReturnBridge;InterlockedIncrement(&g_entered);
        Emit("ENTER",FrameFields(f)+",\"depth\":"+std::to_string(s->depth)+",\"marker_before\":0,\"marker_after\":1",
             "[ENTER] "+std::string(Name(id))+"_on=1 scope="+std::to_string(f.uid));
        if(kind!=Leaf&&kind!=Dialogue)EnterPresentation(s,f,r,sp);
        if(kind==Leaf||kind==Dialogue){Capture(s,f,r,sp);bool changed=ApplyTranslation(s,f,r,sp);Emit("PASS",FrameFields(f)+",\"marker\":1,\"text_modified\":"+(changed?"true":"false"),"[PASS] "+std::string(Name(id))+(changed?" translated screen copy":" original arguments"));}
    } catch(...) {InterlockedIncrement(&g_failures);}
    if(s)s->busy=false;SetLastError(error);
}
extern "C" void __cdecl TraceLeave(SavedRegs* r) noexcept {
    DWORD error=GetLastError();ThreadState* s=(ThreadState*)TlsGetValue(g_tls);
    if(!s||!s->depth){OutputDebugStringA("rus_URWTrace: missing continuation\n");TerminateProcess(GetCurrentProcess(),0xE0770001);}
    Frame f=s->frames[--s->depth];r->bridge_target=f.original_return;
    s->busy=true;
    try {
        bool wasPending=f.pending;f.pending=false;
        Emit("LEAVE",FrameFields(f)+",\"marker_before\":"+(wasPending?"1":"0")+",\"marker_after\":0,\"eax\":"+Quote(Hex(r->eax))+",\"edx\":"+Quote(Hex(r->edx)),
             "[LEAVE] "+std::string(Name(f.id))+"_off marker=0 scope="+std::to_string(f.uid));
        if(kProfile[f.id].kind&(WholeSource|SplitSource)) {
            Emit("CALLER_SUMMARY",FrameFields(f)+",\"direct_530DC0_calls\":"+std::to_string(f.direct_dialogues)+",\"static_split_family\":"+((kProfile[f.id].kind&SplitSource)?"true":"false"),
                 "[GROUP] "+std::string(Name(f.id))+" scope="+std::to_string(f.uid)+" calls_530DC0="+std::to_string(f.direct_dialogues),f.direct_dialogues>0);
        }
        InterlockedIncrement(&g_left);
    } catch(...) {InterlockedIncrement(&g_failures);}
    ReleaseTranslation(f);
    s->busy=false;SetLastError(error);
}

static bool OpenLog(DWORD consolePid,bool testing) {
    InitializeCriticalSection(&g_viewLock);InitializeCriticalSection(&g_logLock);g_start=GetTickCount();g_testing=testing;
    std::wstring root=Directory(ModulePath(g_dll));g_ini=root+L"\\URWTrace.ini";
    wchar_t translationFolder[1024];const bool finnish=g_outputCodePage==1252;
    GetPrivateProfileStringW(L"Languages",finnish?L"FinnishFolder":L"RussianFolder",finnish?L"fin_translate":L"rus_translate",translationFolder,1024,g_ini.c_str());g_translationDirectory=translationFolder;
    g_consoleMode=testing?0:std::min(2U,GetPrivateProfileIntW(L"Hook",L"ConsoleMode",0,g_ini.c_str()));
    g_loggingEnabled=testing||GetPrivateProfileIntW(L"Hook",L"LoggingEnabled",0,g_ini.c_str())!=0;
    g_textLimit=std::max(32U,std::min(65536U,GetPrivateProfileIntW(L"Hook",L"MaxTextBytes",4096,g_ini.c_str())));
    g_codepage=GetPrivateProfileIntW(L"Hook",L"TextCodePage",1252,g_ini.c_str());if(!IsValidCodePage(g_codepage))g_codepage=1252;
    g_repeatMs=GetPrivateProfileIntW(L"Hook",L"ConsoleRepeatMilliseconds",500,g_ini.c_str());
    g_keep=std::max(1U,std::min(32U,GetPrivateProfileIntW(L"Hook",L"LogSegments",8,g_ini.c_str())));
    g_logLimit=(uint64_t)std::max(1U,std::min(512U,GetPrivateProfileIntW(L"Hook",L"LogSegmentMiB",64,g_ini.c_str())))*1024*1024;
    if(!testing&&g_consoleMode) {
        if(!GetConsoleWindow()&&!AttachConsole(consolePid))AllocConsole();
        SetConsoleOutputCP(CP_UTF8);SetConsoleTitleW(L"UnReal World 3.88 - translation hook 0.20");
        g_console=CreateFileW(L"CONOUT$",GENERIC_WRITE,FILE_SHARE_WRITE|FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    }
    if(g_loggingEnabled) {
    CreateDirectoryW((root+L"\\logs").c_str(),nullptr);
    SYSTEMTIME t;GetLocalTime(&t);wchar_t name[128];
    swprintf_s(name,L"\\logs\\%04u%02u%02u_%02u%02u%02u_%03u_%lu%s",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId(),testing?L"_selftest":L"");
    g_folder=root+name;if(!CreateDirectoryW(g_folder.c_str(),nullptr))return false;
    if(!Rotate())return false;
    }
    g_tls=TlsAlloc();if(g_tls==TLS_OUT_OF_INDEXES)return false;
    Emit("START",",\"version\":\"0.20\",\"schema\":1,\"testing\":"+std::string(testing?"true":"false")+",\"exe_sha256\":"+Quote(testing?"selftest":kExpectedSha)+",\"module_base\":"+Quote(Hex(g_base))+",\"translation_scope_functions\":111,\"console_mode\":"+std::to_string(g_consoleMode)+",\"logging_enabled\":"+(g_loggingEnabled?"true":"false")+",\"console_attached\":"+(GetConsoleWindow()?"true":"false"),
         "[READY] Translation hook 0.20; external catalogue. Logs: "+Utf8(g_folder),true);
    return true;
}
static bool Install(const std::vector<TraceOverride>* testTargets) {
    if(MH_Initialize()!=MH_OK)return false;
    if(!testTargets&&!PrepareTranslation()){MH_Uninitialize();return false;}
    for(unsigned i=0;i<kHookCount;++i) {
        void* target=(void*)(GameAddress(kProfile[i].rva+0x400000));
        if(testTargets) {
            target=nullptr;
            for(const auto& x:*testTargets)if(x.preferred_va==kProfile[i].rva+0x400000){target=x.target;break;}
            if(!target)continue;
        } else for(unsigned j=0;j<16;++j)if((((BYTE*)target)[j]&kProfile[i].mask[j])!=(kProfile[i].prefix[j]&kProfile[i].mask[j])) {
            Emit("INSTALL_ERROR",",\"function\":"+Quote(Name(i))+",\"reason\":\"entry_bytes_differ\"","[ERROR] Entry bytes differ: "+std::string(Name(i)),true);MH_Uninitialize();return false;
        }
        MH_STATUS status=MH_CreateHook(target,kDetours[i],&g_trampolines[i]);
        if(status!=MH_OK){Emit("INSTALL_ERROR",",\"function\":"+Quote(Name(i))+",\"status\":"+Quote(MH_StatusToString(status)),"[ERROR] Cannot hook "+std::string(Name(i))+": "+MH_StatusToString(status),true);MH_Uninitialize();return false;}
        g_installed[i]=true;
    }
    if(!testTargets&&g_translationEnabled){
        g_menuDataBase=GameAddress(0x659CEE8);g_menuFormatTarget=GameAddress(0x4010D0);
        auto* target=(BYTE*)(GameAddress(0x4010D0));
        const BYTE expected[]={0x55,0x8B,0xEC,0x8D,0x45,0x10,0x50,0x51,0xFF,0x75,0x0C,0x8B,0x4D,0x08,0xE8};
        if(memcmp(target,expected,sizeof(expected))!=0){Emit("INSTALL_ERROR",",\"function\":\"sub_4010D0\",\"reason\":\"menu_source_monitor_entry_bytes_differ\"","[ERROR] Read-only menu source monitor entry bytes differ",true);MH_Uninitialize();return false;}
        if(MH_CreateHook(target,(void*)&MenuFormatMonitor,&g_menuFormatOriginal)!=MH_OK){MH_Uninitialize();return false;}
    }
    MH_STATUS status=MH_EnableHook(MH_ALL_HOOKS);
    if(status!=MH_OK){MH_Uninitialize();return false;}
    unsigned count=0;for(bool enabled:g_installed)if(enabled)++count;
    Emit("HOOKS_INSTALLED",",\"count\":"+std::to_string(count)+",\"menu_source_monitor\":"+(g_menuFormatOriginal?"true":"false")+",\"font_hook\":"+(g_originalFopen?"true":"false")+",\"translation_enabled\":"+(g_translationEnabled?"true":"false"),"[READY] Text hooks installed: "+std::to_string(count)+(g_originalFopen?" + font redirect":""),true);return true;
}
extern "C" DWORD WINAPI InitializeTrace(void* parameter) {
    if(InterlockedCompareExchange(&g_state,1,0)!=0)return 0;
    auto* cfg=(InitConfig*)parameter;
    if(!cfg||cfg->size!=sizeof(InitConfig))return 0;
    if(cfg->output_codepage&&cfg->output_codepage!=1251&&cfg->output_codepage!=1252)return 0;
    g_outputCodePage=cfg->output_codepage?cfg->output_codepage:1251;
    if(Sha256File(ModulePath())!=kExpectedSha)return 0;
    g_base=(DWORD)GetModuleHandleW(nullptr);
    auto* dos=(IMAGE_DOS_HEADER*)g_base;auto* nt=(IMAGE_NT_HEADERS32*)(g_base+dos->e_lfanew);
    if(nt->OptionalHeader.SizeOfImage!=kExpectedImageSize)return 0;
    if(!OpenLog(cfg->console_pid,false))return 0;
    try {if(!Install(nullptr))return 0;}catch(...){return 0;}
    InterlockedExchange(&g_state,2);return 1;
}
bool InstallTraceTests(const std::vector<TraceOverride>& targets) {
    if(InterlockedCompareExchange(&g_state,1,0)!=0)return false;
    if(!OpenLog(0,true))return false;return Install(&targets);
}
void RemoveTraceTests(){MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();if(g_log!=INVALID_HANDLE_VALUE)FlushFileBuffers(g_log);}
TraceStats ReadTraceStats(){auto* s=(ThreadState*)TlsGetValue(g_tls);return {g_entered,g_left,g_texts,g_consumed,g_abandoned,g_failures,s?s->depth:0};}
bool EnableTraceTranslationTests(const std::wstring& root) {
    g_translationTest=true;g_translationEnabled=g_catalog.Initialize(root)&&g_catalog.GetCodePage()==g_outputCodePage;return g_translationEnabled;
}
bool ReloadTraceTranslationTests(){bool ok=g_catalog.Reload(true);RefreshCatalog();return ok;}
LONG ReadTraceTranslationCount(){return g_translated;}
bool PrepareTracePresentationTests() {
    if(!g_testing||!g_translationTest)return false;
    void* image=VirtualAlloc(nullptr,0xA300000,MEM_RESERVE,PAGE_NOACCESS);if(!image)return false;
    g_base=(DWORD)image;
    auto commit=[](DWORD address,size_t count){DWORD page=address&~4095U;return VirtualAlloc((void*)page,((address-page+count+4095)&~4095U),MEM_COMMIT,PAGE_READWRITE)!=nullptr;};
    if(!commit(GameAddress(0x65B8BA8),sizeof(g_assemblyView))||!commit(GameAddress(0x65A5A18),sizeof(g_messageView))||!commit(GameAddress(0xA622874),sizeof(g_tokenView)+4))return false;
    for(const auto& p:AllPresentationOperands()) {
        DWORD address=GameAddress(p.va);
        if(!commit(address,4)||!StoreWord(address,GameAddress(p.expected)))return false;
    }
    if(!InstallPresentationOperands())return false;
    for(const auto& p:AllPresentationOperands()) {
        DWORD expected=p.buffer==1?(DWORD)g_messageView+p.delta:p.buffer==2?(DWORD)g_tokenView:p.buffer==4?(DWORD)g_assemblyView:(DWORD)&g_tokenCounter;
        if(ReadWord(GameAddress(p.va))!=expected)return false;
    }
    return true;
}
bool SetTracePresentationBuffer(bool stream,const std::string& bytes) {
    if(!g_testing||!g_base||bytes.size()>1023)return false;
    if(stream) {
        memset((void*)(GameAddress(0xA622878)),0,sizeof(g_tokenView));
        memcpy((void*)(GameAddress(0xA622878)),bytes.c_str(),bytes.size()+1);
        StoreWord(GameAddress(0xA622874),1);
    } else memcpy((void*)(GameAddress(0x65A5A18)),bytes.c_str(),bytes.size()+1);
    return true;
}
DWORD SetTraceJournalEntryTests(unsigned index,const std::string& metadata,const std::string& payload) {
    if(!g_testing||!g_translationTest||!g_base||index>=70||metadata.size()>=100||payload.size()>=1024)return 0;
    DWORD meta=GameAddress(0x65A36F0)+index*100,body=GameAddress(0x65A7310)+index*1024;
    auto commit=[](DWORD address,size_t count){DWORD page=address&~4095U;return VirtualAlloc((void*)page,((address-page+count+4095)&~4095U),MEM_COMMIT,PAGE_READWRITE)!=nullptr;};
    if(!commit(meta,100)||!commit(body,1024))return 0;
    memset((void*)meta,0,100);memcpy((void*)meta,metadata.c_str(),metadata.size());
    memset((void*)body,0,1024);memcpy((void*)body,payload.c_str(),payload.size());
    return SetTracePresentationBuffer(true,payload)?meta:0;
}
std::string ReadTracePresentationBuffer(bool stream,bool original) {
    if(!g_testing)return {};
    const char* text=stream?(original?(const char*)(GameAddress(0xA622878)):g_tokenView):(original?(const char*)(GameAddress(0x65A5A18)):g_messageView);
    return std::string(text);
}

std::string ReadTraceAssemblyBuffer(bool original) {
    if(!g_testing)return {};
    return std::string(original?(const char*)(GameAddress(0x65B8BA8)):g_assemblyView);
}
void SetTracePrivateAssemblyBuffer(const std::string& bytes) {
    if(g_testing&&bytes.size()<sizeof(g_assemblyView))memcpy(g_assemblyView,bytes.c_str(),bytes.size()+1);
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH)g_dll=module;
    // No MinHook operations, CRT logging, waits or lock acquisition here.
    if(reason==DLL_PROCESS_DETACH&&g_log!=INVALID_HANDLE_VALUE)CloseHandle(g_log);
    return TRUE;
}
