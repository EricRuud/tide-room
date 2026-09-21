#include "EffectHost.h"
#include <iostream>
int runEffectChecks(const juce::File&,const juce::File&);
int main(int argc,char* argv[]) {
    juce::ScopedJuceInitialiser_GUI initialise;
    if(argc!=4){std::cerr<<"Usage: TideFXCheck --effect-test /path/Effect.vst3 /new/output/directory\n";return 2;}
    const juce::File plugin(argv[2]),output(argv[3]);
    if(juce::String(argv[1])=="--scan-effect")return EffectHost::scanToFile(plugin,output)?0:1;
    if(juce::String(argv[1])=="--effect-test")return runEffectChecks(plugin,output);
    return 2;
}
