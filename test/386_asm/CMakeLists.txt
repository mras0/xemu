add_executable(test386_asm test386_asm.cpp)
target_link_libraries(test386_asm xemu_core)
ADD_TEST(test386_asm)
