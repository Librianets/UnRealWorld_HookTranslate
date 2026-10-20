// Included inside TranslationCatalog.cpp's private namespace. Uses the same
// bounded UTF-8 JSON parser as the existing engine. No game memory access here.
struct CompactValue {
    std::string text, source, gender, unit, selectedForm;
    std::map<std::string,std::string> forms, agreement, genderByForm, bindings;
    bool active=false,preserve=false;
};
static std::string CText(const Json& j,const char* k,const std::string& fallback={}) {
    const Json* v=j.Find(k);return v&&v->type==Json::String?v->text:fallback;
}
static int CInt(const Json& j,const char* k,int fallback=0) {
    const Json* v=j.Find(k);return v&&v->type==Json::Number?std::stoi(v->text):fallback;
}
static std::map<std::string,std::string> CForms(const Json* j) {
    std::map<std::string,std::string> out;if(!j)return out;
    if(j->type!=Json::Object)throw std::runtime_error("Compact forms must be object");
    for(const auto& v:j->object){if(v.second.type!=Json::String)throw std::runtime_error("Compact form must be string");out[v.first]=v.second.text;}
    return out;
}
static std::string CJournalVisible(std::string text) {
    // Presentation only; no saved text or game buffers are changed. Apply the
    // native ASCII controls to UTF-8 RU without truncating multibyte output.
    auto left=text.find('{'),right=text.find('}');
    if(left!=std::string::npos&&right!=std::string::npos&&right>left+1)text=text.substr(right+1);
    std::string out;
    for(size_t i=0;i<text.size();++i){
        if(i>1&&text[i]==' '&&text[i-1]==' ')++i;
        if(i>=text.size())break;
        if(text[i]=='@'||(i==0&&std::string("!#&+:=>?_").find(text[i])!=std::string::npos))continue;
        out+=text[i];
    }
    return out;
}
static std::string CVisibleDocument(const std::string& text) {
    std::string out,line;bool skip=false;
    auto add=[&](){if(skip){skip=false;line.clear();return;}if(!line.empty()&&line[0]=='%'){skip=true;line.clear();return;}if(line.rfind("//",0)==0||line.rfind("##",0)==0||line.find("***")!=std::string::npos){line.clear();return;}out+=' '+line;line.clear();};
    for(char c:text){if(c=='\n')add();else if(c!='\r')line+=c;}if(!line.empty())add();
    std::string normalized;bool space=false;for(char c:out){if(c==' '||c=='\r'||c=='\n'||c=='\t'){space=!normalized.empty();continue;}if(space)normalized+=' ';normalized+=c;space=false;}return normalized;
}
static std::vector<Token> CTokens(const std::string& s,std::set<std::string>* names=nullptr) {
    std::vector<Token> out;std::string text;
    auto flush=[&](){if(!text.empty()){out.push_back({false,text});text.clear();}};
    for(size_t i=0;i<s.size();) {
        char c=s[i++];
        if(c!='{'&&c!='}'){text+=c;continue;}
        if(i<s.size()&&s[i]==c){++i;text+=c;continue;}
        if(c=='}')throw std::runtime_error("Compact unescaped brace");
        size_t end=s.find('}',i);if(end==std::string::npos)throw std::runtime_error("Compact unclosed parameter");
        auto name=s.substr(i,end-i);if(name.empty()||name.size()>64)throw std::runtime_error("Compact invalid parameter");
        for(char a:name)if(!((a>='a'&&a<='z')||(a>='A'&&a<='Z')||(a>='0'&&a<='9')||a=='_'))throw std::runtime_error("Compact invalid parameter name");
        flush();out.push_back({true,name});if(names)names->insert(name);i=end+1;
    }
    flush();if(out.size()>512)throw std::runtime_error("Compact token limit");return out;
}
static std::string CCase(std::string text,const std::string& op,bool finnish=false) {
    bool upper=op=="upper_game"||op=="upper_first_game"||op=="upper_first";
    bool first=op.find("first")!=std::string::npos;std::string out;
    for(size_t i=0;i<text.size();) {
        unsigned char c=text[i++];uint32_t cp=c;
        if(c>=0xC0){unsigned n=c<0xE0?1:c<0xF0?2:3;cp=c&((1u<<(6-n))-1);while(n--&&i<text.size())cp=(cp<<6)|((unsigned char)text[i++]&63);}
        if(!first||out.empty()){
            if(upper){if(cp>='a'&&cp<='z')cp-=32;else if(cp>=0x430&&cp<=0x44F)cp-=32;else if(cp==0x451)cp=0x401;else if(finnish&&(cp==0xE4||cp==0xF6||cp==0xE5))cp-=32;}
            else {if(cp>='A'&&cp<='Z')cp+=32;else if(cp>=0x410&&cp<=0x42F)cp+=32;else if(cp==0x401)cp=0x451;else if(finnish&&(cp==0xC4||cp==0xD6||cp==0xC5))cp+=32;}
        }
        AddCodepoint(out,cp);
    }
    return out;
}
struct CompactUnit {
    std::string id, source, plainSource, translation,kind;
    int apply=0;
    Json data;
    std::vector<Token> sourceTokens;
};
class CompactCatalog {
    Json model_;
    bool finnish_=false;
    std::unordered_map<std::string,CompactUnit> units_;
    std::unordered_map<std::string,std::vector<std::string>> exact_;
    std::unordered_map<uint32_t,std::vector<std::string>> roots_;
    struct Route {uint32_t caller,target;int priority;std::string node;bool foldCase,capitalize;};
    std::vector<Route> routes_;
    const Lexicon* legacy_=nullptr;
    struct Cached {bool matched;std::string text,id,reason;};
    mutable std::mutex cacheMutex_;
    mutable std::unordered_map<std::string,Cached> cache_;
    struct Work {
        size_t budget=120000;unsigned depth=0,foldDepth=0,nestedDepth=0;bool exhausted=false;
        const std::vector<NpcSourceIdentity>* identities=nullptr;
        std::string reason;
        std::unordered_map<std::string,std::vector<CompactValue>> memo;
        std::set<std::string> active;
        bool Tick(){if(!budget){exhausted=true;reason="grammar_budget";return false;}--budget;return true;}
    };
    const Json& Node(const std::string& gid) const {return model_.Find("nodes")->object.at(gid);}
    static bool EqualAt(const std::string& text,size_t at,const std::string& literal,const Work& w){
        if(at>text.size()||text.size()-at<literal.size())return false;
        for(size_t i=0;i<literal.size();++i){unsigned char a=(unsigned char)text[at+i],b=(unsigned char)literal[i];if(w.foldDepth){if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;}if(a!=b)return false;}return true;
    }
    static bool Merge(CompactValue& a,const CompactValue& b) {
        for(const auto& v:b.bindings){auto it=a.bindings.find(v.first);if(it!=a.bindings.end()&&it->second!=v.second)return false;}
        a.bindings.insert(b.bindings.begin(),b.bindings.end());a.active=a.active||b.active;return true;
    }
    static void Push(std::vector<CompactValue>& out,const CompactValue& v,Work& w) {
        for(const auto& prior:out)if(prior.text==v.text&&prior.forms==v.forms&&prior.gender==v.gender&&prior.agreement==v.agreement&&prior.genderByForm==v.genderByForm&&prior.selectedForm==v.selectedForm&&prior.bindings==v.bindings)return;
        if(out.size()>=32){w.exhausted=true;w.reason="grammar_ambiguity_limit";return;}out.push_back(v);
    }
    bool Select(CompactValue& value,const std::string& form,Work& w)const {
        if(form.empty()||form=="nominative")return true;
        value.selectedForm=form;
        auto found=value.forms.find(form);
        if(found!=value.forms.end()){value.text=found->second;auto g=value.genderByForm.find(form);if(g!=value.genderByForm.end())value.gender=g->second;return true;}
        if(form=="one")return true;
        w.reason="missing_form:"+form+":"+value.unit;return false;
    }
    std::vector<CompactValue> Pattern(const std::string& text,const std::vector<Token>& tokens,
                                    const Json& arguments,const CompactUnit* unit,const std::string& form,Work& w)const {
        std::vector<CompactValue> result;
        if(!tokens.empty()&&!tokens.front().variable&&!EqualAt(text,0,tokens.front().text,w))return result;
        if(!tokens.empty()&&!tokens.back().variable&&(text.size()<tokens.back().text.size()||!EqualAt(text,text.size()-tokens.back().text.size(),tokens.back().text,w)))return result;
        std::map<std::string,std::string> captured;
        std::function<void(size_t,size_t)> split;
        auto render=[&](){
            // A disabled translation still has a source grammar. Validate its
            // arguments before preserving English: otherwise a diagnostic
            // pattern such as {a}{b} becomes an unrestricted wildcard.
            std::map<std::string,CompactValue> values;
            // An audio-control argument may disappear from a derived saved
            // message pattern. It contributes no visible text to its RU view.
            if(unit)for(const auto& token:CTokens(unit->translation))if(token.variable&&captured.find(token.text)==captured.end()){
                const Json* policy=unit->data.Find("journal_suppressed_arguments");
                if(policy)for(const auto& name:policy->array)if(name.text==token.text){CompactValue empty;empty.preserve=true;values[token.text]=empty;}
            }
            std::vector<std::string> order;
            for(const auto& t:tokens)if(t.variable&&std::find(order.begin(),order.end(),t.text)==order.end())order.push_back(t.text);
            std::function<void(size_t,CompactValue)> assemble;
            assemble=[&](size_t index,CompactValue combined){
                if(!w.Tick()||w.exhausted)return;
                if(index<order.size()){
                    const auto& name=order[index];auto arg=arguments.object.find(name);if(arg==arguments.object.end())return;
                    std::string requested;
                    const Json* rules=unit?unit->data.Find("argument_forms"):nullptr;
                    if(rules){auto it=rules->object.find(name);if(it!=rules->object.end()){
                        const Json& rule=it->second;requested=CText(rule,"case");
                        if(CText(rule,"operation")=="count_agreement"){
                            auto number=captured.find(CText(rule,"number"));if(number==captured.end()||!Number(number->second))return;requested=Plural(number->second,finnish_);
                        }
                    }}
                    auto candidates=Eval(CText(arg->second,"graph"),captured[name],requested,"component",w);
                    if(unit)if(const Json* literals=unit->data.Find("literal_values"))if(const Json* replacement=literals->Find(name.c_str()))for(auto& v:candidates)v.text=replacement->text;
                    if(unit)if(const Json* dictionary=unit->data.Find("argument_translations"))if(const Json* options=dictionary->Find(name.c_str())){
                        const Json* translated=options->Find(captured[name].c_str());if(!translated)candidates.clear();else for(auto& v:candidates)v.text=translated->text;
                    }
                    for(auto child:candidates){
                        // Recovered presentation recipes type their independent
                        // arguments explicitly. Hidden record IDs cannot bind
                        // two different actors/items in the same sentence.
                        if(unit&&unit->data.Find("independent_arguments")&&unit->data.Find("independent_arguments")->boolean)child.bindings.clear();
                        auto merged=combined;if(!Merge(merged,child))continue;values[name]=child;assemble(index+1,merged);}
                    values.erase(name);return;
                }
                std::string pattern;
                if(unit){
                    if(unit->apply!=1){combined.text=text;combined.source=text;combined.unit=unit->id;combined.active=true;combined.preserve=true;Push(result,combined,w);return;}
                    pattern=unit->translation;
                    auto forms=CForms(unit->data.Find("forms"));
                    if(!form.empty()&&form!="nominative"){
                        auto f=forms.find(form);
                        if(f!=forms.end())pattern=f->second;
                        else if(form!="one"&&CText(unit->data,"head").empty()){w.reason="missing_template_form:"+form+":"+unit->id;return;}
                    }
                    const Json* plural=unit->data.Find("plural");
                    if(plural&&!plural->object.empty()){
                        auto n=captured.find(CText(*plural,"variable"));if(n==captured.end()||!Number(n->second))return;
                        auto pf=plural->Find("forms");auto f=pf?pf->Find(Plural(n->second,finnish_).c_str()):nullptr;if(!f)return;pattern=f->text;
                    }
                }
                auto output=[&](std::string& result,const std::string& category,const std::string& headText)->bool{
                    auto actual=values;const std::string headName=unit?CText(unit->data,"head"):"";
                    if(!headName.empty()&&!category.empty()){auto h=actual.find(headName);if(h==actual.end())return false;h->second.text=headText;auto g=h->second.genderByForm.find(category);if(g!=h->second.genderByForm.end())h->second.gender=g->second;}
                    const Json* rules=unit?unit->data.Find("argument_forms"):nullptr;
                    if(rules)for(const auto& pair:rules->object){
                        auto who=CText(pair.second,"agree_with");if(who.empty())continue;
                        auto head=actual.find(who),adjective=actual.find(pair.first);if(head==actual.end()||adjective==actual.end())return false;
                        if(adjective->second.text.find_first_not_of(" \t")==std::string::npos)continue;
                        std::string gender=head->second.gender,key=gender=="m"?"masculine":gender=="f"?"feminine":gender=="n"?"neuter":gender;
                        std::string counted=category;
                        if(counted.empty()&&(adjective->second.selectedForm=="few"||adjective->second.selectedForm=="many"))counted=adjective->second.selectedForm;
                        if(counted.empty()&&(head->second.selectedForm=="few"||head->second.selectedForm=="many"))counted=head->second.selectedForm;
                        if(finnish_) {
                            std::string selected=category.empty()?head->second.selectedForm:category;
                            if(selected.empty())selected=adjective->second.selectedForm;
                            if(selected=="affiliation"||selected=="adjective")selected.clear();
                            if(selected=="other"||selected=="few"||selected=="many")selected="partitive";
                            if(selected=="one"||selected=="nominative"||selected.empty())key=gender=="plural"?"plural":"masculine";
                            else key=selected+(gender=="plural"&&selected!="plural"&&
                                (selected.size()<7||selected.substr(selected.size()-7)!="_plural")?"_plural":"");
                        } else {
                            if(counted=="few"||counted=="many")key="count_"+counted;
                            else if(category=="other"||category=="genitive")key="genitive_"+key;
                            else if(!category.empty()&&category!="one"&&category!="nominative"&&category!="plural")return false;
                            else if(category=="plural")key="plural";
                        }
                        auto f=adjective->second.agreement.find(key);if(f==adjective->second.agreement.end()){w.reason="missing_gender_agreement:"+unit->id;return false;}
                        auto old=adjective->second.text;auto first=old.find_first_not_of(" \t"),last=old.find_last_not_of(" \t");
                        adjective->second.text=(first==std::string::npos?"":old.substr(0,first))+f->second+(last==std::string::npos?"":old.substr(last+1));
                    }
                    result.clear();const auto targetTokens=unit?CTokens(pattern):tokens;
                    for(const auto& t:targetTokens){if(t.variable){auto it=actual.find(t.text);if(it==actual.end())return false;result+=it->second.text;}else result+=t.text;if(result.size()>kMaxOutput)return false;}return true;
                };
                std::string rendered;if(!output(rendered,"",""))return;
                combined.source=text;combined.text=rendered;
                if(unit){combined.unit=unit->id;combined.active=true;combined.gender=CText(unit->data,"gender");combined.agreement=CForms(unit->data.Find("agreement"));combined.genderByForm=CForms(unit->data.Find("gender_by_form"));
                    for(const auto& f:CForms(unit->data.Find("forms"))){
                        std::string expanded;bool throughHead=false;
                        if(finnish_&&!CText(unit->data,"head").empty()){
                            auto h=values.find(CText(unit->data,"head"));
                            if(h!=values.end()){
                                auto hf=h->second.forms.find(f.first);
                                if(hf!=h->second.forms.end()){
                                    std::string saved=pattern;pattern=f.second;
                                    throughHead=output(expanded,f.first,hf->second);pattern=saved;
                                    if(!throughHead)return;
                                }
                            }
                        }
                        if(!throughHead)for(const auto& t:CTokens(f.second)){if(t.variable){auto it=values.find(t.text);if(it==values.end())return;expanded+=it->second.text;}else expanded+=t.text;}
                        combined.forms[f.first]=expanded;
                    }
                    std::string head=CText(unit->data,"head");
                    if(!head.empty()){auto h=values.find(head);if(h==values.end())return;combined.gender=h->second.gender;combined.genderByForm=h->second.genderByForm;
                        for(const auto& f:h->second.forms){std::string renderedForm;if(!combined.forms.count(f.first)&&output(renderedForm,f.first,f.second))combined.forms[f.first]=renderedForm;}
                        if(!Select(combined,form,w))return;
                    }
                }else if(values.size()==1){auto saved=combined;combined=values.begin()->second;combined.text=rendered;Merge(combined,saved);}
                Push(result,combined,w);
            };
            assemble(0,CompactValue());
        };
        split=[&](size_t index,size_t pos){
            if(!w.Tick()||w.exhausted)return;
            if(index==tokens.size()){if(pos==text.size())render();return;}
            const Token& t=tokens[index];
            if(!t.variable){if(EqualAt(text,pos,t.text,w))split(index+1,pos+t.text.size());return;}
            auto old=captured.find(t.text);if(old!=captured.end()){if(text.compare(pos,old->second.size(),old->second)==0)split(index+1,pos+old->second.size());return;}
            const Json* argument=arguments.Find(t.text.c_str());const Json* hint=nullptr;
            if(argument)if(const Json* hints=model_.Find("source_hints"))hint=hints->Find(CText(*argument,"graph").c_str());
            size_t last=text.size();if(hint&&hint->Find("maximum"))last=std::min(last,pos+(size_t)CInt(*hint,"maximum",0));
            for(size_t end=pos;end<=last&&!w.exhausted;++end){
                // Reject impossible source partitions before constructing all
                // later captures. These are proved bounds of the same child
                // grammar, not target-language or guessed word boundaries.
                if(hint){size_t length=end-pos;auto prefix=CText(*hint,"prefix"),suffix=CText(*hint,"suffix");
                    if(length<(size_t)CInt(*hint,"minimum",0)||length<prefix.size()||length<suffix.size())continue;
                    if(!EqualAt(text,pos,prefix,w)||!EqualAt(text,end-suffix.size(),suffix,w))continue;
                }
                if(index+1==tokens.size()&&end!=text.size())continue;
                if(index+1<tokens.size()&&!tokens[index+1].variable&&!EqualAt(text,end,tokens[index+1].text,w))continue;
                auto piece=text.substr(pos,end-pos);
                // Source acceptance of each independently typed argument is
                // necessary for any complete parse. Check it now rather than
                // enumerating the Cartesian partitions of later arguments.
                // Target forms and cross-argument agreement still run below
                // after all captures have been collected.
                if(argument&&index+1<tokens.size()&&Eval(CText(*argument,"graph"),piece,"","component",w).empty())continue;
                captured[t.text]=piece;split(index+1,end);captured.erase(t.text);
            }
        };
        split(0,0);return result;
    }
    std::vector<CompactValue> Eval(const std::string& gid,const std::string& text,const std::string& form,const std::string& role,Work& w)const {
        if(const auto* hints=model_.Find("source_hints"))if(const auto* hint=hints->Find(gid.c_str())){
            if(text.size()<(size_t)CInt(*hint,"minimum",0))return {};
            if(hint->Find("maximum")&&text.size()>(size_t)CInt(*hint,"maximum"))return {};
            auto prefix=CText(*hint,"prefix"),suffix=CText(*hint,"suffix");
            if(!EqualAt(text,0,prefix,w)||suffix.size()>text.size()||!EqualAt(text,text.size()-suffix.size(),suffix,w))return {};
        }
        if(!w.Tick()||w.depth>=64||gid.empty())return {};
        const std::string memo=gid+'\x1F'+role+'\x1F'+form+'\x1F'+std::to_string(w.nestedDepth)+(w.foldDepth?"F:":"C:")+text;
        auto cached=w.memo.find(memo);if(cached!=w.memo.end())return cached->second;
        if(!w.active.insert(memo).second)return {};
        ++w.depth;auto result=EvalNode(gid,text,form,role,w);--w.depth;w.active.erase(memo);
        if(w.memo.size()<4096)w.memo.emplace(memo,result);return result;
    }
    std::vector<CompactValue> EvalNode(const std::string& gid,const std::string& text,const std::string& form,const std::string& role,Work& w)const {
        const Json& n=Node(gid);const std::string op=CText(n,"op");std::vector<CompactValue> out;
        const Json* bindings=model_.Find("unit_bindings")->Find(gid.c_str());
        if(bindings){const Json* binding=bindings->Find(role.c_str());if(!binding)binding=bindings->Find("component");if(!binding)binding=bindings->Find("message");
            if(binding){const CompactUnit* selected=&units_.at(CText(*binding,"unit"));CompactUnit contextual;
                if(const Json* rules=selected->data.Find("binding_rules"))if(const Json* policy=rules->Find(CText(*binding,"policy_node",gid).c_str())){
                    contextual=*selected;
                    for(const auto& field:policy->object)if(field.first!="reason")contextual.data.object[field.first]=field.second;
                    contextual.translation=CText(*policy,"translation",selected->translation);selected=&contextual;
                }
                if(const Json* source=binding->Find("source_override")){
                    if(selected!=&contextual)contextual=*selected;
                    contextual.source=source->text;contextual.sourceTokens=CTokens(source->text);contextual.plainSource.clear();
                    if(contextual.kind=="exact")for(const auto& token:contextual.sourceTokens)contextual.plainSource+=token.text;
                    if(const Json* empty=binding->Find("journal_empty_component"))if(empty->boolean)contextual.translation.clear();
                    if(const Json* suppressed=binding->Find("suppressed_arguments"))contextual.data.object["journal_suppressed_arguments"]=*suppressed;
                    selected=&contextual;
                }
                const auto& u=*selected;
                if(u.kind=="exact"){
                    if(u.plainSource.size()!=text.size()||!EqualAt(text,0,u.plainSource,w))return {};
                    CompactValue v;v.text=text;if(u.apply==1){v.text.clear();for(const auto& t:CTokens(u.translation))v.text+=t.text;}v.source=text;v.unit=u.id;v.active=true;v.forms=CForms(u.data.Find("forms"));v.agreement=CForms(u.data.Find("agreement"));v.genderByForm=CForms(u.data.Find("gender_by_form"));v.gender=CText(u.data,"gender");
                    if(u.apply!=1){v.forms.clear();v.agreement.clear();v.preserve=true;out.push_back(v);return out;}
                    if(Select(v,form,w))out.push_back(v);return out;
                }
                return Pattern(text,u.sourceTokens,*binding->Find("arguments"),&u,form,w);
            }
        }
        if(op=="literal"){auto original=CText(n,"text");if(original.size()==text.size()&&EqualAt(text,0,original,w)){CompactValue v;v.text=text;v.source=text;out.push_back(v);}return out;}
        if(op=="slot"){
            bool nested=CText(n,"type")=="nested_message";
            if(nested&&w.nestedDepth>=(unsigned)CInt(n,"maximum_depth",8)){w.reason="nested_message_depth";return {};}
            if(nested)++w.nestedDepth;
            auto value=Eval(CText(*n.Find("value"),"name"),text,form,"component",w);
            if(nested)--w.nestedDepth;
            return value;
        }
        if(op=="choice"){
            if(const auto* priority=n.Find("preferred_domain")){
                std::string domain=priority->type==Json::String?priority->text:CText(*priority,"node");
                bool eligible=true;
                if(const auto* atoms=priority->Find("source_atoms")){
                    eligible=false;std::string lower=text;for(char& c:lower)if(c>='A'&&c<='Z')c+=32;
                    auto word=[](unsigned char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c>=128;};
                    for(const auto& atom:atoms->array){
                        size_t at=lower.find(atom.text);
                        while(at!=std::string::npos){size_t end=at+atom.text.size();
                            if((!at||!word(lower[at-1]))&&(end==lower.size()||!word(lower[end]))){eligible=true;break;}
                            at=lower.find(atom.text,at+1);
                        }
                        if(eligible)break;
                    }
                }
                if(eligible){auto selected=Eval(domain,text,form,role,w);if(!selected.empty())return selected;}
            }
            // Explicit reviewed atom overrides avoid competing old compound
            // analyses. Only exact source spellings in the model take priority.
            if(const auto* preferred=n.Find("preferred_exact")){
                std::string key=text;for(char& c:key)if(c>='A'&&c<='Z')c+=32;
                if(const auto* leaf=preferred->Find(key.c_str()))
                    return Eval(leaf->text,text,form,role,w);
            }
            if(const Json* gen=model_.Find("generators")->Find(gid.c_str())){
                for(const auto& option:n.Find("options")->array){const Json& leaf=Node(CText(*option.Find("value"),"name"));auto source=CText(leaf,"text");if(source.size()!=text.size()||!EqualAt(text,0,source,w))continue;
                    unsigned mask=(unsigned)std::stoul(CText(option,"id"));CompactValue value;value.active=true;value.source=text;
                    std::vector<std::string> parts;
                    for(const auto& item:gen->Find("items")->array)if(mask&(1u<<CInt(item,"bit"))){auto child=CText(item,"graph");auto words=Eval(child,CText(Node(child),"text"),form,"component",w);if(words.empty())return {};for(const auto& v:words)if(v.text!=words[0].text)return {};parts.push_back(words[0].text);}
                    for(size_t i=0;i<parts.size();++i){if(i)value.text+=i+1==parts.size()?" "+CText(*gen,"ru_conjunction","и")+" ":CText(*gen,"separator",", ");value.text+=parts[i];}
                    value.bindings["choice:"+CText(n,"key")]=CText(option,"id");out.push_back(value);return out;
                }return {};
            }
            std::string identity;
            if(n.Find("identity_choice")&&w.identities){
                for(const auto& proof:*w.identities)if((proof.branch=="name"||proof.branch=="description"||proof.branch=="invalid")&&proof.label.size()==text.size()&&EqualAt(text,0,proof.label,w)){
                    if(identity.empty())identity=proof.branch;
                    else if(identity!=proof.branch){identity="conflict";break;}
                }
            }
            for(const auto& option:n.Find("options")->array){
                if(!identity.empty()&&identity!="conflict")if(const Json* choices=n.Find("identity_choice")){
                    const Json* modes=choices->Find(CText(option,"id").c_str());bool allowed=false;
                    if(modes){if(modes->type==Json::String)allowed=modes->text==identity;
                        else for(const auto& mode:modes->array)if(mode.text==identity)allowed=true;}
                    if(!allowed)continue;
                }
                for(auto v:Eval(CText(*option.Find("value"),"name"),text,form,role,w)){
                if(const auto* presentation=n.Find("presentation_choice"))if(presentation->boolean){v.bindings.clear();Push(out,v,w);continue;}
                std::string key="choice:"+CText(n,"key"),id=CText(option,"id");auto old=v.bindings.find(key);if(old!=v.bindings.end()&&old->second!=id)continue;v.bindings[key]=id;Push(out,v,w);
            }if(w.exhausted)break;}return out;
        }
        if(op=="parameter"){
            if(CText(n,"source_identity")=="current_player_name"&&w.identities){
                bool supplied=false,accepted=false;
                for(const auto& proof:*w.identities)if(proof.branch=="player_name"){
                    supplied=true;if(proof.label.size()==text.size()&&EqualAt(text,0,proof.label,w))accepted=true;
                }
                if(supplied&&!accepted)return {};
            }
            std::string type=CText(n,"type"),name=CText(n,"name");bool numeric=type=="integer"||type=="float"||type=="decimal_number"||type=="formatted_float";
            if(numeric){if(!Number(text)||((type=="integer")&&text.find('.')!=std::string::npos))return {};double value=std::strtod(text.c_str(),nullptr);if(!std::isfinite(value))return {};
                for(const auto& test:std::vector<std::pair<const char*,bool>>{{"minimum",true},{"maximum",false}}){auto p=n.Find(test.first);if(p&&p->type==Json::Number){double bound=std::strtod(p->text.c_str(),nullptr);if(test.second?value<bound:value>bound)return {};}}
                auto multiple=n.Find("multiple_of");if(multiple&&multiple->type==Json::Number){double step=std::strtod(multiple->text.c_str(),nullptr);if(step<=0||std::fmod(value,step)!=0)return {};}
                CompactValue v;v.text=text;v.source=text;v.bindings["parameter:"+name]=text;out.push_back(v);return out;
            }
            bool preserve=(n.Find("preserve")&&n.Find("preserve")->boolean)||CText(n,"translation_policy")=="preserve_resource_key"||CText(n,"translation_policy")=="preserve_path";
            if(const Json* seed=n.Find("seed_domain")){
                auto a=model_.Find("aliases")->Find(seed->text.c_str());
                if(a)out=Eval(a->text,text,form,"component",w);
                if(!out.empty()){
                    if(preserve){CompactValue v;v.source=text;v.text=text;v.preserve=true;v.bindings["parameter:"+name]=text;return {v};}
                    return out;
                }
                if(n.Find("closed_seed_domain")&&n.Find("closed_seed_domain")->boolean)return {};
            }
            bool raw=type=="proper_name"||type=="generated_proper_name"||type=="person_name"||type=="user_defined_proper_name"||type=="user_text"||type=="user_input"||type=="configuration_value"||type=="filesystem_path"||type=="resource_name"||type=="external_resource"||type=="network_address"||type=="record_label"||type=="control_character"||type=="bitmask_string";
            if(!raw){auto it=legacy_->find(text);if(type=="weapon_plural"&&it!=legacy_->end()&&!it->second.ambiguous){CompactValue v;v.source=text;v.text=it->second.translation;v.forms=it->second.forms;v.active=true;if(Select(v,form,w))out.push_back(v);return out;}w.reason="unknown_parameter_domain:"+name;return {};}
            if(text.size()>1024||text.find('\0')!=std::string::npos)return {};
            if(n.Find("maximum_characters")){
                size_t characters=0;for(unsigned char c:text)if((c&0xC0)!=0x80)++characters;
                if(characters>(size_t)CInt(n,"maximum_characters",1024))return {};
            }
            if(n.Find("maximum_bytes")&&text.size()>(size_t)CInt(n,"maximum_bytes",1024))return {};
            if(text.find_first_of(CText(n,"forbidden_characters"))!=std::string::npos)return {};
            CompactValue v;v.text=text;v.source=text;v.bindings["parameter:"+name]=text;
            v.preserve=true;
            if(name.find("player")==std::string::npos&&type=="generated_proper_name"){
                auto it=legacy_->find(text);if(it!=legacy_->end()&&!it->second.ambiguous){v.text=it->second.translation;v.forms=it->second.forms;v.active=true;v.preserve=false;}
            }
            if(Select(v,form,w))out.push_back(v);return out;
        }
        if(op=="transform"){
            std::string name=CText(n,"name"),child=CText(*n.Find("value"),"name");
            if(name=="journal_message_en"||name=="journal_message_case_preserved_en")if(const Json* projected=n.Find("projected_value")){
                ++w.foldDepth;
                std::vector<CompactValue> values;const Json* exact=nullptr;
                if(const Json* dispatch=n.Find("projected_exact")){
                    std::string key=text;for(char& c:key)if(c>='A'&&c<='Z')c+=32;
                    exact=dispatch->Find(key.c_str());
                }
                if(exact)for(const Json& branch:exact->array){for(auto value:Eval(branch.text,text,form,role,w)){value.bindings.clear();Push(values,value,w);}}
                else values=Eval(CText(*projected,"name"),text,form,role,w);
                for(auto value:values){
                    if(!value.preserve)value.text=CJournalVisible(value.text);
                    value.source=text;Push(out,value,w);
                }
                --w.foldDepth;return out;
            }
            auto apply=[&](const std::string& original){
                std::string requested=form;
                if(form.empty()&&(name=="plural_game"||name=="plural_en"))requested="plural";
                if(form.empty()&&name=="singular_game")requested="one";
                auto values=Eval(child,original,requested,role,w);
                if(values.empty()&&requested=="plural"){
                    auto word=legacy_->find(text);
                    if(word!=legacy_->end()&&!word->second.ambiguous)for(auto v:Eval(child,original,"",role,w)){
                        auto one=word->second.forms.find("one");
                        if(one!=word->second.forms.end()&&CCase(v.text,"lower_game",finnish_)==CCase(one->second,"lower_game",finnish_)){v.text=word->second.translation;values.push_back(v);}
                    }
                }
                for(auto v:values){
                    if(v.preserve){v.text=text;v.source=text;Push(out,v,w);continue;}
                    if(name=="lower"||name=="lower_game"||name=="lower_ascii_crt"||name=="upper_game"||name=="upper_first_game"||name=="upper_first"||name=="lower_first_game"||name=="lower_first")v.text=CCase(v.text,name,finnish_);
                    if(name=="journal_message_en"||name=="journal_message_case_preserved_en")v.text=CJournalVisible(v.text);
                    if(name=="quote_if_unquoted_en"&&(v.text.empty()||v.text[0]!='"'))v.text='"'+v.text+'"';
                    if(name=="list_and_en"||name=="list_or_en"||name=="stream_commas_3"||name=="stream_commas_2"){auto pos=v.text.rfind(", ");if(pos!=std::string::npos)v.text.replace(pos,2,finnish_?(name=="list_or_en"?" tai ":" ja "):(name=="list_or_en"?" или ":" и "));}
                    v.source=text;Push(out,v,w);
                }
            };
            const Json* table=model_.Find("transformed")->Find(gid.c_str());const Json* originals=table?table->Find(text.c_str()):nullptr;
            if(originals){for(const auto& v:originals->array)apply(v.text);return out;}
            if(table&&w.foldDepth){
                for(const auto& candidate:table->object)if(candidate.first.size()==text.size()&&EqualAt(text,0,candidate.first,w))for(const auto& v:candidate.second.array)apply(v.text);
                return out;
            }
            if(table)return out; // A closed finite source domain has no heuristic alternatives.
            if(name=="quote_if_unquoted_en"){
                // Native5362E0 only inspects byte0. An existing initial quote
                // is preserved even when no closing quote exists.
                if(text.empty()||text[0]!='"')return {};
                apply(text);
                if(text.size()>=2&&text.back()=='"'){
                    std::string inner=text.substr(1,text.size()-2);
                    if(inner.empty()||inner[0]!='"')apply(inner);
                }
                return out;
            }
            if(name=="article_en"){
                if(text.rfind("an ",0)==0)apply(text.substr(3));else if(text.rfind("a ",0)==0)apply(text.substr(2));else apply(text);return out;
            }
            if(name=="lower"||name=="lower_game"||name=="lower_ascii_crt"||name=="upper_first_game"||name=="upper_first"||name=="lower_first_game"||name=="lower_first"||name=="upper_game"){
                bool fold=name=="lower"||name=="lower_game"||name=="lower_ascii_crt"||name=="upper_game";if(fold)++w.foldDepth;
                apply(text);if(text.size()){std::string other=text;other[0]=other[0]>='a'&&other[0]<='z'?(char)(other[0]-32):other[0]>='A'&&other[0]<='Z'?(char)(other[0]+32):other[0];if(other!=text)apply(other);}
                if(fold)--w.foldDepth;return out;
            }
            if(name=="plural_game"||name=="plural_en"||name=="singular_game"){
                apply(text);
                if(text.size()>1&&text.back()=='s')apply(text.substr(0,text.size()-1));
                if(text.size()>3&&text.substr(text.size()-3)=="ies")apply(text.substr(0,text.size()-3)+"y");
                if(text.size()>3&&text.substr(text.size()-3)=="ves"){apply(text.substr(0,text.size()-3)+"f");apply(text.substr(0,text.size()-3)+"fe");}
                return out;
            }
            if(name=="truncate_30"||name=="truncate_39"||name=="legacy_menu_clip34"||name=="legacy_inventory_clip40"||name=="legacy_armour_caption_clip26"){w.reason="noninvertible_storage_truncation:"+gid;return {};}
            if(name=="list_and_en"||name=="list_or_en"||name=="stream_commas_3"||name=="stream_commas_2"){
                auto original=text;std::string word=name=="list_or_en"?" or ":" and ";auto pos=original.rfind(word);
                if(pos!=std::string::npos)original.replace(pos,word.size(),", ");
                for(auto v:Eval(child,original,form,role,w)){v.source=text;Push(out,v,w);}
                for(auto v:Eval(child,original+", ",form,role,w)){v.source=text;Push(out,v,w);}
                return out;
            }
            w.reason="structured_source_transform:"+name;return {};
        }
        if(op=="bounded_list"){
            std::string sep=CText(n,"separator",", "),trailing=CText(n,"trailing_separator"),input=text;
            if(!trailing.empty()&&input.size()>=trailing.size()&&input.compare(input.size()-trailing.size(),trailing.size(),trailing)==0)input.resize(input.size()-trailing.size());
            if(input.empty()&&CInt(n,"minimum",0)==0){CompactValue v;v.text="";v.source=text;out.push_back(v);return out;}
            if(sep.empty())return {};
            std::function<void(size_t,size_t,CompactValue)> items;
            items=[&](size_t at,size_t count,CompactValue combined){
                if(!w.Tick()||count>=(size_t)CInt(n,"maximum",0))return;
                if(count>=128){w.reason="list_presentation_limit";return;}
                for(size_t end=at;end<=input.size()&&!w.exhausted;++end){
                    if(end!=input.size()&&input.compare(end,sep.size(),sep)!=0)continue;
                    if(end==at)continue;
                    for(auto v:Eval(CText(*n.Find("item"),"name"),input.substr(at,end-at),form,"component",w)){
                        auto merged=combined;
                        // Each list element is a distinct record. Do not bind its
                        // species/quality choice to every other list element.
                        v.bindings.clear();merged.text+=(count?(end==input.size()?CText(n,"target_last_separator",", "):", "):"")+v.text;merged.active=merged.active||v.active;
                        if(end==input.size()){if(count+1>=(size_t)CInt(n,"minimum",0)){merged.source=text;Push(out,merged,w);}}
                        else items(end+sep.size(),count+1,merged);
                    }
                }
            };items(0,0,CompactValue());return out;
        }
        const Json* synthetic=model_.Find("synthetic")->Find(gid.c_str());
        if(synthetic)return Pattern(text,CTokens(CText(*synthetic,"source")),*synthetic->Find("arguments"),nullptr,form,w);
        w.reason="unsupported_node:"+op;return {};
    }
public:
    CompactCatalog(const Json& catalog,Json model,const Lexicon& legacy):model_(std::move(model)),finnish_(CText(catalog,"language")=="fi"),legacy_(&legacy) {
        if(CInt(catalog,"schema")!=2||CInt(model_,"schema")!=1)throw std::runtime_error("Compact schema mismatch");
        for(const char* key:{"nodes","unit_bindings","aliases","generators","transformed","synthetic"}){const auto* value=model_.Find(key);if(!value||value->type!=Json::Object)throw std::runtime_error(std::string("Missing compact model object: ")+key);}
        if(!model_.Find("families")||model_.Find("families")->type!=Json::Array)throw std::runtime_error("Missing compact families");
        const Json* rows=catalog.Find("units");if(!rows||rows->type!=Json::Array||rows->array.size()>100000)throw std::runtime_error("Compact units missing");
        for(const Json& row:rows->array){CompactUnit u;u.id=Field(row,"id");u.source=Field(row,"source");u.translation=Field(row,"translation");u.kind=Field(row,"kind");u.apply=CInt(row,"apply_translation",-1);u.data=row;
            if(u.apply<0||u.apply>4||u.source.size()>kMaxOutput||u.translation.size()>kMaxOutput||(u.kind!="assembly"&&(u.source.find('\0')!=std::string::npos||u.translation.find('\0')!=std::string::npos)))throw std::runtime_error("Invalid compact text/apply: "+u.id);
            std::set<std::string> a,b;u.sourceTokens=CTokens(u.source,&a);CTokens(u.translation,&b);
            if(u.kind=="exact")for(const auto& t:u.sourceTokens)u.plainSource+=t.text;
            if((u.apply==1&&u.translation.empty())||(!u.translation.empty()&&a!=b))throw std::runtime_error("Compact placeholder mismatch: "+u.id);
            if(u.kind!="exact"&&u.kind!="template"&&u.kind!="assembly")throw std::runtime_error("Invalid compact unit kind");
            if(const auto* contracts=model_.Find("contracts")){const auto* contract=contracts->Find(u.id.c_str());if(!contract||CText(*contract,"source")!=u.source)throw std::runtime_error("Compact immutable source changed: "+u.id);}
            CForms(row.Find("forms"));CForms(row.Find("agreement"));
            if(!units_.emplace(u.id,u).second)throw std::runtime_error("Duplicate compact ID");
            if(u.kind=="exact"&&!(row.Find("scope_only")&&row.Find("scope_only")->boolean)){
                std::string plain;for(const auto& t:u.sourceTokens)plain+=t.text;exact_[plain].push_back(u.id);
                if(const auto* aliases=row.Find("source_aliases")){
                    if(aliases->type!=Json::Array||aliases->array.size()>32)throw std::runtime_error("Invalid display source aliases");
                    for(const auto& alias:aliases->array){
                        if(alias.type!=Json::String||alias.text.empty()||alias.text.size()>kMaxInput||!ValidUtf8(alias.text))throw std::runtime_error("Invalid display source alias text");
                        if(alias.text!=plain)exact_[alias.text].push_back(u.id);
                    }
                }
            }
        }
        auto require=[](const Json& j,const char* key,Json::Type type)->const Json&{const Json* v=j.Find(key);if(!v||v->type!=type)throw std::runtime_error(std::string("Invalid compact field: ")+key);return *v;};
        auto reference=[&](const Json& ref){Node(Field(ref,"name"));};
        for(const auto& pair:model_.Find("nodes")->object){const Json& n=pair.second;const auto op=Field(n,"op");
            if(op=="slot"||op=="transform")reference(require(n,"value",Json::Object));
            else if(op=="choice")for(const auto& o:require(n,"options",Json::Array).array){Field(o,"id");reference(require(o,"value",Json::Object));}
            else if(op=="bounded_list"){if(const Json* last=n.Find("target_last_separator"))if(last->type!=Json::String||last->text.size()>64)throw std::runtime_error("Invalid list target separator");reference(require(n,"item",Json::Object));if(CInt(n,"maximum",-1)<0||CInt(n,"maximum")>100000)throw std::runtime_error("Compact list bound");}
            else if(op=="concat"||op=="sequence"||op=="token_sequence")for(const auto& r:require(n,"parts",Json::Array).array)reference(r);
            else if(op=="format"){reference(require(n,"format",Json::Object));for(const auto& r:require(n,"args",Json::Array).array)reference(r);}
            else if(op=="literal")Field(n,"text");
            else if(op=="parameter"){Field(n,"name");Field(n,"type");}
            else throw std::runtime_error("Unsupported compact graph op "+op);
        }
        for(const auto& pair:model_.Find("unit_bindings")->object)for(const auto& role:pair.second.object){(void)units_.at(Field(role.second,"unit"));for(const auto& a:require(role.second,"arguments",Json::Object).object)Node(Field(a.second,"graph"));}
        for(const auto& pair:units_)if(const Json* policies=pair.second.data.Find("binding_rules")){
            if(policies->type!=Json::Object)throw std::runtime_error("Invalid binding rules object");
            for(const auto& policy:policies->object){const Json* binding=model_.Find("unit_bindings")->Find(policy.first.c_str());if(!binding)throw std::runtime_error("Unknown binding rule node");
                bool owns=false;for(const auto& role:binding->object)if(CText(role.second,"unit")==pair.first)owns=true;if(!owns)throw std::runtime_error("Binding rule unit mismatch");
                if(policy.second.type!=Json::Object)throw std::runtime_error("Invalid binding rule");
                for(const auto& field:policy.second.object){
                    if(field.first=="translation"){if(field.second.type!=Json::String)throw std::runtime_error("Binding translation must be string");std::set<std::string>a,b;CTokens(pair.second.source,&a);CTokens(field.second.text,&b);if(a!=b)throw std::runtime_error("Binding placeholder mismatch");}
                    else if(field.first=="forms"||field.first=="literal_values")CForms(&field.second);
                    else if(field.first=="argument_forms"||field.first=="argument_translations"){if(field.second.type!=Json::Object)throw std::runtime_error("Invalid binding arguments");}
                    else if(field.first=="head"){if(field.second.type!=Json::String)throw std::runtime_error("Invalid binding head");}
                    else if(field.first!="reason")throw std::runtime_error("Unknown binding policy field");
                }
            }
        }
        for(const auto& pair:model_.Find("synthetic")->object){Field(pair.second,"source");for(const auto& a:require(pair.second,"arguments",Json::Object).object)Node(Field(a.second,"graph"));}
        for(const auto& f:model_.Find("families")->array){if(CText(f,"coverage_kind").rfind("classified_",0)==0||CText(f,"coverage_kind")=="recovered_full_producer_family")continue;uint32_t caller=Address(f,"root_va");auto alias=model_.Find("aliases")->Find(CText(f,"node").c_str());if(!alias)throw std::runtime_error("Missing compact family root");auto& roots=roots_[caller];if(std::find(roots.begin(),roots.end(),alias->text)==roots.end())roots.push_back(alias->text);}
        if(const auto* routes=model_.Find("runtime_routes")){
            if(routes->type!=Json::Array)throw std::runtime_error("Invalid recovered routes");
            for(const auto& r:routes->array){auto node=Field(r,"node");Node(node);routes_.push_back({Address(r,"caller"),Address(r,"target"),CInt(r,"priority",100),node,r.Find("fold_case")&&r.Find("fold_case")->boolean,r.Find("capitalize")&&r.Find("capitalize")->boolean});}
            std::stable_sort(routes_.begin(),routes_.end(),[](const Route& a,const Route& b){return a.priority>b.priority;});
        }
    }
    size_t UnitCount()const{return units_.size();}
    bool InspectDomain(const std::string& source,const std::string& domain,const std::string& form,
                       std::string& text,std::string& id,std::string& reason)const {
        text.clear();id.clear();reason.clear();
        const auto* alias=model_.Find("aliases")->Find(domain.c_str());
        if(!alias){reason="unknown_domain";return false;}
        Work w;std::vector<CompactValue> found;
        for(const auto& v:Eval(alias->text,source,form,"component",w))if(v.active)Push(found,v,w);
        if(w.exhausted||found.empty()){reason=w.reason.empty()?"no_domain_match":w.reason;return false;}
        const std::string& output=found.front().text;
        for(const auto& v:found)if(v.text!=output){reason="ambiguous_domain_target";return false;}
        if(output.empty()||!ValidUtf8(output)){reason="invalid_domain_target";return false;}
        text=output;id="compact:"+found.front().unit;return true;
    }
    bool Translate(const std::string& source,uint32_t caller,uint32_t target,std::string& text,std::string& id,bool exactOnly,std::string& reason,const std::vector<NpcSourceIdentity>* identities=nullptr)const {
        std::string cacheKey=std::to_string(caller)+":"+std::to_string(target)+":"+(exactOnly?"E:":"G:")+source;
        if(identities)for(const auto& proof:*identities)cacheKey+="\x1E"+std::to_string(proof.label.size())+":"+proof.label+":"+proof.branch;
        {std::lock_guard<std::mutex> lock(cacheMutex_);auto c=cache_.find(cacheKey);if(c!=cache_.end()){text=c->second.text;id=c->second.id;reason=c->second.reason;return c->second.matched;}}
        Work w;w.identities=identities;std::vector<CompactValue> candidates;
        if(!exactOnly)for(const auto& route:routes_){
            if((route.caller&&route.caller!=caller)||(route.target&&route.target!=target))continue;
            Work local;local.identities=identities;local.foldDepth=route.foldCase?1:0;std::vector<CompactValue> found;
            for(auto v:Eval(route.node,source,"","message",local))if(v.active)Push(found,v,local);
            if(local.exhausted){w.reason=local.reason;continue;}
            if(found.empty()){if(!local.reason.empty())w.reason=local.reason;continue;}
            auto normalize=[&](std::string output){if(route.capitalize){size_t at=output.find_first_not_of(" \t\r\n!#+:&=_>?");if(at!=std::string::npos)output=output.substr(0,at)+CCase(output.substr(at),"upper_first",finnish_);}return output;};
            std::string output=normalize(found[0].text);bool same=true;for(const auto& v:found)if(normalize(v.text)!=output)same=false;
            if(!same){w.reason="ambiguous_recovered_target";continue;}
            if(output==source&&target!=0x004FCF90&&target!=0x004F9D50&&target!=0x004FD710){w.reason="recovered_target_unchanged";continue;}
            if(output.empty()||!ValidUtf8(output)){w.reason="invalid_recovered_target";continue;}
            text=output;id="compact:"+found[0].unit;reason.clear();
            {std::lock_guard<std::mutex> lock(cacheMutex_);if(cache_.size()>=8192)cache_.clear();cache_[cacheKey]={true,text,id,reason};}
            return true;
        }
        // Virtual presentation queries carry a verified native source identity.
        // A failed scoped recipe must never turn a proper name into an item
        // through a global dictionary, unrelated root or legacy fallback.
        if(target==0x004FCF90||target==0x004F9D50||target==0x004FD710||(caller==0x004FB720&&target==0x0050BF70)){
            reason=w.reason.empty()?"no_scoped_source_match":w.reason;text.clear();id.clear();
            std::lock_guard<std::mutex> lock(cacheMutex_);if(cache_.size()>=8192)cache_.clear();cache_[cacheKey]={false,text,id,reason};
            return false;
        }
        auto exact=exact_.find(source);
        if(exact!=exact_.end())for(const auto& uid:exact->second){const auto& u=units_.at(uid);if(u.data.Find("binding_rules"))continue;CompactValue v;v.text=source;if(u.apply==1){v.text.clear();for(const auto& t:CTokens(u.translation))v.text+=t.text;}v.unit=uid;v.active=true;Push(candidates,v,w);}
        bool exactConflict=false;for(const auto& v:candidates)if(v.text!=candidates[0].text)exactConflict=true;
        if(exactConflict)candidates.clear();
        if(candidates.empty()&&!exactOnly){auto roots=roots_.find(caller);if(roots!=roots_.end())for(const auto& gid:roots->second){for(const auto& v:Eval(gid,source,"","message",w))if(v.active)Push(candidates,v,w);if(w.exhausted)break;}}
        if(candidates.empty()&&!exactOnly&&!w.exhausted)if(const auto* domains=model_.Find("display_domains"))for(const auto& route:domains->array){
            if(Address(route,"caller")!=caller||Address(route,"target")!=target)continue;
            for(const auto& v:Eval(CText(route,"node"),source,"","component",w))if(v.active&&(v.text!=source||v.preserve))Push(candidates,v,w);
        }
        bool matched=!candidates.empty()&&!w.exhausted;std::string output,rule;
        if(matched){output=candidates[0].text;rule="compact:"+candidates[0].unit;for(const auto& v:candidates)if(v.text!=output){matched=false;w.reason="ambiguous_target";break;}}
        if(matched&&(output.empty()||!ValidUtf8(output))){matched=false;w.reason="invalid_target";}
        reason=matched?"":w.reason.empty()?"no_compact_match":w.reason;text=matched?output:"";id=matched?rule:"";
        {std::lock_guard<std::mutex> lock(cacheMutex_);if(cache_.size()>=8192)cache_.clear();cache_[cacheKey]={matched,text,id,reason};}
        return matched;
    }
    void OverrideDocuments(std::vector<DocumentEntry>& documents)const {
        // Change only the presentation body. Keep original keys, aliases,
        // image directives and native resource lookup intact.
        std::map<std::string,const CompactUnit*> topics;
        if(const auto* documentsMap=model_.Find("documents"))for(const auto& row:documentsMap->array){auto found=units_.find(CText(row,"unit"));if(found!=units_.end())topics[CText(row,"topic")]=&found->second;}
        else for(const auto& pair:units_){const auto& u=pair.second;auto topic=CText(u.data,"document_topic");if(!topic.empty()&&u.kind=="exact")topics[topic]=&u;}
        for(auto& d:documents){auto it=topics.find(d.topic);if(it==topics.end())continue;const auto& u=*it->second;if(u.apply!=1){d.enabled=false;continue;}
            std::string target;for(const auto& t:CTokens(u.translation)){if(t.variable)throw std::runtime_error("Unbound encyclopedia parameter");target+=t.text;}
            if(CVisibleDocument(d.translation)==CVisibleDocument(target)){d.enabled=true;continue;}
            // The compact visible body omits image directives. Retain them in
            // original order before the replacement prose, without changing paths.
            std::string controls;size_t at=0;
            while(at<d.source.size()){size_t end=d.source.find('\n',at);if(end==std::string::npos)end=d.source.size();if(d.source[at]=='%'){size_t next=d.source.find('\n',std::min(end+1,d.source.size()));if(next==std::string::npos)next=d.source.size();controls+=d.source.substr(at,next-at)+"\n";at=next;}at=end<d.source.size()?end+1:d.source.size();}
            d.translation=controls+target+"\n";d.id="compact:"+u.id;d.enabled=true;
        }
    }
};
