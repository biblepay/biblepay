# Sample toolchain file for building for Windows from an Ubuntu Linux system.
#
# Typical usage:
#    *) install cross compiler: `sudo apt-get install mingw-w64`
#    *) mkdir buildMingw64 && cd buildMingw64
#    *) cmake -DCMAKE_TOOLCHAIN_FILE=~/Toolchain-Ubuntu-mingw64.cmake ..
#
#project(randomx C CXX)
#set(CMAKE_MAKE_PROGRAM randomx)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_C_COMPILER_ENV_VAR C)
set(CMAKE_CXX_COMPILER_ENV_VAR CXX)
#project(randomx C CXX)
#find_package(Threads REQUIRED)
#set(CMAKE_MAKE_PROGRAM randomx)
#project(RandomX C CXX)
#find_package(Threads REQUIRED)

set(BOOST_INCLUDE_DIR /BiblepayDevelop-Evolution/depends/x86_64-w64-mingw32/include/boost)
set(BOOST_LIBRARYDIR /BiblepayDevelop-Evolution/depends/x86_64-w64-mingw32/lib)

#set(Boost_USE_STATIC_LIBS      ON)
set(Boost_NO_SYSTEM_PATHS      OFF)
#find_package(Boost 1.65 COMPONENTS program_options REQUIRED)
#add_executable(anyExecutable ../src/randomx.cpp)
#target_link_libraries(anyExecutable Boost::program_options)
#set(CMAKE_C_STANDARD 99)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
#target_link_libraries(randomx pthread)
#find_package(Threads REQUIRED)
#target_link_libraries(my_app Threads::Threads)
set(CMAKE_SYSTEM_NAME Windows)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)
# target environment on the build host system
#>set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
# modify default behavior of FIND_XXX() commands to
# search for headers/libs in the target environment and
# search for programs in the build host environment
#set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
#set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
#set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
#set(GCC_COVERAGE_COMPILE_FLAGS "-masm=intel")

