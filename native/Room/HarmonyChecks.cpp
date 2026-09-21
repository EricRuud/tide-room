#include "RoomEditor.h"
#include "../../artifacts/harmony-001/Pattern-before.h"
#include "../../artifacts/patterns-001/Pattern-before.h"
#include <iostream>
#include <tuple>
#include <vector>
using namespace tide::room;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using Event=std::tuple<int,int,int,int,float,bool>;
void baseline(){
    for(double sr:{44100.,48000.,96000.})for(float evolution:{0.f,.45f,.9f}){
        tide::room::Pattern now;beforeHarmony::Pattern before;now.reset(sr);before.reset(sr);PatternSettings s;beforeHarmony::PatternSettings old;s.evolution=old.evolution=evolution;
        for(int pos=0;pos<(int)sr*90;){const int n=std::min(pos%3==0?17:pos%3==1?512:2048,(int)sr*90-pos);const auto a=now.advance(n,s);const auto b=before.advance(n,old);check(a.size==b.size,"Default event count changed");
            for(int i=0;i<a.size;++i){const auto x=a.data[(size_t)i];const auto y=b.data[(size_t)i];check(x.offset==y.offset&&x.part==y.part&&x.note==y.note&&x.step==y.step&&x.velocity==y.velocity&&x.on==y.on,"Original D-minor score changed");}pos+=n;
        }
    }std::cout<<"PASS exact original score: three sample rates, three evolution depths, varied callback sizes\n";
}
std::vector<Event> changed(int block,bool change){
    tide::room::Pattern p;p.reset(48000);PatternSettings s;s.evolution=.9f;std::vector<Event> events;std::array<int,3> active{{-1,-1,-1}};
    for(int pos=0;pos<48000*16;){const int section=pos/2401;const int n=std::min({block,(section+1)*2401-pos,48000*16-pos});
        if(change)s.harmony={section%12,(section/7)%2,section%7};
        const auto a=p.advance(n,s);for(int i=0;i<a.size;++i){const auto e=a.data[(size_t)i];events.emplace_back(pos+e.offset,e.part,e.note,e.step,e.velocity,e.on);
            if(e.on){check(active[(size_t)e.part]==-1,"New harmony retriggered a held note");active[(size_t)e.part]=e.note;}else{check(active[(size_t)e.part]==e.note,"Harmony changed note-off pitch");active[(size_t)e.part]=-1;}
        }pos+=n;
    }
    s.density.fill(0);const auto tail=p.advance(48000,s);for(int i=0;i<tail.size;++i){const auto e=tail.data[(size_t)i];check(!e.on&&active[(size_t)e.part]==e.note,"Invalid final release");active[(size_t)e.part]=-1;}
    for(int held:active)check(held==-1,"Harmony left a stuck note");return events;
}
void allKeys(){
    constexpr int scales[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
    for(int key=0;key<12;++key)for(int minor=0;minor<2;++minor)for(int chord=0;chord<7;++chord){
        tide::room::Pattern p;p.reset(48000);PatternSettings s;s.harmony={key,minor,chord};
        for(int i=0;i<1500;++i){const auto a=p.advance(512,s);for(int j=0;j<a.size;++j){const auto e=a.data[(size_t)j];check(e.note>=24&&e.note<108,"Harmony escaped musical register");if(!e.on)continue;
            const int pc=(e.note-key+120)%12;bool belongs=false;for(int pitch:scales[minor])belongs|=pc==pitch;check(belongs,"Note outside selected key");
            if(e.part==0)check(pc==scales[minor][chord]||pc==scales[minor][(chord+4)%7],"Foundation does not follow chord root/fifth");
        }}
    }
    const auto a=changed(512,true),b=changed(17,true),c=changed(2048,true),old=changed(127,false);check(a==b&&a==c,"Changing harmony depends on callback size");check(a.size()==old.size(),"Harmony changed rhythm count");
    for(size_t i=0;i<a.size();++i){check(std::get<0>(a[i])==std::get<0>(old[i])&&std::get<1>(a[i])==std::get<1>(old[i])&&std::get<3>(a[i])==std::get<3>(old[i])&&std::get<4>(a[i])==std::get<4>(old[i])&&std::get<5>(a[i])==std::get<5>(old[i]),"Harmony reset rhythm or altered accents");}
    std::cout<<"PASS 12 keys x 2 scales x 7 chords, continuous timing, callback independence and correct note releases\n";
}
std::vector<Event> bankScore(int bank,int block,double sr,bool changing=false){
    tide::room::Pattern p;p.reset(sr);PatternSettings s;s.bank=bank;s.evolution=.9f;std::vector<Event> events;std::array<int,3> active{{-1,-1,-1}};
    const int duration=(int)(sr*24);
    for(int pos=0;pos<duration;){const int section=pos/2399;const int n=std::min({block,(section+1)*2399-pos,duration-pos});
        if(changing){s.bank=section%8;s.harmony={section%12,(section/7)%2,section%7};s.bpm=section%3==0?160:section%3==1?60:108;}
        const auto a=p.advance(n,s);for(int i=0;i<a.size;++i){const auto e=a.data[(size_t)i];check(e.offset>=0&&e.offset<n,"Bank event offset");events.emplace_back(pos+e.offset,e.part,e.note,e.step,e.velocity,e.on);
            if(e.on){check(e.note>=24&&e.note<108&&e.velocity>0&&e.velocity<=1,"Bank note range");check(e.step>=0&&e.step<selectedPattern(s.bank).parts[(size_t)e.part].length,"Bank step display range");check(active[(size_t)e.part]==-1,"Bank switch retriggered a held note");active[(size_t)e.part]=e.note;}else{check(active[(size_t)e.part]==e.note,"Bank switch changed note-off pitch");active[(size_t)e.part]=-1;}
        }pos+=n;
    }
    s.density.fill(0);const auto tail=p.advance((int)sr,s);for(int i=0;i<tail.size;++i){const auto e=tail.data[(size_t)i];check(!e.on&&active[(size_t)e.part]==e.note,"Bank final release");active[(size_t)e.part]=-1;}for(int note:active)check(note==-1,"Bank left a stuck note");return events;
}
void patternBanks(){
    std::array<std::vector<Event>,8> reference;
    for(size_t bank=0;bank<patternBank.size();++bank){const auto& definition=patternBank[bank];
        check(definition.shortcut<'A'||definition.shortcut>'G',"Pattern shortcut overlaps key names");
        for(size_t other=0;other<bank;++other)check(patternBank[other].shortcut!=definition.shortcut,"Duplicate pattern shortcut");
        for(const auto& part:definition.parts){check(part.length>0&&part.length<=24&&part.mask!=0&&(part.mask>>part.length)==0,"Invalid pattern rhythm");check(part.noteCount>0&&part.noteCount<=8,"Invalid pattern motif");}
        for(double sr:{44100.,48000.,96000.}){auto a=bankScore((int)bank,512,sr);check(a==bankScore((int)bank,17,sr)&&a==bankScore((int)bank,2048,sr),"Pattern bank depends on callback size");if(sr==48000)reference[bank]=std::move(a);}
        for(size_t other=0;other<bank;++other)check(reference[bank]!=reference[other],"Pattern banks sound identical at event level");
    }
    for(double sr:{44100.,48000.,96000.})check(bankScore(0,512,sr,true)==bankScore(0,17,sr,true)&&bankScore(0,512,sr,true)==bankScore(0,2048,sr,true),"Live bank/key/tempo switching depends on callback size");
    for(const Harmony harmony:std::array<Harmony,3>{{{2,1,0},{6,0,4},{11,1,6}}}){
        tide::room::Pattern p;beforePatternBanks::Pattern old;p.reset(48000);old.reset(48000);PatternSettings s;beforePatternBanks::PatternSettings before;s.harmony=before.harmony=harmony;s.evolution=before.evolution=.9f;
        for(int i=0;i<4000;++i){const auto a=p.advance(512,s);const auto b=old.advance(512,before);check(a.size==b.size,"Bank zero changed event count");for(int j=0;j<a.size;++j){const auto x=a.data[(size_t)j];const auto y=b.data[(size_t)j];check(x.offset==y.offset&&x.part==y.part&&x.note==y.note&&x.step==y.step&&x.velocity==y.velocity&&x.on==y.on,"Bank zero changed the existing harmony score");}}
    }
    std::cout<<"PASS eight distinct banks, three sample rates, exact callback independence, rapid bank/key/tempo changes, release pairing and original-score preservation\n";
}
void ui(const juce::File& out){
    RoomProcessor p(false);p.setRateAndBufferSizeDetails(48000,512);HarmonyKeys keys(p);juce::Component ordinary;juce::TextEditor text;
    for(int i=0;i<7;++i){check(keys.handle(juce::KeyPress('1'+i),&ordinary)&&p.get("harmonyChord")==i,"Number key mapping");}
    const char letters[]="ABCDEFG";const int pitches[]={9,11,0,2,4,5,7};
    for(int i=0;i<7;++i){check(keys.handle(juce::KeyPress(letters[i]),&ordinary)&&p.get("harmonyKey")==pitches[i],"Natural key mapping");check(keys.handle(juce::KeyPress(letters[i],juce::ModifierKeys::shiftModifier,0),&ordinary)&&p.get("harmonyKey")==((pitches[i]+1)%12),"Sharp key mapping");}
    const float root=p.get("harmonyKey"),chord=p.get("harmonyChord");
    for(const auto& key:{juce::KeyPress('1'),juce::KeyPress('A')})check(!keys.handle(key,&text),"Typing changed music");
    for(int modifier:{juce::ModifierKeys::commandModifier,juce::ModifierKeys::ctrlModifier,juce::ModifierKeys::altModifier})check(!keys.handle(juce::KeyPress('A',modifier,0),&ordinary),"Standard shortcut stolen");
    check(p.get("harmonyKey")==root&&p.get("harmonyChord")==chord,"Protected input changed harmony");
    p.set("harmonyKey",8);p.set("harmonyScale",0);p.set("harmonyChord",5);p.set("wornDips",.37f);juce::MemoryBlock data;p.getStateInformation(data);p.set("harmonyKey",0);p.set("harmonyScale",1);p.set("harmonyChord",0);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("harmonyKey")==8&&p.get("harmonyScale")==0&&p.get("harmonyChord")==5&&std::abs(p.get("wornDips")-.37f)<.001f,"Harmony state restore");
    auto legacy=p.parameters.copyState();for(const char* id:{"harmonyKey","harmonyScale","harmonyChord"})legacy.removeChild(legacy.getChildWithProperty("id",id),nullptr);juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),data);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("harmonyKey")==2&&p.get("harmonyScale")==1&&p.get("harmonyChord")==0,"Old-scene harmony changed");
    const char* expected[]={"Dm","Edim","F","Gm","Am","Bb","C"};for(int i=0;i<7;++i)check(HarmonyControls::chordName({2,1,i})==expected[i],"D minor chord names");
    for(int i=0;i<8;++i){const auto key=juce::KeyPress(patternBank[(size_t)i].shortcut);check(keys.handle(key,&ordinary)&&p.get("patternBank")==i,"Pattern shortcut mapping");check(!keys.handle(key,&text),"Typing selected a pattern");for(int modifier:{juce::ModifierKeys::commandModifier,juce::ModifierKeys::ctrlModifier,juce::ModifierKeys::altModifier})check(!keys.handle(juce::KeyPress(patternBank[(size_t)i].shortcut,modifier,0),&ordinary),"Pattern stole standard shortcut");}
    check(p.get("harmonyKey")==2&&p.get("harmonyScale")==1&&p.get("harmonyChord")==0&&std::abs(p.get("wornDips")-.37f)<.001f,"Pattern selected harmony or tape settings");
    p.set("patternBank",4);p.getStateInformation(data);p.set("patternBank",0);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("patternBank")==4,"Pattern state recall");auto oldPattern=p.parameters.copyState();oldPattern.removeChild(oldPattern.getChildWithProperty("id","patternBank"),nullptr);juce::AudioProcessor::copyXmlToBinary(*oldPattern.createXml(),data);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("patternBank")==0,"Old scene did not restore original pattern");
    RoomEditor editor(p);const auto shot=editor.createComponentSnapshot(editor.getLocalBounds());juce::PNGImageFormat png;auto stream=out.getChildFile("harmony-panel.png").createOutputStream();check(stream!=nullptr&&png.writeImageToStream(shot,*stream),"Snapshot write");
    std::cout<<"PASS harmony/pattern shortcuts, editing protection, independent harmony/tape settings, state recall, migration, chord labels and UI snapshot\n";
}
}
int main(int argc,char**argv){std::cout<<std::unitbuf;try{check(argc==2,"TideHarmonyCheck output-folder");juce::ScopedJuceInitialiser_GUI gui;juce::File out=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);out.createDirectory();baseline();allKeys();patternBanks();ui(out);return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
