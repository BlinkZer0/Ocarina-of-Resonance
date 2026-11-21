add_library(ocarina_core STATIC
    dsp/AirJetModel.cpp
    dsp/OcarinaVoice.cpp
    dsp/VesselResonator.cpp
    dsp/VoiceManager.cpp)
target_include_directories(ocarina_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ocarina_core PUBLIC cxx_std_20)
target_compile_options(ocarina_core PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(ocarina_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
