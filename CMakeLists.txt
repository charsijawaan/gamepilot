cmake_minimum_required(VERSION 3.28)

project(GamePilot VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "GamePilot targets Windows.")
endif()

add_executable(gamepilot WIN32
    src/main.cpp
    src/win32/application.cpp
    resources/gamepilot.rc
)
add_library(gamepilot-core STATIC src/core/gaming-mode.cpp)
add_library(gamepilot-platform STATIC src/win32/power-settings.cpp)
target_link_libraries(gamepilot-platform PUBLIC gamepilot-core PRIVATE powrprof ole32 shell32 uuid)

function(gamepilot_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
        CXX_SCAN_FOR_MODULES OFF
    )
    target_include_directories(${target} PUBLIC src PRIVATE resources)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
        )
        target_link_options(${target} PRIVATE -static)
    endif()
endfunction()

gamepilot_target(gamepilot)
gamepilot_target(gamepilot-core)
gamepilot_target(gamepilot-platform)
target_link_libraries(gamepilot PRIVATE gamepilot-platform user32 gdi32 dwmapi)
if(NOT MSVC)
    target_link_options(gamepilot PRIVATE -municode)
endif()

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(gamepilot-tests tests/gaming-mode-tests.cpp)
    gamepilot_target(gamepilot-tests)
    target_link_libraries(gamepilot-tests PRIVATE gamepilot-platform)
    add_test(NAME gaming_mode COMMAND gamepilot-tests)
    add_test(NAME win32_smoke COMMAND gamepilot --smoke-test)
    set_tests_properties(win32_smoke gaming_mode PROPERTIES TIMEOUT 30)
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    set(GAMEPILOT_CPP_FILES
        "${PROJECT_SOURCE_DIR}/src/main.cpp"
        "${PROJECT_SOURCE_DIR}/src/win32/application.cpp"
        "${PROJECT_SOURCE_DIR}/src/win32/application.hpp"
        "${PROJECT_SOURCE_DIR}/src/core/gaming-mode.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/gaming-mode.hpp"
        "${PROJECT_SOURCE_DIR}/src/win32/power-settings.cpp"
        "${PROJECT_SOURCE_DIR}/src/win32/power-settings.hpp"
        "${PROJECT_SOURCE_DIR}/tests/gaming-mode-tests.cpp"
    )
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i ${GAMEPILOT_CPP_FILES}
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${GAMEPILOT_CPP_FILES}
        VERBATIM
    )
endif()
