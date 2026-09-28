# Run with -DPACKAGE_DIR=<staged openglad directory> and
# -DDEPENDENCY_DIR=<MSYS2 mingw64/bin directory> before creating the archive.
cmake_minimum_required(VERSION 3.25)

foreach(_dir IN ITEMS PACKAGE_DIR DEPENDENCY_DIR)
  if(NOT DEFINED ${_dir} OR NOT IS_ABSOLUTE "${${_dir}}" OR
     NOT IS_DIRECTORY "${${_dir}}")
    message(FATAL_ERROR "${_dir} must name an existing absolute directory")
  endif()
endforeach()

foreach(_exe IN ITEMS openglad.exe openscen.exe)
  if(NOT EXISTS "${PACKAGE_DIR}/${_exe}")
    message(FATAL_ERROR "Missing package executable: ${PACKAGE_DIR}/${_exe}")
  endif()
endforeach()

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DEPENDENCY_DIR}/objdump.exe")
if(NOT EXISTS "${CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND}")
  message(FATAL_ERROR "Missing MinGW objdump: ${CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND}")
endif()

# Windows owns these resolved files; exclude their exact paths so CMake does
# not recurse into OS DLLs. A dependency resolved from elsewhere stays bundled.
if("$ENV{SystemRoot}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{SystemRoot}")
  message(FATAL_ERROR "SystemRoot must name the Windows installation directory")
endif()
file(TO_CMAKE_PATH "$ENV{SystemRoot}" _windows_dir)
file(GLOB _system_dlls
  "${_windows_dir}/*.dll"
  "${_windows_dir}/System32/*.dll"
  "${_windows_dir}/SysWOW64/*.dll")

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${PACKAGE_DIR}/openglad.exe" "${PACKAGE_DIR}/openscen.exe"
  DIRECTORIES "${DEPENDENCY_DIR}" "${_windows_dir}/System32"
    "${_windows_dir}"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
  CONFLICTING_DEPENDENCIES_PREFIX _conflicts
  PRE_EXCLUDE_REGEXES "^api-ms-win-" "^ext-ms-win-"
  POST_EXCLUDE_FILES ${_system_dlls})

if(_unresolved)
  message(FATAL_ERROR "Unresolved Windows runtime dependencies: ${_unresolved}")
endif()
if(_conflicts_FILENAMES)
  foreach(_name IN LISTS _conflicts_FILENAMES)
    message(STATUS "Conflicting ${_name}: ${_conflicts_${_name}}")
  endforeach()
  message(FATAL_ERROR "Conflicting Windows runtime dependencies")
endif()

foreach(_dll IN LISTS _resolved)
  get_filename_component(_name "${_dll}" NAME)
  file(COPY_FILE "${_dll}" "${PACKAGE_DIR}/${_name}"
    ONLY_IF_DIFFERENT RESULT _copy_result)
  if(NOT _copy_result STREQUAL "0")
    message(FATAL_ERROR "Could not bundle ${_dll}: ${_copy_result}")
  endif()
  message(STATUS "Bundled ${_name}")
endforeach()
