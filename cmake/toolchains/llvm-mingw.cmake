if(NOT DEFINED ENV{GAMEPILOT_LLVM_ROOT})
    message(FATAL_ERROR "Activate scripts/enter-dev-shell.ps1 or use scripts/build.ps1 first.")
endif()

file(TO_CMAKE_PATH "$ENV{GAMEPILOT_LLVM_ROOT}" LLVM_MINGW_ROOT)
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++.exe")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/llvm-windres.exe")
