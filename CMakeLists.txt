cmake_minimum_required(VERSION 3.20...4.0)

project(PlusLib VERSION 2.9.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(PlusLibMacros)
include(PlusLibOptions)
include(PlusGitInfo)

# The historical spellings, kept because PlusApp, the resource templates and
# the generated package configuration all read them.
set(PLUSLIB_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(PLUSLIB_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(PLUSLIB_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(PLUSLIB_VERSION ${PROJECT_VERSION})

message(STATUS "Configuring the Plus toolkit, version ${PLUSLIB_VERSION}")

#-----------------------------------------------------------------------------
# C++ standard
#-----------------------------------------------------------------------------
set(_minimum_cxx_standard 17)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD ${_minimum_cxx_standard} CACHE STRING "C++ standard")
endif()
if(CMAKE_CXX_STANDARD LESS _minimum_cxx_standard)
  message(FATAL_ERROR "CMAKE_CXX_STANDARD must be ${_minimum_cxx_standard} or newer; VTK 9 and ITK 5 require it.")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#-----------------------------------------------------------------------------
# Generator and platform
#-----------------------------------------------------------------------------
# Whether the generator picks its configuration at build time (Visual Studio,
# Xcode, Ninja Multi-Config) or at configure time (Makefiles, Ninja). Nearly
# everywhere this was previously written as "MSVC or the generator is Xcode",
# which is a parse error with a generator whose name contains a space, and
# which also gets Ninja on Windows wrong in both directions.
get_property(PLUS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(PLUS_MULTI_CONFIG)
  # Usable anywhere a generator expression is allowed.
  set(PLUS_CONFIG_SUBDIR "$<CONFIG>/")
  # Usable at configure time, when the whole set has to be iterated.
  set(PLUS_CONFIG_DIRS ${CMAKE_CONFIGURATION_TYPES})
else()
  set(PLUS_CONFIG_SUBDIR "")
  set(PLUS_CONFIG_DIRS "")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake Targets")
set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER "AutoGen (Qt) Targets")

if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_platform "Win64")
  else()
    set(_platform "Win32")
  endif()
elseif(APPLE)
  set(_platform "OSX")
else()
  set(_platform "Linux")
endif()
set(PLUSLIB_PLATFORM "${_platform}" CACHE STRING "Name of the platform this instance of PlusLib is built on")
message(STATUS "Detected platform: ${PLUSLIB_PLATFORM}")

if(WIN32)
  # Windows 8 and newer, consumed by PlusConfigure.h.
  set(RUNTIME_MINIMUM_WINDOWS_VERSION "0x0602")
endif()

#-----------------------------------------------------------------------------
# Run-time library search path
#
# Keeps the installed tree relocatable: executables look beside themselves and
# in the sibling lib directory instead of at absolute build paths.
#-----------------------------------------------------------------------------
if(APPLE)
  set(CMAKE_MACOSX_RPATH ON)
  set(CMAKE_INSTALL_RPATH "@executable_path/../lib;@loader_path/../lib;@loader_path")
elseif(UNIX)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN")
  # DT_RPATH rather than DT_RUNPATH, because only the loading executable's
  # DT_RPATH is consulted when resolving a library's own dependencies.
  add_link_options("$<$<CXX_COMPILER_ID:GNU,Clang>:LINKER:--disable-new-dtags>")
endif()
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)

#-----------------------------------------------------------------------------
# Install layout, relative to CMAKE_INSTALL_PREFIX
#-----------------------------------------------------------------------------
set(PLUSLIB_INSTALL_FOLDERNAME "PlusLib-${PLUSLIB_VERSION_MAJOR}.${PLUSLIB_VERSION_MINOR}")
set(PLUSLIB_BINARY_INSTALL "bin")
set(PLUSLIB_INCLUDE_INSTALL "include/${PLUSLIB_INSTALL_FOLDERNAME}")
set(PLUSLIB_LIBRARY_INSTALL "lib")
set(PLUSLIB_ARCHIVE_INSTALL "lib")
set(PLUSLIB_DATA_INSTALL "data")
set(PLUSLIB_CMAKE_INSTALL "lib/cmake/${PLUSLIB_INSTALL_FOLDERNAME}")
set(PLUSLIB_SHARE_INSTALL "share/${PLUSLIB_INSTALL_FOLDERNAME}")

#-----------------------------------------------------------------------------
# Testing
#-----------------------------------------------------------------------------
option(BUILD_TESTING "Build test programs" ON)
include(CTest)

if(BUILD_TESTING)
  foreach(_mode Experimental Nightly Continuous NightlyMemoryCheck)
    if(TARGET ${_mode})
      set_property(TARGET ${_mode} PROPERTY FOLDER "CTest Targets")
    endif()
  endforeach()
endif()

#-----------------------------------------------------------------------------
# Repository revision
#-----------------------------------------------------------------------------
plus_git_info(PLUSLIB)

#-----------------------------------------------------------------------------
# Developer setup
#-----------------------------------------------------------------------------
if(Git_FOUND)
  get_filename_component(_git_directory "${GIT_EXECUTABLE}" DIRECTORY)
endif()
set(_x86env "ProgramFiles(x86)")
find_program(BASH_EXECUTABLE bash
  HINTS
    "${_git_directory}"
    "$ENV{ProgramFiles}/Git/bin"
    "$ENV{${_x86env}}/Git/bin"
  )
mark_as_advanced(BASH_EXECUTABLE)
# Guarded on .git: bash is always present on Linux and macOS, so without this
# the hook installer ran on every configure, including from a source archive
# that has no repository to install hooks into.
if(BASH_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${BASH_EXECUTABLE}" -c "cd '${CMAKE_SOURCE_DIR}' && ./Utilities/SetupForDevelopment.sh copyOnly"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _bash_output
    ERROR_VARIABLE _bash_output
    RESULT_VARIABLE _bash_result
    )
  if(NOT _bash_result EQUAL 0)
    message(STATUS "Could not install the development hooks: ${_bash_output}")
  endif()
endif()

#-----------------------------------------------------------------------------
# Bundled vendor SDKs under Tools/
#
# These are not FORCE cache entries any more. Forcing them meant a value the
# user set was overwritten on every configure, so the paths could not be
# pointed at a different copy of an SDK.
#-----------------------------------------------------------------------------
set(PLUSLIB_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tools")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_sdk_arch_x64 TRUE)
else()
  set(_sdk_arch_x64 FALSE)
endif()

set(ATC_TRAKSTAR_BASE_DIR "${PLUSLIB_TOOLS_DIR}/Ascension/trakSTAR_940041_RevE")
set(ATC_TRAKSTAR_INCLUDE_DIR "${ATC_TRAKSTAR_BASE_DIR}/inc" CACHE PATH "Path to the Ascension trakSTAR 3D Guidance include folder (where the ATC3DG.h file is located)")
set(ATC_TRAKSTAR_LIB_DIR "${ATC_TRAKSTAR_BASE_DIR}/lib" CACHE PATH "Path to the Ascension trakSTAR 3D Guidance lib folder")
set(ATC_TRAKSTAR_BINARY_DIR "${ATC_TRAKSTAR_BASE_DIR}/bin" CACHE PATH "Path to the Ascension trakSTAR 3D Guidance dll folder")
set(ATC_MEDSAFE_BASE_DIR "${PLUSLIB_TOOLS_DIR}/Ascension/medSAFE_940033_Rev_F")
set(ATC_MEDSAFE_INCLUDE_DIR "${ATC_MEDSAFE_BASE_DIR}/inc" CACHE PATH "Path to the Ascension medSAFE 3D Guidance include folder (where the ATC3DGm.h file is located)")
set(ATC_MEDSAFE_LIB_DIR "${ATC_MEDSAFE_BASE_DIR}/lib" CACHE PATH "Path to the Ascension medSAFE 3D Guidance lib folder")
set(ATC_MEDSAFE_BINARY_DIR "${ATC_MEDSAFE_BASE_DIR}/bin" CACHE PATH "Path to the Ascension medSAFE 3D Guidance dll folder")
mark_as_advanced(
  ATC_TRAKSTAR_INCLUDE_DIR ATC_TRAKSTAR_LIB_DIR ATC_TRAKSTAR_BINARY_DIR
  ATC_MEDSAFE_INCLUDE_DIR ATC_MEDSAFE_LIB_DIR ATC_MEDSAFE_BINARY_DIR
  )
if(PLUS_USE_Ascension3DG)
  add_subdirectory("${ATC_TRAKSTAR_BASE_DIR}")
endif()
if(PLUS_USE_Ascension3DGm)
  add_subdirectory("${ATC_MEDSAFE_BASE_DIR}")
endif()

set(PHIDGET_SPATIAL_BASE_DIR "${PLUSLIB_TOOLS_DIR}/Phidget/Phidget22")
set(PHIDGET_SPATIAL_INCLUDE_DIR "${PHIDGET_SPATIAL_BASE_DIR}" CACHE PATH "Path to the Phidget Spatial include folder")
if(_sdk_arch_x64)
  set(_phidget_arch_dir "${PHIDGET_SPATIAL_BASE_DIR}/x64")
else()
  set(_phidget_arch_dir "${PHIDGET_SPATIAL_BASE_DIR}/x86")
endif()
set(PHIDGET_SPATIAL_LIB_DIR "${_phidget_arch_dir}" CACHE PATH "Path to the Phidget Spatial lib folder")
set(PHIDGET_SPATIAL_BINARY_DIR "${_phidget_arch_dir}" CACHE PATH "Path to the Phidget Spatial dll folder")
mark_as_advanced(PHIDGET_SPATIAL_INCLUDE_DIR PHIDGET_SPATIAL_LIB_DIR PHIDGET_SPATIAL_BINARY_DIR)
if(PLUS_USE_PHIDGET_SPATIAL_TRACKER)
  add_subdirectory("${PHIDGET_SPATIAL_BASE_DIR}")
endif()

set(EPIPHAN_SDK_BASE_DIR "${PLUSLIB_TOOLS_DIR}/Epiphan")
set(EPIPHAN_INCLUDE_DIR "${EPIPHAN_SDK_BASE_DIR}" CACHE PATH "Path to the Epiphan SDK include directory.")
if(_sdk_arch_x64)
  set(_epiphan_arch_dir "${EPIPHAN_SDK_BASE_DIR}/x64")
else()
  set(_epiphan_arch_dir "${EPIPHAN_SDK_BASE_DIR}/Win32")
endif()
set(EPIPHAN_BINARY_DIR "${_epiphan_arch_dir}" CACHE PATH "Directory containing Epiphan shared library file.")
set(EPIPHAN_LIB_DIR "${_epiphan_arch_dir}" CACHE PATH "Directory containing Epiphan static library file.")
mark_as_advanced(EPIPHAN_INCLUDE_DIR EPIPHAN_BINARY_DIR EPIPHAN_LIB_DIR)
if(PLUS_USE_EPIPHAN)
  add_subdirectory("${EPIPHAN_SDK_BASE_DIR}")
endif()

set(USDIGITAL_SEI_BASE_DIR "${PLUSLIB_TOOLS_DIR}/UsDigital/SEI_5.22")
set(USDIGITAL_SEI_INCLUDE_DIR "${USDIGITAL_SEI_BASE_DIR}/inc" CACHE PATH "Path to the US Digital SEI include directory (needed for the CIVCO stepper).")
set(USDIGITAL_SEI_BINARY_DIR "${USDIGITAL_SEI_BASE_DIR}/bin" CACHE PATH "Path to the US Digital SEI binary directory (needed for the CIVCO stepper).")
set(USDIGITAL_SEI_LIB_DIR "${USDIGITAL_SEI_BASE_DIR}/lib" CACHE PATH "Path to the US Digital SEI library directory (needed for the CIVCO stepper).")
mark_as_advanced(USDIGITAL_SEI_INCLUDE_DIR USDIGITAL_SEI_BINARY_DIR USDIGITAL_SEI_LIB_DIR)
if(PLUS_USE_BRACHY_TRACKER)
  add_subdirectory("${USDIGITAL_SEI_BASE_DIR}")
endif()

if(WIN32)
  set(NVIDIA_DVP_BASE_DIR "${PLUSLIB_TOOLS_DIR}/NVidia/dvp170")
  set(NVIDIA_DVP_INCLUDE_DIR "${NVIDIA_DVP_BASE_DIR}/include" CACHE PATH "Path to the NVidia DVP include directory.")
  if(_sdk_arch_x64)
    set(_nvidia_arch x64)
  else()
    set(_nvidia_arch win32)
  endif()
  set(NVIDIA_DVP_BINARY_DIR "${NVIDIA_DVP_BASE_DIR}/bin/${_nvidia_arch}" CACHE PATH "Directory containing NVidia DVP shared library file.")
  set(NVIDIA_DVP_LIB_DIR "${NVIDIA_DVP_BASE_DIR}/lib/${_nvidia_arch}" CACHE PATH "Directory containing NVidia DVP static library file.")
  mark_as_advanced(NVIDIA_DVP_INCLUDE_DIR NVIDIA_DVP_BINARY_DIR NVIDIA_DVP_LIB_DIR)
  if(PLUS_USE_NVIDIA_DVP)
    add_subdirectory("${NVIDIA_DVP_BASE_DIR}")
  endif()
endif()

#-----------------------------------------------------------------------------
# The NDI tracker compiles a threaded code path when <future> is available.
# C++17 guarantees it, so the try_compile that used to decide this is gone.
set(HAVE_FUTURE ON)

add_subdirectory(src)

#-----------------------------------------------------------------------------
# Convenience scripts for an automatic build and test cycle
#-----------------------------------------------------------------------------
if(PLUS_MULTI_CONFIG)
  set(PLUS_BUILD_CONFIG_ARG "--config Release")
  set(PLUS_CTEST_CONFIG_ARG "-C Release")
else()
  # A single-configuration build has one configuration, chosen at configure
  # time, so naming one here would at best be redundant and at worst wrong.
  set(PLUS_BUILD_CONFIG_ARG "")
  set(PLUS_CTEST_CONFIG_ARG "")
endif()

if(WIN32)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildAndTest.bat.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BuildAndTest.bat"
    )
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/CreateApiReference.bat.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CreateApiReference.bat"
    )
else()
  # Configured into a scratch directory first, then copied so that the execute
  # bit can be set.
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildAndTest.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_FILES_DIRECTORY}/BuildAndTest.sh"
    @ONLY
    )
  file(COPY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_FILES_DIRECTORY}/BuildAndTest.sh"
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()
