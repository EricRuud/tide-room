#include "RoomPresets.h"
#include "RoomPresetAssets.h"
#include <cmath>
#include <cstdlib>

juce::ValueTree RoomPresets::slowTides(){
    if(auto xml=juce::parseXML(juce::String::fromUTF8(RoomPresetAssets::SlowTides_xml,RoomPresetAssets::SlowTides_xmlSize)))return juce::ValueTree::fromXml(*xml);
    return {};
}
RoomPresets::RoomPresets(RoomProcessor& p,juce::File directory):processor(p),folder(directory==juce::File{}?juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Tide Room/Presets"):directory){
    auto& state=p.parameters.state;
    if(state.getProperty("presetName").toString().isEmpty()){
        const bool factory=matchesFactory();state.setProperty("presetName",factory?"Slow tides":"Current scene",nullptr);state.setProperty("presetId",factory?"factory:slow-tides":"",nullptr);state.setProperty("presetModified",!factory,nullptr);
    }
    dirty=(bool)state.getProperty("presetModified",false);
    for(auto* parameter:p.getParameters())if(auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))p.parameters.addParameterListener(named->paramID,this);
}
RoomPresets::~RoomPresets(){persistMetadata();for(auto* parameter:processor.getParameters())if(auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))processor.parameters.removeParameterListener(named->paramID,this);}
void RoomPresets::persistMetadata(){processor.parameters.state.setProperty("presetModified",dirty.load(),nullptr);}
juce::String RoomPresets::currentName()const{return processor.parameters.state.getProperty("presetName","Current scene").toString();}
juce::String RoomPresets::currentId()const{return processor.parameters.state.getProperty("presetId").toString();}
bool RoomPresets::matchesFactory()const{
    const auto saved=slowTides();if(!saved.isValid())return false;
    for(const auto& value:saved)if(value.hasType("PARAM")){
        const auto id=value.getProperty("id").toString();auto* parameter=processor.parameters.getRawParameterValue(id);
        if(!parameter||std::abs(parameter->load()-(float)value.getProperty("value"))>.00001f)return false;
    }
    for(const auto& c:tide::tides::controls)if(std::abs(processor.get(c.id)-c.initial)>.00001f)return false;
    return true;
}
std::vector<RoomPresets::Entry> RoomPresets::entries()const{
    std::vector<Entry> result{{"factory:slow-tides","Slow tides",{},true}};
    for(const auto& file:folder.findChildFiles(juce::File::findFiles,false,"*.tideroom")){
        juce::ValueTree state;if(read(file,state).wasOk())result.push_back({file.getFileName(),state.getProperty("presetName",file.getFileNameWithoutExtension()).toString(),file,false});
    }
    std::sort(result.begin()+1,result.end(),[](const auto& a,const auto& b){return a.name.compareNatural(b.name)<0;});return result;
}
juce::Result RoomPresets::validate(const juce::ValueTree& state)const{
    if(!state.hasType("TideRoomState"))return juce::Result::fail("This file is not a Tide Room preset.");
    int known=0;juce::StringArray seen;
    for(const auto& value:state)if(value.hasType("PARAM")){
        const auto id=value.getProperty("id").toString();if(seen.contains(id))return juce::Result::fail("The preset contains duplicate parameters.");seen.add(id);
        const auto raw=value.getProperty("value");if(!(raw.isInt()||raw.isInt64()||raw.isDouble()||raw.isBool()||raw.isString()))return juce::Result::fail("The preset contains an invalid value.");
        const auto text=raw.toString();char* end=nullptr;const double number=std::strtod(text.toRawUTF8(),&end);
        if(!std::isfinite(number)||end==text.toRawUTF8()||*end!='\0')return juce::Result::fail("The preset contains an invalid number.");
        if(auto* p=processor.parameters.getParameter(id)){const auto range=p->getNormalisableRange();if(number<range.start-.0001||number>range.end+.0001)return juce::Result::fail("A preset value is outside its supported range.");++known;}
    }
    return known>0?juce::Result::ok():juce::Result::fail("The preset contains no scene parameters.");
}
juce::Result RoomPresets::read(const juce::File& file,juce::ValueTree& state)const{
    if(!file.existsAsFile()||file.getSize()>2*1024*1024)return juce::Result::fail("Cannot read this preset file.");
    if(auto xml=juce::XmlDocument::parse(file))state=juce::ValueTree::fromXml(*xml);else return juce::Result::fail("The preset file is not valid XML.");return validate(state);
}
juce::Result RoomPresets::apply(juce::ValueTree state,const Entry& entry){
    auto result=validate(state);if(result.failed())return result;
    state.setProperty("presetName",entry.name,nullptr);state.setProperty("presetId",entry.id,nullptr);state.setProperty("presetVersion",1,nullptr);state.setProperty("presetModified",false,nullptr);
    juce::MemoryBlock data;juce::AudioProcessor::copyXmlToBinary(*state.createXml(),data);
    const bool playing=processor.isPlaying();loading=true;processor.setStateInformation(data.getData(),(int)data.getSize());loading=false;dirty=false;
    if(playing)processor.play();return juce::Result::ok();
}
juce::Result RoomPresets::load(const Entry& entry){
    if(entry.factory)return apply(slowTides(),entry);
    juce::ValueTree state;auto result=read(entry.file,state);return result.failed()?result:apply(state,entry);
}
juce::Result RoomPresets::saveAs(juce::String name){
    name=name.trim().substring(0,80);if(name.isEmpty())return juce::Result::fail("Enter a preset name.");
    const auto made=folder.createDirectory();if(made.failed())return made;
    auto stem=juce::File::createLegalFileName(name).trim();if(stem.isEmpty()||stem=="."||stem=="..")return juce::Result::fail("Choose another preset name.");
    auto file=folder.getNonexistentChildFile(stem,".tideroom",true);
    const auto finalName=file.getFileNameWithoutExtension();juce::MemoryBlock data;processor.getStateInformation(data);
    auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),(int)data.getSize());if(!xml)return juce::Result::fail("Could not capture the scene.");
    auto state=juce::ValueTree::fromXml(*xml);state.setProperty("presetName",finalName,nullptr);state.setProperty("presetId",file.getFileName(),nullptr);state.setProperty("presetVersion",1,nullptr);state.setProperty("presetModified",false,nullptr);
    if(!file.replaceWithText(state.toXmlString()))return juce::Result::fail("Could not save the preset.");
    processor.parameters.state.setProperty("presetName",finalName,nullptr);processor.parameters.state.setProperty("presetId",file.getFileName(),nullptr);dirty=false;persistMetadata();return juce::Result::ok();
}
juce::Result RoomPresets::importFile(const juce::File& file){
    juce::ValueTree state;auto result=read(file,state);if(result.failed())return result;
    // Import into the library before changing the running scene.
    const auto made=folder.createDirectory();if(made.failed())return made;
    auto name=juce::File::createLegalFileName(state.getProperty("presetName",file.getFileNameWithoutExtension()).toString()).trim().substring(0,80);
    if(name.isEmpty()||name=="."||name=="..")name="Imported scene";
    const auto destination=folder.getNonexistentChildFile(name,".tideroom",true);Entry entry{destination.getFileName(),destination.getFileNameWithoutExtension(),destination,false};
    state.setProperty("presetName",entry.name,nullptr);state.setProperty("presetId",entry.id,nullptr);
    if(!destination.replaceWithText(state.toXmlString()))return juce::Result::fail("Could not import the preset.");return apply(state,entry);
}
