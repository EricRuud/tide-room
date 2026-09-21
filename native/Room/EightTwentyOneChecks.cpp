#include "EightTwentyOneTape.h"
#include "RoomProcessor.h"
#include "RoomEditor.h"
#include "../Lab/LF2/P821NativeConfiguration.h"
#include "../Lab/LF2/P821NativeQualityModel.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace tide;
namespace {
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
std::vector<double> taps(juce::File f){juce::MemoryBlock b;check(f.loadFileAsData(b),"Missing FIR taps");std::vector<double> v(b.getSize()/8);std::memcpy(v.data(),b.getData(),b.getSize());return v;}
float difference(const juce::AudioBuffer<float>& a,const juce::AudioBuffer<float>& b,int start=0){float peak=0;for(int c=0;c<2;++c)for(int n=start;n<a.getNumSamples();++n){check(std::isfinite(a.getSample(c,n)),"Nonfinite output");peak=std::max(peak,std::abs(a.getSample(c,n)-b.getSample(c,n)));}return peak;}
juce::AudioBuffer<float> signal(){juce::AudioBuffer<float> b(2,49152);for(int c=0;c<2;++c)for(int i=0;i<b.getNumSamples();++i){const double t=i/48000.;const double envelope=i<8192?0:.2+.15*std::sin(t*13.);b.setSample(c,i,(float)(envelope*(std::sin(t*301+c*.3)+.35*std::sin(t*20517-c*.7)+.1*std::sin(t*6117))));}return b;}
juce::AudioBuffer<float> render(room::EightTwentyOneTape& model,const juce::AudioBuffer<float>& input,int partition){juce::AudioBuffer<float> result;result.makeCopyOf(input);for(int pos=0;pos<result.getNumSamples();pos+=partition){const int n=std::min(partition,result.getNumSamples()-pos);juce::AudioBuffer<float> b(result.getArrayOfWritePointers(),2,pos,n);model.process(b);}return result;}
}
int main(int argc,char** argv){std::cout<<std::unitbuf;try{
 check(argc>=3,"Usage: Tide821Check resources output [--ui]");juce::ScopedNoDenormals noDenormals;
 const juce::File folder(argv[1]),out(argv[2]);out.createDirectory();const auto input=signal();
 for(int calibration=0;calibration<2;++calibration)for(int q=0;q<3;++q){room::EightTwentyOneTape model;model.setSettings({0,0,q,calibration});model.prepare(48000,512,folder);check(model.ready(),model.status().toRawUTF8());
  const auto start=std::chrono::steady_clock::now();const auto result=render(model,input,512);const double cpu=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/(input.getNumSamples()/48000.);
  p821lab::NativeConfiguration config;check(config.load(folder.getChildFile(calibration?"90030":"model").getFullPathName().toStdString()),"Config load");p821lab::NativeModel base(config.features,config.controls,config.kernel,config.audio,config.slow,config.acEnabled,config.ac);
  std::unique_ptr<lab::ContinuousMultistageLimiter> limiter;std::unique_ptr<p821lab::NativeQualityModel> quality;int delay=768;
  if(q){int factor=q==1?4:8;limiter=std::make_unique<lab::ContinuousMultistageLimiter>(factor,std::clamp(config.audio.release,.0001,.05),taps(folder.getChildFile("hq/long.bin")),taps(folder.getChildFile("hq/short.bin")),taps(folder.getChildFile("hq/compensation-"+juce::String(factor)+".bin")));quality=std::make_unique<p821lab::NativeQualityModel>(base,*limiter);delay-=(int)quality->latency();}
  juce::AudioBuffer<float> expected(2,input.getNumSamples());expected.clear();std::array<float,512> x{};std::array<double,512> y{};
  for(int pos=0;pos<input.getNumSamples();pos+=256){for(int i=0;i<256;++i)for(int c=0;c<2;++c)x[(size_t)(2*i+c)]=input.getSample(c,pos+i);if(q)quality->process(x.data(),y.data());else base.process(x.data(),y.data());for(int i=0;i<256&&pos+i+delay<expected.getNumSamples();++i)for(int c=0;c<2;++c)expected.setSample(c,pos+i+delay,(float)y[(size_t)(2*i+c)]);}
  const float error=difference(result,expected);check(error==0,"Wrapper differs from frozen native DSP");
  for(int partition:{17,127,1024}){model.reset(q);check(difference(result,render(model,input,partition))==0,"Host partition changed audio");}
  model.setSettings({12,-3,q,calibration});model.reset(q);auto driven=render(model,input,127);check(difference(result,driven)>.005f,"Drive has no effect");check(model.guardCount()==0,"Output guard activated");
  model.setSettings({0,0,q,calibration});model.reset(q);check(difference(result,render(model,input,127))==0,"Reset did not restore original response");
  room::EightTwentyOneSettings moving{0,0,q,calibration};moving.motion=true;moving.wow=1;moving.flutter=1;model.setSettings(moving);model.reset(q);
  const auto motionStart=std::chrono::steady_clock::now();auto moved=render(model,input,512);const double motionCpu=std::chrono::duration<double>(std::chrono::steady_clock::now()-motionStart).count()/(input.getNumSamples()/48000.);
  check(difference(result,moved)>.0001f,"Motion has no effect");model.reset(q);check(difference(moved,render(model,input,127))==0,"Motion depends on callback size");
  moving.motion=false;model.setSettings(moving);model.reset(q);check(difference(result,render(model,input,17))==0,"Motion off changed calibrated audio");
  std::cout<<"PASS motion response, callback independence and exact off; moving CPU "<<motionCpu*100<<"%\n";
  std::cout<<"PASS calibration "<<calibration<<" quality "<<q<<": exact frozen-model parity, 768-sample alignment, partitions/reset/drive; CPU "<<cpu*100<<"%\n";
 }
 room::RoomTape bus;room::TapeSettings a;a.enabled=true;a.mix=1;room::SpatialSettings spatial;room::StudioSettings studio;room::EightTwentyOneSettings eight;
 bus.setSettings(a,spatial,studio,eight,3);bus.prepare(48000,512,folder);check(bus.eightReady(),"Bus model unavailable");
 room::EightTwentyOneTape model;model.prepare(48000,512,folder);auto expected=render(model,input,512);juce::AudioBuffer<float> result;result.makeCopyOf(input);
 for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}check(difference(result,expected,4096)<1e-7,"Room bus does not reach 100% model");
 a.enabled=false;bus.setSettings(a,spatial,studio,eight,3);result.makeCopyOf(input);for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}for(int c=0;c<2;++c)for(int i=4096;i<result.getNumSamples();++i)check(result.getSample(c,i)==input.getSample(c,i-768),"Bypass is not aligned dry");
 a.enabled=true;for(int calibration:{1,0,1})for(int q:{2,1,0}){eight.calibration=calibration;eight.quality=q;bus.setSettings(a,spatial,studio,eight,3);result.makeCopyOf(input);for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}check(result.getMagnitude(0,result.getNumSamples())>0,"Quality switch silenced bus");check(difference(result,result)==0,"Invalid switch output");}check(bus.guardCount()==0,"Bus guards");bus.release();
 bus.prepare(44100,512,folder);check(!bus.eightReady(),"Unsupported sample rate accepted");result.makeCopyOf(input);for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}for(int c=0;c<2;++c)for(int i=768;i<result.getNumSamples();++i)check(result.getSample(c,i)==input.getSample(c,i-768),"Unsupported sample rate is not dry");bus.release();
 std::cout<<"PASS room bus wet/bypass, quality switching and unsupported-rate dry fallback\n";
 // The new worn engine must remain on the common latency and bypass path.
 room::WornSettings worn;worn.noise=0;worn.motion=1.15f;room::WornTape wornReference;wornReference.setSettings(worn);wornReference.prepare(48000,512);
 expected.makeCopyOf(input);for(int pos=0;pos<expected.getNumSamples();pos+=512){juce::AudioBuffer<float> block(expected.getArrayOfWritePointers(),2,pos,512);wornReference.process(block);}
 a.enabled=true;bus.setSettings(a,spatial,studio,eight,worn,4);bus.prepare(48000,512,folder);result.makeCopyOf(input);
 for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}check(difference(result,expected,8192)<1e-7,"Worn bus differs from standalone engine");
 a.enabled=false;bus.setSettings(a,spatial,studio,eight,worn,4);result.makeCopyOf(input);
 for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}
 for(int c=0;c<2;++c)for(int i=4096;i<result.getNumSamples();++i)check(result.getSample(c,i)==input.getSample(c,i-768),"Worn bypass alignment");
 a.enabled=true;for(int mode:{3,4,2,4}){bus.setSettings(a,spatial,studio,eight,worn,mode);result.makeCopyOf(input);for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}check(difference(result,result)==0,"Nonfinite worn switch");}check(bus.guardCount()==0,"Worn switching guard");bus.release();
 std::cout<<"PASS worn bus parity, 768-sample bypass and mode switching\n";
 // A missing new calibration must fade the working old one out, then stay dry.
 const auto incomplete=out.getChildFile("missing-calibration");incomplete.createDirectory();
 check(folder.getChildFile("model").copyDirectoryTo(incomplete.getChildFile("model")),"Copy fallback fixture");
 check(folder.getChildFile("hq").copyDirectoryTo(incomplete.getChildFile("hq")),"Copy HQ fallback fixture");
 check(!incomplete.getChildFile("90030").exists(),"Fallback fixture unexpectedly has new calibration");
 eight={};bus.setSettings(a,spatial,studio,eight,3);bus.prepare(48000,512,incomplete);
 check(bus.eightReady(),"Missing optional calibration disabled old one");
 for(int selection:{0,1}){eight.calibration=selection;bus.setSettings(a,spatial,studio,eight,3);result.makeCopyOf(input);
  for(int pos=0;pos<result.getNumSamples();pos+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,pos,512);bus.process(block);}
  check(difference(result,result)==0,"Invalid missing-calibration switch output");
 }
 for(int c=0;c<2;++c)for(int i=4096;i<result.getNumSamples();++i)check(result.getSample(c,i)==input.getSample(c,i-768),"Missing calibration did not become aligned dry");
 bus.release();std::cout<<"PASS missing-calibration transition and independent availability\n";
 if(argc>3&&juce::String(argv[3])=="--ui"){juce::ScopedJuceInitialiser_GUI gui;RoomProcessor p(false);p.setRateAndBufferSizeDetails(48000,512);p.set("tapeModel",3);p.set("eightDrive",9);p.set("eightTrim",-3);p.set("eightQuality",2);p.set("eightCalibration",1);p.set("eightMotionOn",1);p.set("eightWow",.5f);p.set("eightFlutter",.25f);p.set("tempo",94.3f);juce::MemoryBlock saved;p.getStateInformation(saved);p.set("eightDrive",0);p.set("tapeModel",1);p.setStateInformation(saved.getData(),(int)saved.getSize());check(p.get("tapeModel")==3&&p.get("eightQuality")==2&&p.get("eightCalibration")==1&&p.get("eightMotionOn")==1&&p.get("eightWow")==.5f&&p.get("eightFlutter")==.25f&&std::abs(p.get("eightDrive")-9)<.001f&&std::abs(p.get("tempo")-94.3f)<.01f,"State restore");
 auto old=p.parameters.copyState();for(const char* key:{"eightDrive","eightTrim","eightQuality","eightCalibration","eightWow","eightFlutter","eightMotionOn"})old.removeChild(old.getChildWithProperty("id",key),nullptr);juce::AudioProcessor::copyXmlToBinary(*old.createXml(),saved);p.setStateInformation(saved.getData(),(int)saved.getSize());check(p.get("eightDrive")==0&&p.get("eightTrim")==0&&p.get("eightQuality")==0&&p.get("eightCalibration")==0&&std::abs(p.get("eightWow")-.1f)<.001f&&std::abs(p.get("eightFlutter")-.1f)<.001f&&p.get("eightMotionOn")==0,"Old scene defaults");
 RoomEditor editor(p);auto snapshot=editor.tapePanelSnapshot();juce::PNGImageFormat png;auto stream=out.getChildFile("821-panel.png").createOutputStream();png.writeImageToStream(snapshot,*stream);std::cout<<"PASS state roundtrip, old-scene defaults and panel snapshot\n";
 p.set("eightDrive",13);p.selectWornPreset(2);check(p.get("tapeModel")==4&&p.get("wornMedium")==1&&p.get("tapeMix")==1&&p.get("tapeOn")==1&&p.get("wornDips")==1,"Worn preset application");p.getStateInformation(saved);p.set("wornAge",0);p.set("wornMedium",0);p.setStateInformation(saved.getData(),(int)saved.getSize());check(std::abs(p.get("wornAge")-.64f)<.001f&&p.get("wornMedium")==1&&p.get("eightDrive")==13,"Worn state restore or 821 preservation");
 snapshot=editor.tapePanelSnapshot();auto wornStream=out.getChildFile("worn-panel.png").createOutputStream();png.writeImageToStream(snapshot,*wornStream);
 p.set("wornDips",.37f);p.getStateInformation(saved);p.set("wornDips",0);p.setStateInformation(saved.getData(),(int)saved.getSize());check(std::abs(p.get("wornDips")-.37f)<.001f,"Dip depth state restore");
 auto legacy=p.parameters.copyState();for(const char* key:{"wornDrive","wornAge","wornMotion","wornDamage","wornNoise","wornTrim","wornMedium","wornDips"})legacy.removeChild(legacy.getChildWithProperty("id",key),nullptr);juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),saved);p.setStateInformation(saved.getData(),(int)saved.getSize());check(p.get("wornDrive")==14&&std::abs(p.get("wornAge")-.72f)<.001f&&p.get("wornMedium")==0&&p.get("wornDips")==1,"Old-scene worn defaults");std::cout<<"PASS worn preset, state restore, independent 821 controls, old defaults and panel snapshot\n";}
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
