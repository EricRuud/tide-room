cmake_minimum_required(VERSION 3.22)
project(Tide VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.cache/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_PATH} juce EXCLUDE_FROM_ALL)
juce_add_binary_data(TideAssets HEADER_NAME TideAssets.h NAMESPACE TideAssets
    SOURCES native/generated/close.wav native/generated/bloom.wav)
juce_add_plugin(Tide
    COMPANY_NAME "Eric Ruud"
    BUNDLE_ID "com.ericruud.tide"
    PLUGIN_MANUFACTURER_CODE ErRd
    PLUGIN_CODE TIde
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Tide"
    DESCRIPTION "Expressive wavefolding instrument"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_SANDBOX_SAFE TRUE
    VST3_CATEGORIES Instrument Synth
    MICROPHONE_PERMISSION_ENABLED FALSE)
target_sources(Tide PRIVATE native/Source/Engine.cpp native/Source/Processor.cpp native/Source/Editor.cpp)
target_include_directories(Tide PUBLIC native/Source native/generated)
target_compile_definitions(Tide PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Tide PRIVATE TideAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
juce_add_console_app(TideCheck PRODUCT_NAME "TideCheck")
target_sources(TideCheck PRIVATE native/Tests/Checks.cpp)
target_link_libraries(TideCheck PRIVATE Tide juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(TideCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
enable_testing()
add_test(NAME TideNative COMMAND TideCheck)
juce_add_console_app(TideHostCheck PRODUCT_NAME "TideHostCheck")
target_sources(TideHostCheck PRIVATE native/Tests/Host.cpp)
target_link_libraries(TideHostCheck PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(TideHostCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)

# A standalone host edition. The existing Tide AU/VST3 stay simple instruments.
juce_add_plugin(TideFX VERSION 0.3.0 COMPANY_NAME "Eric Ruud"
    BUNDLE_ID "com.ericruud.tide.fx" PLUGIN_MANUFACTURER_CODE ErRd PLUGIN_CODE TFxe
    FORMATS Standalone PRODUCT_NAME "Tide FX" DESCRIPTION "Tide with a stereo VST3 effect slot"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE MICROPHONE_PERMISSION_ENABLED FALSE)
target_sources(TideFX PRIVATE native/Source/Engine.cpp native/Source/Processor.cpp native/Source/Editor.cpp
    native/Source/EffectHost.cpp native/Source/FXApplication.cpp)
target_include_directories(TideFX PUBLIC native/Source native/generated)
target_compile_definitions(TideFX PUBLIC TIDE_EFFECT_HOST=1 JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 JUCE_USE_CUSTOM_PLUGIN_STANDALONE_ENTRYPOINT=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TideFX PRIVATE TideAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
juce_add_console_app(TideFXCheck PRODUCT_NAME "TideFXCheck")
target_sources(TideFXCheck PRIVATE native/Tests/EffectChecks.cpp native/Tests/FXMain.cpp)
target_link_libraries(TideFXCheck PRIVATE TideFX juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TidePluckCheck PRODUCT_NAME "TidePluckCheck")
target_sources(TidePluckCheck PRIVATE native/Tests/Percussion.cpp)
target_link_libraries(TidePluckCheck PRIVATE TideFX juce::juce_audio_utils juce::juce_dsp)

juce_add_binary_data(RoomHrtfAssets HEADER_NAME RoomHrtfAssets.h NAMESPACE RoomHrtfAssets SOURCES native/generated/kemar.bin)
juce_add_binary_data(RoomPresetAssets HEADER_NAME RoomPresetAssets.h NAMESPACE RoomPresetAssets SOURCES native/Assets/Presets/SlowTides.xml)
option(TIDE_NATIVE_WOOD "Build the native shared room audition" OFF)
option(TIDE_CLEAR_ROOM "Use the transparent full-rate room in the native audition" OFF)
if(TIDE_CLEAR_ROOM AND NOT TIDE_NATIVE_WOOD)
    message(FATAL_ERROR "TIDE_CLEAR_ROOM requires TIDE_NATIVE_WOOD")
endif()
juce_add_plugin(TideRoom VERSION 0.12.0 COMPANY_NAME "Eric Ruud"
    BUNDLE_ID "com.ericruud.tide.room" PLUGIN_MANUFACTURER_CODE ErRd PLUGIN_CODE TRom
    ICON_BIG "native/Assets/tide-room-icon.png"
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string></dict></plist>"
    FORMATS Standalone PRODUCT_NAME "Tide Room" DESCRIPTION "Generative spatial synthesizer with reactive glass and water"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE MICROPHONE_PERMISSION_ENABLED FALSE)
target_sources(TideRoom PRIVATE native/Source/Engine.cpp native/Room/ReversideRoom.cpp native/Room/Binaural.cpp native/Room/Tape.cpp native/Room/Voices.cpp
    native/Room/StudioTape.cpp native/Room/EightTwentyOneTape.cpp native/Room/WornTape.cpp native/Room/RoomProcessor.cpp native/Room/RoomEditor.cpp native/Room/MotionPanel.cpp native/Room/RoomApplication.cpp native/Room/SpatialSolver.cpp native/Room/SpatialTape.cpp)
target_include_directories(TideRoom PUBLIC native/Room native/Source native/generated)
target_sources(TideRoom PRIVATE native/Room/OutputRecorder.cpp native/Room/OceanPanel.cpp)
target_sources(TideRoom PRIVATE native/Room/RoomPresets.cpp)
juce_add_console_app(TidePresetCheck PRODUCT_NAME "TidePresetCheck")
target_sources(TidePresetCheck PRIVATE native/Room/PresetChecks.cpp)
target_link_libraries(TidePresetCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideOceanCheck PRODUCT_NAME "TideOceanCheck")
target_sources(TideOceanCheck PRIVATE native/Room/OceanChecks.cpp)
target_link_libraries(TideOceanCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideGpuCheck PRODUCT_NAME "TideGpuCheck")
target_sources(TideGpuCheck PRIVATE native/Room/GpuChecks.cpp)
target_link_libraries(TideGpuCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(TideRoom PUBLIC JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TideRoom PRIVATE RoomHrtfAssets RoomPresetAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(APPLE)
    enable_language(OBJCXX)
    find_program(TIDE_XCRUN xcrun REQUIRED)
    set(TIDE_METAL_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/RoomEffects.metallib")
    add_custom_command(OUTPUT "${TIDE_METAL_LIBRARY}"
        COMMAND "${TIDE_XCRUN}" -sdk macosx metal -std=macos-metal2.4 -fno-fast-math
            "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-fmodules-cache-path=${CMAKE_CURRENT_BINARY_DIR}/metal-modules"
            -c "${CMAKE_CURRENT_SOURCE_DIR}/native/Room/RoomEffects.metal"
            -o "${CMAKE_CURRENT_BINARY_DIR}/RoomEffects.air"
        COMMAND "${TIDE_XCRUN}" -sdk macosx metallib "${CMAKE_CURRENT_BINARY_DIR}/RoomEffects.air"
            -o "${TIDE_METAL_LIBRARY}"
        DEPENDS native/Room/RoomEffects.metal native/Room/TowerOptics.h VERBATIM)
    juce_add_binary_data(RoomShaderAssets HEADER_NAME RoomShaderAssets.h NAMESPACE RoomShaderAssets SOURCES "${TIDE_METAL_LIBRARY}")
    target_sources(TideRoom PRIVATE native/Room/GpuEffects.mm)
    set_source_files_properties(native/Room/GpuEffects.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(TideRoom PRIVATE RoomShaderAssets "-framework Metal" "-framework Foundation")
else()
    target_sources(TideRoom PRIVATE native/Room/GpuEffectsStub.cpp)
endif()
if(TIDE_NATIVE_WOOD)
    target_compile_definitions(TideRoom PUBLIC TIDE_NATIVE_WOOD=1 TIDE_NO_CONVOLUTION=1)
    target_sources(TideRoom PRIVATE native/Room/WoodRoom.cpp
        native/third_party/airwindows/kwoodroom/kWoodRoom.cpp
        native/third_party/airwindows/kwoodroom/kWoodRoomProc.cpp)
    if(TIDE_CLEAR_ROOM)
        target_compile_definitions(TideRoom PUBLIC TIDE_CLEAR_ROOM=1)
        target_sources(TideRoom PRIVATE native/Room/ClearRoom.cpp)
        juce_add_console_app(TideClearCheck PRODUCT_NAME "TideClearCheck")
        target_sources(TideClearCheck PRIVATE native/Room/ClearChecks.cpp)
        target_link_libraries(TideClearCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
    endif()
    # Upstream DSP is retained verbatim, including its original style.
    set_source_files_properties(native/third_party/airwindows/kwoodroom/kWoodRoom.cpp
        native/third_party/airwindows/kwoodroom/kWoodRoomProc.cpp PROPERTIES COMPILE_OPTIONS "-w")
    juce_add_console_app(TideWoodCheck PRODUCT_NAME "TideWoodCheck")
    target_sources(TideWoodCheck PRIVATE native/Room/WoodChecks.cpp)
    target_link_libraries(TideWoodCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
else()
    target_link_libraries(TideRoom PRIVATE TideAssets)
endif()
set_source_files_properties(native/Room/EightTwentyOneTape.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
set(TIDE_ROOM_OUTPUT_DIRECTORY "" CACHE PATH "Optional standalone output folder for a separate audition build")
set(TIDE_900_MODEL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native/Assets/821/90030" CACHE PATH "Selected 900/30 calibration coefficients")
if(TIDE_ROOM_OUTPUT_DIRECTORY)
    set_target_properties(TideRoom_Standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TIDE_ROOM_OUTPUT_DIRECTORY}")
endif()
add_custom_command(TARGET TideRoom_Standalone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_BUNDLE_DIR:TideRoom_Standalone>/Contents/Resources/LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_BUNDLE_DIR:TideRoom_Standalone>/Contents/Resources/THIRD_PARTY_NOTICES.md"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/native/Assets/ThirdPartyNotices" "$<TARGET_BUNDLE_DIR:TideRoom_Standalone>/Contents/Resources/ThirdPartyNotices"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/native/Assets/821/model" "$<TARGET_BUNDLE_DIR:TideRoom_Standalone>/Contents/Resources/821/model"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${TIDE_900_MODEL_DIRECTORY}" "$<TARGET_BUNDLE_DIR:TideRoom_Standalone>/Contents/Resources/821/90030"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/native/Assets/821/hq" "$<TARGET_BUNDLE_DIR:TideRoom_Standalone>/Contents/Resources/821/hq")
juce_add_console_app(Tide821Check PRODUCT_NAME "Tide821Check")
target_sources(Tide821Check PRIVATE native/Room/EightTwentyOneChecks.cpp)
target_compile_options(Tide821Check PRIVATE -ffp-contract=off)
target_link_libraries(Tide821Check PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideWornCheck PRODUCT_NAME "TideWornCheck")
target_sources(TideWornCheck PRIVATE native/Room/WornChecks.cpp native/Room/WornTape.cpp)
target_include_directories(TideWornCheck PRIVATE native/Room)
target_link_libraries(TideWornCheck PRIVATE juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(TideWornCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
juce_add_console_app(TideRoomCheck PRODUCT_NAME "TideRoomCheck")
target_sources(TideRoomCheck PRIVATE native/Room/RoomChecks.cpp)
target_link_libraries(TideRoomCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideHarmonyCheck PRODUCT_NAME "TideHarmonyCheck")
target_sources(TideHarmonyCheck PRIVATE native/Room/HarmonyChecks.cpp)
target_link_libraries(TideHarmonyCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideRecordingCheck PRODUCT_NAME "TideRecordingCheck")
target_sources(TideRecordingCheck PRIVATE native/Room/RecordingChecks.cpp)
target_link_libraries(TideRecordingCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideColourCheck PRODUCT_NAME "TideColourCheck")
target_sources(TideColourCheck PRIVATE native/Room/ColourChecks.cpp native/Tests/legacy/Engine02.cpp)
target_link_libraries(TideColourCheck PRIVATE TideRoom TideAssets juce::juce_audio_utils juce::juce_dsp)

# Offline Airwindows comparison and experimental hybrid; not linked into Tide Room.
juce_add_console_app(TideTapeLab PRODUCT_NAME "TideTapeLab")
target_sources(TideTapeLab PRIVATE native/Lab/TapeLab.cpp native/Lab/HybridTape.cpp native/Room/Tape.cpp
    native/Lab/airwindows/ToTape9.cpp native/Lab/airwindows/ToTape9Proc.cpp)
target_link_libraries(TideTapeLab PRIVATE juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(TideTapeLab PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

juce_add_console_app(TideSpatialCheck PRODUCT_NAME "TideSpatialCheck")
target_sources(TideSpatialCheck PRIVATE native/Room/SpatialChecks.cpp)
target_link_libraries(TideSpatialCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideEfficiencyCheck PRODUCT_NAME "TideEfficiencyCheck")
target_sources(TideEfficiencyCheck PRIVATE native/Room/EfficiencyChecks.cpp)
target_link_libraries(TideEfficiencyCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)
juce_add_console_app(TideMotionCheck PRODUCT_NAME "TideMotionCheck")
target_sources(TideMotionCheck PRIVATE native/Room/MotionChecks.cpp)
target_link_libraries(TideMotionCheck PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(TideQualityAudit PRODUCT_NAME "TideQualityAudit")
target_sources(TideQualityAudit PRIVATE native/Room/QualityAudit.cpp)
target_link_libraries(TideQualityAudit PRIVATE TideRoom juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(TideStudioCheck PRODUCT_NAME "TideStudioCheck")
target_sources(TideStudioCheck PRIVATE native/Room/StudioChecks.cpp native/Room/StudioTape.cpp)
target_include_directories(TideStudioCheck PRIVATE native/Room)
target_link_libraries(TideStudioCheck PRIVATE juce::juce_audio_formats juce::juce_dsp juce::juce_events juce::juce_recommended_config_flags)
target_compile_definitions(TideStudioCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# Isolated third-party plugin measurement host; never opens an audio device.
juce_add_gui_app(TidePluginBench PRODUCT_NAME "Tide Plugin Bench" BUNDLE_ID "com.ericruud.tide.pluginbench")
target_sources(TidePluginBench PRIVATE native/Lab/PluginBench.cpp)
target_link_libraries(TidePluginBench PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(TidePluginBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
