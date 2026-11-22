cmake_minimum_required(VERSION 3.21)
project(guikit_text VERSION 1.0.0 LANGUAGES CXX C)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(GUIKIT_TEXT_USE_FREETYPE "Use optional FreeType glyph rasterizer backend (OFF = keel::font zero-dependency C++20 rasterizer)" OFF)
option(GUIKIT_TEXT_BUILD_TESTS "Build GuiKit Text unit tests" ${PROJECT_IS_TOP_LEVEL})
option(GUIKIT_TEXT_INSTALL "Enable CMake install targets for guikit_text" ${PROJECT_IS_TOP_LEVEL})

if (GUIKIT_TEXT_BUILD_TESTS)
    enable_testing()
endif()

if (NOT TARGET guikit::core)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/guikit/CMakeLists.txt")
        add_subdirectory(third_party/guikit EXCLUDE_FROM_ALL)
    else()
        find_package(guikit REQUIRED)
    endif()
endif()

function(_guikit_text_add_bundled_freetype)
    set(_FT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype")
    set(_FT_GEN_INC "${CMAKE_CURRENT_BINARY_DIR}/freetype_config")
    set(_FT_OPT_OUT "${_FT_GEN_INC}/freetype/config/ftoption.h")

    # Disable bundled zlib/WOFF1 decompressor cleanly so src/gzip/ftgzip.c is not needed
    file(READ "${_FT_ROOT}/include/freetype/config/ftoption.h" _FT_OPT_CONTENT)
    string(REPLACE
        "#define FT_CONFIG_OPTION_USE_ZLIB"
        "/* #define FT_CONFIG_OPTION_USE_ZLIB */"
        _FT_OPT_CONTENT
        "${_FT_OPT_CONTENT}"
    )
    if (EXISTS "${_FT_OPT_OUT}")
        file(READ "${_FT_OPT_OUT}" _FT_OPT_OLD)
    else()
        set(_FT_OPT_OLD "")
    endif()
    if (NOT (_FT_OPT_OLD STREQUAL _FT_OPT_CONTENT))
        file(MAKE_DIRECTORY "${_FT_GEN_INC}/freetype/config")
        file(WRITE "${_FT_OPT_OUT}" "${_FT_OPT_CONTENT}")
    endif()

    # Minimal modern FreeType build: TTF/TTC + OTF (CFF) + Variable Fonts + autofit + smooth/Harmony LCD + embolden
    add_library(guikit_freetype STATIC
        "${_FT_ROOT}/src/base/ftbase.c"
        "${_FT_ROOT}/src/base/ftbitmap.c"
        "${_FT_ROOT}/src/base/ftdebug.c"
        "${_FT_ROOT}/src/base/ftinit.c"
        "${_FT_ROOT}/src/base/ftmm.c"
        "${_FT_ROOT}/src/base/ftsynth.c"
        "${_FT_ROOT}/src/base/ftsystem.c"
        "${_FT_ROOT}/src/autofit/autofit.c"
        "${_FT_ROOT}/src/sfnt/sfnt.c"
        "${_FT_ROOT}/src/truetype/truetype.c"
        "${_FT_ROOT}/src/cff/cff.c"
        "${_FT_ROOT}/src/psaux/psaux.c"
        "${_FT_ROOT}/src/pshinter/pshinter.c"
        "${_FT_ROOT}/src/psnames/psnames.c"
        "${_FT_ROOT}/src/smooth/smooth.c"
    )
    target_compile_definitions(guikit_freetype PRIVATE FT2_BUILD_LIBRARY)
    if (MSVC)
        target_compile_definitions(guikit_freetype PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
        target_compile_options(guikit_freetype PRIVATE /W0)
    else()
        set_target_properties(guikit_freetype PROPERTIES C_VISIBILITY_PRESET hidden)
        target_compile_options(guikit_freetype PRIVATE -w)
    endif()
    # Order matters: generated ftoption.h + minimal ftmodule.h override upstream defaults without modifying submodule
    target_include_directories(guikit_freetype
        PUBLIC
            $<BUILD_INTERFACE:${_FT_GEN_INC}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/config>
            $<BUILD_INTERFACE:${_FT_ROOT}/include>
    )
endfunction()

set(GUIKIT_TEXT_BUNDLED_FREETYPE OFF)
set(GUIKIT_TEXT_BUNDLED_KEEL_FONT OFF)
set(GUIKIT_TEXT_FREETYPE_ENABLED OFF)
if (GUIKIT_TEXT_USE_FREETYPE)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftbase.c")
        set(GUIKIT_TEXT_BUNDLED_FREETYPE ON)
        set(GUIKIT_TEXT_FREETYPE_ENABLED ON)
        _guikit_text_add_bundled_freetype()
    else()
        find_package(Freetype QUIET)
        if (Freetype_FOUND)
            set(GUIKIT_TEXT_FREETYPE_ENABLED ON)
        else()
            message(STATUS "GuiKit Text: FreeType not found; falling back to keel::font C++20 rasterizer")
        endif()
    endif()
endif()

if (GUIKIT_TEXT_FREETYPE_ENABLED)
    message(STATUS "GuiKit Text: using optional FreeType backend")
    add_library(guikit_text STATIC
        src/guikit/backend/freetype_text_engine.cpp
    )
    add_library(guikit::text ALIAS guikit_text)
    add_library(guikit::text_freetype ALIAS guikit_text)
    set_target_properties(guikit_text PROPERTIES EXPORT_NAME text)

    target_compile_features(guikit_text PUBLIC cxx_std_20)
    target_include_directories(guikit_text PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(guikit_text PUBLIC
        GUIKIT_HAS_TEXT_ENGINE=1
        GUIKIT_HAS_FREETYPE=1
    )
    if (GUIKIT_TEXT_BUNDLED_FREETYPE)
        target_link_libraries(guikit_text PUBLIC guikit::core PRIVATE guikit_freetype)
    else()
        target_link_libraries(guikit_text PUBLIC guikit::core PRIVATE Freetype::Freetype)
    endif()
else()
    message(STATUS "GuiKit Text: using keel::font zero-dependency C++20 rasterizer")
    if (NOT TARGET keel::font)
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/font/CMakeLists.txt")
            set(GUIKIT_TEXT_BUNDLED_KEEL_FONT ON)
            set(KEEL_FONT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            add_subdirectory(third_party/font EXCLUDE_FROM_ALL)
        else()
            find_package(keel_font REQUIRED CONFIG)
        endif()
    endif()

    add_library(guikit_text STATIC
        src/guikit/backend/builtin_text_engine.cpp
    )
    add_library(guikit::text ALIAS guikit_text)
    set_target_properties(guikit_text PROPERTIES EXPORT_NAME text)

    target_compile_features(guikit_text PUBLIC cxx_std_20)
    target_include_directories(guikit_text PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(guikit_text PUBLIC
        GUIKIT_HAS_TEXT_ENGINE=1
        GUIKIT_HAS_FREETYPE=0
    )
    target_link_libraries(guikit_text PUBLIC guikit::core PRIVATE keel::font)
endif()

if (COMMAND guikit_enforce_guidelines)
    guikit_enforce_guidelines(guikit_text
        HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
        STANDALONE_HEADERS ${GUIKIT_TEXT_BUILD_TESTS}
        TEST_NAME guikit_text_header_purity_scan
    )
endif()

if (GUIKIT_TEXT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(_GUIKIT_TEXT_EXPORT_TARGETS guikit_text)
    if (GUIKIT_TEXT_BUNDLED_FREETYPE AND TARGET guikit_freetype)
        list(APPEND _GUIKIT_TEXT_EXPORT_TARGETS guikit_freetype)
    endif()
    if (GUIKIT_TEXT_BUNDLED_KEEL_FONT AND TARGET keel_font)
        list(APPEND _GUIKIT_TEXT_EXPORT_TARGETS keel_font)
    endif()

    install(TARGETS ${_GUIKIT_TEXT_EXPORT_TARGETS}
        EXPORT guikitTextTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/guikit"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(EXPORT guikitTextTargets
        FILE guikitTextTargets.cmake
        NAMESPACE guikit::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_text"
    )
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/guikit_textConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_textConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_text"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_textConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_textConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_textConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_text"
    )
endif()

if (GUIKIT_TEXT_BUILD_TESTS AND TARGET guikit::text)
    add_executable(test_text
        tests/test_text.cpp
    )
    target_link_libraries(test_text PRIVATE guikit::text)
    if (COMMAND guikit_enforce_guidelines)
        guikit_enforce_guidelines(test_text)
    endif()
    add_test(NAME text COMMAND test_text)
endif()
