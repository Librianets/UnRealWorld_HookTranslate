#include "TranslationCatalog.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

namespace urwtranslation {
namespace {
thread_local std::string compactDiagnostic;
const size_t kMaxInput = 4096, kMaxOutput = 65536, kMaxFile = 64 * 1024 * 1024;
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    std::string text;
    bool boolean = false;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json* Find(const char* key) const {
        auto it = object.find(key); return it == object.end() ? nullptr : &it->second;
    }
};
bool ValidUtf8(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i++];
        if (c == 0) return false;
        if (c < 0x80) continue;
        unsigned count; uint32_t cp, minimum;
        if (c >= 0xC2 && c <= 0xDF) { count = 1; cp = c & 31; minimum = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { count = 2; cp = c & 15; minimum = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { count = 3; cp = c & 7; minimum = 0x10000; }
        else return false;
        if (i + count > text.size()) return false;
        while (count--) { unsigned char d = (unsigned char)text[i++]; if ((d & 0xC0) != 0x80) return false; cp = (cp << 6) | (d & 63); }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}
void AddCodepoint(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
}
class Parser {
    const std::string& s; size_t p = 0;
    [[noreturn]] void Fail() const { throw std::runtime_error("Invalid JSON near byte " + std::to_string(p)); }
    void Space() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p; }
    uint32_t Hex() { uint32_t n = 0; for (unsigned i = 0; i < 4; ++i) { if (p == s.size()) Fail(); char c = s[p++]; unsigned d = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : 99; if (d > 15) Fail(); n = n*16+d; } return n; }
    std::string String() {
        if (p == s.size() || s[p++] != '"') Fail();
        std::string out;
        while (p < s.size()) {
            char c = s[p++]; if (c == '"') return out;
            if ((unsigned char)c < 32) Fail();
            if (c != '\\') { out += c; continue; }
            if (p == s.size()) Fail();
            c = s[p++];
            switch (c) {
                case '"': case '\\': case '/': out += c; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': { uint32_t cp = Hex(); if (cp >= 0xD800 && cp <= 0xDBFF) { if (p+2 > s.size() || s[p++] != '\\' || s[p++] != 'u') Fail(); uint32_t low = Hex(); if (low < 0xDC00 || low > 0xDFFF) Fail(); cp = 0x10000 + ((cp-0xD800)<<10) + low-0xDC00; } else if (cp >= 0xDC00 && cp <= 0xDFFF) Fail(); AddCodepoint(out, cp); break; }
                default: Fail();
            }
        }
        Fail();
    }
    Json Value(unsigned depth) {
        if (depth > 32) Fail();
        Space(); if (p == s.size()) Fail(); Json v; char c = s[p];
        if (c == '"') { v.type = Json::String; v.text = String(); return v; }
        if (c == '{') {
            ++p; v.type = Json::Object; Space(); if (p < s.size() && s[p] == '}') { ++p; return v; }
            for (;;) { Space(); std::string k = String(); Space(); if (p == s.size() || s[p++] != ':') Fail(); Json child = Value(depth+1); if (!v.object.emplace(k, std::move(child)).second) Fail(); Space(); if (p == s.size()) Fail(); c = s[p++]; if (c == '}') return v; if (c != ',') Fail(); }
        }
        if (c == '[') {
            ++p; v.type = Json::Array; Space(); if (p < s.size() && s[p] == ']') { ++p; return v; }
            for (;;) { v.array.push_back(Value(depth+1)); Space(); if (p == s.size()) Fail(); c = s[p++]; if (c == ']') return v; if (c != ',') Fail(); }
        }
        for (const char* literal : {"true", "false", "null"}) {
            size_t n = std::char_traits<char>::length(literal);
            if (s.compare(p, n, literal) == 0) { p += n; v.type = literal[0] == 'n' ? Json::Null : Json::Bool; v.boolean = literal[0] == 't'; return v; }
        }
        size_t begin = p;
        if (s[p] == '-') ++p;
        if (p == s.size()) Fail();
        if (s[p] == '0') ++p; else { if (s[p] < '1' || s[p] > '9') Fail(); while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; }
        if (p < s.size() && s[p] == '.') { ++p; size_t start = p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; if (start == p) Fail(); }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) { ++p; if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p; size_t start = p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; if (start == p) Fail(); }
        v.type = Json::Number; v.text = s.substr(begin, p-begin); return v;
    }
public:
    explicit Parser(const std::string& input) : s(input) { if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) p = 3; }
    Json Parse() { Json out = Value(0); Space(); if (p != s.size()) Fail(); return out; }
};
std::string Field(const Json& obj, const char* key, bool required = true) {
    const Json* value = obj.Find(key); if (!value && !required) return "";
    if (!value || value->type != Json::String) throw std::runtime_error(std::string("Expected string: ") + key);
    return value->text;
}
uint32_t Address(const Json& obj, const char* name) {
    std::string s = Field(obj, name, false); if (s.empty() || s == "*") return 0;
    if (s.size() < 3 || s.size() > 10 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) throw std::runtime_error("Invalid address: " + s);
    uint32_t n = 0; for (size_t i = 2; i < s.size(); ++i) { unsigned d; char c = s[i]; if (c >= '0' && c <= '9') d = c-'0'; else if (c >= 'A' && c <= 'F') d = c-'A'+10; else if (c >= 'a' && c <= 'f') d = c-'a'+10; else throw std::runtime_error("Invalid address: " + s); n = (n<<4) | d; } return n;
}
struct Token { bool variable; std::string text; };
std::vector<Token> Tokenize(const std::string& text, std::set<std::string>& names) {
    std::vector<Token> tokens; std::string literal;
    auto flush = [&]() { if (!literal.empty()) { tokens.push_back({false,literal}); literal.clear(); } };
    for (size_t i = 0; i < text.size();) {
        char c = text[i++];
        if (c != '{' && c != '}') { literal += c; continue; }
        if (i < text.size() && text[i] == c) { ++i; literal += c; continue; }
        if (c == '}') throw std::runtime_error("Unescaped closing brace");
        size_t end = text.find('}', i); if (end == std::string::npos) throw std::runtime_error("Unclosed placeholder");
        std::string name = text.substr(i, end-i); if (name.empty() || name.size() > 64) throw std::runtime_error("Invalid placeholder");
        for (size_t j = 0; j < name.size(); ++j) { unsigned char ch = (unsigned char)name[j]; if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || (j && ch >= '0' && ch <= '9'))) throw std::runtime_error("Invalid placeholder: " + name); }
        flush(); tokens.push_back({true,name}); names.insert(name); i = end+1;
    }
    flush();
    if (tokens.size() > 64 || names.size() > 16) throw std::runtime_error("Too many template components");
    return tokens;
}
struct Entry {
    std::string id, source, translation, kind, pluralVariable;
    uint32_t caller = 0, target = 0;
    std::vector<Token> sourceTokens, translationTokens;
    std::map<std::string,std::vector<Token>> plural;
    std::map<std::string,std::string> variableNumbers;
    std::map<std::string,std::string> variableForms,literalValues;
    std::set<std::string> numericNames;
    std::set<std::string> requiredLexemes, rawNames;
    size_t specificity = 0;
    uint64_t templateRank = 0;
};
bool Number(const std::string& s) {
    size_t p = 0; if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
    size_t before = p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; if (before == p) return false;
    if (p < s.size() && s[p] == '.') { ++p; before = p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; if (p == before) return false; }
    return p == s.size();
}
std::string Plural(const std::string& n, bool finnish = false) {
    if (!Number(n) || n.find('.') != std::string::npos) return "other";
    // Finnish has no Russian 21/22/25 cycle. Whole +/-1 selects singular;
    // the catalogue supplies the actual case (often partitive) for other counts.
    if(finnish) {
        size_t start=(n[0]=='-'||n[0]=='+')?1:0;
        while(start+1<n.size()&&n[start]=='0')++start;
        return n.substr(start)=="1"?"one":"other";
    }
    // Last two digits suffice, including INT64_MIN and arbitrarily large counts.
    unsigned mod = 0; for (char c : n) if (c >= '0' && c <= '9') mod = (mod*10 + c-'0')%100;
    if (mod%10 == 1 && mod != 11) return "one";
    if (mod%10 >= 2 && mod%10 <= 4 && (mod < 12 || mod > 14)) return "few";
    return "many";
}
using Captures = std::map<std::string,std::string>;
struct Lexeme {
    std::string translation;
    std::map<std::string,std::string> forms;
    bool ambiguous = false;
};
using Lexicon = std::unordered_map<std::string,Lexeme>;
Captures Localize(const Entry& entry, const Captures& original, const Lexicon& lexicon, bool finnish = false) {
    Captures localized = original;
    for (auto& capture : localized) {
        auto literal=entry.literalValues.find(capture.first);
        if(literal!=entry.literalValues.end()){capture.second=literal->second;continue;}
        if (entry.rawNames.count(capture.first) || Number(capture.second)) continue;
        auto found = lexicon.find(capture.second);
        if (found == lexicon.end() || found->second.ambiguous) continue;
        const Lexeme& lexeme = found->second;
        capture.second = lexeme.translation;
        auto requested=entry.variableForms.find(capture.first);
        if(requested!=entry.variableForms.end()){
            auto form=lexeme.forms.find(requested->second);
            if(form!=lexeme.forms.end())capture.second=form->second;
        }
        auto number = entry.variableNumbers.find(capture.first);
        if (number != entry.variableNumbers.end()) {
            auto value = original.find(number->second);
            if (value != original.end()) {
                auto form = lexeme.forms.find(Plural(value->second,finnish));
                if (form != lexeme.forms.end()) capture.second = form->second;
            }
        }
    }
    return localized;
}
bool Match(const Entry& entry, const std::string& text, Captures& result) {
    // Most catalogued templates start with a count or end with a fixed noun.
    // Reject these cheaply before constructing any backtracking state.
    const Token& first = entry.sourceTokens.front();
    if (!first.variable && text.compare(0,first.text.size(),first.text) != 0) return false;
    if (first.variable && (first.text == "n" || entry.numericNames.count(first.text))) {
        const char c = text[0]; if ((c < '0' || c > '9') && c != '-' && c != '+') return false;
    }
    const Token& last = entry.sourceTokens.back();
    if (!last.variable && (text.size() < last.text.size() || text.compare(text.size()-last.text.size(),last.text.size(),last.text) != 0)) return false;
    size_t literalPosition = 0;
    for (const Token& token : entry.sourceTokens) if (!token.variable) {
        literalPosition = text.find(token.text,literalPosition);
        if (literalPosition == std::string::npos) return false;
        literalPosition += token.text.size();
    }
    size_t budget = 2048; unsigned count = 0; bool exhausted = false;
    Captures current;
    std::function<void(size_t,size_t)> visit = [&](size_t index, size_t position) {
        if (count > 1 || exhausted) return;
        if (!budget--) { exhausted = true; return; }
        if (index == entry.sourceTokens.size()) { if (position == text.size()) { ++count; result = current; } return; }
        const Token& token = entry.sourceTokens[index];
        if (!token.variable) { if (text.compare(position,token.text.size(),token.text) == 0) visit(index+1,position+token.text.size()); return; }
        auto found = current.find(token.text);
        if (found != current.end()) { if (text.compare(position,found->second.size(),found->second) == 0) visit(index+1,position+found->second.size()); return; }
        size_t endLimit = std::min(text.size(), position + kMaxInput);
        for (size_t end = position+1; end <= endLimit && !exhausted && count < 2; ++end) {
            if (index+1 == entry.sourceTokens.size() && end != text.size()) continue;
            if (index+1 < entry.sourceTokens.size() && !entry.sourceTokens[index+1].variable && text.compare(end,entry.sourceTokens[index+1].text.size(),entry.sourceTokens[index+1].text) != 0) continue;
            std::string value = text.substr(position,end-position);
            if (token.text == "n" || entry.numericNames.count(token.text)) { if (!Number(value)) continue; }
            current[token.text] = std::move(value); visit(index+1,end); current.erase(token.text);
        }
    };
    visit(0,0); return !exhausted && count == 1;
}
bool Expand(const std::vector<Token>& tokens, const Captures& captures, std::string& out) {
    out.clear(); for (const Token& t : tokens) {
        if (!t.variable) out += t.text;
        else { auto it = captures.find(t.text); if (it == captures.end()) return false; out += it->second; }
        if (out.size() > kMaxOutput) return false;
    }
    return ValidUtf8(out);
}
bool Stat(const std::wstring& path, uint64_t& stamp, uint64_t& size) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    stamp = ((uint64_t)data.ftLastWriteTime.dwHighDateTime<<32) | data.ftLastWriteTime.dwLowDateTime;
    size = ((uint64_t)data.nFileSizeHigh<<32) | data.nFileSizeLow;
