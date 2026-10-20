#include "EncyclopediaFile.h"
#include "NfoQueryCase.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

using urwtranslation::DocumentEntry;
namespace {
std::vector<std::pair<std::string,bool>> checks;
size_t productionDocuments=0,productionAliases=0,productionBytes=0;
std::string productionCatalogSHA,productionNfoSHA,productionBuildError;
void Check(bool okay,const char* name){checks.emplace_back(name,okay);if(!okay)printf("FAIL %s\n",name);}
std::string Hex(const std::string& raw){std::string result;for(unsigned char c:raw){char pair[3];sprintf_s(pair,"%02X",c);result+=pair;}return result;}
DocumentEntry Doc(const std::string& body="Body.") {
    DocumentEntry d;d.id="test";d.file="GAME.NFO";d.topic="TEST";d.headerHex=Hex(".TEST.");d.source=body;d.translation=body;d.enabled=true;return d;
}
std::string Utf8(const std::string& bytes) {
    if(bytes.empty())return {};
    int size=MultiByteToWideChar(1251,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    std::wstring wide(size,L'\0');MultiByteToWideChar(1251,0,bytes.data(),static_cast<int>(bytes.size()),&wide[0],size);
    size=WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),&out[0],size,nullptr,nullptr);return out;
}
std::wstring Directory(const std::wstring& path){size_t pos=path.find_last_of(L"\\/");return path.substr(0,pos);}
std::wstring ModulePath(){wchar_t path[32768]={};GetModuleFileNameW(nullptr,path,32768);return path;}
bool Build(const DocumentEntry& doc,std::string& bytes,std::string& error){return urwtranslation::BuildEncyclopedia({doc},bytes,error);}
size_t Count(const std::string& text,const std::string& needle){size_t count=0,pos=0;while((pos=text.find(needle,pos))!=std::string::npos){++count;pos+=needle.size();}return count;}

