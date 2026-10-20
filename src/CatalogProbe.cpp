// Read-only batch lookup using the exact production matching engine.
// Input TSV: caller VA, target VA, UTF-8 source encoded as hexadecimal.
// Optional fourth column: comma-separated branch:UTF8-label-hex identities
// from explicit fixture state (name, description, invalid); not text inference.
// Output TSV: matched (0/1), rule ID, UTF-8 result encoded as hexadecimal.
#include "TranslationCatalog.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <chrono>
static std::string Decode(const std::string& s) {
    if(s.size()%2)throw std::runtime_error("Odd hex input");
    std::string out;
    for(size_t i=0;i<s.size();i+=2) {
        size_t used=0;unsigned value=std::stoul(s.substr(i,2),&used,16);
        if(used!=2)throw std::runtime_error("Invalid hex input");
        out+=static_cast<char>(value);
    }
    return out;
}
static std::string Encode(const std::string& s) {
    static const char digits[]="0123456789abcdef";
    std::string out;for(unsigned char c:s){out+=digits[c>>4];out+=digits[c&15];}return out;
}
int main(int argc,char** argv) {
    if(argc<4||argc>6){std::cerr<<"CatalogProbe catalog_root input.tsv output.tsv [diagnostics.tsv [folder]]\n";return 2;}
    try {
        urwtranslation::TranslationCatalog catalog;
        auto root=std::filesystem::u8path(argv[1]);
        std::wstring folder=std::filesystem::exists(root/"rus_translate/catalog.json")?L"rus_translate":L"translate";
        if(argc==6)folder=std::filesystem::u8path(argv[5]).wstring();
        if(!catalog.Initialize(root.wstring(),folder))throw std::runtime_error(catalog.LastError());
        std::ifstream input(std::filesystem::u8path(argv[2]),std::ios::binary);
        std::ofstream output(std::filesystem::u8path(argv[3]),std::ios::binary);
        std::ofstream diagnostics;if(argc>=5)diagnostics.open(std::filesystem::u8path(argv[4]),std::ios::binary);
        if(!input||!output)throw std::runtime_error("Cannot open batch files");
        if(argc>=5&&!diagnostics)throw std::runtime_error("Cannot open diagnostics output");
        std::string line;size_t count=0;
        while(std::getline(input,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            size_t a=line.find('\t'),b=line.find('\t',a+1);
            if(a==std::string::npos||b==std::string::npos)throw std::runtime_error("Invalid batch row");
            uint32_t caller=std::stoul(line.substr(0,a),nullptr,16),target=std::stoul(line.substr(a+1,b-a-1),nullptr,16);
            std::string translated,rule;size_t c=line.find('\t',b+1);
            auto started=std::chrono::steady_clock::now();auto source=Decode(line.substr(b+1,c==std::string::npos?std::string::npos:c-b-1));
            std::vector<urwtranslation::NpcSourceIdentity> identities;
            std::string domain,form;bool exactOnly=c!=std::string::npos&&line.substr(c+1)=="@exact";
            if(exactOnly){}
            else if(c!=std::string::npos&&line.compare(c+1,8,"@domain:")==0){
                auto spec=line.substr(c+9);auto colon=spec.find(':');domain=spec.substr(0,colon);
                if(colon!=std::string::npos)form=spec.substr(colon+1);
            }else if(c!=std::string::npos){std::stringstream fields(line.substr(c+1));std::string field;
                while(std::getline(fields,field,',')){size_t colon=field.find(':');if(colon==std::string::npos)throw std::runtime_error("Invalid identity evidence");
                    auto branch=field.substr(0,colon);if(branch!="name"&&branch!="description"&&branch!="invalid"&&branch!="player_name")throw std::runtime_error("Invalid identity branch");
                    identities.push_back({Decode(field.substr(colon+1)),branch});}}
            bool matched=domain.empty()?catalog.Translate(source,caller,target,translated,rule,exactOnly,identities.empty()?nullptr:&identities):
                catalog.InspectDomain(source,domain,form,translated,rule);
            output<<(matched?"1":"0")<<'\t'<<rule<<'\t'<<Encode(translated)<<'\n';++count;
            if(diagnostics)diagnostics<<(matched&&translated!=source?"1":"0")<<'\t'<<rule<<'\t'<<catalog.CompactDiagnostic()<<'\t'<<std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count()<<'\t'<<Encode(source)<<'\n';
        }
        if(!output)throw std::runtime_error("Cannot write batch results");
        std::cout<<"Probed "<<count<<" inputs; catalogue unchanged.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
