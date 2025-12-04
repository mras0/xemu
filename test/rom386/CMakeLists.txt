BUILD_ROM(test_rom386)

add_executable(test_rom386 test_rom386.cpp)
target_link_libraries(test_rom386 xemu_core)
add_dependencies(test_rom386 test_rom386_rom)
ADD_TEST(test_rom386)
