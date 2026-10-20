#include "TraceTestApi.h"
#include <setjmp.h>
#include <cstring>
#include <cmath>

static volatile LONG g_bodyFailures=0;
static jmp_buf g_jump;
static void BodyCheck(bool ok){if(!ok)InterlockedIncrement(&g_bodyFailures);}

static __declspec(noinline) int __fastcall FakeLeaf(int x,int y,const char* text,int color,int outline) {
    SetLastError(0xBEEF);return x+y+color+outline+(int)strlen(text);
}
static __declspec(noinline) int __fastcall FakeDialogue(const char* text,int unused) {
    int result=FakeLeaf(1,2,text,3,4);BodyCheck(result==10+(int)strlen(text));
    SetLastError(0xCAFE);return (int)strlen(text)+unused;
}
static __declspec(noinline) int __stdcall FakeBypass(int n) {
    BodyCheck(GetLastError()==0x11223344);
    int a=FakeLeaf(10,20,"standalone",30,40);BodyCheck(GetLastError()==0xBEEF);
    int b=FakeLeaf(10,20,"second draw",30,40);return a+b+n;
}
static __declspec(noinline) int __cdecl FakeWhole() {return FakeDialogue("One complete sentence.",0);}
static __declspec(noinline) int __cdecl FakeSplit() {
    int a=FakeDialogue("What I have now is",0);
    int b=FakeDialogue("not quite enough",0);
    int c=FakeDialogue("to make me happy.",0);return a+b+c;
}
static __declspec(noinline) unsigned __int64 __stdcall FakeWide(DWORD low,DWORD high) {return ((unsigned __int64)high<<32)|low;}
static __declspec(noinline) double __cdecl FakeFloat(double value) {return value+0.25;}
static __declspec(naked) void FakeOpaque() {
    __asm nop
    __asm nop
    __asm nop
    __asm nop
    __asm nop
    __asm nop
    __asm ret
}
static __declspec(naked) unsigned __int64 __stdcall FakeTail(DWORD,DWORD) {__asm jmp FakeWide}
static __declspec(noinline) int __cdecl FakeRecursive(int n) {
    if(n==0)return 9;
    int(__cdecl *volatile next)(int)=FakeRecursive;return next(n-1)+1;
}
static __declspec(noinline) void __cdecl FakeJump(){longjmp(g_jump,1);}
#pragma warning(push)
// Deliberate C-style longjmp test; no C++ objects cross this helper.
#pragma warning(disable:4611)
static void ExerciseLongJump(){if(setjmp(g_jump)==0)FakeJump();}
#pragma warning(pop)
static DWORD WINAPI Parallel(void*) {
    for(int i=0;i<20;++i){SetLastError(0x11223344);if(FakeBypass(i)!=221+i)return 0;}
    return ReadTraceStats().depth==0?1:0;
}

extern "C" DWORD WINAPI RunTraceSelfTest(void*) {
    std::vector<std::pair<std::string,bool>> checks;
    auto check=[&](const char* name,bool ok){checks.emplace_back(name,ok);};
    std::vector<TraceOverride> targets={
        {0x50B9D0,(void*)&FakeLeaf},{0x530DC0,(void*)&FakeDialogue},
        {0x502B60,(void*)&FakeBypass},{0x531B40,(void*)&FakeWhole},{0x5322F0,(void*)&FakeSplit},
        {0x4F62A0,(void*)&FakeWide},{0x4DE590,(void*)&FakeFloat},{0x511AC0,(void*)&FakeOpaque},
        {0x4F6400,(void*)&FakeTail},{0x4F6550,(void*)&FakeRecursive},{0x4F6680,(void*)&FakeJump}
    };
    bool installed=InstallTraceTests(targets);check("install_native_test_hooks",installed);
    if(installed) {
        SetLastError(0x11223344);check("stdcall_and_fastcall_arguments_and_return",FakeBypass(7)==228);
        check("callee_last_error_preserved",GetLastError()==0xBEEF);
        check("whole_dialogue_return",FakeWhole()==22);
        check("split_dialogue_return",FakeSplit()==51);
        check("wide_EDX_EAX_return",FakeWide(0x89abcdef,0x12345678)==0x1234567889abcdefULL);
        check("x87_return",FakeFloat(3.0)==3.25);
        check("tail_call_continuation",FakeTail(0xaabbccdd,0x12344321)==0x12344321aabbccddULL);
        check("recursive_continuations",FakeRecursive(12)==21);
        __declspec(align(16)) DWORD before[4]={0x12345678,0x1234ABCD,0xFEEDFACE,0x99887766};
        __declspec(align(16)) DWORD after[4]={};DWORD flags=0;double x87=0;
        SetLastError(0x55667788);
        __asm movdqu xmm7, before
        __asm fld1
        __asm std
        __asm stc
        __asm call FakeOpaque
        __asm pushfd
        __asm pop flags
        __asm cld
        __asm movdqu after, xmm7
        __asm fstp x87
        check("XMM_registers_preserved",memcmp(before,after,sizeof(before))==0);
        check("x87_stack_preserved",x87==1.0);
        check("carry_and_direction_flags_preserved",(flags&0x401)==0x401);
        check("opaque_last_error_preserved",GetLastError()==0x55667788);
        ExerciseLongJump();
        SetLastError(0x11223344);check("longjmp_continuation_recovery",FakeBypass(1)==222&&ReadTraceStats().depth==0);
        HANDLE threads[3];for(auto& t:threads)t=CreateThread(nullptr,0,Parallel,nullptr,0,nullptr);
        bool parallel=true;
        for(HANDLE t:threads){DWORD result=0;if(!t||WaitForSingleObject(t,10000)!=WAIT_OBJECT_0||!GetExitCodeThread(t,&result)||result!=1)parallel=false;if(t)CloseHandle(t);}
        check("thread_local_frames",parallel);
        check("filtered_menu_source_monitor_preserves_original_formatter",TestMenuSourceMonitor());
        check("menu_selection_private_composition_known_preserve_and_identity",TestMenuSelectionComposition());
        TraceStats stats=ReadTraceStats();check("markers_consumed",stats.consumed>0);
        check("unwound_frame_detected",stats.abandoned==1);
        check("all_continuations_accounted_for",stats.entered==stats.left+stats.abandoned&&stats.depth==0);
        check("no_callback_failures",stats.failures==0&&g_bodyFailures==0);
        RemoveTraceTests();
        check("unhook_restores_original_execution",FakeWide(7,8)==0x800000007ULL);
    }
    unsigned failures=0;for(const auto& c:checks)if(!c.second)++failures;
    std::wstring path=Directory(ModulePath(GetModuleHandleW(L"rus_URWTraceHook.dll")))+L"\\SELFTEST.json";
    FILE* out=nullptr;_wfopen_s(&out,path.c_str(),L"wb");
    if(out){fprintf(out,"{\"checks\":%u,\"failures\":%u,\"results\":[\n",(unsigned)checks.size(),failures);for(size_t i=0;i<checks.size();++i)fprintf(out,"%s{\"name\":\"%s\",\"passed\":%s}\n",i?",":"",checks[i].first.c_str(),checks[i].second?"true":"false");fprintf(out,"]}\n");fclose(out);}
    return failures?0:1;
}
