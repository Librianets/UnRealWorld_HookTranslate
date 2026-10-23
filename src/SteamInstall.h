#pragma once
// Shared, reversible Steam entry-point installation. No changes to game bytes.
#include <windows.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <string>
#include <fstream>
#include <cstdio>
#pragma comment(lib,"advapi32.lib")
namespace SteamInstall {
inline const char* GameHash="c6ed73642c1b1cb1f86b818c7586224398c2f0554f6114ced31bc40ff071806d";
inline bool Exists(const std::wstring& p){return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
inline std::string Hash(const std::wstring& p){
 HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
 if(f==INVALID_HANDLE_VALUE)return {};
 HCRYPTPROV c=0;HCRYPTHASH h=0;bool ok=CryptAcquireContextW(&c,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=0;
 if(ok)ok=CryptCreateHash(c,CALG_SHA_256,0,0,&h)!=0;
 BYTE b[65536],d[32];DWORD n=0,size=32;
 while(ok){if(!ReadFile(f,b,sizeof(b),&n,nullptr)){ok=false;break;}if(!n)break;ok=CryptHashData(h,b,n,0)!=0;}
 if(ok)ok=CryptGetHashParam(h,HP_HASHVAL,d,&size,0)!=0;
 if(h)CryptDestroyHash(h);if(c)CryptReleaseContext(c,0);CloseHandle(f);
 std::string r;if(ok)for(BYTE v:d){char x[3];sprintf_s(x,"%02x",v);r+=x;}return r;
}
inline std::wstring W(const std::string& s){return std::wstring(s.begin(),s.end());}
struct State{std::string loader,previous,game;bool valid=false;};
inline bool Digest(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline std::wstring StatePath(const std::wstring& r){return r+L"\\URWSteam.state";}
inline bool Managed(const std::wstring& r){return Exists(StatePath(r));}
inline State Read(const std::wstring& r){
 State s;std::ifstream f(StatePath(r));std::string tag;f>>tag>>s.loader>>s.previous>>s.game;
 s.valid=tag=="URWSteam1"&&Digest(s.loader)&&(s.previous=="-"||Digest(s.previous))&&Digest(s.game);return s;
}
inline bool Own(const State& s,const std::string& h){return s.valid&&!h.empty()&&(h==s.loader||h==s.previous);}
inline bool Write(const std::wstring& r,const State& s){
 auto p=StatePath(r),t=p+L".tmp";HANDLE f=CreateFileW(t.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
 if(f==INVALID_HANDLE_VALUE)return false;
 std::string text="URWSteam1\n"+s.loader+"\n"+s.previous+"\n"+s.game+"\n";DWORD n=0;
 bool ok=WriteFile(f,text.data(),(DWORD)text.size(),&n,nullptr)&&n==text.size()&&FlushFileBuffers(f);CloseHandle(f);
 if(ok)ok=MoveFileExW(t.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(t.c_str());return ok;
}
struct Lock{
 HANDLE h=INVALID_HANDLE_VALUE;
 explicit Lock(const std::wstring& r){h=CreateFileW((r+L"\\URWSteam.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);}
 ~Lock(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
 bool okay()const{return h!=INVALID_HANDLE_VALUE;}
 Lock(const Lock&)=delete;Lock& operator=(const Lock&)=delete;
};
inline bool Running(const std::wstring& r){
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return true;
 PROCESSENTRY32W e={};e.dwSize=sizeof(e);bool busy=false;
 for(BOOL more=Process32FirstW(snap,&e);more;more=Process32NextW(snap,&e)){
  if(e.th32ProcessID==GetCurrentProcessId())continue;
  if(_wcsicmp(e.szExeFile,L"urw.exe")&&_wcsicmp(e.szExeFile,L"urw-original.exe")&&_wcsicmp(e.szExeFile,L"URWTrace.exe"))continue;
  HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID);
  if(!p){busy=true;break;}wchar_t path[32768];DWORD len=32768;
  if(!QueryFullProcessImageNameW(p,0,path,&len))busy=true;
  else{std::wstring v=path;auto i=v.find_last_of(L"\\/");busy=i!=std::wstring::npos&&_wcsicmp(v.substr(0,i).c_str(),r.c_str())==0;}
  CloseHandle(p);if(busy)break;
 }CloseHandle(snap);return busy;
}
enum class Result{Okay,Busy,InvalidState,Missing,Unsupported,Collision,IO};
inline bool CopyAtomic(const std::wstring& source,const std::wstring& dest){
 auto temp=dest+L".steam-tmp";if(!CopyFileW(source.c_str(),temp.c_str(),TRUE))return false;
 bool ok=Hash(source)==Hash(temp)&&MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 if(!ok)DeleteFileW(temp.c_str());return ok;
}
inline bool Archive(const std::wstring& r,const std::wstring& original,const std::string& hash){
 if(!Digest(hash))return false;
 auto dest=r+L"\\urw-original."+W(hash)+L".exe";
 if(Exists(dest)){if(Hash(dest)!=hash)return false;return DeleteFileW(original.c_str())!=0;}
 return MoveFileExW(original.c_str(),dest.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
}
// Caller verifies the master loader belongs to this package before invoking.
inline Result Install(const std::wstring& r){
 Lock lock(r);if(!lock.okay()||Running(r))return Result::Busy;
 auto live=r+L"\\urw.exe",orig=r+L"\\urw-original.exe",master=r+L"\\URWTrace.exe";
 State old=Read(r);bool managed=Managed(r);
 if(managed&&!old.valid)return Result::InvalidState;
 if(!managed&&Exists(orig))return Result::Collision;
 auto current=Hash(live),backup=Hash(orig),loader=Hash(master);
 if(!Digest(loader))return Result::Missing;
 bool own=Own(old,current);
 // An updater's new urw.exe is always the source, never the stale backup.
 std::string sourceHash=own?backup:current;
 if(sourceHash!=GameHash)return sourceHash.empty()?Result::Missing:Result::Unsupported;
 if(Exists(orig)&&backup!=sourceHash){
  if(!old.valid||backup!=old.game)return Result::Collision;
  if(!Archive(r,orig,backup))return Result::IO;
 }
 State next;next.loader=loader;next.previous=own?current:"-";next.game=sourceHash;next.valid=true;
 // Durable intent before renaming. Interrupted installs remain repairable.
 if(!Write(r,next))return Result::IO;
 if(!own&&!CopyAtomic(live,orig))return Result::IO;
 if(Hash(orig)!=GameHash)return Result::IO;
 if(current!=loader&&!CopyAtomic(master,live))return Result::IO;
 return Result::Okay;
}
inline Result Restore(const std::wstring& r){
 Lock lock(r);if(!lock.okay()||Running(r))return Result::Busy;
 State s=Read(r);if(!s.valid)return Result::InvalidState;
 auto live=r+L"\\urw.exe",orig=r+L"\\urw-original.exe";
 auto current=Hash(live),backup=Hash(orig);
 if(Own(s,current)||!Exists(live)){
  if(backup!=s.game)return Result::Collision;
  if(!CopyAtomic(orig,live))return Result::IO;
  current=backup;
 }
 // After Steam update leave its new executable in place. Never replace it
 // with an older backup. Keep differing backups under their SHA256 names.
 if(current.empty())return Result::IO;
 if(Exists(orig)){
  if(backup!=s.game)return Result::Collision;
  if(backup==current){if(!DeleteFileW(orig.c_str()))return Result::IO;}
  else if(!Archive(r,orig,backup))return Result::IO;
 }
 return DeleteFileW(StatePath(r).c_str())?Result::Okay:Result::IO;
}
}
