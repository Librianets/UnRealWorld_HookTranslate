#include "EncyclopediaFile.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace urwtranslation {
namespace {
constexpr size_t kLineLimit = 90;
constexpr size_t kWordLimit = 128;
constexpr DWORD kPreferredBase = 0x400000;
constexpr DWORD kOldSynopsis = 0x6D2178;
constexpr size_t kSynopsisBytes = 200000;
constexpr DWORD kInstructions[] = {0x43CA5B, 0x43D1EB};
constexpr BYTE kOpcodes[][2] = {{0x8D,0x88},{0x81,0xC6}};
std::mutex synopsisMutex;
// Allocations intentionally live until process exit: native instructions keep
// pointers to them even when a caller releases its C++ translation catalogue.
std::unordered_map<std::uintptr_t, void*> synopsisTables;

bool Fail(std::string& error, const std::string& message) { error = message; return false; }

bool DecodeUtf8(const std::string& input,std::wstring& wide,std::string& error) {
    wide.clear();
    if(input.find('\0')!=std::string::npos)return Fail(error,"NUL in encyclopedia text");
    if(input.empty())return true;
    if(input.size()>static_cast<size_t>(INT_MAX))return Fail(error,"Encyclopedia text too large");
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0);
    if(!count)return Fail(error,"Invalid UTF-8 encyclopedia text");
    wide.resize(count);
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),&wide[0],count))
        return Fail(error,"Cannot decode UTF-8 encyclopedia text");
    return true;
}

bool ToLegacy(const std::string& input,unsigned codePage,std::string& output,std::string& error) {
    output.clear();std::wstring wide;
    if(!DecodeUtf8(input,wide,error))return false;
    if(wide.empty())return true;
    BOOL substituted=FALSE;
    int count=static_cast<int>(wide.size());
    int bytes=WideCharToMultiByte(codePage,WC_NO_BEST_FIT_CHARS,wide.data(),count,nullptr,0,nullptr,&substituted);
    if(!bytes||substituted)return Fail(error,"Encyclopedia character is not representable in CP"+std::to_string(codePage));
    output.resize(bytes);
    if(!WideCharToMultiByte(codePage,WC_NO_BEST_FIT_CHARS,wide.data(),count,&output[0],bytes,nullptr,&substituted)||substituted)
        return Fail(error,"Encyclopedia character is not representable in CP"+std::to_string(codePage));
    error.clear();return true;
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r') continue;
        lines.push_back(text.substr(start,i-start));
        if (text[i]=='\r' && i+1<text.size() && text[i+1]=='\n') ++i;
        start = i+1;
    }
    if (start < text.size()) lines.push_back(text.substr(start));
    return lines;
}

std::string Trim(const std::string& text) {
    size_t start=text.find_first_not_of(" \t");
    if(start==std::string::npos)return {};
    size_t end=text.find_last_not_of(" \t");
    return text.substr(start,end-start+1);
}

bool Comment(const std::string& line) { return line.rfind("//",0)==0 || line.rfind("##",0)==0; }

bool RemoveTerminal(std::vector<std::string>& lines, std::string& error) {
    while (!lines.empty() && Trim(lines.back()).empty()) lines.pop_back();
    for (size_t i=0;i<lines.size();++i) {
        if(lines[i]=="***") {
            // Published NFO contains historical // editor notes after the end
            // marker. These non-rendered comments may remain before our one
            // generated terminator; actual prose after a marker is invalid.
            for(size_t j=i+1;j<lines.size();++j)
                if(!Trim(lines[j]).empty() && !Comment(lines[j]))
                    return Fail(error,"Unexpected article terminator inside encyclopedia body");
            lines.erase(lines.begin()+i);break;
        }
    }
    for (const auto& line:lines) {
        if (!line.empty() && line[0]=='.') return Fail(error,"New article header inside encyclopedia body");
    }
    return true;
}

bool Controls(const std::vector<std::string>& lines, std::vector<std::string>& controls, std::string& error) {
    for (size_t i=0; i<lines.size(); ++i) {
        const auto& line=lines[i];
        if (!line.empty() && line[0]=='%') {
            if(i+1==lines.size() || Trim(lines[i+1]).empty())return Fail(error,"Missing resource name after encyclopedia control");
            controls.push_back(line);controls.push_back(lines[++i]);
        }
    }
    return true;
}

