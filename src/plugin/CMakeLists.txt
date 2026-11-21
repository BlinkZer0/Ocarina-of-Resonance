set(OCARINA_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
if(OCARINA_JUCE_DIR)
    add_subdirectory("${OCARINA_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.17-security
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(OcarinaVST
    COMPANY_NAME BlinkZer0
    BUNDLE_ID com.blinkzer0.ocarinavst
    PRODUCT_NAME "Ocarina of Resonance"
    PLUGIN_MANUFACTURER_CODE Blz0
    PLUGIN_CODE OcRs
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    VST3_CATEGORIES Instrument Synth)

target_sources(OcarinaVST PRIVATE PluginProcessor.cpp)
target_compile_definitions(OcarinaVST PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(OcarinaVST PRIVATE ocarina_core juce::juce_audio_processors juce::juce_recommended_config_flags)

if(BUILD_TESTING)
    add_executable(ocarina_vst3_test "${PROJECT_SOURCE_DIR}/tests/Vst3Test.cpp")
    target_compile_definitions(ocarina_vst3_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(ocarina_vst3_test PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_dependencies(ocarina_vst3_test OcarinaVST_VST3)
    add_test(NAME ocarina_vst3
        COMMAND ocarina_vst3_test "$<TARGET_GENEX_EVAL:OcarinaVST_VST3,$<TARGET_PROPERTY:OcarinaVST_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
endif()
