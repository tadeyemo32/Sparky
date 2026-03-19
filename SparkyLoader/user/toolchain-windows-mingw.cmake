# toolchain-windows-mingw.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# The toolchain prefix
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)

set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

# Where to search for headers & libs (important!)
set(CMAKE_FIND_ROOT_PATH
    /usr/local/opt/mingw-w64/x86_64-w64-mingw32
    /opt/homebrew/opt/mingw-w64/x86_64-w64-mingw32   # Apple Silicon path
)

# Adjust search behavior
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Windows-specific defines
add_definitions(-DWIN32 -D_WIN32 -DWIN64 -D_WIN64)
