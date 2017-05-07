# the name of the target operating system
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR "x86_64")
set(TOOLCHAIN "x86_64-w64-mingw32")

# which compilers to use for C and C++
set(CMAKE_C_COMPILER "${TOOLCHAIN}-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN}-g++")
set(CMAKE_RC_COMPILER "${TOOLCHAIN}-windres")

# here is the target environment located
set(CMAKE_FIND_ROOT_PATH "/usr/${TOOLCHAIN}")

# adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search 
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(STATIC_LILV "/opt/${TOOLCHAIN}/lib/liblilv-0.a")
set(STATIC_SRATOM "/opt/${TOOLCHAIN}/lib/libsratom-0.a")
set(STATIC_SERD "/opt/${TOOLCHAIN}/lib/libserd-0.a")
set(STATIC_SORD "/opt/${TOOLCHAIN}/lib/libsord-0.a")
set(LIBS ${LIBS} "-static-libgcc -Wl,-Bstatic -luserenv")

set(WINE wine64)