#else
    std::string filename(path.begin(),path.end()); struct stat data;
    if (stat(filename.c_str(),&data)) return false;
    stamp = (uint64_t)data.st_mtim.tv_sec*1000000000 + data.st_mtim.tv_nsec; size = (uint64_t)data.st_size;
#endif
    return true;
}
std::string Read(const std::wstring& path) {
    FILE* file = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&file,path.c_str(),L"rb")) file = nullptr;
#else
    std::string filename(path.begin(),path.end()); file = fopen(filename.c_str(),"rb");
#endif
    if (!file) throw std::runtime_error("Cannot open catalogue JSON");
    std::string out; char block[16384]; size_t got;
    while ((got = fread(block,1,sizeof(block),file)) != 0) { if (out.size()+got > kMaxFile) { fclose(file); throw std::runtime_error("Catalogue exceeds 64 MiB"); } out.append(block,got); }
    bool failed = ferror(file) != 0; fclose(file); if (failed) throw std::runtime_error("Cannot read catalogue");
    std::string check = out.compare(0,3,"\xEF\xBB\xBF") == 0 ? out.substr(3) : out;
    if (!ValidUtf8(check)) throw std::runtime_error("Catalogue must be valid UTF-8 without NUL bytes");
    return out;
}
#include "CompactCatalog.inc"
}

