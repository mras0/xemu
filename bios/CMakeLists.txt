set(commonIncludes int10h.asm common.inc)

BUILD_ROM(bios DEPENDS ${commonIncludes})
BUILD_ROM(videobios CHECKSUM DEPENDS ${commonIncludes})
