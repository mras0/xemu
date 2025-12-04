#ifndef FILEIO_H
#define FILEIO_H

#include <vector>
#include <cstdint>
#include <string>

std::vector<std::uint8_t> ReadFile(const std::string& filename);

bool IsStdioInteractive(void);

#endif
