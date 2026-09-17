#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CMAKE="$ROOT/CMakeLists.txt"

grep -q 'WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS' "$CMAKE"
grep -q 'SHELL:-framework ${_wh_pj_link_item}' "$CMAKE"
grep -Fq 'target_link_options(${APP_EXECUTABLE} PRIVATE' "$CMAKE"

# Simulate the token shape produced by pkg-config on macOS and prove that the
# framework name is not left in the normal-library list.
tmp=$(mktemp "${TMPDIR:-/tmp}/wh-cmake-framework.XXXXXX.cmake")
trap 'rm -f "$tmp"' EXIT HUP INT TERM
cat >"$tmp" <<'CMAKE_TEST'
set(APPLE TRUE)
set(PJPROJECT_STATIC_LDFLAGS "-L/opt/pj/lib;-lpjsua2;-framework;CoreServices;-framework;AudioToolbox;-lpthread")
set(WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS)
set(WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS)
set(_wh_expect_pj_framework FALSE)
foreach(_wh_pj_link_item IN LISTS PJPROJECT_STATIC_LDFLAGS)
  if(APPLE AND _wh_expect_pj_framework)
    list(APPEND WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS "SHELL:-framework ${_wh_pj_link_item}")
    set(_wh_expect_pj_framework FALSE)
  elseif(APPLE AND _wh_pj_link_item STREQUAL "-framework")
    set(_wh_expect_pj_framework TRUE)
  elseif(APPLE AND _wh_pj_link_item MATCHES "^-framework[ \\t]+(.+)$")
    list(APPEND WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS "SHELL:-framework ${CMAKE_MATCH_1}")
  else()
    list(APPEND WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS "${_wh_pj_link_item}")
  endif()
endforeach()
if(NOT WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS STREQUAL "SHELL:-framework CoreServices;SHELL:-framework AudioToolbox")
  message(FATAL_ERROR "framework normalization failed: ${WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS}")
endif()
list(FIND WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS "CoreServices" _bad_core)
list(FIND WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS "AudioToolbox" _bad_audio)
if(NOT _bad_core EQUAL -1 OR NOT _bad_audio EQUAL -1)
  message(FATAL_ERROR "framework leaked into target_link_libraries list")
endif()
CMAKE_TEST
cmake -P "$tmp"

echo "macOS framework linkage regression: PASS"
