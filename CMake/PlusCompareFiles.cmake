# Compares a file written by a test against its checked-in baseline.
#
# Usage:
#   cmake -DTEST_FILE=<path> -DBASELINE_FILE=<path> -P PlusCompareFiles.cmake
#
# This is "cmake -E compare_files" with one allowance. A compressed NRRD
# sequence file is a text header followed by a gzip stream, and RFC 1952 gives
# that stream a ten-byte header holding a modification time, an extra-flags
# byte and an operating-system code. zlib fills the operating-system code in
# from a constant fixed when zlib itself was compiled, so the same image written
# on Windows and on Linux differs in exactly that one byte. Comparing it would
# force a separate baseline for every platform, and for every zlib version, to
# describe data that is otherwise identical.

cmake_minimum_required(VERSION 3.20...4.0)

foreach(_required IN ITEMS TEST_FILE BASELINE_FILE)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "PlusCompareFiles: ${_required} was not set.")
  endif()
  if(NOT EXISTS "${${_required}}")
    message(FATAL_ERROR "PlusCompareFiles: ${_required} does not exist: ${${_required}}")
  endif()
endforeach()

# Blanks the modification time, extra flags and operating-system code of every
# gzip header in a hex-encoded file. Takes the name of the variable holding the
# hex and rewrites it in place. Only headers that start on a byte boundary are
# considered; the same bytes are blanked in both files, so the comparison is
# never made more permissive than those six bytes.
function(_plus_blank_gzip_metadata hex_variable)
  set(_hex "${${hex_variable}}")
  string(LENGTH "${_hex}" _length)
  set(_from 0)
  while(_from LESS _length)
    string(SUBSTRING "${_hex}" ${_from} -1 _tail)
    string(FIND "${_tail}" "1f8b08" _offset)
    if(_offset LESS 0)
      break()
    endif()
    math(EXPR _start "${_from} + ${_offset}")
    math(EXPR _aligned "${_start} % 2")
    # Skip the two magic bytes, the compression method and the flags, then
    # blank the four modification-time bytes, the extra-flags byte and the
    # operating-system code that follow them.
    math(EXPR _metadata_start "${_start} + 8")
    math(EXPR _metadata_end "${_metadata_start} + 12")
    if(_aligned EQUAL 0 AND NOT _metadata_end GREATER _length)
      string(SUBSTRING "${_hex}" 0 ${_metadata_start} _before)
      string(SUBSTRING "${_hex}" ${_metadata_end} -1 _after)
      set(_hex "${_before}000000000000${_after}")
    endif()
    math(EXPR _from "${_start} + 2")
  endwhile()
  set(${hex_variable} "${_hex}" PARENT_SCOPE)
endfunction()

file(READ "${TEST_FILE}" _test_hex HEX)
file(READ "${BASELINE_FILE}" _baseline_hex HEX)

_plus_blank_gzip_metadata(_test_hex)
_plus_blank_gzip_metadata(_baseline_hex)

if(_test_hex STREQUAL _baseline_hex)
  return()
endif()

string(LENGTH "${_test_hex}" _test_length)
string(LENGTH "${_baseline_hex}" _baseline_length)
math(EXPR _test_bytes "${_test_length} / 2")
math(EXPR _baseline_bytes "${_baseline_length} / 2")

if(NOT _test_length EQUAL _baseline_length)
  set(_detail "The files are ${_test_bytes} and ${_baseline_bytes} bytes long.")
else()
  # Report where the two first diverge, which is the part worth knowing. The
  # search runs over blocks first so that the whole hex string is not expanded
  # once per byte.
  set(_block_length 512)
  set(_block_start 0)
  set(_test_block "")
  set(_baseline_block "")
  while(_block_start LESS _test_length)
    math(EXPR _remaining "${_test_length} - ${_block_start}")
    if(_remaining LESS _block_length)
      set(_this_block ${_remaining})
    else()
      set(_this_block ${_block_length})
    endif()
    string(SUBSTRING "${_test_hex}" ${_block_start} ${_this_block} _test_block)
    string(SUBSTRING "${_baseline_hex}" ${_block_start} ${_this_block} _baseline_block)
    if(NOT _test_block STREQUAL _baseline_block)
      break()
    endif()
    math(EXPR _block_start "${_block_start} + ${_this_block}")
  endwhile()

  set(_offset 0)
  set(_test_byte "??")
  set(_baseline_byte "??")
  while(_offset LESS _this_block)
    string(SUBSTRING "${_test_block}" ${_offset} 2 _test_byte)
    string(SUBSTRING "${_baseline_block}" ${_offset} 2 _baseline_byte)
    if(NOT _test_byte STREQUAL _baseline_byte)
      break()
    endif()
    math(EXPR _offset "${_offset} + 2")
  endwhile()

  math(EXPR _byte "(${_block_start} + ${_offset}) / 2")
  set(_detail "Both are ${_test_bytes} bytes long and first differ at byte ${_byte}, 0x${_test_byte} against 0x${_baseline_byte}.")
endif()

message(FATAL_ERROR
  "Files differ.\n"
  "  Written:  ${TEST_FILE}\n"
  "  Baseline: ${BASELINE_FILE}\n"
  "  ${_detail}")
