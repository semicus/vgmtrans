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

add_executable(furnace_resources_test ResourceTest.cpp)
target_link_libraries(furnace_resources_test PRIVATE vgmtranscore)
target_compile_definitions(furnace_resources_test PRIVATE FURNACE_TEST_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/bin")
vgmtrans_enable_project_warnings(furnace_resources_test)
add_test(NAME furnace_resources COMMAND furnace_resources_test)

option(VGMTRANS_BUILD_PUNISHER_RESOURCE_TOOL "Build local-only ROM resource utility (not a CI test)" OFF)
if(VGMTRANS_BUILD_PUNISHER_RESOURCE_TOOL)
  add_executable(punisher_resources PunisherResources.cpp)
  target_link_libraries(punisher_resources PRIVATE vgmtranscore)
  target_compile_definitions(punisher_resources PRIVATE FURNACE_TEST_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/bin")
  vgmtrans_enable_project_warnings(punisher_resources)
endif()
