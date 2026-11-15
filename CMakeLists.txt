cmake_minimum_required(VERSION 3.16)
project(PressPNG VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PRESSPNG_WERROR "Treat warnings as errors" OFF)
option(PRESSPNG_QT_GUI "Build the Qt 6 GUI" ON)
option(PRESSPNG_WIN32_GUI "Build the legacy Win32 GUI" ON)

# ---------------------------------------------------------------- core
set(PRESSPNG_CORE_SOURCES
    src/core/image.cpp
    src/core/image_loader.cpp
    src/core/codecs.cpp
    src/core/codec_factory.cpp
    src/core/target_size_solver.cpp
    src/core/preview.cpp
    src/core/transform.cpp
    src/core/qoi.cpp
    src/core/compressor.cpp
    src/core/format_bytes.cpp
    src/core/stb_impl.cpp
)

# Vendored code is built without the project's warning flags.
add_library(presspng_thirdparty STATIC third_party/lodepng.cpp)
target_include_directories(presspng_thirdparty SYSTEM
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_library(presspng_core STATIC ${PRESSPNG_CORE_SOURCES})
target_include_directories(presspng_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(presspng_core PUBLIC presspng_thirdparty)

if(MSVC)
    target_compile_options(presspng_core PRIVATE /W4
        $<$<BOOL:${PRESSPNG_WERROR}>:/WX>)
else()
    target_compile_options(presspng_core PRIVATE -Wall -Wextra -Wpedantic
        $<$<BOOL:${PRESSPNG_WERROR}>:-Werror>)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # stb's JPEG writer performs a benign left shift of negative values;
    # it is not worth patching vendored code over, so exclude it from
    # UBSan shift checks (stb is only ever used through this unit).
    set_source_files_properties(src/core/stb_impl.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-sanitize=shift")
endif()

# ---------------------------------------------------------------- cli
add_executable(presspng-cli tools/cli.cpp)
target_link_libraries(presspng-cli PRIVATE presspng_core)

# ---------------------------------------------------------------- tests
enable_testing()
add_executable(presspng-tests tests/test_core.cpp)
target_link_libraries(presspng-tests PRIVATE presspng_core)
add_test(NAME core COMMAND presspng-tests)

# ---------------------------------------------------------------- gui
# Qt is the primary front-end. It is optional so the core still builds on
# machines without Qt; the Win32 GUI remains available as a fallback.
if(PRESSPNG_QT_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt6 Widgets not found - disabling PRESSPNG_QT_GUI")
        set(PRESSPNG_QT_GUI OFF)
    endif()
endif()

if(PRESSPNG_QT_GUI)
    set(PRESSPNG_QT_SOURCES
        src/gui/qt/main.cpp
        src/gui/qt/main_window.cpp
        src/gui/qt/preview_view.cpp
        src/gui/qt/worker.cpp
        src/gui/qt/theme.cpp
        src/gui/qt/icons.cpp
        src/gui/qt/image_utils.cpp
        src/gui/qt/gui_types.cpp
        src/gui/qt/qt_settings_store.cpp
    )

    qt_standard_project_setup()
    qt_add_executable(PressPNG WIN32 ${PRESSPNG_QT_SOURCES})
    target_include_directories(PressPNG
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/qt)
    target_link_libraries(PressPNG PRIVATE presspng_core Qt6::Widgets)
    if(MSVC)
        target_compile_options(PressPNG PRIVATE /W4)
    else()
        target_compile_options(PressPNG PRIVATE -Wall -Wextra)
    endif()
endif()

if(WIN32 AND PRESSPNG_WIN32_GUI)
    add_executable(PressPNG-win32 WIN32
        src/gui/win_main.cpp src/gui/registry_settings_store.cpp)
    target_link_libraries(PressPNG-win32 PRIVATE
        presspng_core comdlg32 shell32 comctl32)
    target_compile_definitions(PressPNG-win32 PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MINGW)
        target_link_options(PressPNG-win32 PRIVATE -static -municode)
    endif()
endif()

if(MINGW)
    target_link_options(presspng-cli PRIVATE -static)
endif()
