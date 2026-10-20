#include "TraceTestApi.h"
#include <cstring>
#include <exception>
#include <utility>
#include "GeneratedPresentation.h"

// Test-only fake-image seeding; implemented by TraceRuntime, no game/save IO.
DWORD SetTraceJournalEntryTests(unsigned index,const std::string& metadata,const std::string& payload);

namespace {
std::string g_leafText;
std::string g_dialogueText;
volatile LONG g_bodyFailures = 0;
const char kKnifeRussian[] = "\xCD\xEE\xE6";
const char kBladeRussian[] = "\xCA\xEB\xE8\xED\xEE\xEA";
const char kHungryRussian[] = "\xC2\xFB \xE3\xEE\xEB\xEE\xE4\xED\xFB.";

void BodyCheck(bool condition) {
    if (!condition) InterlockedIncrement(&g_bodyFailures);
}

__declspec(noinline) int __fastcall FakeLeaf(int x, int y, const char* text, int color, int outline) {
    BodyCheck(x == 10 && y == 20 && color == 30 && outline == 40);
    g_leafText = text;
    SetLastError(0xBEEF);
    return x + y + color + outline + static_cast<int>(strlen(text));
}

__declspec(noinline) int __fastcall FakeDialogue(const char* text, int salt) {
    g_dialogueText = text;
    int leaf = FakeLeaf(10, 20, text, 30, 40);
    BodyCheck(leaf == 100 + static_cast<int>(strlen(text)));
    BodyCheck(GetLastError() == 0xBEEF);
    SetLastError(0xCAFE);
    return static_cast<int>(strlen(text)) + salt;
}

__declspec(noinline) int __stdcall FakeSource(const char* text, int salt) {
    BodyCheck(GetLastError() == 0x11223344);
    int result = FakeLeaf(10, 20, text, 30, 40);
    BodyCheck(GetLastError() == 0xBEEF);
    return result + salt;
}

__declspec(noinline) int __cdecl FakeWhole(const char* text) {
    int result = FakeDialogue(text, 9);
    BodyCheck(GetLastError() == 0xCAFE);
    return result + 1;
}

__declspec(noinline) int __cdecl FakeSplit(const char* text) {
    int result = FakeDialogue(text, 9);
    return result + 1;
}

__declspec(noinline) int __cdecl UnhookedSource(const char* text) {
    int result = FakeLeaf(10, 20, text, 30, 40);
    return result + 1;
}

std::string g_wrapperText; int g_introX=0;
__declspec(noinline) int __fastcall FakeBracket(int x,int y,const char* text,int color) {
    BodyCheck(x==10&&y==20&&color==30);g_wrapperText=text;
    return FakeLeaf(10,20,text,30,40);
}
__declspec(noinline) int __fastcall FakeCentered(int x,int y,int width,const char* text,int color,int outline) {
    BodyCheck(x==10&&y==20&&width==200&&color==30&&outline==40);g_wrapperText=text;
    return FakeLeaf(10,20,text,30,40);
}
__declspec(noinline) int __fastcall FakeButton(int x,int y,const char* text) {
    BodyCheck(x==10&&y==20);g_wrapperText=text;return FakeLeaf(10,20,text,30,40);
}
__declspec(noinline) int __fastcall FakeReply(const char* text,int salt) {
    BodyCheck(salt==17);g_wrapperText=text;return FakeLeaf(10,20,text,30,40);
}
__declspec(noinline) int __fastcall FakeTokenizer(const char* text,const char* delimiters) {
    BodyCheck(strcmp(delimiters,"[]<>")==0);g_wrapperText=text;return 23;
}
__declspec(noinline) int __cdecl FakeNext() {
    int result=FakeTokenizer("[+]Next, or [-]Previous creature","[]<>");
    BodyCheck(result==23);return FakeLeaf(10,20,g_wrapperText.c_str(),30,40);
}
__declspec(noinline) int __fastcall FakeAnimated(int x,int y,const char* text) {
    BodyCheck(y==123);g_introX=x;g_wrapperText=text;
    for(int i=0;i<64;++i)FakeLeaf(10,20,text,30,40);
    return 64;
}
__declspec(noinline) int __cdecl FakeIntro() {
    return FakeAnimated(400,123,kFirstWinterLines[0]);
}

__declspec(noinline) int __fastcall FakeStream(int x,int y,int width,int mode,int color,int outline) {
    BodyCheck(x==10&&y==20&&width==100&&color==30&&outline==40);
    g_wrapperText=ReadTracePresentationBuffer(true);
    return mode==-1?73:FakeLeaf(10,20,g_wrapperText.c_str(),30,40);
}
// Match the native saved-journal renderer ABI: EDI points at record metadata,
// while ECX/EDX and four stack arguments belong to50BF70. Preserve caller EDI.
__declspec(naked) int __cdecl FakeSavedNote(DWORD metadata) {
    __asm {
        push edi
        mov edi, [esp+8]
        push 40
        push 30
        push 5
        push 100
        mov edx, 20
        mov ecx, 10
        call FakeStream
        pop edi
        ret
    }
}
__declspec(noinline) int __cdecl FakeStreamSource() {return FakeStream(10,20,100,5,30,40);}
__declspec(noinline) int __cdecl FakeLogicalBuilder() {return FakeStream(10,20,100,-1,30,40);}
__declspec(noinline) int __fastcall FakeMessageBoundary(int color) {BodyCheck(color==19);return 71;}
__declspec(noinline) int __fastcall FakeMessage(const char* source,int salt) {
    BodyCheck(strcmp(source,"Knife")==0&&salt==7);
    std::string originalBuffer=ReadTracePresentationBuffer(false,true);
    int marker=FakeMessageBoundary(19);BodyCheck(marker==71);
    g_wrapperText=ReadTracePresentationBuffer(false);
    int result=FakeLeaf(10,20,g_wrapperText.c_str(),30,40);
    BodyCheck(ReadTracePresentationBuffer(false,true)==originalBuffer);return result;
}
int g_textureX=0,g_registeredLength=0;
__declspec(noinline) int __fastcall FakeTextureLeaf(int renderer,int x,int y,const char* text,int color,int outline) {
    BodyCheck(renderer==77&&y==20&&color==30&&outline==40);
    g_textureX=x;g_leafText=text;SetLastError(0xBEEF);return 100+(int)strlen(text);
}
__declspec(noinline) int __cdecl FakeTextureSource(const char* text) {
    return FakeTextureLeaf(77,10,20,text,30,40);
}
__declspec(noinline) int __fastcall FakeTextureButton(int renderer,int x,int y,const char* text,int color,int outline) {
    g_wrapperText=text;return FakeTextureLeaf(renderer,x,y,text,color,outline);
}
__declspec(noinline) int __fastcall FakeRegisteredButton(int renderer,int id,int x,int y,const char* text) {
    BodyCheck(id==17);
    int result=FakeTextureButton(renderer,x,y,text,30,40);
    g_registeredLength=(int)strlen(text);return result;
}
__declspec(noinline) int __fastcall FakeTextureHeader(int renderer,const char* text) {
    g_wrapperText=text;return FakeTextureLeaf(renderer,400-4*(int)strlen(text),20,text,30,40);
}
__declspec(noinline) int __fastcall FakeTextureField(int renderer,int x,int y,int width,const char* text,int mode) {
    BodyCheck(width==200&&mode==19);g_wrapperText=text;return FakeTextureLeaf(renderer,x,y,text,30,40);
}
__declspec(noinline) int __fastcall FakeTextureField2(int renderer,int x,int y,int width,const char* text,int mode) {
    BodyCheck(width==200&&mode==19);g_textureX=width;g_wrapperText=text;return FakeTextureLeaf(renderer,x,y,text,30,40);
}
__declspec(noinline) int __fastcall FakeTextureStream(int renderer,int x,int y,int width,int height,int color,int outline) {
    BodyCheck(width==200&&height==100);
    g_wrapperText=ReadTracePresentationBuffer(true);
    BodyCheck(ReadTracePresentationBuffer(true,true)=="Knife");
    SetTracePrivateAssemblyBuffer(g_wrapperText);
    return FakeTextureLeaf(renderer,x,y,g_wrapperText.c_str(),color,outline);
}
__declspec(noinline) int __cdecl FakeTextureStreamSource() {return FakeTextureStream(77,10,20,200,100,30,40);}
__declspec(noinline) int __cdecl FakeUserMarker() {g_textureX=-1234;return FakeTextureStream(77,10,20,200,100,30,40);}
__declspec(noinline) int __cdecl FakeInputEditor() {return FakeTextureLeaf(77,10,20,"Knife",30,40);}
__declspec(noinline) int __fastcall FakeOverlay(int unused,int x,int y,const char* text,int color) {
    BodyCheck(x==10&&y==20&&color==30);g_leafText=text;return unused+(int)strlen(text);
}
__declspec(noinline) int __cdecl FakeOverlaySource() {return FakeOverlay(0,10,20,"Knife",30);}

bool WriteFixture(const std::wstring& path, const std::string& json) {
    FILE* stream = nullptr;
    if (_wfopen_s(&stream, path.c_str(), L"wb") || !stream) return false;
    bool okay = fwrite(json.data(), 1, json.size(), stream) == json.size();
    if (fclose(stream)) okay = false;
    return okay;
}

std::string Rule(const char* id, const char* source, const std::string& translation) {
    return std::string("{\"id\":\"") + id + "\",\"kind\":\"exact\",\"enabled\":true,\"source\":\"" + source + "\",\"translation\":\"" + translation + "\"}";
}

std::string Fixture(bool changed) {
    std::string rules = Rule("knife", "Knife", changed ? "\\u041a\\u043b\\u0438\\u043d\\u043e\\u043a" : "\\u041d\\u043e\\u0436");
    // A deliberate known-phrase collision: notes must remain raw even when the
    // catalogue would translate the identical English payload on another route.
    rules += "," + Rule("saved_note_collision", "-- You close the door.", "DOOR_TRANSLATION_FIXTURE");
    rules += "," + Rule("hungry", "You are hungry.", "\\u0412\\u044b \\u0433\\u043e\\u043b\\u043e\\u0434\\u043d\\u044b.");
    // A translation result must not be translated a second time by the leaf.
    rules += "," + Rule("repeat_trap", "\\u0412\\u044b \\u0433\\u043e\\u043b\\u043e\\u0434\\u043d\\u044b.", "DOUBLE_TRANSLATION_ERROR");
    rules += "," + Rule("too_long", "oversized", std::string(300, 'L'));
    rules += "," + Rule("at_limit", "fits299", std::string(299, 'S'));
    std::string longProse;for(unsigned i=0;i<100;++i)longProse+="abc ";
    rules+=","+Rule("long_brackets","long bracket prose",longProse);
    rules+=","+Rule("long_button","K.long prose","K."+longProse);
    rules+=","+Rule("button","K.Knife","K.\\u041d\\u043e\\u0436");
    rules+=","+Rule("bad_key","B.Knife","X.invalid");
    rules+=","+Rule("next","[+]Next, or [-]Previous creature","[+]\\u0414\\u0430\\u043b\\u044c\\u0448\\u0435, [-]\\u043d\\u0430\\u0437\\u0430\\u0434");
    std::string introSource,introTranslation;
    for(unsigned i=0;i<11;++i){if(i){introSource+="\\n";introTranslation+="\\n";}introSource+=kFirstWinterLines[i];introTranslation+="\\u0421\\u0442\\u0440\\u043e\\u043a\\u0430 "+std::to_string(i+1);}
    rules+=","+Rule("intro",introSource.c_str(),introTranslation);
    return "{\"schema\":1,\"version\":\"native-hook-test\",\"encoding\":\"cp1251\",\"entries\":[" + rules + "]}\n";
}
}

