#include "TranslationCatalog.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
int main(int argc,char** argv){
    if(argc!=2)return 2;std::filesystem::path folder=std::filesystem::u8path(argv[1]);
    urwtranslation::TranslationCatalog catalog;unsigned checked=0,failed=0;
    auto check=[&](bool value,const char* name){++checked;if(!value){++failed;std::cerr<<"FAIL "<<name<<" ("<<catalog.LastError()<<")\n";}};
    check(catalog.Initialize(folder.wstring(),L"rus_translate"),"compact+legacy initialize");
    auto query=[&](const std::string& source,const std::string& expected,unsigned caller){std::string out,id;bool ok=catalog.Translate(source,caller,0x50B9D0,out,id);check(ok&&out==expected&&id.rfind("compact:",0)==0,source.c_str());};
    query("spear","копьё",0);query("2 spears","2 копья",0x1000);query("5 spears","5 копий",0x1000);query("21 spears","21 копьё",0x1000);query("22 knives","22 ножа",0x1000);
    query("fine spear","добротное копьё",0x2000);query("fine knife","добротный нож",0x2000);
    query("2 fine spears","2 добротных копья",0x4000);query("5 fine knives","5 добротных ножей",0x4000);
    query("Knife meets Knife.","Knife встречает себя.",0x3000);query("in the north","на севере",0x5000);query("Off text","Off text",0);
    for(const auto& q:std::vector<std::pair<std::string,unsigned>>{{"0 spears",0x1000},{"-2 spears",0x1000},{"10000 spears",0x1000},{"two spears",0x1000},{"2 invented-weapon",0x1000},{"Sami meets Erkka.",0x3000},{"2 spear",0x1000}}){std::string out,id;check(!catalog.Translate(q.first,q.second,0x50B9D0,out,id),q.first.c_str());}
    std::atomic<bool> okay{true};std::vector<std::thread> threads;
    for(int i=0;i<4;++i)threads.emplace_back([&](){for(int j=0;j<300;++j){std::string out,id;if(!catalog.Translate("2 fine spears",0x4000,0x50B9D0,out,id)||out!="2 добротных копья")okay=false;}});
    for(auto& t:threads)t.join();check(okay,"compact cache thread safety");
    const auto path=folder/"rus_translate/catalog.json";std::ifstream in(path,std::ios::binary);std::string original((std::istreambuf_iterator<char>(in)),{});in.close();
    auto write=[&](const std::string& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;};
    const std::string aliasMarker="\"translation\":\"нож\"";
    auto aliased=original;auto aliasAt=aliased.find(aliasMarker);
    if(aliasAt!=std::string::npos){
        aliased.replace(aliasAt,aliasMarker.size(),"\"source_aliases\":[\"native knife\"],"+aliasMarker);
        write(aliased);check(catalog.Reload(true),"display alias metadata loads");query("native knife","нож",0);
        write(original);check(catalog.Reload(true),"display alias fixture restored");
    }else check(false,"display alias fixture marker");
    auto changed=original;const std::string find="\"translation\":\"нож\"";auto at=changed.find(find);check(at!=std::string::npos,"hot reload fixture marker");
    if(at!=std::string::npos){changed.replace(at,find.size(),"\"translation\":\"клинок\"");write(changed);check(catalog.Reload(true),"target edit reload");query("knife","клинок",0);}
    write("{broken}");auto generation=catalog.GetStats().generation;check(!catalog.Reload(true),"broken compact JSON rejected");query("knife","клинок",0);check(catalog.GetStats().generation==generation,"old compact snapshot retained");
    write(original);check(catalog.Reload(true),"valid compact restored");query("knife","нож",0);
    std::cout<<"CompactCatalog: "<<checked-failed<<"/"<<checked<<" passed\n";return failed?1:0;
}
