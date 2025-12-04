set(_testList "")
macro(ADD_TEST targetName)
    set(_testList ${_testList} ${targetName} PARENT_SCOPE)
endmacro()

add_subdirectory(decode)
add_subdirectory(moo)
add_subdirectory(386_asm)
add_subdirectory(rom386)

set(_commands "")
foreach(test ${_testList})
    if (MSVC)
        set(_commands ${_commands} COMMAND cd $<TARGET_FILE_DIR:${test}>/.. && $<TARGET_FILE:${test}>)
    else()
        set(_commands ${_commands} COMMAND cd $<TARGET_FILE_DIR:${test}> && ./${test})
    endif()
endforeach()

add_custom_target(run_tests
    ${_commands}
    )