extern "C" DWORD WINAPI RunTranslationHookSelfTest(void*) {
    std::vector<std::pair<std::string, bool>> checks;
    auto check = [&](const char* name, bool condition) { checks.emplace_back(name, condition); };
    check("recovered91_exact_output_sites_and_wrong_call_rejection",TestRecoveredOutputSites014());
    bool installed = false;
    std::wstring dllDirectory = Directory(ModulePath(GetModuleHandleW(L"rus_URWTraceHook.dll")));
    std::wstring root = dllDirectory + L"\\.translation_test";
    std::wstring translationDirectory = root + L"\\translate";
    std::wstring catalogPath = translationDirectory + L"\\catalog.json";
    g_bodyFailures = 0;
    g_leafText.clear(); g_dialogueText.clear();
    try {
        bool directories = (CreateDirectoryW(root.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS);
        directories = (CreateDirectoryW(translationDirectory.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS) && directories;
        check("isolated_fixture_directory", directories);
        bool written = directories && WriteFixture(catalogPath, Fixture(false));
        check("isolated_fixture_catalogue_written", written);
        std::vector<TraceOverride> targets = {
            {0x50B9D0, reinterpret_cast<void*>(&FakeLeaf)},
            {0x530DC0, reinterpret_cast<void*>(&FakeDialogue)},
            {0x502B60, reinterpret_cast<void*>(&FakeSource)},
            {0x531B40, reinterpret_cast<void*>(&FakeWhole)},
            {0x5322F0, reinterpret_cast<void*>(&FakeSplit)},
            {0x50BB50,reinterpret_cast<void*>(&FakeBracket)},
            {0x50BD40,reinterpret_cast<void*>(&FakeCentered)},
            {0x511AC0,reinterpret_cast<void*>(&FakeButton)},
            {0x531760,reinterpret_cast<void*>(&FakeReply)},
            {0x4D18F0,reinterpret_cast<void*>(&FakeTokenizer)},
            {0x511E60,reinterpret_cast<void*>(&FakeNext)},
            {0x522660,reinterpret_cast<void*>(&FakeAnimated)},
            {0x522790,reinterpret_cast<void*>(&FakeIntro)},
            {0x50BF70,reinterpret_cast<void*>(&FakeStream)},
            {0x4FB720,reinterpret_cast<void*>(&FakeSavedNote)},
            {0x522540,reinterpret_cast<void*>(&FakeStreamSource)},
            {0x4ABA60,reinterpret_cast<void*>(&FakeLogicalBuilder)},
            {0x4F2F80,reinterpret_cast<void*>(&FakeMessageBoundary)},
            {0x4F3990,reinterpret_cast<void*>(&FakeMessage)},
            {0x50B690,reinterpret_cast<void*>(&FakeTextureLeaf)},
            {0x4E04C0,reinterpret_cast<void*>(&FakeTextureSource)},
            {0x512160,reinterpret_cast<void*>(&FakeTextureButton)},
            {0x4CD210,reinterpret_cast<void*>(&FakeRegisteredButton)},
            {0x510860,reinterpret_cast<void*>(&FakeTextureHeader)},
            {0x50E990,reinterpret_cast<void*>(&FakeTextureField)},
            {0x512F10,reinterpret_cast<void*>(&FakeTextureField2)},
            {0x50C400,reinterpret_cast<void*>(&FakeTextureStream)},
            {0x4DD0C0,reinterpret_cast<void*>(&FakeTextureStreamSource)},
            {0x4E2110,reinterpret_cast<void*>(&FakeUserMarker)},
            {0x50CD80,reinterpret_cast<void*>(&FakeInputEditor)},
            {0x50B810,reinterpret_cast<void*>(&FakeOverlay)},
            {0x508810,reinterpret_cast<void*>(&FakeOverlaySource)}
        };
        installed = written && InstallTraceTests(targets);
        check("real_bridges_and_minhook_installed", installed);
        bool enabled = installed && EnableTraceTranslationTests(root);
        check("isolated_translation_catalogue_enabled", enabled);
        if (enabled) {
            char knife[] = "Knife";
            const std::string originalKnife(knife);
            LONG before = ReadTraceTranslationCount();
            SetLastError(0x11223344);
            int result = FakeSource(knife, 7);
            DWORD lastError = GetLastError();
            check("whitelisted_leaf_receives_CP1251", g_leafText == kKnifeRussian);
            check("stdcall_fastcall_arguments_and_return", result == 110);
            check("callee_last_error_preserved", lastError == 0xBEEF);
            check("one_leaf_translation", ReadTraceTranslationCount() == before + 1);
            check("leaf_original_buffer_unchanged", strcmp(knife, originalKnife.c_str()) == 0);

            char dialogue[] = "You are hungry.";
            const std::string originalDialogue(dialogue);
            before = ReadTraceTranslationCount();
            result = FakeWhole(dialogue);
            lastError = GetLastError();
            check("whitelisted_dialogue_receives_CP1251", g_dialogueText == kHungryRussian);
            check("nested_leaf_receives_same_translated_bytes", g_leafText == kHungryRussian);
            check("nested_dialogue_translation_happens_once", ReadTraceTranslationCount() == before + 1);
            check("nested_dialogue_fastcall_and_return", result == static_cast<int>(strlen(kHungryRussian)) + 10);
            check("dialogue_last_error_preserved", lastError == 0xCAFE);
            check("dialogue_original_buffer_unchanged", strcmp(dialogue, originalDialogue.c_str()) == 0);

            before = ReadTraceTranslationCount();
            result = FakeSplit(dialogue);
            check("excluded_split_caller_passes_english", g_dialogueText == originalDialogue && g_leafText == originalDialogue);
            check("excluded_split_caller_does_not_translate", ReadTraceTranslationCount() == before);
            check("excluded_split_original_return", result == static_cast<int>(originalDialogue.size()) + 10);

            before = ReadTraceTranslationCount();
            result = UnhookedSource(knife);
            check("unhooked_caller_passes_english", g_leafText == originalKnife);
            check("unhooked_caller_does_not_translate", ReadTraceTranslationCount() == before);
            check("unhooked_caller_original_return", result == 106);

            char oversized[] = "oversized";
            before = ReadTraceTranslationCount();
            SetLastError(0x11223344);
            result = FakeSource(oversized, 0);
            check("300_byte_leaf_translation_rejected", g_leafText == "oversized" && result == 109);
            check("rejected_leaf_translation_not_counted", ReadTraceTranslationCount() == before);
            check("rejected_leaf_original_buffer_unchanged", strcmp(oversized, "oversized") == 0);
            SetLastError(0x11223344);
            result = FakeSource("fits299", 0);
            check("299_byte_leaf_translation_accepted", g_leafText == std::string(299, 'S') && result == 399);

            check("changed_fixture_written", WriteFixture(catalogPath, Fixture(true)));
            check("forced_catalogue_reload", ReloadTraceTranslationTests());
            SetLastError(0x11223344);
            FakeSource(knife, 0);
            check("reload_changes_next_output", g_leafText == kBladeRussian);
            check("malformed_fixture_written", WriteFixture(catalogPath, "{malformed native integration test}"));
            check("malformed_catalogue_reload_rejected", !ReloadTraceTranslationTests());
            SetLastError(0x11223344);
            FakeSource(knife, 0);
            check("malformed_reload_preserves_previous_snapshot", g_leafText == kBladeRussian);
            check("all_original_arrays_still_unchanged", strcmp(knife, "Knife") == 0 && strcmp(dialogue, "You are hungry.") == 0);
            before=ReadTraceTranslationCount();
            char wrapperKnife[]="Knife";
            FakeBracket(10,20,wrapperKnife,30);
            check("bracket_input_translated_before_splitting",g_wrapperText==kBladeRussian&&g_leafText==kBladeRussian);
            check("bracket_child_draw_not_translated_twice",ReadTraceTranslationCount()==before+1);
            FakeCentered(10,20,200,wrapperKnife,30,40);
            check("centered_stack_second_argument",g_wrapperText==kBladeRussian&&g_leafText==kBladeRussian);
            FakeReply(wrapperKnife,17);
            check("reply_register_input",g_wrapperText==kBladeRussian);
            FakeButton(10,20,"K.Knife");
            check("button_hotkey_retained",g_wrapperText==std::string("K.")+kKnifeRussian);
            char newButton[]="J.Knife";FakeButton(10,20,newButton);
            check("button_new_key_reuses_whole_label",g_wrapperText==std::string("J.")+kBladeRussian);
            check("button_original_key_and_text_unchanged",strcmp(newButton,"J.Knife")==0);
            FakeButton(10,20,"K.unrecognized item");
            check("button_unknown_label_remains_english",g_wrapperText=="K.unrecognized item");
            char bracketed[]="(Knife)";FakeBracket(10,20,bracketed,30);
            check("whole_parenthesized_field_reuses_known_text",g_wrapperText==std::string("(")+kBladeRussian+")");
            check("parenthesized_source_is_not_modified",strcmp(bracketed,"(Knife)")==0);
            before=ReadTraceTranslationCount();FakeNext();
            check("tokenizer_hook_uses_whole_static_hint",g_wrapperText!="[+]Next, or [-]Previous creature"&&g_wrapperText.find("[+]")!=std::string::npos);
            check("tokenized_hint_child_is_single_translation",ReadTraceTranslationCount()==before+1&&g_leafText==g_wrapperText);
            before=ReadTraceTranslationCount();result=FakeIntro();
            check("intro_block_11_lines_selected",g_wrapperText=="\xD1\xF2\xF0\xEE\xEA\xE0 1");
            check("intro_recentered_before_animation",g_introX==368&&result==64);
            check("intro_64_draws_share_one_translation",ReadTraceTranslationCount()==before+1&&g_leafText==g_wrapperText);
            check("presentation_inputs_unchanged",strcmp(wrapperKnife,"Knife")==0);
            FakeBracket(10,20,"long bracket prose",30);
            check("bracket_plain_segment_300_byte_guard",g_wrapperText=="long bracket prose");
            FakeButton(10,20,"K.long prose");
            check("button_description_300_byte_guard",g_wrapperText=="K.long prose");
            check("private_view_operands_verified_on_fake_image",PrepareTracePresentationTests());
            const std::string savedNote="-- You close the door.";
            const std::string noteMetadata="(600060):ABCD:[\"]{01230123}      ";
            DWORD noteAddress=SetTraceJournalEntryTests(69,noteMetadata,savedNote);
            check("saved_note_last_valid_record_seeded",noteAddress!=0);
            before=ReadTraceTranslationCount();
            if(noteAddress)FakeSavedNote(noteAddress);
            check("saved_player_note_known_English_phrase_preserved",g_wrapperText==savedNote&&g_leafText==savedNote&&ReadTraceTranslationCount()==before);
            check("saved_player_note_original_record_unchanged",ReadTracePresentationBuffer(true,true)==savedNote);
            std::string gameMetadata=noteMetadata;gameMetadata.replace(0,8,"(000000)");
            DWORD gameAddress=SetTraceJournalEntryTests(0,gameMetadata,savedNote);
            check("saved_generated_message_record_seeded",gameAddress!=0);
            before=ReadTraceTranslationCount();
            if(gameAddress)FakeSavedNote(gameAddress);
            check("saved_game_message_same_payload_still_translated",g_wrapperText=="DOOR_TRANSLATION_FIXTURE"&&ReadTraceTranslationCount()==before+1);
            check("saved_journal_out_of_range_slot_rejected",SetTraceJournalEntryTests(70,noteMetadata,savedNote)==0);
            check("original_stream_fixture_seeded",SetTracePresentationBuffer(true,"Knife"));
            before=ReadTraceTranslationCount();result=FakeStreamSource();
            check("complete_stream_uses_private_russian_slots",g_wrapperText==kBladeRussian&&g_leafText==kBladeRussian);
            check("original_english_token_slots_unchanged",ReadTracePresentationBuffer(true,true)=="Knife");
            check("stream_children_not_retranslated",ReadTraceTranslationCount()==before+1);
            before=ReadTraceTranslationCount();result=FakeLogicalBuilder();
            check("logical_builder_4ABA60_remains_english",result==73&&g_wrapperText=="Knife"&&ReadTraceTranslationCount()==before);
            check("original_message_fixture_seeded",SetTracePresentationBuffer(false,"Knife"));
            before=ReadTraceTranslationCount();result=FakeMessage("Knife",7);
            check("message_translated_after_english_processing",g_wrapperText==kBladeRussian&&g_leafText==kBladeRussian);
            check("message_source_english_during_and_after_render",ReadTracePresentationBuffer(false,true)=="Knife");
            check("message_children_translated_once",ReadTraceTranslationCount()==before+1);
            check("prefixed_message_fixture_seeded",SetTracePresentationBuffer(false,":You are hungry."));
            before=ReadTraceTranslationCount();FakeMessage("Knife",7);
            check("render_control_prefix_preserved_around_whole_translation",g_wrapperText==std::string(":")+kHungryRussian);
            check("prefixed_message_original_english_and_children_preserved",ReadTracePresentationBuffer(false,true)==":You are hungry."&&g_leafText==g_wrapperText&&ReadTraceTranslationCount()==before+1);
            before=ReadTraceTranslationCount();result=FakeTextureSource(knife);
            check("SDL_fastcall_renderer_and_stack_text_slot",g_leafText==kBladeRussian&&result==100+(int)strlen(kBladeRussian));
            check("SDL_direct_translation_happens_once",ReadTraceTranslationCount()==before+1);
            check("SDL_original_source_unchanged",strcmp(knife,"Knife")==0);
            before=ReadTraceTranslationCount();FakeTextureButton(77,10,20,"K.Knife",30,40);
            check("SDL_button_translated_before_split",g_wrapperText==std::string("K.")+kKnifeRussian&&g_leafText==g_wrapperText);
            check("SDL_button_children_not_retranslated",ReadTraceTranslationCount()==before+1);
            FakeTextureButton(77,10,20,"B.Knife",30,40);
            check("SDL_bad_full_key_falls_back_to_valid_label",g_wrapperText==std::string("B.")+kBladeRussian);
            const char registered[]="J.Knife";before=ReadTraceTranslationCount();
            FakeRegisteredButton(77,17,10,20,registered);
            check("SDL_registration_uses_translated_geometry",g_registeredLength==2+(int)strlen(kBladeRussian));
            check("SDL_nested_button_registration_translated_once",ReadTraceTranslationCount()==before+1);
            check("SDL_registration_keeps_original_label",strcmp(registered,"J.Knife")==0);
            FakeTextureHeader(77,knife);
            check("SDL_header_text_in_EDX_translated",g_wrapperText==kBladeRussian);
            check("SDL_header_center_measured_after_translation",g_textureX==400-4*(int)strlen(kBladeRussian));
            FakeTextureField(77,10,20,200,knife,19);
            check("SDL_field_text_stack_third_slot",g_wrapperText==kBladeRussian&&g_leafText==g_wrapperText);
            FakeTextureField2(77,10,20,200,knife,19);
            check("SDL_second_field_wrapper",g_wrapperText==kBladeRussian);
            before=ReadTraceTranslationCount();FakeTextureStreamSource();
            check("SDL_complete_stream_translated",g_wrapperText==kBladeRussian&&g_leafText==g_wrapperText);
            check("SDL_stream_children_translated_once",ReadTraceTranslationCount()==before+1);
            check("SDL_English_token_source_preserved",ReadTracePresentationBuffer(true,true)=="Knife");
            check("SDL_English_aggregate_postcondition_preserved",ReadTraceAssemblyBuffer(true)=="Knife");
            check("SDL_private_scratch_restored_after_render",ReadTraceAssemblyBuffer().empty());
            before=ReadTraceTranslationCount();FakeUserMarker();
            check("SDL_user_note_stream_not_translated",g_wrapperText=="Knife"&&ReadTraceTranslationCount()==before);
            FakeInputEditor();
            check("SDL_user_input_editor_not_translated",g_leafText=="Knife"&&ReadTraceTranslationCount()==before);
            FakeOverlaySource();
            check("overlay_own_stack_ABI_exact_lookup",g_leafText==kBladeRussian);
            TraceStats stats = ReadTraceStats();
            check("no_abi_or_callback_failures", stats.failures == 0 && g_bodyFailures == 0);
            check("all_native_continuations_balanced", stats.depth == 0 && stats.entered == stats.left + stats.abandoned);
            const std::wstring legacyPath=translationDirectory+L"\\legacy_catalog.json",modelPath=translationDirectory+L"\\runtime_grammar.json";
            const std::string compact=R"({"schema":2,"version":"0.12","units":[{"id":"native_compact","kind":"exact","source":"Knife","translation":"Нож","variables":{},"forms":{},"apply_translation":1}]})";
            const std::string model=R"({"schema":1,"nodes":{"g":{"op":"literal","text":"Knife"}},"aliases":{},"families":[],"unit_bindings":{"g":{"component":{"unit":"native_compact","arguments":{}}}},"generators":{},"transformed":{},"synthetic":{},"contracts":{"native_compact":{"source":"Knife","variables":{}}}})";
            check("compact_native_fixture_written",WriteFixture(legacyPath,Fixture(true))&&WriteFixture(modelPath,model)&&WriteFixture(catalogPath,compact));
            check("compact_native_snapshot_loaded",ReloadTraceTranslationTests());
            SetLastError(0x11223344);FakeSource("Knife",7);
            check("compact_catalogue_reaches_real_hook_callback",g_leafText==kKnifeRussian);
            FakeWhole("You are hungry.");check("compact_keeps_legacy_fallback_for_other_text",g_leafText==kHungryRussian);
            check("compact_malformed_update_written",WriteFixture(catalogPath,"{broken"));
            check("compact_malformed_update_rejected",!ReloadTraceTranslationTests());
            SetLastError(0x11223344);FakeSource("Knife",7);
            check("compact_native_last_good_snapshot_retained",g_leafText==kKnifeRussian);
            check("compact_native_fixture_restored",WriteFixture(catalogPath,Fixture(true)));
            DeleteFileW(legacyPath.c_str());DeleteFileW(modelPath.c_str());
            check("compact_to_legacy_rollback_loaded",ReloadTraceTranslationTests());
            stats=ReadTraceStats();check("compact_native_stack_and_ABI_balanced",stats.failures==0&&g_bodyFailures==0&&stats.depth==0&&stats.entered==stats.left+stats.abandoned);
        }
    } catch (const std::exception&) {
        check("no_native_test_exception", false);
    } catch (...) {
        check("no_unknown_native_test_exception", false);
    }
    if (installed) {
        RemoveTraceTests();
        SetLastError(0x11223344);
        int result = FakeSource("Knife", 0);
        check("unhook_restores_original_execution", g_leafText == "Knife" && result == 105);
    }
    // Keep a valid isolated fixture for inspection/re-running; never touch translate/ beside the DLL.
    check("isolated_fixture_restored_after_malformed_test", WriteFixture(catalogPath, Fixture(true)));
    unsigned failures = 0;
    for (const auto& item : checks) if (!item.second) ++failures;
    FILE* stream = nullptr;
    std::wstring reportPath = dllDirectory + L"\\TRANSLATION_SELFTEST.json";
    if (_wfopen_s(&stream, reportPath.c_str(), L"wb") || !stream) return 0;
    fprintf(stream, "{\"checks\":%u,\"failures\":%u,\"results\":[\n", static_cast<unsigned>(checks.size()), failures);
    for (size_t i = 0; i < checks.size(); ++i)
        fprintf(stream, "%s{\"name\":\"%s\",\"passed\":%s}\n", i ? "," : "", checks[i].first.c_str(), checks[i].second ? "true" : "false");
    fprintf(stream, "]}\n");
    if (fclose(stream)) return 0;
    return failures ? 0 : 1;
}
