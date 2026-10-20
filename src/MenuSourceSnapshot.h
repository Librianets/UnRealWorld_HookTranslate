#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
// Read-only source identities. This header neither reads nor writes game memory.
namespace urwtranslation {
constexpr unsigned kDataMenuSlots=51,kDataMenuVisibleRows=25;
inline bool IsDataMenuScope(std::uint32_t va){return va==0x4FDD50||va==0x4FD710;}
constexpr std::uintptr_t kDataMenuStride=524,kDrawMenuStride=232;
inline std::string BeforeMenuHelp(const std::string& s){return s.substr(0,s.find('|'));}
inline std::string NativeMenuClip(const std::string& s){return s.size()>34?s.substr(0,32)+"..":s;}
inline std::string NativeUpperCaption(std::string s){
    if(s.size()>500)s.resize(500);
    for(char& byte:s){unsigned char c=(unsigned char)byte;if(c>='a'&&c<='z')c-=32;
        if(c==0x84||c==0x8E||c==0xE4)c=0xC4;else if(c==0x94||c==0x99||c==0xF6)c=0xD6;byte=(char)c;}
    return s;
}
inline std::string NativeArmourClip(const std::string& s){return s.size()>=26?s.substr(0,24)+"..":s;}
// The native selection uses actual stored (possibly clipped) DATA, not full.
inline std::string NativeMenuSelection(const std::string& title,const std::string& record){
    auto dot=record.find('.');if(dot==std::string::npos)return {};
    std::string out="#"+title+": "+record.substr(dot+1);
    if(out.find("NO DISPLAY")!=std::string::npos)return {};
    return BeforeMenuHelp(out);
}
inline std::string NativeMenuSelectionView(std::string text,bool preserve_case){
    if(text.size()>1020)text.resize(1020); //4F3990 strncpy(...,0x3FC)
    if(preserve_case)return text;
    bool cap=true;
    for(size_t i=0;i<text.size();++i){
        unsigned char c=(unsigned char)text[i];
        if(cap){if(c>='a'&&c<='z')text[i]=(char)(c-32);cap=std::string("#!+?:&,=_()1234567890 ").find(text[i])!=std::string::npos;}
        else if(c=='.'||c=='!'||c==':'||c=='?')cap=c!='.'||i+1>=text.size()||text[i+1]<'0'||text[i+1]>'9';
    }
    return text;
}
struct NativeMenuRecord{std::string bytes;std::uint32_t key=0;};
struct MenuSourceFrame{
    struct Row{bool valid=false,native_clipped=false;std::uint32_t key=0,origin=0,formatter_site=0;std::string expected,full,stored;};
    std::array<Row,kDataMenuSlots> rows{};
    std::uintptr_t menu_base=0;unsigned count=0;
    bool FullRecord(std::uintptr_t pointer,const std::string& current,std::uint32_t key,std::string& full,std::uint32_t* origin=nullptr,bool* was_clipped=nullptr)const{
        if(pointer<menu_base)return false;
        auto delta=pointer-menu_base;
        if(delta%kDrawMenuStride||delta/kDrawMenuStride>=count)return false;
        const auto& row=rows[delta/kDrawMenuStride];
        if(!row.valid||row.key!=key||current!=row.expected||row.full==current)return false;
        full=row.full;if(origin)*origin=row.origin;if(was_clipped)*was_clipped=row.native_clipped;return true;
    }
};
// Lookup must resolve ONLY the specified source identity; it reports both
// known-preserved fragments and changed fragments. No unknown-text fallback.
template<class Lookup>
bool BuildMenuSelectionView(const MenuSourceFrame& frame,unsigned index,
    const std::string& title,const std::string& stored,std::uint32_t key,
    const std::string& actual,bool preserveCase,Lookup lookup,
    std::string& expected,std::string& translated){
    if(index>=frame.count||title.empty())return false;
    const auto& row=frame.rows[index];
    if(!row.valid||row.key==27||row.key==(std::uint32_t)-1||row.key!=key||row.stored!=stored)return false;
    std::string native=NativeMenuSelection(title,stored);
    if(native.empty()||native!=actual||native.find('{')!=std::string::npos||native.find('}')!=std::string::npos)return false;
    auto dot=row.full.find('.');if(dot==std::string::npos||dot+1>=row.full.size())return false;
    std::string ruTitle,ruLabel;bool titleChanged=false,labelChanged=false;
    if(!lookup(title,0x4FD710,0x4FD710,ruTitle,titleChanged)||
       !lookup(row.full.substr(dot+1),row.origin,0x4FCF90,ruLabel,labelChanged)||(!titleChanged&&!labelChanged))return false;
    translated="#"+ruTitle+": "+ruLabel;
    if(translated.size()>=1024)return false;
    expected=NativeMenuSelectionView(native,preserveCase);return true;
}
class MenuSourceCache{
    std::array<std::string,kDataMenuSlots> sources{};
    std::array<bool,kDataMenuSlots> valid{};
    std::array<std::uint32_t,kDataMenuSlots> origins{},formatter_sites{};
public:
    void Erase(unsigned index){if(index<kDataMenuSlots){valid[index]=false;sources[index].clear();}}
    void Observe(unsigned index,const std::string& full,std::uint32_t origin,std::uint32_t formatter_site=0){
        Erase(index);if(index>=kDataMenuSlots||!origin||full.empty()||full.size()>1023||full.find('\0')!=std::string::npos)return;
        sources[index]=full;origins[index]=origin;formatter_sites[index]=formatter_site;valid[index]=true;
    }
    MenuSourceFrame Freeze(std::uintptr_t menu_base,const std::vector<NativeMenuRecord>& records)const{
        MenuSourceFrame frame;frame.menu_base=menu_base;frame.count=(unsigned)(records.size()>kDataMenuVisibleRows?kDataMenuVisibleRows:records.size());
        for(unsigned i=0;i<frame.count;++i){
            if(!valid[i]||records[i].key==(std::uint32_t)-1)continue;
            const auto& full=sources[i];const auto& actual=records[i].bytes;
            // Full equality or exact native34-byte clipping, never a prefix search.
            if(actual!=full&&actual!=NativeMenuClip(full))continue;
            auto& row=frame.rows[i];row.valid=true;row.native_clipped=actual!=full;row.key=records[i].key;row.origin=origins[i];row.formatter_site=formatter_sites[i];
            row.stored=actual;row.expected=BeforeMenuHelp(actual);row.full=BeforeMenuHelp(full);
        }
        return frame;
    }
};
}
