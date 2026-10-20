#pragma once
// Pure, read-only model of4AB2B0(flags=2), including4AB270's local effect.
// Input/output strings are bounded native-codepage bytes. The presentation
// owner converts the completed label toUTF-8 before passing catalog context.
// No game calls, memory reads, global writes or name translation occur here.
#include "TranslationCatalog.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <limits>

namespace urwtranslation {
struct NpcRecordSnapshot {
    std::int32_t record_id = -2;
    std::uint8_t class_id = 0;
    std::array<char,40> type{};   //614E968,40bytes up toNAME614E990
    std::array<char,16> name{};   //614E990,15characters plusNUL
    std::uint8_t name_flags = 0; //614EB3C bit1known,bit2called
    std::uint8_t sex_age = 0;    //614EA2F bit1male,bit2juvenile
    std::int32_t prototype_id = 0; //614EA1C
    bool debug_names = false;   //A6CFE04:human names bypass knowledge
};

namespace npc_source_identity_detail {
template<std::size_t N>
inline bool Bounded(const std::array<char,N>& bytes,std::string& value) {
    auto end=std::find(bytes.begin(),bytes.end(),'\0');
    if(end==bytes.end())return false;
    value.assign(bytes.begin(),end);return true;
}
inline char LowerAscii(char value) {
    return value>='A'&&value<='Z'?static_cast<char>(value+'a'-'A'):value;
}
inline void LowerFirst(std::string& text) {
    if(!text.empty())text[0]=LowerAscii(text[0]);
}
}

inline bool BuildNpcFlags2Identity(const NpcRecordSnapshot& record,NpcSourceIdentity& output) {
    output.label.clear();output.branch.clear();
    if(record.record_id==-1) { output.label="human";output.branch="invalid";return true; }
    if(record.record_id<0||record.record_id>=500)return false;
    std::string base,name;
    if(!npc_source_identity_detail::Bounded(record.type,base)||
       !npc_source_identity_detail::Bounded(record.name,name))return false;
    auto flags=record.name_flags;
    //4AB270 removes a legacy stored "' the " descriptor prefix and setsknown.
    //Apply this to local copies only;4F3990 snapshots normally see it alreadydone.
    auto legacy=base.find("' the ");
    if(legacy!=std::string::npos) { base=base.substr(legacy+6);flags|=1; }
    //Forflags2, bothknown andcalled overwrite the entire result with rawNAME.
    //The called branch has no "called" suffix becauseflags4 isclear.
    if((flags&3)!=0||(record.class_id==1&&record.debug_names)) {
        if(name.empty())return false;
        output.label=name;output.branch="name";return true;
    }
    if(base.empty())return false;
    std::string type=base;
    if(record.class_id!=1) {
        npc_source_identity_detail::LowerFirst(type);
        const bool male=(record.sex_age&1)!=0;
        const auto species=record.prototype_id;
        if(male) {
            if(species==660)type="bull "+type;
            if(species==616)type="ram";
            if(species==615)type="boar";
            if(species==619||species==620)type+=" stag";
        } else {
            if(species==605)type="female "+type;
            if(species==615)type="sow";
            if(species==618)type="female dog";
            if(species==619||species==620)type+=" doe";
        }
        //Juvenile replacement resets from the originalbase, not its sex label.
        if((record.sex_age&2)!=0) {
            switch(species) {
                case 606:case 619:case 620:case 660:type=base+" calf";break;
                case 613:case 614:case 615:type="piglet";break;
                case 616:type="lamb";break;
                case 618:type=base+" puppy";break;
                default:type=base+" cub";break;
            }
            npc_source_identity_detail::LowerFirst(type);
        }
    }
    output.label="the "+type;output.branch="description";return true;
}
//Reader(address,destination,byteCount) must returnfalse on any inaccessible
//range. All addresses are rebased preferredVAs; recordid is always an index.
template<class Reader>
inline bool ReadNpcFlags2Identity(std::int32_t recordId,std::uintptr_t imageBase,
                                 Reader reader,NpcSourceIdentity& output) {
    output.label.clear();output.branch.clear();
    if(recordId==-1) {
        NpcRecordSnapshot invalid;invalid.record_id=-1;
        return BuildNpcFlags2Identity(invalid,output);
    }
    if(recordId<0||recordId>=500)return false;
    constexpr std::uintptr_t preferredBase=0x00400000;
    constexpr std::uintptr_t classVA=0x6151400;
    constexpr std::uintptr_t debugVA=0xA6D28A0;
    constexpr std::uintptr_t maximumOffset=debugVA-preferredBase+sizeof(std::uint32_t);
    if(imageBase>std::numeric_limits<std::uintptr_t>::max()-maximumOffset)return false;
    const std::uintptr_t address=imageBase+(classVA-preferredBase)+
                                 static_cast<std::uintptr_t>(recordId)*1384;
    NpcRecordSnapshot record;record.record_id=recordId;
    std::uint32_t debug=0;
    if(!reader(address,&record.class_id,sizeof(record.class_id))||
       !reader(address+8,record.type.data(),record.type.size())||
       !reader(address+0x30,record.name.data(),record.name.size())||
       !reader(address+0x1DC,&record.name_flags,sizeof(record.name_flags))||
       !reader(address+0xCF,&record.sex_age,sizeof(record.sex_age))||
       !reader(address+0xBC,&record.prototype_id,sizeof(record.prototype_id))||
       !reader(imageBase+(debugVA-preferredBase),&debug,sizeof(debug)))return false;
    record.debug_names=debug!=0;
    return BuildNpcFlags2Identity(record,output);
}
} //namespaceurwtranslation
