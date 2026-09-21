#pragma once
#include "RoomProcessor.h"

// Keep the saved five-engine parameter intact while exposing the two current
// choices. A ComboBoxAttachment would map item indices to the wrong engines.
class TapeModelSelector final : public juce::ComboBox {
public:
    explicit TapeModelSelector(RoomProcessor& p) {
        setName("Tape model");setTooltip("Choose Worn tape or 821. Each model keeps its own settings; Mix and Enabled are shared.");
        addItem("Worn tape",5);addItem("821",4);setTextWhenNothingSelected("Legacy tape");
        attachment=std::make_unique<juce::ParameterAttachment>(*p.parameters.getParameter("tapeModel"),[this](float value){
            const int mode=juce::roundToInt(value);setSelectedId(mode==3||mode==4?mode+1:0,juce::dontSendNotification);
            if(onModelChange)onModelChange(mode);
        },nullptr);
        onChange=[this]{if(getSelectedId()>0)attachment->setValueAsCompleteGesture((float)(getSelectedId()-1));};
        attachment->sendInitialUpdate();
    }
    std::function<void(int)> onModelChange;
private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