void TextTests(){
    std::string query;
    Check(UpperNfoQuery("\xEA\xEE\xEF\xFC\xB8",1251,query)&&query=="\xCA\xCE\xCF\xDC\xA8","Cyrillic encyclopedia query uses uppercase private copy");
    Check(UpperNfoQuery("s\xE4\xE4",1252,query)&&query=="S\xC4\xC4","Finnish encyclopedia query preserves diacritics");
    Check(UpperNfoQuery("cloth",1251,query)&&query=="CLOTH","English topic keys normalize without translation");

    std::string output,error;auto d=Doc();
    Check(Build(d,output,error)&&output==".TEST.\r\nBody.\r\n***\r\n\r\n","CP1251 CRLF canonical document");
    d.translation=Utf8("\xCA\xEE\xEF\xFC\xE5");
    Check(Build(d,output,error)&&output.find("\xCA\xEE\xEF\xFC\xE5")!=std::string::npos,"Cyrillic encoded CP1251");
    d.enabled=false;Check(Build(d,output,error)&&output.find("Body.")!=std::string::npos,"disabled document preserves English source");
    d=Doc("Tiet\xC3\xA4j\xC3\xA4 means sage.");d.translation="Russian prose.";
    Check(Build(d,output,error)&&output.find("Russian prose.")!=std::string::npos,"Finnish source does not prevent CP1251 Russian article");
    d.enabled=false;Check(Build(d,output,error)&&output.find("Tiet\x84j\x84 means sage.")!=std::string::npos,"disabled Tietaja source retains exact CP437 bytes");
    d=Doc("Russian source.");d.source=Utf8("\xCC\xF3\xE4\xF0\xE5\xF6");d.enabled=false;
    Check(Build(d,output,error)&&output.find("\xCC\xF3\xE4\xF0\xE5\xF6")!=std::string::npos,"disabled Russian-only article still prefers CP1251");
    d=Doc("English source.");d.source="\xF0\x9F\x98\x80";d.enabled=false;
    Check(!Build(d,output,error),"disabled source unsupported by CP1251 and CP437 rejects without substitution");
    d=Doc("First.\r\nSecond.\n***\r\n\r\n");
    Check(Build(d,output,error)&&Count(output,"***")==1,"existing terminal marker normalized once");
    d=Doc("First.\n***\nSecond.");Check(!Build(d,output,error)&&output.empty(),"interior terminator rejected transactionally");
    d=Doc("First.\n***\n\n// historical editor note\n");Check(Build(d,output,error)&&Count(output,"***")==1,"historical comment after terminal marker retained safely");
    d=Doc("Text.\n.INJECTED.\nOther.");Check(!Build(d,output,error),"injected header rejected");
    d=Doc("Text % not a control.");Check(Build(d,output,error),"inline percentage preserved");
    d=Doc("First.\n%POP\nimage-name.bmp\nAfter.");Check(Build(d,output,error)&&output.find("%POP\r\nimage-name.bmp\r\n")!=std::string::npos,"POP directive and resource preserved exactly");
    d.translation="First.\n%POP\nwrong.bmp\nAfter.";Check(!Build(d,output,error),"changed resource rejected");
    d.translation="First.\n%POP%\nimage-name.bmp\nAfter.";Check(!Build(d,output,error),"changed control rejected");
    d=Doc("%POP");Check(!Build(d,output,error),"missing resource rejected");
    d=Doc("%POP%\nimage\nText.");Check(Build(d,output,error),"alternate POP spelling retained");
    d=Doc("//comment\n##comment\nFirst sentence.\nSecond sentence.");Check(Build(d,output,error),"comments and synopsis text supported");
    d=Doc(std::string(90,'x'));Check(Build(d,output,error),"90-byte word fits physical line");
    d=Doc(std::string(91,'x'));Check(!Build(d,output,error),"91-byte indivisible token rejected");
    d=Doc(std::string(129,'x'));Check(!Build(d,output,error)&&error.find("128")!=std::string::npos,"129-byte word rejected by native word bound");
    std::string longText;for(unsigned i=0;i<80;++i)longText+="word ";d=Doc(longText);bool wrapped=Build(d,output,error);
    size_t pos=output.find("\r\n")+2;while(wrapped&&pos<output.size()){size_t end=output.find("\r\n",pos);if(end==std::string::npos){wrapped=false;break;}if(end-pos>90)wrapped=false;pos=end+2;}
    Check(wrapped,"long prose wraps below 91 bytes");
    d=Doc(std::string(89,'x')+" .word");Check(Build(d,output,error)&&output.find("\r\n .word\r\n")!=std::string::npos,"wrapping cannot introduce native header");
    d=Doc("First.\n%POP\n"+std::string(91,'a'));Check(!Build(d,output,error),"resource token never silently wrapped");
    d=Doc();d.translation=std::string("bad\0text",8);Check(!Build(d,output,error),"embedded NUL rejected");
    d=Doc();d.translation="\xF0\x9F\x98\x80";Check(!Build(d,output,error),"unrepresentable Unicode rejected");
    d=Doc();d.translation="\xC0\xAF";Check(!Build(d,output,error),"invalid UTF8 rejected");
    d=Doc();d.headerHex=Hex("."+std::string(188,'A')+".");Check(Build(d,output,error),"190-byte native header accepted");
    d.headerHex=Hex("."+std::string(189,'A')+".");Check(!Build(d,output,error),"191-byte native header rejected");
    d=Doc();d.headerHex="2E4142Z2";Check(!Build(d,output,error),"nonhex header rejected");
    d=Doc();d.headerHex=Hex(".TEST\nEVIL.");Check(!Build(d,output,error),"newline in raw header rejected");
    d=Doc();d.headerHex=Hex(std::string(".TE\0ST.",7));Check(!Build(d,output,error),"NUL in raw header rejected");
    d=Doc();d.headerHex=Hex(".TEST");Check(!Build(d,output,error),"unclosed header rejected");
    d=Doc();d.headerHex=Hex(".TEST..OTHER.");Check(!Build(d,output,error),"empty header alias rejected");
    std::vector<DocumentEntry> documents(1000,Doc());Check(urwtranslation::BuildEncyclopedia(documents,output,error),"1000 aliases fit native keyword capacity");
    documents.push_back(Doc());Check(!urwtranslation::BuildEncyclopedia(documents,output,error)&&output.empty(),"1001 aliases rejected before publishing");
    Check(!urwtranslation::BuildEncyclopedia({},output,error),"empty encyclopedia rejected");
}

