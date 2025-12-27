add_executable(disasm
    disasm.cpp
    exehdr.cpp exehdr.h
    )
target_link_libraries(disasm xemu_core)