struct TranslationCatalog::Snapshot {
    CatalogStats stats;
    std::vector<Entry> entries;
    std::unordered_map<std::string,std::vector<size_t>> exact;
    std::vector<size_t> templates;
    std::array<std::vector<size_t>,256> prefixTemplates;
    std::unordered_map<std::string,std::vector<size_t>> suffixTemplates;
    std::array<std::vector<size_t>,256> shortSuffixTemplates;
    std::vector<size_t> unanchoredTemplates;
    Lexicon lexicon;
    std::vector<DocumentEntry> documents;
    std::shared_ptr<CompactCatalog> compact;
};
TranslationCatalog::TranslationCatalog() : checked_(std::chrono::steady_clock::time_point::min()) {}
void TranslationCatalog::SetProgressCallback(ProgressCallback callback, void* user) {
    std::lock_guard<std::mutex> guard(mutex_); progress_ = callback; progressUser_ = user;
}
void TranslationCatalog::Progress(const char* stage, size_t completed, size_t total) noexcept {
    if (progress_) try { progress_(stage,completed,total,progressUser_); } catch (...) {}
}
bool TranslationCatalog::Initialize(const std::wstring& root, const std::wstring& folder) {
    { std::lock_guard<std::mutex> guard(mutex_); path_ = root;
      Progress("initialize_lock_acquired");
      if (!path_.empty() && path_.back() != '/' && path_.back() != '\\') path_ += L'/';
      path_ += folder;
      if (!path_.empty() && path_.back() != '/' && path_.back() != '\\') path_ += L'/';
      path_ += L"catalog.json"; observed_ = false; checked_ = std::chrono::steady_clock::time_point::min(); }
    return Reload(true);
}
bool TranslationCatalog::Reload(bool force) {
    std::lock_guard<std::mutex> guard(mutex_);
    Progress("reload_lock_acquired");
    auto now = std::chrono::steady_clock::now();
    if (!force && checked_ != std::chrono::steady_clock::time_point::min() && now-checked_ < std::chrono::seconds(1)) return lastReloadOkay_;
    checked_ = now;
    try {
        Progress("stat_begin");
        uint64_t stamp, size; if (path_.empty() || !Stat(path_,stamp,size)) throw std::runtime_error("Cannot stat catalogue JSON");
        Progress("stat_ready",(size_t)size,(size_t)size);
        // The compact editor is a second, independent input. A change in either
        // editable text or grammar publishes one new complete snapshot.
        const auto slash=path_.find_last_of(L"/\\");
        const std::wstring directory=slash==std::wstring::npos?L"":path_.substr(0,slash+1);
        const std::wstring modelPath=directory+L"runtime_grammar.json",legacyPath=directory+L"legacy_catalog.json";
        uint64_t cs=0,cz=0,gs=0,gz=0;
        bool compactPresent=Stat(legacyPath,cs,cz),grammarPresent=Stat(modelPath,gs,gz);
        uint64_t combined=stamp^(cs*1099511628211ULL)^(gs*16777619ULL),combinedSize=size+cz+gz;
        if (!force && observed_ && combined == stamp_ && combinedSize == size_) return lastReloadOkay_;
        stamp_ = combined; size_ = combinedSize; observed_ = true;
        if (size > kMaxFile) throw std::runtime_error("Catalogue exceeds 64 MiB");
        Progress("read_begin");
        std::string input = Read(path_);
        Progress("read_ready",input.size(),input.size());
        Progress("parse_begin");
        Json root = Parser(input).Parse(), compactRoot;
        Progress("parse_ready");
        if (root.type != Json::Object) throw std::runtime_error("Catalogue root must be an object");
        const Json* schema = root.Find("schema");
        const bool compactMode=schema&&schema->type==Json::Number&&schema->text=="2";
        if(compactMode){
            if(!compactPresent||!grammarPresent)throw std::runtime_error("Compact catalog requires legacy_catalog.json and runtime_grammar.json");
            compactRoot=std::move(root);root=Parser(Read(legacyPath)).Parse();schema=root.Find("schema");
        }
        if (!schema || schema->type != Json::Number || schema->text != "1") throw std::runtime_error("Unsupported catalogue schema");
        auto fresh = std::make_shared<Snapshot>(); fresh->stats.version = Field(root,"version",false);
        fresh->stats.encoding = Field(root,"encoding");
        if (fresh->stats.encoding != "cp866" && fresh->stats.encoding != "cp1251" && fresh->stats.encoding != "cp1252" && fresh->stats.encoding != "utf-8") throw std::runtime_error("Unsupported encoding metadata");
        fresh->stats.language=Field(root,"language",false);
        const Json* lexicon = root.Find("lexicon");
        Progress("lexicon_begin");
        if (lexicon) {
            if (lexicon->type != Json::Array) throw std::runtime_error("Expected lexicon array");
            for (const Json& item : lexicon->array) {
                if (item.type != Json::Object) throw std::runtime_error("Lexicon item must be an object");
                std::string source = Field(item,"source"); Lexeme lexeme; lexeme.translation = Field(item,"translation");
                if (source.empty() || source.size() > kMaxInput || lexeme.translation.empty() || lexeme.translation.size() > kMaxOutput || !ValidUtf8(source) || !ValidUtf8(lexeme.translation)) throw std::runtime_error("Invalid lexicon text");
                const Json* forms = item.Find("forms");
                if (forms) {
                    if (forms->type != Json::Object) throw std::runtime_error("Expected lexicon forms object");
                    if(fresh->stats.language=="fi") {
                        for(const auto& pair:forms->object) {
                            const auto& value=pair.second;
                            if(pair.first.empty()||pair.first.size()>64||value.type!=Json::String||value.text.empty()||value.text.size()>kMaxOutput||!ValidUtf8(value.text))throw std::runtime_error("Invalid Finnish lexicon form");
                            lexeme.forms[pair.first]=value.text;
                        }
                    }
                    for (const char* form : {"one","few","many","other","genitive"}) {
                        const Json* value = forms->Find(form); if (!value) continue;
                        if (value->type != Json::String || value->text.empty() || value->text.size() > kMaxOutput || !ValidUtf8(value->text)) throw std::runtime_error("Invalid lexicon form");
                        lexeme.forms[form] = value->text;
                    }
                }
                auto inserted = fresh->lexicon.emplace(source,lexeme);
                if (!inserted.second && (inserted.first->second.translation != lexeme.translation || inserted.first->second.forms != lexeme.forms)) inserted.first->second.ambiguous = true;
            }
        }
        Progress("lexicon_ready",fresh->lexicon.size(),fresh->lexicon.size());
        const Json* entries = root.Find("entries"); if (!entries || entries->type != Json::Array) throw std::runtime_error("Expected entries array");
        std::set<std::string> ids;
        Progress("index_begin",0,entries->array.size());
        for (const Json& item : entries->array) {
            if (item.type != Json::Object) throw std::runtime_error("Entry must be an object");
            Entry e; e.id = Field(item,"id"); if (e.id.empty() || !ids.insert(e.id).second) throw std::runtime_error("Empty or duplicate entry id: " + e.id);
            ++fresh->stats.entries;
            if (fresh->stats.entries % 1000 == 0) Progress("index_progress",fresh->stats.entries,entries->array.size());
            e.kind = Field(item,"kind"); if (e.kind != "exact" && e.kind != "template" && e.kind != "dynamic" && e.kind != "document") throw std::runtime_error("Unknown kind in " + e.id);
            const Json* enabled = item.Find("enabled"); if (!enabled || enabled->type != Json::Bool) throw std::runtime_error("Expected enabled boolean in " + e.id);
            if (e.kind == "document") {
                DocumentEntry document;
                document.id = e.id; document.file = Field(item,"file"); document.topic = Field(item,"topic");
                document.headerHex = Field(item,"header_hex");
                document.source = Field(item,"source"); document.translation = Field(item,"translation");
                document.enabled = enabled->boolean;
                if (document.file != "GAME.NFO") throw std::runtime_error("Unsupported document file in " + e.id);
                if (document.topic.empty() || !ValidUtf8(document.topic) || !ValidUtf8(document.id)) throw std::runtime_error("Invalid document topic/id in " + e.id);
                if (document.headerHex.empty() || document.headerHex.size() > 400 || (document.headerHex.size() % 2)) throw std::runtime_error("Invalid document header hex length in " + e.id);
                for (unsigned char c : document.headerHex) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) throw std::runtime_error("Invalid document header hex in " + e.id);
                if (document.source.size() > kMaxOutput || document.translation.size() > kMaxOutput || !ValidUtf8(document.source) || !ValidUtf8(document.translation)) throw std::runtime_error("Invalid document body length/UTF-8 in " + e.id);
                ++fresh->stats.documents;
                if (document.enabled) ++fresh->stats.enabledDocuments; else ++fresh->stats.disabled;
                fresh->documents.push_back(std::move(document));
                continue;
            }
            if (!enabled->boolean || e.kind == "dynamic") { ++fresh->stats.disabled; continue; }
            e.source = Field(item,"source"); e.translation = Field(item,"translation");
            if (e.source.empty() || e.source.size() > kMaxInput || e.translation.empty() || e.translation.size() > kMaxOutput || !ValidUtf8(e.source) || !ValidUtf8(e.translation)) throw std::runtime_error("Invalid text length/UTF-8 in " + e.id);
            e.caller = Address(item,"caller"); e.target = Address(item,"target"); e.specificity = (e.caller ? 1 : 0)+(e.target ? 1 : 0);
            if (e.kind == "template") {
                std::set<std::string> sourceNames, translatedNames;
                e.sourceTokens = Tokenize(e.source,sourceNames); e.translationTokens = Tokenize(e.translation,translatedNames);
                if (sourceNames.empty() || sourceNames != translatedNames) throw std::runtime_error("Placeholder mismatch in " + e.id);
                const Json* variables = item.Find("variables");
                if (variables) {
                    if (variables->type != Json::Object) throw std::runtime_error("Expected variables object in " + e.id);
                    for (const auto& variable : variables->object) {
                        if (!sourceNames.count(variable.first) || variable.second.type != Json::Object) throw std::runtime_error("Invalid variable metadata in " + e.id);
                        std::string kind = Field(variable.second,"kind");
                        if(kind=="number") {
                            if(variable.second.Find("number")||variable.second.Find("required"))throw std::runtime_error("Numeric variable cannot require a lexeme in "+e.id);
                            e.numericNames.insert(variable.first);continue;
                        }
                        if(kind=="raw") {
                            if(variable.second.Find("number")||variable.second.Find("required"))throw std::runtime_error("Raw variable cannot require a lexeme or number in "+e.id);
                            e.rawNames.insert(variable.first);continue;
                        }
                        if(kind!="lexicon")throw std::runtime_error("Invalid variable kind in "+e.id);
                        std::string selectedForm=Field(variable.second,"form",false);
                        if(!selectedForm.empty())e.variableForms[variable.first]=selectedForm;
                        std::string number = Field(variable.second,"number",false);
                        if (!number.empty()) { if (!sourceNames.count(number)) throw std::runtime_error("Unknown number variable in " + e.id); e.variableNumbers[variable.first] = number; e.numericNames.insert(number); }
                        if(const Json* required=variable.second.Find("required")) {
                            if(required->type!=Json::Bool)throw std::runtime_error("Expected required boolean in " + e.id);
                            if(required->boolean)e.requiredLexemes.insert(variable.first);
                        }
                    }
                }
                if(const Json* literals=item.Find("literal_values")){
                    if(literals->type!=Json::Object)throw std::runtime_error("Invalid literal values in "+e.id);
                    for(const auto& pair:literals->object){
                        if(!sourceNames.count(pair.first)||pair.second.type!=Json::String||pair.second.text.size()>kMaxOutput||!ValidUtf8(pair.second.text))throw std::runtime_error("Invalid literal override in "+e.id);
                        e.literalValues[pair.first]=pair.second.text;
                    }
                }
                for (size_t i = 1; i < e.sourceTokens.size(); ++i) if (e.sourceTokens[i-1].variable && e.sourceTokens[i].variable) throw std::runtime_error("Adjacent placeholders in " + e.id);
                const Json* plural = item.Find("plural");
                if (plural && !(plural->type == Json::Object && plural->object.empty())) {
                    if (plural->type != Json::Object) throw std::runtime_error("Invalid plural in " + e.id);
                    e.pluralVariable = Field(*plural,"variable"); if (!sourceNames.count(e.pluralVariable)) throw std::runtime_error("Unknown plural variable in " + e.id);
                    e.numericNames.insert(e.pluralVariable);
                    for (const char* form : {"one","few","many","other"}) {
                        std::string value = Field(*plural,form); std::set<std::string> names;
                        auto tokens = Tokenize(value,names); if (names != sourceNames || value.size() > kMaxOutput || !ValidUtf8(value)) throw std::runtime_error("Plural placeholder mismatch in " + e.id);
                        e.plural.emplace(form,std::move(tokens));
                    }
                }
                size_t literalBytes = 0;
                for (const Token& token : e.sourceTokens) if (!token.variable) literalBytes += token.text.size();
                size_t numeric = e.numericNames.size();
                if (sourceNames.count("n") && !e.numericNames.count("n")) ++numeric;
                e.templateRank = ((uint64_t)e.specificity<<32) + literalBytes*32 + numeric;
                const size_t index = fresh->entries.size();
                const Token& first = e.sourceTokens.front(); const Token& last = e.sourceTokens.back();
                if (!first.variable) fresh->prefixTemplates[(unsigned char)first.text.front()].push_back(index);
                else if (!last.variable && last.text.size() >= 2) fresh->suffixTemplates[last.text.substr(last.text.size()-2)].push_back(index);
                else if (!last.variable) fresh->shortSuffixTemplates[(unsigned char)last.text.back()].push_back(index);
                else fresh->unanchoredTemplates.push_back(index);
                ++fresh->stats.enabledTemplates; fresh->templates.push_back(index);
            } else { ++fresh->stats.enabledExact; fresh->exact[e.source].push_back(fresh->entries.size()); }
            fresh->entries.push_back(std::move(e));
        }
        Progress("index_ready",fresh->stats.entries,entries->array.size());
        if(compactMode) {
            if(!compactPresent||!grammarPresent||cz>kMaxFile||gz>kMaxFile)throw std::runtime_error("Incomplete/oversized compact catalogue pair");
            fresh->compact=std::make_shared<CompactCatalog>(compactRoot,Parser(Read(modelPath)).Parse(),fresh->lexicon);
            fresh->compact->OverrideDocuments(fresh->documents);
            fresh->stats.compactUnits=fresh->compact->UnitCount();
            fresh->stats.version=Field(compactRoot,"version",false);
            fresh->stats.enabledDocuments=0;for(const auto& d:fresh->documents)if(d.enabled)++fresh->stats.enabledDocuments;
            Progress("compact_ready",fresh->stats.compactUnits,fresh->stats.compactUnits);
        }
        fresh->stats.generation = ++generation_; snapshot_ = fresh; error_.clear(); lastReloadOkay_ = true;
        Progress("published",fresh->stats.entries,entries->array.size());
        return true;
    } catch (const std::exception& e) { error_ = e.what(); lastReloadOkay_ = false; return false; }
}
bool TranslationCatalog::Translate(const std::string& text, uint32_t caller, uint32_t target, std::string& translated, std::string& ruleId, bool exactOnly, const std::vector<NpcSourceIdentity>* identities) const {
    compactDiagnostic.clear();
    translated.clear(); ruleId.clear(); if (text.empty() || text.size() > kMaxInput || !ValidUtf8(text)) return false;
    std::shared_ptr<const Snapshot> view;
    { std::lock_guard<std::mutex> guard(mutex_); view = snapshot_; }
    if (!view) return false;
    if(view->compact) {
        if(view->compact->Translate(text,caller,target,translated,ruleId,exactOnly,compactDiagnostic,identities)) {
#ifdef _WIN32
            if(view->stats.encoding=="cp1251") {
                int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,translated.data(),(int)translated.size(),nullptr,0);
                std::wstring wide(n,0);BOOL substituted=FALSE;
                bool okay=n>0&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,translated.data(),(int)translated.size(),&wide[0],n)>0;
                if(okay)okay=WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,wide.data(),n,nullptr,0,nullptr,&substituted)>0&&!substituted;
                if(okay)return true;
                compactDiagnostic="target_not_cp1251";translated.clear();ruleId.clear();
            }else return true;
