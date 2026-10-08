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
target_compile_features(gamepilot PRIVATE cxx_std_23)
set_target_properties(gamepilot PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON
    CXX_SCAN_FOR_MODULES OFF
)
target_include_directories(gamepilot PRIVATE src resources)
target_compile_definitions(gamepilot PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_link_libraries(gamepilot PRIVATE user32 gdi32)

if(MSVC)
    target_compile_options(gamepilot PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
else()
    target_compile_options(gamepilot PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
    # Unicode entry point; ship one executable without toolchain runtime DLLs.
    target_link_options(gamepilot PRIVATE -municode -static)
endif()

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME win32_smoke COMMAND gamepilot --smoke-test)
    set_tests_properties(win32_smoke PROPERTIES TIMEOUT 30)
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    set(GAMEPILOT_CPP_FILES
        "${PROJECT_SOURCE_DIR}/src/main.cpp"
        "${PROJECT_SOURCE_DIR}/src/win32/application.cpp"
        "${PROJECT_SOURCE_DIR}/src/win32/application.hpp"
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
