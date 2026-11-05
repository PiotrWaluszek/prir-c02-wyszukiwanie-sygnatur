include(FetchContent)
FetchContent_Declare(doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
  URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(doctest)

add_executable(sigscan-tests test_main.cpp test_automaton.cpp test_generator.cpp test_io.cpp)
target_link_libraries(sigscan-tests PRIVATE sigscan_core doctest::doctest)
add_test(NAME unit COMMAND sigscan-tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  set(INTEG ${CMAKE_CURRENT_SOURCE_DIR}/integration.py)
  # Visual Studio puts the programs in build/<Config>
  set(INTEG_ARGS --bin $<TARGET_FILE_DIR:sigscan-seq> --work ${CMAKE_BINARY_DIR}/itest)
  if(MPIEXEC_EXECUTABLE)
    list(APPEND INTEG_ARGS --mpirun ${MPIEXEC_EXECUTABLE})
  endif()
  add_test(NAME integration COMMAND ${Python3_EXECUTABLE} ${INTEG} ${INTEG_ARGS})
  set_tests_properties(integration PROPERTIES TIMEOUT 900)
  if(NOT WIN32)
    set_tests_properties(integration PROPERTIES ENVIRONMENT "TMPDIR=/tmp")
  endif()
else()
  message(STATUS "sigscan: Python 3 not found - integration test skipped (unit tests still run)")
endif()
