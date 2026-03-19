# Set VCPKG_ROOT to your vcpkg installation directory or add the following to your cmake call:
# -DCMAKE_TOOLCHAIN=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake

cmake_minimum_required(VERSION 3.25)
set(CMAKE_VERBOSE_MAKEFILE ON)

# if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
#   set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")  
#   message(STATUS "Using vcpkg toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

#   if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
#     set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
#     message(STATUS "Using vcpkg target triplet: ${VCPKG_TARGET_TRIPLET}")
#   endif()
# endif()

project(cpp-async LANGUAGES CXX)

option(ENABLE_SANITIZERS "Enable sanitizers" ON)

include(cmake/Sanitizers.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

SET(OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}/bin")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}/bin")
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}/lib")
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}/lib")
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIRECTORY}/lib")
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}/lib")

########################################################################
# Dependencies

Include(FetchContent)

# Catch2
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.13.0 # or a later release
)
FetchContent_MakeAvailable(Catch2)
# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# stdexec
set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
  GIT_TAG        5076be2b35de2e78330201b888d82c81b8cb428b
)
FetchContent_MakeAvailable(stdexec)
# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE STDEXEC::stdexec)

# capy
set(BOOST_CAPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BOOST_CAPY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BOOST_CAPY_BUILD_BENCH OFF CACHE BOOL "" FORCE)

FetchContent_Declare(capy
    GIT_REPOSITORY https://github.com/cppalliance/capy.git
    GIT_TAG develop
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(capy)

# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Boost::capy)

# # if unix and not apple, then add liburing
# if(UNIX AND NOT APPLE)
#   # liburing
#   find_package(PkgConfig)
#   pkg_check_modules(liburing REQUIRED IMPORTED_TARGET GLOBAL liburing>=2.0)
#   # later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE PkgConfig::liburing)
# endif()

enable_testing()

add_subdirectory(senders-receivers)
add_subdirectory(coroutines)
add_subdirectory(coroutines-capy)