bool WrapLine(const std::string& input,std::vector<std::string>& result,std::string& error) {
    std::string line;
    size_t position=0;
    while (position<input.size()) {
        position=input.find_first_not_of(" \t",position);
        if(position==std::string::npos)break;
        size_t end=input.find_first_of(" \t",position);
        if(end==std::string::npos)end=input.size();
        std::string word=input.substr(position,end-position);
        if(word.size()>kWordLimit)return Fail(error,"Encyclopedia word exceeds 128 CP1251 bytes");
        // Do not silently cut a resource/link/word into different game tokens.
        if(word.size()>kLineLimit)return Fail(error,"Encyclopedia word cannot fit 90-byte physical line");
        if(!line.empty() && line.size()+1+word.size()>kLineLimit) { result.push_back(line);line.clear(); }
        if(!line.empty())line+=' ';
        line+=word;position=end;
    }
    if(!line.empty())result.push_back(line);
    else result.emplace_back();
    return true;
}

bool BodyLines(const std::vector<std::string>& input,std::vector<std::string>& result,std::string& error) {
    for(size_t i=0;i<input.size();++i) {
        const auto& line=input[i];
        if(!line.empty() && line[0]=='%') {
            if(line.size()>kLineLimit || i+1==input.size() || input[i+1].size()>kLineLimit)
                return Fail(error,"Encyclopedia resource control/name exceeds physical line limit");
            result.push_back(line);result.push_back(input[++i]);
        } else if(Comment(line)) {
            if(line.size()>kLineLimit)return Fail(error,"Encyclopedia comment exceeds physical line limit");
            result.push_back(line);
        } else {
            size_t first=result.size();
            if(!WrapLine(line,result,error))return false;
            for(size_t j=first;j<result.size();++j) {
                // Wrapping may otherwise make a prose word look like a header,
                // control or comment. Escaping with a space is native-safe.
                auto& wrapped=result[j];
                if(!wrapped.empty() && (wrapped[0]=='.' || wrapped[0]=='%' || Comment(wrapped) || wrapped=="***")) {
                    if(wrapped.size()==kLineLimit)return Fail(error,"Wrapped encyclopedia control-like line needs shortening");
                    wrapped.insert(wrapped.begin(),' ');
                }
            }
        }
    }
    return true;
}

int Hex(char c) {
    if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;
}

bool Header(const std::string& hex,std::string& header,size_t& aliases,std::string& error) {
    if(hex.empty() || hex.size()%2 || hex.size()>380)return Fail(error,"Encyclopedia header exceeds 190 bytes or has invalid hex length");
    header.clear();
    for(size_t i=0;i<hex.size();i+=2) {
        int high=Hex(hex[i]),low=Hex(hex[i+1]);
        if(high<0||low<0)return Fail(error,"Invalid encyclopedia header hex");
        header+=static_cast<char>((high<<4)|low);
    }
    if(header.size()<3 || header.front()!='.' || header.back()!='.' || header.find_first_of("\r\n\0",0,3)!=std::string::npos)
        return Fail(error,"Malformed encyclopedia header");
    size_t start=1;
    while(start<header.size()) {
        size_t end=header.find('.',start);
        if(end==std::string::npos || end==start)return Fail(error,"Empty encyclopedia alias");
        ++aliases;start=end+1;
    }
    return true;
}

bool Readable(const void* pointer,size_t size) {
    auto address=reinterpret_cast<std::uintptr_t>(pointer);
    if(address>UINTPTR_MAX-size)return false;
    auto end=address+size;
    while(address<end) {
        MEMORY_BASIC_INFORMATION info={};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        std::uintptr_t next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+static_cast<std::uintptr_t>(info.RegionSize);
        if(next<=address)return false;address=(std::min)(next,end);
    }
    return true;
}
}

