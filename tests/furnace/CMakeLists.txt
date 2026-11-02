find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Intentionally independent of vgmtranscore, Qt, MIDI, and all source parsers.
add_executable(fur_writer_test FurWriterTest.cpp ../../src/main/conversion/FurWriter.cpp)
target_include_directories(fur_writer_test PRIVATE ../../src/main/conversion)
target_compile_features(fur_writer_test PRIVATE cxx_std_20)
vgmtrans_enable_project_warnings(fur_writer_test)

set(FUR_TEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/artifacts/$<CONFIG>")
add_test(NAME furnace_writer COMMAND fur_writer_test "${FUR_TEST_OUTPUT}")
set_tests_properties(furnace_writer PROPERTIES FIXTURES_SETUP furnace_files)
add_test(NAME furnace_binary_layout
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/verify_fur.py" "${FUR_TEST_OUTPUT}")
set_tests_properties(furnace_binary_layout PROPERTIES FIXTURES_REQUIRED furnace_files)
