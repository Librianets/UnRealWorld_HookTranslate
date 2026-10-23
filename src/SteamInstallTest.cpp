#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SteamInstall.h"
#include <filesystem>
#include <iostream>
using namespace SteamInstall;
static int count=0;
static void Check(bool v,const char* name){std::cout<<(v?"PASS ":"FAIL ")<<name<<std::endl;if(!v)throw std::runtime_error(name);++count;}
static void Put(const std::wstring& p,const char* s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<s;}
int wmain(){
 wchar_t temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);
 std::wstring r=std::wstring(temp)+L"URWSteamTest_"+std::to_wstring(GetCurrentProcessId());
 if(!CreateDirectoryW(r.c_str(),nullptr))return 2;
 try{
  auto live=r+L"\\urw.exe",orig=r+L"\\urw-original.exe",master=r+L"\\URWTrace.exe";
  Put(live,"game v1");Put(master,"loader v1");std::string one=Hash(live);GameHash=one.c_str();
  Check(Install(r)==Result::Okay,"initial install");
  Put(live,"game v2");std::string two=Hash(live);GameHash=two.c_str();Put(master,"loader v2");
  Check(Install(r)==Result::Okay,"supported update repair");
  Check(Hash(orig)==two&&Hash(live)==Hash(master),"new original replaces old; latest loader installed");
  Check(Hash(r+L"\\urw-original."+W(one)+L".exe")==one,"previous game preserved under its hash");
  Put(live,"game v2");SetFileAttributesW(orig.c_str(),FILE_ATTRIBUTE_READONLY);
  Check(Install(r)==Result::IO,"readonly destination interrupts repair safely");
  Check(Hash(live)==two&&Hash(orig)==two,"failed repair preserves current original and backup");
  SetFileAttributesW(orig.c_str(),FILE_ATTRIBUTE_NORMAL);
  Check(Install(r)==Result::Okay,"retry after interruption");
  {
   Lock other(r);Check(other.okay(),"exclusive lock acquired");
   Check(Install(r)==Result::Busy&&Restore(r)==Result::Busy,"parallel installation and rollback blocked");
  }
  Check(!Exists(r+L"\\URWSteam.lock"),"lock file removed automatically");
  auto state=Read(r);Put(orig,"unexpected backup");
  Check(Restore(r)==Result::Collision,"corrupted original never installed");
  Check(Hash(live)==Hash(master),"collision leaves deployed executable unchanged");
  Put(orig,"game v2");
  Check(Restore(r)==Result::Okay,"normal rollback");
  Check(Hash(live)==two&&!Managed(r)&&!Exists(orig),"latest original restored exactly");
  Check(!Exists(live+L".steam-tmp")&&!Exists(StatePath(r)+L".tmp"),"no transaction temp leftovers");
  std::cout<<"TOTAL "<<count<<std::endl;std::filesystem::remove_all(r);return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
