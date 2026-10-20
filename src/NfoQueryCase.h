#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
// Encyclopedia UI lookup only; caller storage and game resource keys unchanged.
inline bool UpperNfoQuery(const std::string& source,unsigned cp,std::string& output) {
    output.clear();if(source.empty()||source.size()>190||source.find('\0')!=std::string::npos)return false;
    int count=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,source.data(),(int)source.size(),nullptr,0);
    if(count<=0)return false;std::wstring wide(count,L'\0');
    if(!MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,source.data(),(int)source.size(),&wide[0],count))return false;
    if(CharUpperBuffW(&wide[0],count)!=(DWORD)count)return false;
    BOOL replaced=FALSE;count=WideCharToMultiByte(cp,WC_NO_BEST_FIT_CHARS,wide.data(),(int)wide.size(),nullptr,0,nullptr,&replaced);
    if(count<=0||count>190||replaced)return false;output.resize(count);replaced=FALSE;
    return WideCharToMultiByte(cp,WC_NO_BEST_FIT_CHARS,wide.data(),(int)wide.size(),&output[0],count,nullptr,&replaced)>0&&!replaced;
}
