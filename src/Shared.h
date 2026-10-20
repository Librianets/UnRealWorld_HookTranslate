#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>

// Language is selected by the bootstrap; source text always stays CP1252.
// Zero keeps the Russian default for internal diagnostic callers.
struct InitConfig { DWORD size; DWORD console_pid; DWORD output_codepage; };

inline std::wstring ModulePath(HMODULE module = nullptr) {
    wchar_t path[32768] = {};
    DWORD n = GetModuleFileNameW(module, path, 32768);
    return n && n < 32768 ? std::wstring(path, n) : std::wstring();
}
inline std::wstring Directory(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/"); return p == std::wstring::npos ? L"." : path.substr(0,p);
}
inline std::string Sha256File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE;
    if (ok) ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    BYTE buffer[65536]; DWORD got;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, nullptr)) { ok = false; break; }
        if (!got) break;
        if (!CryptHashData(hash, buffer, got, 0)) ok = false;
    }
    BYTE digest[32]; DWORD length = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) != FALSE;
    if (hash) CryptDestroyHash(hash); if (provider) CryptReleaseContext(provider, 0); CloseHandle(file);
    std::string result;
    if (ok) for (BYTE value : digest) { char pair[3]; sprintf_s(pair, "%02x", value); result += pair; }
    return result;
}
