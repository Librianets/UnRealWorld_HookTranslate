// Offline inspection: exercise the same complete encyclopedia builder as the hook.
#include "TranslationCatalog.h"
#include "EncyclopediaFile.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    if(argc!=4){std::cerr<<"CatalogDocuments root language_folder output.nfo\n";return 2;}
    urwtranslation::TranslationCatalog catalog;
    if(!catalog.Initialize(argv[1],argv[2])){std::cerr<<catalog.LastError()<<'\n';return 1;}
    auto documents=catalog.GetDocuments();size_t enabled=0,aliases=0;
    for(const auto& d:documents){
        if(d.enabled)++enabled;size_t dots=0;
        for(size_t i=0;i+1<d.headerHex.size();i+=2)if(d.headerHex.substr(i,2)=="2e"||d.headerHex.substr(i,2)=="2E")++dots;
        if(dots)aliases+=dots-1;
    }
    std::string bytes,error;
    if(!urwtranslation::BuildEncyclopedia(documents,bytes,error,catalog.GetCodePage())){
        std::cerr<<error<<'\n';return 1;
    }
    std::ofstream output(std::filesystem::path(argv[3]),std::ios::binary);
    output.write(bytes.data(),bytes.size());
    if(!output){std::cerr<<"Cannot save encyclopedia inspection output\n";return 1;}
    std::cout<<"{\"documents\":"<<documents.size()<<",\"enabled\":"<<enabled
             <<",\"aliases\":"<<aliases<<",\"bytes\":"<<bytes.size()
             <<",\"codepage\":"<<catalog.GetCodePage()<<"}\n";
    return 0;
}
