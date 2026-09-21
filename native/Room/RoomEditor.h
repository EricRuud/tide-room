#pragma once
#include "RoomProcessor.h"
#include "HarmonyControls.h"
#include "RoomSceneRenderer.h"
#include "GlassLook.h"
#include "GlassCaseRenderer.h"
#include "MinimalTidesPanel.h"
#include "PostProcessingStrip.h"
#include "RoomLayout.h"
#include "PresetBar.h"

class RoomEditor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit RoomEditor(RoomProcessor&);
    ~RoomEditor() override;
    void paint(juce::Graphics&) override;
    juce::Image tapePanelSnapshot();
    juce::Image motionPanelSnapshot();
    juce::Image oceanPanelSnapshot();
    void advanceOceanPreview(double seconds);
    void openTapeControls();
    void openAdvancedTides();
    ListenerSpace roomView()const{return space();}
    juce::Image tidesPanelSnapshot();
    void openImmersiveView();
    void closeImmersiveView();
    bool isImmersiveViewOpen()const;
    juce::Component* immersiveComponent()const;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
private:
    RoomProcessor& scene;
    RoomSceneRenderer sceneRenderer;
    GlassLook look;
    HarmonyControls harmony;
    HarmonyKeys harmonyKeys;
    PresetBar presetBar;
    MinimalTidesPanel minimalTides;
    PostProcessingStrip postProcessing;
    using SliderAttachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment=juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::array<juce::Slider,
#if TIDE_NATIVE_WOOD
        10
#else
        9
#endif
        > global;
    std::array<std::unique_ptr<SliderAttachment>,
#if TIDE_NATIVE_WOOD
        10
#else
        9
#endif
        > globalAttachments;
    struct Card {
        juce::ComboBox voice;
        juce::ToggleButton mute{"Mute"};
        std::unique_ptr<ButtonAttachment> muteAttachment;
        std::array<juce::Slider,4> controls;
        std::array<std::unique_ptr<SliderAttachment>,4> attachments;
    };
    std::array<Card,3> cards;
    juce::ComboBox placement;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> placementAttachment;
    juce::TextButton tapeOn{"Tape off"};
    std::unique_ptr<ButtonAttachment> tapeAttachment;
    juce::TextButton tapeButton{"Tape controls..."};
    struct TapeWindow;
    std::unique_ptr<TapeWindow> tapeWindow;
    struct MotionWindow;
    std::unique_ptr<MotionWindow> motionWindow;
    struct TidesWindow;
    std::unique_ptr<TidesWindow> tidesWindow;
    struct ImmersiveWindow;
    std::unique_ptr<ImmersiveWindow> immersiveWindow;
    struct SceneButton final : juce::TextButton {
        SceneButton():TextButton("Fullscreen immersive"){}
        void paintButton(juce::Graphics& g,bool over,bool down)override{g.setColour(tide::glass::background.withAlpha(.85f));g.fillRoundedRectangle(getLocalBounds().toFloat(),4);juce::TextButton::paintButton(g,over,down);}
    } immersiveButton;
    struct OceanWindow;
    std::unique_ptr<OceanWindow> oceanWindow;
    juce::TextButton oceanButton{"Tide / Oceans..."};
    juce::TextButton motionButton{"Motion / LFOs..."};
    juce::TextButton recordButton{"Record"},recordingFileButton{"Show file"};
    juce::Label recordingTime;
    juce::String recordingStatus;
    juce::TextButton playButton{"Play"},quietButton{"Quiet"},variationButton{"New variation"},editorButton{"Reverside..."};
    juce::TooltipWindow tips{this,700};
    int dragged=-1;
    juce::Point<float> dragOffset;
    void timerCallback() override;
    ListenerSpace space() const;
    juce::Point<float> movingSource(int) const;
    juce::Point<float> source(int) const;
    void moveSource(juce::Point<float>);
};
