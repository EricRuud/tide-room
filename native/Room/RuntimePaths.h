#pragma once
#include <juce_core/juce_core.h>

namespace tide::room {
inline juce::File runtimeResources() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile).getChildFile("Contents/Resources");
#else
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("Resources");
#endif
}
}