bool BuildEncyclopedia(const std::vector<DocumentEntry>& documents,std::string& bytes,std::string& error,unsigned codePage) {
    bytes.clear();error.clear();
    if(documents.empty())return Fail(error,"Encyclopedia documents are empty");
    std::string complete;size_t aliases=0;
    for(const auto& document:documents) {
        std::string header,body;
        auto failTopic=[&](){error=document.id+" ("+document.topic+"): "+error;return false;};
        if(document.file!="GAME.NFO") { error="Unsupported encyclopedia document file";return failTopic(); }
        if(!Header(document.headerHex,header,aliases,error))return failTopic();
        if(aliases>1000) {error="Encyclopedia alias table exceeds 1000 entries";return failTopic();}
        const std::string& selected=document.enabled?document.translation:document.source;
        std::wstring sourceUnicode;
        if(!DecodeUtf8(document.source,sourceUnicode,error))return failTopic();
        // A Finnish letter in English prose must not make a valid Russian
        // translation fail. Compare resource controls before byte encoding.
        auto sourceLines=Lines(document.source),selectedLines=Lines(selected);
        if(!RemoveTerminal(sourceLines,error)||!RemoveTerminal(selectedLines,error))return failTopic();
        std::vector<std::string> oldControls,newControls;
        if(!Controls(sourceLines,oldControls,error)||!Controls(selectedLines,newControls,error))return failTopic();
        if(oldControls!=newControls) {error="Encyclopedia resource controls/names differ from source";return failTopic();}
        if(!ToLegacy(selected,codePage,body,error)) {
            // A disabled article preserves original CP437 English bytes when
            // they are not representable in the selected Russian font codepage.
            if(document.enabled || !ToLegacy(selected,437,body,error))return failTopic();
        }
        auto translatedLines=Lines(body);
        if(!RemoveTerminal(translatedLines,error))return failTopic();
        std::vector<std::string> output;
        if(!BodyLines(translatedLines,output,error))return failTopic();
        output.push_back("***");
        size_t synopsisBytes=2,physical=0;
        for(const auto& line:output) {
            if(Comment(line))continue;
            synopsisBytes+=line.size();
            if(++physical==2)break;
        }
        if(synopsisBytes>199) {error="First two encyclopedia lines exceed synopsis capacity";return failTopic();}
        complete+=header+"\r\n";
        for(const auto& line:output)complete+=line+"\r\n";
        complete+="\r\n";
    }
    bytes.swap(complete);return true;
}

bool InstallSynopsisTable(std::uintptr_t imageBase,std::string& error) {
    error.clear();
    if constexpr(sizeof(void*)!=4)return Fail(error,"Synopsis redirection requires x86 process");
    if(imageBase>UINT32_MAX-(kOldSynopsis-kPreferredBase))return Fail(error,"Synopsis image base overflows x86 address");
    std::lock_guard<std::mutex> lock(synopsisMutex);
    BYTE* instructions[2];DWORD expected=static_cast<DWORD>(imageBase+kOldSynopsis-kPreferredBase);
    auto installed=synopsisTables.find(imageBase);
    if(installed!=synopsisTables.end())expected=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(installed->second));
    for(size_t i=0;i<2;++i) {
        instructions[i]=reinterpret_cast<BYTE*>(imageBase+kInstructions[i]-kPreferredBase);
        if(!Readable(instructions[i],6))return Fail(error,"Synopsis instruction is not readable");
        DWORD current=0;memcpy(&current,instructions[i]+2,sizeof(current));
        if(memcmp(instructions[i],kOpcodes[i],2) || current!=expected)
            return Fail(error,"Synopsis instruction or relocated original base differs");
    }
    if(installed!=synopsisTables.end())return true;
    void* table=VirtualAlloc(nullptr,kSynopsisBytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!table)return Fail(error,"Cannot allocate replacement encyclopedia synopsis table");
    DWORD replacement=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(table));
    DWORD protection[2]={};size_t writable=0;
    for(;writable<2;++writable) {
        if(!VirtualProtect(instructions[writable],6,PAGE_EXECUTE_READWRITE,&protection[writable]))break;
    }
    if(writable!=2) {
        for(size_t i=0;i<writable;++i){DWORD unused=0;VirtualProtect(instructions[i],6,protection[i],&unused);}
        VirtualFree(table,0,MEM_RELEASE);return Fail(error,"Cannot make synopsis operands writable");
    }
    // Reserve bookkeeping before publishing native pointers (allocation may throw).
    try { synopsisTables.emplace(imageBase,table); }
    catch(...) {
        for(size_t i=0;i<2;++i){DWORD unused=0;VirtualProtect(instructions[i],6,protection[i],&unused);}
        VirtualFree(table,0,MEM_RELEASE);return Fail(error,"Cannot record replacement synopsis table");
    }
    for(size_t i=0;i<2;++i)memcpy(instructions[i]+2,&replacement,sizeof(replacement));
    FlushInstructionCache(GetCurrentProcess(),instructions[0],6);
    FlushInstructionCache(GetCurrentProcess(),instructions[1],6);
    bool restored=true;
    for(size_t i=0;i<2;++i){DWORD unused=0;if(!VirtualProtect(instructions[i],6,protection[i],&unused))restored=false;}
    // Both native operands already target valid persistent memory. Never free a
    // published table because a protection restoration reported a failure.
    return restored || Fail(error,"Synopsis redirected but original page protection could not be restored");
}
}