#else
            return true;
#endif
        }
    }
    if(target==0x004FCF90||target==0x004F9D50||target==0x004FD710||(view->compact&&caller==0x004FB720&&target==0x0050BF70))return false;
    auto applies = [&](const Entry& e) { return (!e.caller || e.caller == caller) && (!e.target || e.target == target); };
    auto exact = view->exact.find(text); bool found = false, conflict = false; size_t best = 0;
    if (exact != view->exact.end()) for (size_t id : exact->second) {
        const Entry& e = view->entries[id]; if (!applies(e)) continue;
        if (!found || e.specificity > best) { translated = e.translation; ruleId = e.id; best = e.specificity; found = true; conflict = false; }
        else if (e.specificity == best && e.translation != translated) conflict = true;
    }
    if (found) { if (conflict) { translated.clear(); ruleId.clear(); } return !conflict; }
    if(exactOnly)return false;
    std::vector<size_t> candidates = view->prefixTemplates[(unsigned char)text.front()];
    if (text.size() >= 2) {
        auto suffix = view->suffixTemplates.find(text.substr(text.size()-2));
        if (suffix != view->suffixTemplates.end()) candidates.insert(candidates.end(),suffix->second.begin(),suffix->second.end());
    }
    const auto& shortSuffix = view->shortSuffixTemplates[(unsigned char)text.back()];
    candidates.insert(candidates.end(),shortSuffix.begin(),shortSuffix.end());
    candidates.insert(candidates.end(),view->unanchoredTemplates.begin(),view->unanchoredTemplates.end());
    std::sort(candidates.begin(),candidates.end(),[&](size_t a, size_t b) { return view->entries[a].templateRank > view->entries[b].templateRank; });
    uint64_t bestTemplate = 0;
    for (size_t id : candidates) {
        const Entry& e = view->entries[id]; if (!applies(e)) continue;
        if (found && e.templateRank < bestTemplate) break;
        Captures captures; if (!Match(e,text,captures)) continue;
        bool known=true;
        for(const auto& name:e.requiredLexemes) {
            if(e.literalValues.count(name))continue;
            auto value=captures.find(name);
            auto word=value==captures.end()?view->lexicon.end():view->lexicon.find(value->second);
            if(word==view->lexicon.end()||word->second.ambiguous){known=false;break;}
        }
        for(const auto& request:e.variableForms){
            if(e.literalValues.count(request.first))continue;
            auto value=captures.find(request.first);
            auto word=value==captures.end()?view->lexicon.end():view->lexicon.find(value->second);
            if(word==view->lexicon.end()||word->second.ambiguous||!word->second.forms.count(request.second)){known=false;break;}
        }
        if(!known)continue;
        const std::vector<Token>* tokens = &e.translationTokens;
        const bool finnish=view->stats.language=="fi";
        if (!e.plural.empty()) tokens = &e.plural.at(Plural(captures.at(e.pluralVariable),finnish));
        Captures localized = Localize(e,captures,view->lexicon,finnish);
        std::string output; if (!Expand(*tokens,localized,output)) continue;
        if (!found || e.templateRank > bestTemplate) { translated = std::move(output); ruleId = e.id; bestTemplate = e.templateRank; found = true; conflict = false; }
        else if (e.templateRank == bestTemplate && output != translated) conflict = true;
    }
    if (conflict) { translated.clear(); ruleId.clear(); return false; } return found;
}
std::string TranslationCatalog::LastError() const { std::lock_guard<std::mutex> guard(mutex_); return error_; }
std::string TranslationCatalog::CompactDiagnostic() const { return compactDiagnostic; }
bool TranslationCatalog::InspectDomain(const std::string& text,const std::string& domain,
                                      const std::string& form,std::string& translated,std::string& ruleId) const {
    compactDiagnostic.clear();translated.clear();ruleId.clear();
    if(text.empty()||text.size()>kMaxInput||!ValidUtf8(text))return false;
    std::shared_ptr<const Snapshot> view;
    {std::lock_guard<std::mutex> guard(mutex_);view=snapshot_;}
    return view&&view->compact&&view->compact->InspectDomain(text,domain,form,translated,ruleId,compactDiagnostic);
}
CatalogStats TranslationCatalog::GetStats() const { std::lock_guard<std::mutex> guard(mutex_); return snapshot_ ? snapshot_->stats : CatalogStats(); }
unsigned TranslationCatalog::GetCodePage() const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!snapshot_) return 0;
    const std::string& encoding = snapshot_->stats.encoding;
    return encoding == "cp866" ? 866u : encoding == "cp1251" ? 1251u : encoding == "cp1252" ? 1252u : 65001u;
}
std::vector<DocumentEntry> TranslationCatalog::GetDocuments() const {
    std::shared_ptr<const Snapshot> view;
    { std::lock_guard<std::mutex> guard(mutex_); view = snapshot_; }
    return view ? view->documents : std::vector<DocumentEntry>();
}
}
