# Tests of the kb tool (tools/kb) and of the configurator's resolver against the shared vectors (ADR-0014):
#   kb_cli_unit             the Python unit tests and every vector (skipped, code 77, when PyYAML is missing)
#   kb_resolver_js_vectors  every vector through configurator/resolver.mjs (when node is installed)
# Included from the root CMakeLists.txt; adds nothing unless KB_BUILD_TESTS is ON.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The kb-core authors
include_guard(GLOBAL)

if(NOT KB_BUILD_TESTS)
  return()
endif()
enable_testing()

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME kb_cli_unit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../tools/kb/tests/run.py)
  set_tests_properties(kb_cli_unit PROPERTIES SKIP_RETURN_CODE 77 LABELS tools)
else()
  message(STATUS "[kb] Python 3.10 not found: kb_cli_unit is not added")
endif()

find_program(KB_NODE node)
if(KB_NODE)
  add_test(NAME kb_resolver_js_vectors
    COMMAND ${KB_NODE} ${CMAKE_CURRENT_LIST_DIR}/../tests/resolver/run_js_vectors.mjs)
  set_tests_properties(kb_resolver_js_vectors PROPERTIES LABELS tools)
else()
  message(STATUS "[kb] node not found: kb_resolver_js_vectors is not added")
endif()
