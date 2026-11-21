cmake_minimum_required(VERSION 3.21)
project(guikit_text VERSION 1.0.0 LANGUAGES CXX C)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(GUIKIT_TEXT_USE_FREETYPE "Build FreeType hinted glyph rasterizer backend" ON)
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
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(FT_DISABLE_ZLIB ON)
    set(FT_DISABLE_BZIP2 ON)
    set(FT_DISABLE_PNG ON)
    set(FT_DISABLE_HARFBUZZ ON)
    set(FT_DISABLE_BROTLI ON)
    set(BUILD_SHARED_LIBS OFF)
    set(SKIP_INSTALL_ALL ON)
    if (GUIKIT_TEXT_INSTALL)
        add_subdirectory(third_party/freetype)
    else()
        add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)
    endif()
endfunction()

set(GUIKIT_TEXT_BUNDLED_FREETYPE OFF)
set(GUIKIT_TEXT_FREETYPE_ENABLED OFF)
if (GUIKIT_TEXT_USE_FREETYPE)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/CMakeLists.txt")
        set(GUIKIT_TEXT_BUNDLED_FREETYPE ON)
        set(GUIKIT_TEXT_FREETYPE_ENABLED ON)
        _guikit_text_add_bundled_freetype()
    else()
        find_package(Freetype QUIET)
        if (Freetype_FOUND)
            set(GUIKIT_TEXT_FREETYPE_ENABLED ON)
        else()
            message(STATUS "GuiKit Text: FreeType not found; guikit::text FreeType backend disabled")
        endif()
    endif()
endif()

if (GUIKIT_TEXT_FREETYPE_ENABLED)
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
    target_compile_definitions(guikit_text PUBLIC GUIKIT_HAS_FREETYPE=1)
    if (GUIKIT_TEXT_BUNDLED_FREETYPE)
        target_link_libraries(guikit_text PUBLIC guikit::core PRIVATE freetype)
    else()
        target_link_libraries(guikit_text PUBLIC guikit::core PRIVATE Freetype::Freetype)
    endif()
else()
    add_library(guikit_text INTERFACE)
    add_library(guikit::text ALIAS guikit_text)
    set_target_properties(guikit_text PROPERTIES EXPORT_NAME text)
    target_include_directories(guikit_text INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(guikit_text INTERFACE GUIKIT_HAS_FREETYPE=0)
    target_link_libraries(guikit_text INTERFACE guikit::core)
endif()

if (COMMAND guikit_enforce_guidelines AND GUIKIT_TEXT_FREETYPE_ENABLED)
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
    if (GUIKIT_TEXT_BUNDLED_FREETYPE AND TARGET freetype)
        list(APPEND _GUIKIT_TEXT_EXPORT_TARGETS freetype)
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
