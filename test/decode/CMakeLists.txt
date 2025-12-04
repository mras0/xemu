add_executable(test_decode test_decode.cpp)
target_link_libraries(test_decode xemu_core)
ADD_TEST(test_decode)