std::vector<BYTE*> fakeImages;
BYTE* FakeImage(std::uintptr_t preferred){
    constexpr size_t size=0x2F0000;
    BYTE* image=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(preferred),size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)image=static_cast<BYTE*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return nullptr;
    memset(image,0xA5,size);DWORD oldBase=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(image)+0x2D2178);
    BYTE lea[]={0x8D,0x88,0,0,0,0,0x8B,0xC1,0xC3};
    BYTE add[]={0x81,0xC6,0,0,0,0,0x8B,0xC6,0xC3};
    memcpy(lea+2,&oldBase,4);memcpy(add+2,&oldBase,4);
    memcpy(image+0x3CA5B,lea,sizeof(lea));memcpy(image+0x3D1EB,add,sizeof(add));
    fakeImages.push_back(image);return image;
}
DWORD Operand(BYTE* image,size_t rva){DWORD result=0;memcpy(&result,image+rva,4);return result;}

void ImageTests(){
    std::string error;
    BYTE* first=FakeImage(0x03000000);BYTE* second=FakeImage(0x13000000);
    Check(first&&second&&first!=second,"two distinct fake ASLR images allocated");
    if(!first||!second)return;
    for(BYTE* image:{first,second}) {
        auto base=reinterpret_cast<std::uintptr_t>(image);
        std::vector<BYTE> snapshot(image,image+0x2F0000);
        bool installed=urwtranslation::InstallSynopsisTable(base,error);
        Check(installed,"verified fake image installs replacement synopsis");
        if(!installed){printf("Install error: %s\n",error.c_str());continue;}
        DWORD pointer=Operand(image,0x3CA5D);
        Check(pointer==Operand(image,0x3D1ED)&&pointer!=base+0x2D2178,"both native operands point at replacement");
        BYTE* table=reinterpret_cast<BYTE*>(pointer);
        bool zero=true;for(size_t i=0;i<200000;++i)if(table[i]){zero=false;break;}
        Check(zero,"all 200000 replacement bytes initially zero");
        bool neighbors=true;
        for(size_t i=0;i<snapshot.size();++i) {
            if((i>=0x3CA5D&&i<0x3CA61)||(i>=0x3D1ED&&i<0x3D1F1))continue;
            if(image[i]!=snapshot[i]){neighbors=false;break;}
        }
        Check(neighbors,"original code data old table and adjacent inventory preserved");
        void* leaFunction=image+0x3CA5B;void* addFunction=image+0x3D1EB;DWORD leaResult=0,addResult=0;DWORD index=983*200;
        __asm mov eax,index
        __asm call leaFunction
        __asm mov leaResult,eax
        __asm push esi
        __asm mov esi,index
        __asm call addFunction
        __asm mov addResult,eax
        __asm pop esi
        Check(leaResult==pointer+index&&addResult==pointer+index,"native LEA and ADD use relocated replacement for alias 983");
        for(size_t i=0;i<984;++i){char description[32];sprintf_s(description,"synopsis_%u",static_cast<unsigned>(i));strcpy_s(reinterpret_cast<char*>(table+i*200),200,description);}
        Check(strcmp(reinterpret_cast<char*>(table+983*200),"synopsis_983")==0,"all 984 published aliases have independent writable synopses");
        bool unused=true;for(size_t i=984*200;i<200000;++i)if(table[i])unused=false;
        Check(unused,"unused synopsis entries remain zero");
        Check(urwtranslation::InstallSynopsisTable(base,error)&&Operand(image,0x3CA5D)==pointer&&strcmp(reinterpret_cast<char*>(table+983*200),"synopsis_983")==0,"reinstall idempotent without zeroing existing descriptions");
    }
    BYTE* bad=FakeImage(0x23000000);Check(bad!=nullptr,"bad-instruction fake image allocated");
    if(bad){bad[0x3D1EB]=0x90;std::vector<BYTE> snapshot(bad,bad+0x2F0000);Check(!urwtranslation::InstallSynopsisTable(reinterpret_cast<std::uintptr_t>(bad),error)&&memcmp(bad,snapshot.data(),snapshot.size())==0,"wrong second opcode rejects before changing first operand");}
    BYTE* wrong=FakeImage(0x33000000);Check(wrong!=nullptr,"bad-pointer fake image allocated");
    if(wrong){wrong[0x3CA5D]^=1;std::vector<BYTE> snapshot(wrong,wrong+0x2F0000);Check(!urwtranslation::InstallSynopsisTable(reinterpret_cast<std::uintptr_t>(wrong),error)&&memcmp(wrong,snapshot.data(),snapshot.size())==0,"wrong ASLR operand rejects without mutations");}
    Check(!urwtranslation::InstallSynopsisTable(0,error),"unreadable fake image rejected");
}

