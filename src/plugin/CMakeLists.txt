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

if(WIN32 AND NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
    set(webview_version 1.0.3485.44)
    set(webview_name "Microsoft.Web.WebView2.${webview_version}")
    set(nuget "$ENV{LOCALAPPDATA}/PackageManagement/NuGet/Packages")
    if(EXISTS "${nuget}/${webview_name}/build/native/include/WebView2.h")
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${nuget}")
    else()
        include(FetchContent)
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        set(webview_parent "${CMAKE_BINARY_DIR}/dependencies")
        FetchContent_Declare(ocarina_webview2
            URL "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${webview_version}/microsoft.web.webview2.${webview_version}.nupkg"
            URL_HASH SHA256=bc09150b179246ac90189649b13be8e6b11b3ac200e817e18df106e1f3cf489e
            DOWNLOAD_NAME webview2.zip
            SOURCE_DIR "${webview_parent}/${webview_name}")
        FetchContent_MakeAvailable(ocarina_webview2)
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${webview_parent}")
    endif()
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
    NEEDS_WEBVIEW2 TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth)

set(panel "${PROJECT_SOURCE_DIR}/docs")
juce_add_binary_data(OcarinaPanel NAMESPACE OcarinaPanel HEADER_NAME OcarinaPanel.h SOURCES
    "${panel}/index.html" "${panel}/assets/main.js" "${panel}/assets/presets.js" "${panel}/assets/style.css"
    "${panel}/assets/sequence.js" "${panel}/assets/midi-file.js"
    "${panel}/assets/parameters.json" "${panel}/assets/ocarina.webp")

target_sources(OcarinaVST PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_compile_definitions(OcarinaVST PRIVATE JUCE_WEB_BROWSER=1 JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(OcarinaVST PRIVATE ocarina_core OcarinaPanel juce::juce_audio_processors juce::juce_gui_extra juce::juce_recommended_config_flags)

if(BUILD_TESTING)
    add_executable(ocarina_vst3_test "${PROJECT_SOURCE_DIR}/tests/Vst3Test.cpp")
    target_compile_definitions(ocarina_vst3_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(ocarina_vst3_test PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_dependencies(ocarina_vst3_test OcarinaVST_VST3)
    add_test(NAME ocarina_vst3
        COMMAND ocarina_vst3_test "$<TARGET_GENEX_EVAL:OcarinaVST_VST3,$<TARGET_PROPERTY:OcarinaVST_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
endif()
