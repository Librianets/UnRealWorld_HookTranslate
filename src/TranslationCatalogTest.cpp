#include "TranslationCatalog.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
using urwtranslation::TranslationCatalog;
namespace fs = std::filesystem;
static unsigned checked = 0, failed = 0;
static void Check(bool condition, const char* name) { ++checked; if (!condition) { ++failed; std::cerr << "FAIL " << name << '\n'; } }
static std::string Wrap(const std::string& entries) { return "{\"schema\":1,\"version\":\"test\",\"encoding\":\"cp866\",\"entries\":[" + entries + "]}"; }
static std::string Rule(const std::string& id, const std::string& source, const std::string& ru, const std::string& more = "") {
    return "{\"id\":\"" + id + "\",\"kind\":\"exact\",\"enabled\":true,\"source\":\"" + source + "\",\"translation\":\"" + ru + "\"" + more + "}";
}
static std::string Document(const std::string& id, const std::string& source, const std::string& ru,
                            bool enabled = true, const std::string& file = "GAME.NFO", const std::string& header = "2E53504541520D0A") {
    return "{\"id\":\"" + id + "\",\"kind\":\"document\",\"enabled\":" + (enabled ? "true" : "false") +
           ",\"file\":\"" + file + "\",\"topic\":\"SPEAR\",\"header_hex\":\"" + header +
           "\",\"source\":\"" + source + "\",\"translation\":\"" + ru + "\"}";
}
int main(int argc, char** argv) {
    fs::path root = fs::temp_directory_path() / ("urw_catalog_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "translate");
    auto write = [&](const std::string& data) { std::ofstream out(root / "translate" / "catalog.json",std::ios::binary|std::ios::trunc); out << data; };
    TranslationCatalog c; std::string out, id;
    auto translate = [&](const std::string& source, const std::string& expected, uint32_t caller = 0, uint32_t target = 0) { return c.Translate(source,caller,target,out,id) && out == expected; };
    write(Wrap(Rule("a","You are hungry.","Вы голодны.")));
    Check(c.Initialize(root.wstring()),"valid catalogue loads");
    Check(translate("You are hungry.","Вы голодны."),"exact display text");
    Check(id == "a" && c.GetStats().enabledExact == 1 && c.GetCodePage() == 866,"metadata and rule ID");
    Check(!c.Translate("you are hungry.",0,0,out,id),"case-sensitive source");
    Check(!c.Translate(std::string(4097,'x'),0,0,out,id),"input bound");
    Check(!c.Translate(std::string("\xED\xA0\x80",3),0,0,out,id),"invalid UTF-8 rejected");
    Check(!c.Translate(std::string("a\0b",3),0,0,out,id),"embedded NUL rejected");
    uint64_t generation = c.GetStats().generation;
    Check(c.Reload(false) && c.GetStats().generation == generation,"unchanged reload does not rebuild");
    write("{broken}");
    Check(!c.Reload(true) && !c.LastError().empty(),"broken JSON rejected");
    Check(translate("You are hungry.","Вы голодны.") && c.GetStats().generation == generation,"invalid reload preserves snapshot");
    write(Wrap(Rule("global","Knife","Нож") + "," + Rule("scoped","Knife","Клинок",",\"caller\":\"0x00502B60\",\"target\":\"0x0050B9D0\"")));
    Check(c.Reload(true) && translate("Knife","Клинок",0x502B60,0x50B9D0),"scoped exact wins");
    Check(translate("Knife","Нож",0x123,0x50B9D0),"other caller uses global");
    write(Wrap(Rule("a","Knife","Нож") + "," + Rule("b","Knife","Клинок")));
    Check(c.Reload(true) && !c.Translate("Knife",0,0,out,id) && out.empty(),"conflicting exact returns English");
    const std::string count = R"({"id":"count","kind":"template","enabled":true,"source":"You see {n} spears.","translation":"Вы видите {n} копий.","plural":{"variable":"n","one":"Вы видите {n} копьё.","few":"Вы видите {n} копья.","many":"Вы видите {n} копий.","other":"Вы видите {n} доли копья."}})";
    write(Wrap(count)); Check(c.Reload(true),"count template loads");
    for (const auto& item : std::vector<std::pair<std::string,std::string>>{{"1","копьё"},{"2","копья"},{"5","копий"},{"11","копий"},{"14","копий"},{"21","копьё"},{"22","копья"},{"-1","копьё"},{"-9223372036854775808","копий"},{"1.5","доли копья"}})
        Check(translate("You see " + item.first + " spears.","Вы видите " + item.first + " " + item.second + "."),"Russian count form");
    Check(!c.Translate("You see many spears.",0,0,out,id),"numeric placeholder type enforced");
    const std::string finnish=R"({"schema":1,"encoding":"cp1252","language":"fi","entries":[{"id":"fi_count","kind":"template","enabled":true,"source":"{n} arrows","translation":"{n} nuolta","plural":{"variable":"n","one":"{n} nuoli","few":"{n} nuolta","many":"{n} nuolta","other":"{n} nuolta"}}]})";
    write(finnish);Check(c.Reload(true)&&c.GetCodePage()==1252,"Finnish CP1252 metadata loads");
    for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"1","nuoli"},{"2","nuolta"},{"5","nuolta"},{"11","nuolta"},{"21","nuolta"},{"101","nuolta"},{"0","nuolta"},{"1.5","nuolta"}})
        Check(translate(item.first+" arrows",item.first+" "+item.second),"Finnish count form has no Russian modulo cycle");
    const std::string guarded=R"({"id":"guard","kind":"template","enabled":true,"source":"You are {arg1}.","translation":"Вы {arg1}.","variables":{"arg1":{"kind":"lexicon","required":true}}})";
    write("{\"schema\":1,\"encoding\":\"cp866\",\"entries\":["+guarded+"],\"lexicon\":[{\"id\":\"hungry\",\"source\":\"hungry\",\"translation\":\"голодны\"}]}");
    Check(c.Reload(true)&&translate("You are hungry.","Вы голодны."),"required known capture translates");
    Check(!c.Translate("You are about to enter the game.",0,0,out,id),"required capture blocks accidental whole sentence match");
    write(Wrap(guarded));
    Check(c.Reload(true)&&!c.Translate("You are hungry.",0,0,out,id),"required capture does not guess missing dictionary value");
    std::string invalidGuard=guarded;invalidGuard.replace(invalidGuard.find("true",invalidGuard.find("required")),4,"\"yes\"");write(Wrap(invalidGuard));
    Check(!c.Reload(true),"invalid required capture type rejected");
    write(Wrap(count + R"(,{"id":"generic","kind":"template","enabled":true,"source":"{arg1}","translation":"{arg1}","plural":{}})"));
    Check(c.Reload(true) && translate("You see 3 spears.","Вы видите 3 копья."),"specific literal template wins over generic field");
    write(Wrap(count + R"(,{"id":"less-typed","kind":"template","enabled":true,"source":"You see {arg1} spears.","translation":"Без формы: {arg1}"})"));
    Check(c.Reload(true) && translate("You see 3 spears.","Вы видите 3 копья."),"numeric template wins over untyped equivalent");
    const std::string repeat = R"({"id":"repeat","kind":"template","enabled":true,"source":"{name} meets {name}.","translation":"{name} встречает себя."})";
    write(Wrap(repeat)); Check(c.Reload(true) && translate("Sami meets Sami.","Sami встречает себя."),"repeated placeholder matches");
    Check(!c.Translate("Sami meets Erkka.",0,0,out,id),"repeated placeholder identity enforced");
    write(Wrap(R"({"id":"ambiguous","kind":"template","enabled":true,"source":"{a} - {b}","translation":"{b}: {a}"})"));
    Check(c.Reload(true) && !c.Translate("one - two - three",0,0,out,id),"ambiguous captures rejected");
    write(Wrap(R"({"id":"braces","kind":"template","enabled":true,"source":"{{{name}}}","translation":"{{{name}}}"})"));
    Check(c.Reload(true) && translate("{Sami}","{Sami}"),"escaped literal braces");
    write(Wrap(R"({"id":"bad","kind":"template","enabled":true,"source":"{name}","translation":"{other}"})"));
    Check(!c.Reload(true) && translate("{Sami}","{Sami}"),"placeholder mismatch preserves previous snapshot");
    write(Wrap(Rule("unicode","hello",R"(\u041f\u0440\u0438\u0432\u0435\u0442 \uD83D\uDE00)")));
    Check(c.Reload(true) && translate("hello","Привет 😀"),"JSON Unicode and surrogate pair");
    write(Wrap(Rule("unicode","hello",R"(\uD800)")));
    Check(!c.Reload(true),"unpaired surrogate rejected");
    write(Wrap(Rule("a","hello","one") + "," + Rule("a","hello","two")));
    Check(!c.Reload(true),"duplicate ID rejected");
    write(Wrap(R"({"id":"off","kind":"dynamic","enabled":false,"source":"ignored"})"));
    Check(c.Reload(true) && c.GetStats().disabled == 1 && !c.Translate("ignored",0,0,out,id),"dynamic disabled entry retained as metadata");
    write(Wrap(Rule("concurrent","Knife","Нож"))); Check(c.Reload(true),"concurrent fixture loads");
    std::atomic<bool> errors{false};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i) threads.emplace_back([&]() { for (unsigned j = 0; j < 2000; ++j) { std::string translated, rule; if (!c.Translate("Knife",0,0,translated,rule) || translated != "Нож") errors = true; } });
    for (unsigned j = 0; j < 10; ++j) if (!c.Reload(true)) errors = true;
    for (auto& thread : threads) thread.join();
    Check(!errors,"concurrent readers and snapshot replacement");
    write(Wrap(Rule("hot","Knife","Клинок")));
    Check(c.Reload(false) && translate("Knife","Нож"),"hot reload is throttled before one second");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    Check(c.Reload(false) && translate("Knife","Клинок"),"automatic hot reload picks up edited UTF-8 without force");
    const std::string lexicon = R"({"schema":1,"version":"lexicon-test","encoding":"cp1251","lexicon":[{"source":"knife","translation":"нож","forms":{"one":"нож","few":"ножа","many":"ножей","other":"ножа"}}],"entries":[{"id":"items","kind":"template","enabled":true,"source":"You see {n} {item}.","translation":"Вы видите {n} {item}.","variables":{"item":{"kind":"lexicon","number":"n"}}}]})";
    write(lexicon); Check(c.Reload(true) && c.GetCodePage() == 1251,"lexicon and font metadata load");
    Check(translate("You see 1 knife.","Вы видите 1 нож."),"lexicon one");
    Check(translate("You see 2 knife.","Вы видите 2 ножа."),"lexicon few");
    Check(translate("You see 11 knife.","Вы видите 11 ножей."),"lexicon many");
    Check(translate("You see 1.5 knife.","Вы видите 1.5 ножа."),"lexicon decimal other");
    Check(translate("You see 2 Sami.","Вы видите 2 Sami."),"unknown lexicon value preserved");
    Check(translate("You see 2 knife blade.","Вы видите 2 knife blade."),"no partial lexicon substitution");
    std::string conflict = lexicon;
    size_t insert = conflict.find("],\"entries\"");
    conflict.insert(insert,R"(,{"source":"knife","translation":"клинок"})");
    write(conflict); Check(c.Reload(true) && translate("You see 2 knife.","Вы видите 2 knife."),"ambiguous lexicon alias stays original");
    const std::string original = "  On widescreen displays you can set LOGICAL RESIZE to NO for full, although stretched output.";
    const std::string existingTranslation = "  На широком экране выберите НЕТ в пункте СОХРАНЯТЬ ПРОПОРЦИИ, чтобы растянуть изображение на весь экран.";
    write(Wrap(Rule("ascii:001A7498",original,existingTranslation,",\"caller\":\"0x00502B60\",\"target\":\"0x0050B9D0\"")));
    Check(c.Reload(true) && translate(original,existingTranslation,0x502B60,0x50B9D0),"existing reviewed CSV text and translation");
    Check(!c.Translate(original,0x530DC0,0x50B9D0,out,id),"existing CSV rule does not cross render boundary");
    TranslationCatalog empty;
    Check(empty.GetDocuments().empty(),"uninitialized document API is empty");
    write(Wrap(Document("spear","Spear article.","Описание копья.") + "," + Rule("knife","Knife","Нож")));
    Check(c.Reload(true),"document entry loads alongside display rules");
    auto documents = c.GetDocuments();
    Check(documents.size() == 1 && documents[0].id == "spear" && documents[0].file == "GAME.NFO" && documents[0].topic == "SPEAR" && documents[0].enabled && documents[0].headerHex == "2E53504541520D0A" && documents[0].source == "Spear article." && documents[0].translation == "Описание копья.","document fields preserved as UTF-8 and header hex");
    Check(c.GetStats().entries == 2 && c.GetStats().documents == 1 && c.GetStats().enabledDocuments == 1 && c.GetStats().enabledExact == 1,"documents have separate statistics");
    Check(!c.Translate("Spear article.",0,0,out,id) && translate("Knife","Нож"),"document body excluded from ordinary translation indices");
    documents[0].translation = "changed copy";
    Check(c.GetDocuments()[0].translation == "Описание копья.","document API returns owned copy");
    write(Wrap(Document("spear","English fallback.","Отключённый перевод.",false)));
    Check(c.Reload(true) && c.GetDocuments().size() == 1 && !c.GetDocuments()[0].enabled && c.GetDocuments()[0].source == "English fallback." && c.GetStats().disabled == 1 && c.GetStats().enabledDocuments == 0,"disabled document retained for English fallback");
    write(Wrap(Document("large",std::string(65536,'x'),std::string(65536,'y'))));
    Check(c.Reload(true) && c.GetDocuments()[0].source.size() == 65536 && c.GetDocuments()[0].translation.size() == 65536,"document body supports full 64 KiB beyond ordinary 4096-byte input");
    const uint64_t documentGeneration = c.GetStats().generation;
    write(Wrap(Document("too-large",std::string(65537,'x'),"RU")));
    Check(!c.Reload(true) && c.GetStats().generation == documentGeneration && c.GetDocuments()[0].id == "large","oversized source rejects reload and preserves documents");
    write(Wrap(Document("too-large","EN",std::string(65537,'y'))));
    Check(!c.Reload(true) && c.GetDocuments()[0].id == "large","oversized translation rejects reload");
    write(Wrap(Document("wrong-file","EN","RU",true,"../GAME.NFO")));
    Check(!c.Reload(true) && c.GetDocuments()[0].id == "large","document path traversal rejected");
    write(Wrap(Document("wrong-file","EN","RU",true,"OTHER.NFO")));
    Check(!c.Reload(true),"unsupported document file rejected");
    for (const auto& header : std::vector<std::string>{"", "ABC", "ABQF", std::string(402,'A'), "АБ"}) {
        write(Wrap(Document("bad-header","EN","RU",true,"GAME.NFO",header)));
        Check(!c.Reload(true) && c.GetDocuments()[0].id == "large","invalid header hex preserves previous documents");
    }
    write(Wrap(Document("header-limit","EN","RU",true,"GAME.NFO",std::string(400,'A'))));
    Check(c.Reload(true) && c.GetDocuments()[0].headerHex.size() == 400,"200-byte header hex boundary accepted");
    write(Wrap(Document("nul","EN",R"(RU\u0000text)")));
    Check(!c.Reload(true) && c.GetDocuments()[0].id == "header-limit","document embedded NUL rejected");
    write(Wrap(Document("duplicate","EN","RU") + "," + Rule("duplicate","Knife","Нож")));
    Check(!c.Reload(true),"document ID colliding with display rule rejected");
    write(Wrap(Document("empty","","")));
    Check(c.Reload(true) && c.GetDocuments()[0].source.empty(),"empty document body accepted");
    write(Wrap(Document("hot-document","English article.","Статья после правки.")));
    Check(c.Reload(false) && c.GetDocuments()[0].id == "empty","document reload respects one-second throttle");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    Check(c.Reload(false) && c.GetDocuments()[0].translation == "Статья после правки.","document automatically reloads edited text");
    if (argc > 1) {
        TranslationCatalog actual;
        // Optional integration test root; never writes to this catalogue.
        std::string path = argv[1];
        auto loadStart = std::chrono::steady_clock::now();
        Check(actual.Initialize(std::wstring(path.begin(),path.end()), fs::exists(fs::path(path)/"rus_translate/catalog.json")?L"rus_translate":L"translate"),"production catalogue loads");
        auto loadEnd = std::chrono::steady_clock::now();
        std::cout << "Production entries=" << actual.GetStats().entries << " exact=" << actual.GetStats().enabledExact << " templates=" << actual.GetStats().enabledTemplates
                  << " documents=" << actual.GetStats().documents << " enabled_documents=" << actual.GetStats().enabledDocuments << '\n';
        if (!actual.LastError().empty()) std::cerr << actual.LastError() << '\n';
        Check(actual.Translate(original,0x502B60,0x50B9D0,out,id) && out == existingTranslation,"production reviewed exact");
        Check(actual.Translate("3 knives",0x502B60,0x50B9D0,out,id) && out == "3 ножа","production numeric plural");
        Check(actual.Translate("8 knives",0x502B60,0x50B9D0,out,id) && out == "8 ножей","production numeric many");
        Check(actual.Translate("15 straw",0x502B60,0x50B9D0,out,id) && out == "солома (15)","production printf-derived template");
        auto benchStart = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 1000; ++i) actual.Translate("Uncatalogued harmless message " + std::to_string(i),0x502B60,0x50B9D0,out,id);
        auto benchEnd = std::chrono::steady_clock::now();
        std::cout << "Production load_ms=" << std::chrono::duration<double,std::milli>(loadEnd-loadStart).count()
                  << " unknown_1000_ms=" << std::chrono::duration<double,std::milli>(benchEnd-benchStart).count() << '\n';
        if (argc > 2) {
            std::ifstream input(argv[2],std::ios::binary); std::string line; size_t total = 0, translated = 0;
            benchStart = std::chrono::steady_clock::now();
            while (std::getline(input,line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); ++total; if (actual.Translate(line,0x502B60,0x50B9D0,out,id)) ++translated; }
            benchEnd = std::chrono::steady_clock::now();
            Check(total >= 1000,"production benchmark contains at least 1000 sources");
            std::cout << "Production real_cases=" << total << " matched=" << translated << " total_ms=" << std::chrono::duration<double,std::milli>(benchEnd-benchStart).count() << '\n';
        }
    }
    write(R"({"schema":1,"encoding":"cp1251","entries":[{"id":"raw_name","kind":"template","enabled":true,"source":"Hello, {player}.","translation":"Привет, {player}.","variables":{"player":{"kind":"raw"}}}],"lexicon":[{"id":"collision","source":"Knife","translation":"Нож","forms":{}}]})");
    Check(c.Reload(true)&&translate("Hello, Knife.","Привет, Knife."),"raw player name bypasses matching lexicon");
    Check(translate("Hello, dffg.","Привет, dffg."),"raw arbitrary player name preserved");
    fs::create_directories(root / "custom_language");
    { std::ofstream other(root/"custom_language"/"catalog.json",std::ios::binary);
      other << "{\"schema\":1,\"encoding\":\"utf-8\",\"entries\":[" << Rule("jp","Knife","小刀") << "]}"; }
    TranslationCatalog language;
    Check(language.Initialize(root.wstring(),L"custom_language"),"configurable language folder loads");
    Check(language.GetCodePage()==65001&&language.Translate("Knife",0,0,out,id)&&out=="小刀","UTF8 Japanese catalogue is independent of Russian");
    Check(c.Translate("Hello, Knife.",0,0,out,id)&&out=="Привет, Knife.","other language folder does not mutate existing catalogue");
    fs::create_directories(root / "fi_language");
    {std::ofstream fi(root/"fi_language"/"catalog.json",std::ios::binary);
     fi<<R"({"schema":1,"language":"fi","encoding":"cp1252","entries":[{"id":"fi_case","kind":"template","enabled":true,"source":"You see {article} {item}.","translation":"Näet {article}{item}.","variables":{"article":{"kind":"raw"},"item":{"kind":"lexicon","required":true,"form":"partitive"}},"literal_values":{"article":""}}],"lexicon":[{"source":"knife","translation":"veitsi","forms":{"partitive":"veistä"}},{"source":"unknown","translation":"tuntematon"}]})";}
    Check(language.Initialize(root.wstring(),L"fi_language"),"Finnish legacy case catalogue loads");
    Check(language.Translate("You see a knife.",0,0,out,id)&&out=="Näet veistä.","Finnish requested case and suppressed article");
    Check(!language.Translate("You see an unknown.",0,0,out,id),"missing Finnish requested case rejects instead of mixed output");
    fs::remove_all(root);
    std::cout << "TranslationCatalog: " << (checked-failed) << "/" << checked << " checks passed\n";
    return failed ? 1 : 0;
}