std::string SHA256(const std::string& bytes){
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;std::string result;
    bool okay=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE;
    if(okay)okay=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    if(okay)okay=CryptHashData(hash,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),0)!=FALSE;
    BYTE digest[32]={};DWORD size=sizeof(digest);
    if(okay)okay=CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)!=FALSE;
    if(okay)for(BYTE byte:digest){char pair[3];sprintf_s(pair,"%02x",byte);result+=pair;}
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);return result;
}
void ProductionCatalogTest(const std::wstring& root){
    urwtranslation::TranslationCatalog catalog;
    std::wstring folder=GetFileAttributesW((root+L"\\rus_translate\\catalog.json").c_str())!=INVALID_FILE_ATTRIBUTES?L"rus_translate":L"translate";
    bool loaded=catalog.Initialize(root,folder);Check(loaded,"production translation catalogue loads read-only");
    if(!loaded){productionBuildError=catalog.LastError();printf("Production load error: %s\n",productionBuildError.c_str());return;}
    auto documents=catalog.GetDocuments();productionDocuments=documents.size();
    for(const auto& document:documents){
        size_t separators=0;
        for(size_t i=0;i+1<document.headerHex.size();i+=2){
            std::string byte=document.headerHex.substr(i,2);
            if(byte=="2e"||byte=="2E")++separators;
        }
        if(separators)productionAliases+=separators-1;
    }
    Check(productionDocuments==313,"GetDocuments production topic count313");
    Check(productionAliases>=313&&productionAliases<=1000,"GetDocuments production aliases fit3.89 native capacity");
    std::string output,error;
    bool built=urwtranslation::BuildEncyclopedia(documents,output,error);
    productionBuildError=error;productionBytes=output.size();productionNfoSHA=SHA256(output);
    Check(built,"GetDocuments production encyclopedia builds entirely");
    if(!built)printf("Production build error: %s\n",error.c_str());
    else Check(Count(output,"***\r\n")==313,"GetDocuments production retains every article");
    FILE* input=nullptr;_wfopen_s(&input,(root+L"\\"+folder+L"\\catalog.json").c_str(),L"rb");
    if(input){std::string bytes;char block[65536];size_t got=0;while((got=fread(block,1,sizeof(block),input))!=0)bytes.append(block,got);fclose(input);productionCatalogSHA=SHA256(bytes);}
    Check(productionCatalogSHA.size()==64&&productionNfoSHA.size()==64,"production catalogue and generated bytes SHA256 computed");
    printf("Production encyclopedia: documents=%u aliases=%u bytes=%u SHA256=%s\n",static_cast<unsigned>(productionDocuments),static_cast<unsigned>(productionAliases),static_cast<unsigned>(productionBytes),productionNfoSHA.c_str());
}

