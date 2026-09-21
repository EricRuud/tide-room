#pragma once
#include "Tape.h"
#include "SpatialTape.h"
#include "StudioTape.h"
#include "EightTwentyOneTape.h"
#include "WornTape.h"

namespace tide::room {
// Keep the approved magnetic engine available, and align both engines and dry
// to a fixed latency. Mode changes fade through dry without running both DSPs.
class RoomTape {
public:
    void setSettings(TapeSettings a,SpatialSettings b,int mode){legacySettings=a;spatialSettings=b;target=juce::jlimit(0,4,mode);}
    void setSettings(TapeSettings a,SpatialSettings b,StudioSettings c,int mode){setSettings(a,b,mode);studioSettings=c;}
    void setSettings(TapeSettings a,SpatialSettings b,StudioSettings c,EightTwentyOneSettings d,int mode){setSettings(a,b,c,mode);eightSettings=d;}
    void setSettings(TapeSettings a,SpatialSettings b,StudioSettings c,EightTwentyOneSettings d,WornSettings e,int mode){setSettings(a,b,c,d,mode);wornSettings=e;}
    bool eightReady() const {return eight.ready();}
    juce::String eightStatus(int calibration=0) const {return eight.status(calibration);}
    void prepare(double rate,int block,const juce::File& eightResources={}){
        maximum=block;active=target;write=0;legacyWrite=0;studioQuality=studioSettings.quality;studio.setSettings(studioSettings);studio.prepare(rate,block);studioRunning=false;studioWarmup=0;
        eightQuality=eightSettings.quality;eightCalibration=eightSettings.calibration;eight.setSettings(eightSettings);eight.prepare(rate,block,eightResources);eightRunning=false;eightWarmup=0;
        worn.setSettings(wornSettings);worn.prepare(rate,block);wornRunning=false;wornWarmup=0;
        auto a=legacySettings;a.enabled=a.enabled&&active==0;legacy.setSettings(a);legacy.prepare(rate,block);
        auto s=spatialSettings;s.enabled=s.enabled&&active==1;spatial.setSettings(s);spatial.prepare(rate);
        oldBuffer.setSize(2,block);newBuffer.setSize(2,block);studioBuffer.setSize(2,block);eightBuffer.setSize(2,block);wornBuffer.setSize(2,block);for(auto& x:dry)x.fill(0);for(auto& x:oldDelay)x.fill(0);
        extra=latency()-legacy.latency();jassert(extra>=0&&extra<2048);blend.reset(rate,.025);blend.setCurrentAndTargetValue(legacySettings.enabled&&(active!=3||eight.ready())?legacySettings.mix:0);
    }
    void release(){spatial.release();}
    void workgroupChanged(const juce::AudioWorkgroup& g){spatial.workgroupChanged(g);}
    int latency() const {return SpatialTape::latencySamples;}
    uint64_t guardCount() const {return legacy.guardCount()+spatial.guardCount()+studio.guardCount()+eight.guardCount()+worn.guardCount();}
    void process(juce::AudioBuffer<float>& b){
        if(active!=target){blend.setTargetValue(0);if(blend.getCurrentValue()==0)active=target;}
        const bool qualityChange=active==2&&studioQuality!=studioSettings.quality;
        if(qualityChange){blend.setTargetValue(0);if(blend.getCurrentValue()==0){studioQuality=studioSettings.quality;studioRunning=false;}}
        const bool eightQualityChange=active==3&&(eightQuality!=eightSettings.quality||eightCalibration!=eightSettings.calibration);
        if(eightQualityChange){blend.setTargetValue(0);if(blend.getCurrentValue()==0){eightQuality=eightSettings.quality;eightCalibration=eightSettings.calibration;eightRunning=false;}}
        if(active==target&&!qualityChange&&!eightQualityChange)blend.setTargetValue(legacySettings.enabled&&studioWarmup==0&&eightWarmup==0&&wornWarmup==0&&(active!=3||eight.ready(eightCalibration))?legacySettings.mix:0);
        auto machineSettings=studioSettings;machineSettings.quality=studioQuality;studio.setSettings(machineSettings);
        const bool machineWanted=active==2&&(legacySettings.enabled||blend.getCurrentValue()>0);
        if(machineWanted&&!studioRunning){studio.reset();studioWarmup=latency();blend.setCurrentAndTargetValue(0);studioRunning=true;}
        if(!machineWanted)studioRunning=false;
        auto selectedEight=eightSettings;selectedEight.calibration=eightCalibration;selectedEight.quality=eightQuality;eight.setSettings(selectedEight);
        const bool eightWanted=active==3&&eight.ready(eightCalibration)&&(legacySettings.enabled||blend.getCurrentValue()>0);
        if(eightWanted&&!eightRunning){eight.reset(eightQuality);eightWarmup=latency();blend.setCurrentAndTargetValue(0);eightRunning=true;}
        if(!eightWanted)eightRunning=false;
        worn.setSettings(wornSettings);const bool wornWanted=active==4&&(legacySettings.enabled||blend.getCurrentValue()>0);
        if(wornWanted&&!wornRunning){worn.reset();wornWarmup=latency();blend.setCurrentAndTargetValue(0);wornRunning=true;}
        if(!wornWanted)wornRunning=false;
        const bool audible=blend.getCurrentValue()>0||blend.getTargetValue()>0;
        auto a=legacySettings;a.enabled=audible&&active==0;a.mix=1;legacy.setSettings(a);
        auto s=spatialSettings;s.enabled=audible&&active==1;s.mix=1;spatial.setSettings(s);
        if(!a.enabled)legacy.suspend();if(!s.enabled)spatial.suspend();
        for(int offset=0;offset<b.getNumSamples();offset+=maximum){const int n=std::min(maximum,b.getNumSamples()-offset);
            for(int c=0;c<2;++c){oldBuffer.copyFrom(c,0,b,c,offset,n);newBuffer.copyFrom(c,0,b,c,offset,n);studioBuffer.copyFrom(c,0,b,c,offset,n);eightBuffer.copyFrom(c,0,b,c,offset,n);wornBuffer.copyFrom(c,0,b,c,offset,n);}
            juce::AudioBuffer<float> old(oldBuffer.getArrayOfWritePointers(),2,n),fresh(newBuffer.getArrayOfWritePointers(),2,n);legacy.process(old);spatial.process(fresh);
            juce::AudioBuffer<float> machine(studioBuffer.getArrayOfWritePointers(),2,n);if(machineWanted)studio.process(machine);
            juce::AudioBuffer<float> fitted(eightBuffer.getArrayOfWritePointers(),2,n);if(eightWanted)eight.process(fitted);
            juce::AudioBuffer<float> wornOut(wornBuffer.getArrayOfWritePointers(),2,n);if(wornWanted)worn.process(wornOut);
            for(int i=0;i<n;++i){const float wet=blend.getNextValue();for(int c=0;c<2;++c){const auto ch=(size_t)c;dry[ch][(size_t)write]=b.getSample(c,offset+i);const float original=dry[ch][(size_t)((write+1)%(latency()+1))];oldDelay[ch][(size_t)legacyWrite]=old.getSample(c,i);
                    const float oldAligned=oldDelay[ch][(size_t)((legacyWrite-extra+2048)%2048)];const float processed=active==4?wornOut.getSample(c,i):active==3?fitted.getSample(c,i):active==2?machine.getSample(c,i):active==1?fresh.getSample(c,i):oldAligned;b.setSample(c,offset+i,original+wet*(processed-original));}
                if(studioWarmup>0)--studioWarmup;if(eightWarmup>0)--eightWarmup;if(wornWarmup>0)--wornWarmup;write=(write+1)%(latency()+1);legacyWrite=(legacyWrite+1)%2048;
            }
        }
    }
private:
    Tape legacy;SpatialTape spatial;StudioTape studio;EightTwentyOneTape eight;EightTwentyOneSettings eightSettings;TapeSettings legacySettings;SpatialSettings spatialSettings;StudioSettings studioSettings;
    WornTape worn;WornSettings wornSettings;int wornWarmup=0;bool wornRunning=false;juce::AudioBuffer<float> wornBuffer;
    int eightQuality=0,eightCalibration=0,eightWarmup=0;bool eightRunning=false;
    int studioQuality=1,studioWarmup=0;bool studioRunning=false;
    juce::AudioBuffer<float> oldBuffer,newBuffer,studioBuffer,eightBuffer;
    std::array<std::array<float,SpatialTape::latencySamples+1>,2> dry{};
    std::array<std::array<float,2048>,2> oldDelay{};
    juce::SmoothedValue<float> blend;
    int active=1,target=1,maximum=512,extra=0,write=0,legacyWrite=0;
};
}
