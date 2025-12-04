add_executable(test_moo test_moo.cpp)
target_link_libraries(test_moo PRIVATE xemu_core)
ADD_TEST(test_moo)
