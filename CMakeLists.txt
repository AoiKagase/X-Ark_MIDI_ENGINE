cmake_minimum_required(VERSION 3.16)
project(XArkMidiEngine VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(XAME_SOURCES
    src/api.cpp
    src/dllmain.cpp
    src/common/BinaryReader.cpp
    src/common/CpuFeatures.cpp
    src/common/FileUtil.cpp
    src/midi/MidiFile.cpp
    src/midi/MidiSequencer.cpp
    src/midi/MidiTrack.cpp
    src/midi2/Midi2File.cpp
    src/midi2/UmpDownConverter.cpp
    src/dls/DlsFile.cpp
    src/sf2/Sf2File.cpp
    src/synth/SimdKernels.cpp
    src/synth/SimdKernelsAvx2.cpp
    src/synth/Synthesizer.cpp
    src/synth/Voice.cpp
    src/synth/VoicePool.cpp
)

# ---------------------------------------------------------------------------
# Shared library target
# ---------------------------------------------------------------------------
add_library(XArkMidiEngine SHARED ${XAME_SOURCES})

target_include_directories(XArkMidiEngine
    PUBLIC  include
    PRIVATE src
)

target_compile_definitions(XArkMidiEngine PRIVATE XARKMIDIENGINE_EXPORTS)

# Symbol visibility: hide all by default, export only XAME_API symbols
if(NOT WIN32)
    target_compile_options(XArkMidiEngine PRIVATE -fvisibility=hidden)
endif()

# ---------------------------------------------------------------------------
# AVX2: compile SimdKernelsAvx2.cpp with AVX2 enabled
# ---------------------------------------------------------------------------
if(MSVC)
    set_source_files_properties(
        src/synth/SimdKernelsAvx2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
    )
else()
    set_source_files_properties(
        src/synth/SimdKernelsAvx2.cpp
        PROPERTIES COMPILE_OPTIONS "-mavx2"
    )
endif()

# ---------------------------------------------------------------------------
# Compiler warnings & optimizations
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(XArkMidiEngine PRIVATE /W4 /WX-)
else()
    target_compile_options(XArkMidiEngine PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
    )
endif()

# ---------------------------------------------------------------------------
# Test executable (optional, built by default)
# ---------------------------------------------------------------------------
option(XAME_BUILD_TESTS "Build test executables" ON)

if(XAME_BUILD_TESTS)
    add_executable(XArkMidiTest tests/main.cpp)
    target_link_libraries(XArkMidiTest PRIVATE XArkMidiEngine)
    target_include_directories(XArkMidiTest PRIVATE include)

    # Internal dump/inspect tools (link directly against sources, not the shared lib)
    foreach(_tool dump_midi_track dump_sf2_zone dump_dls_zone)
        add_executable(${_tool} tests/${_tool}.cpp)
        target_include_directories(${_tool} PRIVATE src)
        target_compile_features(${_tool} PRIVATE cxx_std_17)
        # These tools compile the parser sources directly (no DLL dependency)
        target_sources(${_tool} PRIVATE
            src/common/BinaryReader.cpp
            src/common/FileUtil.cpp
            src/midi/MidiFile.cpp
            src/midi/MidiSequencer.cpp
            src/midi/MidiTrack.cpp
            src/midi2/Midi2File.cpp
            src/midi2/UmpDownConverter.cpp
            src/dls/DlsFile.cpp
            src/sf2/Sf2File.cpp
        )
    endforeach()

    add_executable(sf2_compliance
        tests/sf2_compliance.cpp
        src/common/BinaryReader.cpp
        src/common/CpuFeatures.cpp
        src/common/FileUtil.cpp
        src/sf2/Sf2File.cpp
        src/synth/SimdKernels.cpp
        src/synth/SimdKernelsAvx2.cpp
        src/synth/Voice.cpp
    )
    target_include_directories(sf2_compliance PRIVATE src)
    target_compile_features(sf2_compliance PRIVATE cxx_std_17)
    if(MSVC)
        set_source_files_properties(
            src/synth/SimdKernelsAvx2.cpp
            PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
        )
    else()
        set_source_files_properties(
            src/synth/SimdKernelsAvx2.cpp
            PROPERTIES COMPILE_OPTIONS "-mavx2"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS XArkMidiEngine
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES include/XArkMidiEngine.h
    DESTINATION include
)
