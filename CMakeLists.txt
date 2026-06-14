cmake_minimum_required(VERSION 3.22)
project(Vocalizer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phase 6: native arm64 on Apple Silicon (no Rosetta). The TTS stack is now
# arm64 (official onnxruntime arm64 + Homebrew espeak-ng + piper-phonemize built
# from source). Universal (arm64;x86_64) is a future step needing x86_64 builds
# of the same libs.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13      # latest stable 8.x (matches Computerizer)
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

#==============================================================================
# Neural-TTS stack (arm64). See CLAUDE.md §2 (license: GPLv3, espeak-ng path).
#  - onnxruntime : official arm64 prebuilt (ThirdParty/onnxruntime-arm64), MIT
#  - espeak-ng   : Homebrew arm64 (+ its libpcaudio dep), GPLv3
#  - piper-phonemize : compiled from vendored source (no dylib) — MIT
# ThirdParty/piper-phonemize still provides the piper headers + uni_algo.h.
#==============================================================================
set(PP_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/piper-phonemize)

if(APPLE)
    # macOS arm64 — the proven, shipping configuration (unchanged).
    set(ORT_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/onnxruntime-arm64)
    set(ESPEAK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/espeak-ng-install)
    # espeak-ng (rhasspy fork) is built from source by scripts/build_espeak_ng.sh
    # with the same options piper expects (no pcaudio; exports the *WithTerminator
    # entry point). It ships libespeak-ng.dylib + libucd.dylib (both @rpath) + data.
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION ${ORT_DIR}/lib/libonnxruntime.1.14.1.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${ORT_DIR}/include)

    add_library(espeak-ng SHARED IMPORTED)
    set_target_properties(espeak-ng PROPERTIES
        IMPORTED_LOCATION ${ESPEAK_DIR}/lib/libespeak-ng.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${ESPEAK_DIR}/include)
elseif(WIN32)
    # Windows x64 — UNVERIFIED scaffold (see WINDOWS.md). CI
    # (.github/workflows/windows.yml) provisions these dirs:
    #   ThirdParty/onnxruntime-win-x64   : lib/onnxruntime.{dll,lib} + include
    #   ThirdParty/espeak-ng-install-win : bin/espeak-ng.dll + lib/espeak-ng.lib
    #                                      + include + share/espeak-ng-data
    set(ORT_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/onnxruntime-win-x64)
    set(ESPEAK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/espeak-ng-install-win)
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION ${ORT_DIR}/lib/onnxruntime.dll
        IMPORTED_IMPLIB   ${ORT_DIR}/lib/onnxruntime.lib
        INTERFACE_INCLUDE_DIRECTORIES ${ORT_DIR}/include)

    add_library(espeak-ng SHARED IMPORTED)
    set_target_properties(espeak-ng PROPERTIES
        IMPORTED_LOCATION ${ESPEAK_DIR}/bin/espeak-ng.dll
        IMPORTED_IMPLIB   ${ESPEAK_DIR}/lib/espeak-ng.lib
        INTERFACE_INCLUDE_DIRECTORIES ${ESPEAK_DIR}/include)
else()
    message(FATAL_ERROR "Vocalizer: unsupported platform (only macOS + Windows).")
endif()

# piper-phonemize compiled in (the two functions we use: phonemize_eSpeak +
# phonemes_to_ids). Consumers add these sources + include dirs.
set(PIPER_PHONEMIZE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/tts/pp_phonemize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/tts/pp_phoneme_ids.cpp)
set(PIPER_PHONEMIZE_INCLUDES
    ${PP_DIR}/include ${PP_DIR}/include/piper-phonemize
    ${ORT_DIR}/include ${ESPEAK_DIR}/include)

# Dev paths baked in for the console tools (the shipped plugin resolves bundled
# resources at runtime via BundlePaths). espeak appends "espeak-ng-data".
set(VOCALIZER_ESPEAK_DATA_DIR  ${ESPEAK_DIR}/share)
set(VOCALIZER_VOICES_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/Resources/voices)
set(VOCALIZER_DEFAULT_VOICE_JSON
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/voices/en_US-lessac-medium.onnx.json)