void PublishedEncyclopediaTest(const std::wstring& root){
    std::wstring path=root+L"\\..\\..\\..\\work\\translate\\3.88\\exe\\GAME.NFO";
    FILE* stream=nullptr;_wfopen_s(&stream,path.c_str(),L"rb");
    if(!stream){printf("SKIP optional legacy GAME.NFO comparison (not included in standalone hook).\n");return;}
    Check(true,"existing translated encyclopedia opened read-only");
    std::string file;char buffer[4096];size_t got=0;while((got=fread(buffer,1,sizeof(buffer),stream))!=0)file.append(buffer,got);fclose(stream);
    std::vector<DocumentEntry> documents;DocumentEntry* current=nullptr;size_t aliases=0,pos=0;
    while(pos<file.size()) {
        size_t end=file.find('\n',pos);if(end==std::string::npos)end=file.size();
        std::string line=file.substr(pos,end-pos);if(!line.empty()&&line.back()=='\r')line.pop_back();pos=end+1;
        if(!line.empty()&&line[0]=='.') {
            DocumentEntry d;d.id="existing_"+std::to_string(documents.size());d.file="GAME.NFO";d.topic=d.id;d.headerHex=Hex(line);d.enabled=true;documents.push_back(d);current=&documents.back();
            aliases+=Count(line,".")-1;
        }else if(current){current->source+=Utf8(line)+"\n";current->translation=current->source;}
    }
    Check(documents.size()==292&&aliases==984,"existing encyclopedia has 292 topics and 984 aliases");
    std::string output,error;bool built=urwtranslation::BuildEncyclopedia(documents,output,error);
    Check(built,"entire existing encyclopedia accepted by new bounded builder");if(!built)printf("Existing encyclopedia error: %s\n",error.c_str());
    if(built)Check(Count(output,"***\r\n")==292,"all existing articles retained with one terminator each");
}
}

int wmain(int argc,wchar_t** argv){
    TextTests();ImageTests();std::wstring root=argc>1?argv[1]:Directory(ModulePath());PublishedEncyclopediaTest(root);
    std::wstring catalogRoot=root;
    if(GetFileAttributesW((root+L"\\rus_translate\\catalog.json").c_str())==INVALID_FILE_ATTRIBUTES&&GetFileAttributesW((root+L"\\translate\\catalog.json").c_str())==INVALID_FILE_ATTRIBUTES)
        catalogRoot=root+L"\\..\\UnRealWorld";
    ProductionCatalogTest(catalogRoot);
    unsigned failures=0;for(const auto& check:checks)if(!check.second)++failures;
    FILE* report=nullptr;_wfopen_s(&report,(root+L"\\ENCYCLOPEDIA_SELFTEST.json").c_str(),L"wb");
    if(report){fprintf(report,"{\"checks\":%u,\"failures\":%u,\"production_documents\":%u,\"production_aliases\":%u,\"production_bytes\":%u,\"production_catalog_sha256\":\"%s\",\"production_nfo_sha256\":\"%s\",\"results\":[\n",static_cast<unsigned>(checks.size()),failures,static_cast<unsigned>(productionDocuments),static_cast<unsigned>(productionAliases),static_cast<unsigned>(productionBytes),productionCatalogSHA.c_str(),productionNfoSHA.c_str());for(size_t i=0;i<checks.size();++i)fprintf(report,"%s{\"name\":\"%s\",\"passed\":%s}\n",i?",":"",checks[i].first.c_str(),checks[i].second?"true":"false");fprintf(report,"]}\n");fclose(report);}
    // These are synthetic process allocations, never mappings of a game EXE.
    for(BYTE* image:fakeImages)VirtualFree(image,0,MEM_RELEASE);
    printf("Encyclopedia native self-test: %u/%u passed. See ENCYCLOPEDIA_SELFTEST.json\n",static_cast<unsigned>(checks.size())-failures,static_cast<unsigned>(checks.size()));
    return failures?1:0;
}
