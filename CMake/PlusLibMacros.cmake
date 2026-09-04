# plus_copy_runtime_files(FILES <file>... [DEBUG_FILES <file>...])
#
# Copy vendor libraries next to the executables so that the build tree can be
# run without those libraries on PATH. On a multi-configuration generator that
# means one copy per configuration directory; on a single-configuration one, a
# single flat copy.
#
# Files listed under DEBUG_FILES go only to the Debug directory and take
# precedence there over a release file of the same name, which would otherwise
# overwrite them.
#
# The copies happen at configure time on purpose: the libraries have to be in
# place before the first build, and before any test runs.
function(plus_copy_runtime_files)
  cmake_parse_arguments(PARSE_ARGV 0 _copy "" "" "FILES;DEBUG_FILES")

  if(NOT PLUS_MULTI_CONFIG)
    if(_copy_FILES)
      file(COPY ${_copy_FILES} DESTINATION "${PLUS_EXECUTABLE_OUTPUT_PATH}")
    endif()
    return()
  endif()

  # Release files whose name is also provided as a debug file must not be
  # copied into the Debug directory.
  set(_debug_names)
  foreach(_file IN LISTS _copy_DEBUG_FILES)
    get_filename_component(_name "${_file}" NAME)
    list(APPEND _debug_names "${_name}")
  endforeach()

  set(_release_only)
  foreach(_file IN LISTS _copy_FILES)
    get_filename_component(_name "${_file}" NAME)
    if(NOT _name IN_LIST _debug_names)
      list(APPEND _release_only "${_file}")
    endif()
  endforeach()

  foreach(_config IN LISTS PLUS_CONFIG_DIRS)
    if(_config STREQUAL "Debug")
      if(_release_only)
        file(COPY ${_release_only} DESTINATION "${PLUS_EXECUTABLE_OUTPUT_PATH}/${_config}")
      endif()
      if(_copy_DEBUG_FILES)
        file(COPY ${_copy_DEBUG_FILES} DESTINATION "${PLUS_EXECUTABLE_OUTPUT_PATH}/${_config}")
      endif()
    elseif(_copy_FILES)
      file(COPY ${_copy_FILES} DESTINATION "${PLUS_EXECUTABLE_OUTPUT_PATH}/${_config}")
    endif()
  endforeach()
endfunction()

MACRO(PlusLibInstallLibrary _target_name _variable_root)
  IF(CMAKE_CONFIGURATION_TYPES)
    SET(CONFIG ${CMAKE_CONFIGURATION_TYPES})
  ELSEIF(CMAKE_BUILD_TYPE)
    SET(CONFIG ${CMAKE_BUILD_TYPE})
  ELSE()
    SET(CONFIG Release)
  ENDIF()
  INSTALL(TARGETS ${_target_name} EXPORT PlusLib
    RUNTIME DESTINATION "${PLUSLIB_BINARY_INSTALL}" CONFIGURATIONS ${CONFIG} COMPONENT RuntimeLibraries
    LIBRARY DESTINATION "${PLUSLIB_LIBRARY_INSTALL}" CONFIGURATIONS ${CONFIG} COMPONENT RuntimeLibraries
    ARCHIVE DESTINATION "${PLUSLIB_ARCHIVE_INSTALL}" CONFIGURATIONS ${CONFIG} COMPONENT Development
    )
  INSTALL(FILES ${${_variable_root}_HDRS}
    DESTINATION "${PLUSLIB_INCLUDE_INSTALL}" COMPONENT Development
    )
  GET_TARGET_PROPERTY(_library_type ${_target_name} TYPE)
  IF(${_library_type} STREQUAL SHARED_LIBRARY AND MSVC)
    INSTALL(FILES "$<TARGET_PDB_FILE:${_target_name}>" OPTIONAL
      DESTINATION "${PLUSLIB_BINARY_INSTALL}" COMPONENT RuntimeLibraries
      )
  ENDIF()
ENDMACRO()

MACRO(PlusLibAddVersionInfo target_name description internal_name product_name)
  IF(MSVC)
    IF(NOT TARGET ${target_name})
      MESSAGE(FATAL_ERROR PlusLibAddVersionInfo called but target parameter does not exist)
    ENDIF()

    # Configure file does not see these variables unless we re-set them locally
    set(target_name ${target_name})
    set(description ${description})
    set(internal_name ${internal_name})
    set(product_name ${product_name})

    CONFIGURE_FILE(
      ${CMAKE_SOURCE_DIR}/CMake/MSVCVersion.rc.in 
      ${CMAKE_CURRENT_BINARY_DIR}/${target_name}MSVCVersion.rc
      )

    GET_TARGET_PROPERTY(_target_type ${target_name} TYPE)
    IF(${_target_type} STREQUAL "EXECUTABLE")
      TARGET_SOURCES(${target_name} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/${target_name}MSVCVersion.rc)
    ELSE()
      TARGET_SOURCES(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${target_name}MSVCVersion.rc)
    ENDIF()

    # If this macro was called from the target they're currently configuring
    IF(${PROJECT_NAME} STREQUAL ${target_name} OR vtk${PROJECT_NAME} STREQUAL ${target_name})
      SOURCE_GROUP(Resources FILES ${CMAKE_CURRENT_BINARY_DIR}/${target_name}MSVCVersion.rc)
    ENDIF()
  ENDIF()
ENDMACRO()