#==============================================================================
# Vocalizer — instrument that reads MIDI (pitch target only) and generates
# audio. Phase 0: empty synth that passes silence. TTS / ONNX / pitch-correct
# land in later phases (see CLAUDE.md §8).
#==============================================================================
juce_add_plugin(Vocalizer
    COMPANY_NAME              "Craig Cowden"
    BUNDLE_ID                 com.cowdenaudio.vocalizer
    PLUGIN_MANUFACTURER_CODE  Cowd          # 4 chars, one uppercase
    PLUGIN_CODE               Vclz          # unique 4 chars, one uppercase
    FORMATS                   VST3 AU
    PRODUCT_NAME              "Vocalizer"
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE        # multiline text-entry field (later phase)
    COPY_PLUGIN_AFTER_BUILD   FALSE         # phase 6: we copy after self-containing
    VST3_CATEGORIES           "Instrument" "Synth"
    AU_MAIN_TYPE              "kAudioUnitType_MusicDevice"
)

# Phase 0 — skeleton: processor + editor + APVTS layout + neon look only.
# Uncomment additional sources as each phase lands them (tts/, dsp/, midi/, ...).
target_sources(Vocalizer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/params/Parameters.cpp
    Source/params/VoicePresets.cpp    # phase 5
    Source/gui/NeonLookAndFeel.cpp
    Source/tts/VoiceModel.cpp         # phase 1
    Source/tts/Phonemizer.cpp         # phase 1
    Source/tts/PiperEngine.cpp        # phase 1
    Source/dsp/AuditionPlayer.cpp     # phase 2
    Source/render/RenderJob.cpp       # phase 2
    Source/gui/WaveformDisplay.cpp    # phase 2
    Source/gui/DragHandle.cpp         # phase 3
    Source/dsp/PitchTracker.cpp       # phase 4
    Source/dsp/PitchCorrector.cpp     # phase 4
    Source/midi/MelodyRecorder.cpp    # phase 4
    Source/gui/MelodyStrip.cpp        # phase 4
    Source/gui/AutotunePanel.cpp      # phase 5
    Source/util/BundlePaths.cpp       # phase 6
    ${PIPER_PHONEMIZE_SOURCES}        # phase 6: piper-phonemize compiled in
)

target_include_directories(Vocalizer PRIVATE ${PIPER_PHONEMIZE_INCLUDES})

target_compile_definitions(Vocalizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Vocalizer
    PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    onnxruntime
    espeak-ng
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(Vocalizer PRIVATE
    ESPEAK_DATA_DIR="${VOCALIZER_ESPEAK_DATA_DIR}"
    VOCALIZER_VOICES_DIR="${VOCALIZER_VOICES_DIR}"
    VOCALIZER_DEFAULT_VOICE_JSON="${VOCALIZER_DEFAULT_VOICE_JSON}")

#==============================================================================
# Phase 6 — self-contained bundle. Each format's binary loads its @rpath TTS
# dylibs from @loader_path (Contents/MacOS), and the engine finds espeak-ng-data
# + voices under Contents/Resources at runtime (BundlePaths). A POST_BUILD step
# populates the freshly-built bundle, fixes up rpaths, ad-hoc codesigns, then
# installs it to the user plugin folder (replacing JUCE's COPY_PLUGIN_AFTER_BUILD,
# which would copy before we populate).
#==============================================================================
function(vocalizer_self_contain fmt installSubdir)
    if(NOT TARGET ${fmt})
        return()
    endif()

    if(APPLE)
        # The format binary must find its sibling dylibs via @loader_path.
        set_target_properties(${fmt} PROPERTIES BUILD_RPATH "@loader_path")

        add_custom_command(TARGET ${fmt} POST_BUILD VERBATIM
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle_fixup.sh"
                "$<TARGET_BUNDLE_DIR:${fmt}>"
                "$<TARGET_FILE_DIR:${fmt}>"
                "$<TARGET_BUNDLE_CONTENT_DIR:${fmt}>/Resources"
                "${ORT_DIR}/lib/libonnxruntime.1.14.1.dylib"
                "${ESPEAK_DIR}/lib/libespeak-ng.dylib"
                "${ESPEAK_DIR}/lib/libucd.dylib"
                "${ESPEAK_DIR}/share/espeak-ng-data"
                "${VOCALIZER_VOICES_DIR}"
                "$ENV{HOME}/Library/Audio/Plug-Ins/${installSubdir}/$<TARGET_BUNDLE_DIR_NAME:${fmt}>"
            COMMENT "Self-containing + installing ${fmt}")
    elseif(WIN32)
        # Windows: copy the DLLs next to the binary (Contents/x86_64-win) and the
        # espeak data + voices into Contents/Resources. UNVERIFIED — see WINDOWS.md.
        # Windows finds DLLs in the loading binary's own directory, so no rpath.
        add_custom_command(TARGET ${fmt} POST_BUILD VERBATIM
            COMMAND pwsh -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle_fixup_win.ps1"
                "-BinaryDir"   "$<TARGET_FILE_DIR:${fmt}>"
                "-ResourcesDir" "$<TARGET_BUNDLE_CONTENT_DIR:${fmt}>/Resources"
                "-OnnxDll"     "${ORT_DIR}/lib/onnxruntime.dll"
                "-EspeakDll"   "${ESPEAK_DIR}/bin/espeak-ng.dll"
                "-EspeakData"  "${ESPEAK_DIR}/share/espeak-ng-data"
                "-VoicesDir"   "${VOCALIZER_VOICES_DIR}"
            COMMENT "Self-containing ${fmt} (Windows)")
    endif()
endfunction()

vocalizer_self_contain(Vocalizer_VST3 "VST3")
vocalizer_self_contain(Vocalizer_AU   "Components")

#==============================================================================
# Dev-only: offline GUI snapshot tool. Renders the real editor to a PNG so the
# neon layout can be eyeballed without a DAW. Opt-in to keep normal plugin
# builds untouched:  cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
#==============================================================================
option(VOCALIZER_BUILD_TOOLS "Build dev tools (GUI snapshot)" OFF)

if(VOCALIZER_BUILD_TOOLS)
    add_executable(render_gui Tests/render_gui.cpp)
    target_include_directories(render_gui PRIVATE Source)
    target_link_libraries(render_gui PRIVATE
        Vocalizer                          # shared code (processor + editor + factory)
        onnxruntime
        espeak-ng
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(render_gui PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)      # optional "generate" mode pumps the loop
    set_target_properties(render_gui PROPERTIES
        BUILD_RPATH "${ORT_DIR}/lib;${ESPEAK_DIR}/lib")

    # tts_smoke — Phase 1 toolchain proof: text -> WAV via Piper, no audio thread.
    add_executable(tts_smoke Tests/tts_smoke.cpp
        Source/tts/VoiceModel.cpp
        Source/tts/Phonemizer.cpp
        Source/tts/PiperEngine.cpp
        ${PIPER_PHONEMIZE_SOURCES})
    target_include_directories(tts_smoke PRIVATE Source ${PIPER_PHONEMIZE_INCLUDES})
    target_link_libraries(tts_smoke PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        onnxruntime
        espeak-ng
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(tts_smoke PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        ESPEAK_DATA_DIR="${VOCALIZER_ESPEAK_DATA_DIR}"
        VOCALIZER_DEFAULT_VOICE_JSON="${VOCALIZER_DEFAULT_VOICE_JSON}")
    set_target_properties(tts_smoke PROPERTIES
        BUILD_RPATH "${ORT_DIR}/lib;${ESPEAK_DIR}/lib")

    # render_smoke — Phase 2 integration proof: drive the real plugin through a
    # full Generate + audition, headless. Links the plugin's shared code.
    add_executable(render_smoke Tests/render_smoke.cpp)
    target_include_directories(render_smoke PRIVATE Source)
    target_link_libraries(render_smoke PRIVATE
        Vocalizer
        onnxruntime
        espeak-ng
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(render_smoke PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)   # headless test pumps the message loop
    set_target_properties(render_smoke PROPERTIES
        BUILD_RPATH "${ORT_DIR}/lib;${ESPEAK_DIR}/lib")

    # dsp_smoke — Phase 4 numeric DSP checks (pitch accuracy, formant preserve,
    # scale-snap). No TTS deps needed.
    add_executable(dsp_smoke Tests/dsp_smoke.cpp
        Source/dsp/PitchTracker.cpp
        Source/dsp/PitchCorrector.cpp
        Source/midi/MelodyRecorder.cpp)
    target_include_directories(dsp_smoke PRIVATE Source)
    target_link_libraries(dsp_smoke PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(dsp_smoke PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)

    # state_smoke — Phase 5: state round-trip + voice switching.
    add_executable(state_smoke Tests/state_smoke.cpp)
    target_include_directories(state_smoke PRIVATE Source)
    target_link_libraries(state_smoke PRIVATE
        Vocalizer
        onnxruntime
        espeak-ng
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(state_smoke PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    set_target_properties(state_smoke PROPERTIES
        BUILD_RPATH "${ORT_DIR}/lib;${ESPEAK_DIR}/lib")
endif()
