#include "fileio.h"
#include <stdexcept>
#include <cstdio>
#include <memory>

FilePointer OpenFile(std::string_view filename, const char* mode)
{
    auto fn = std::string(filename);
    FilePointer fp { std::fopen(fn.c_str(), mode) };
    if (!fp)
        throw std::runtime_error { "Could not open " + fn + " with mode \"" + mode + "\"" };
    return fp;
}

std::vector<std::uint8_t> ReadFile(const std::string& filename)
{
    FilePointer fp = OpenFile(filename, "rb");

    std::fseek(fp.get(), 0, SEEK_END);
    const auto size = static_cast<size_t>(std::ftell(fp.get()));
    std::fseek(fp.get(), 0, SEEK_SET);

    std::vector<std::uint8_t> bytes(size);
    if (size)
        std::fread(&bytes[0], 1, size, fp.get());
    if (std::ferror(fp.get()))
        throw std::runtime_error { "Error reading from " + filename };
    return bytes;
}

#ifdef _MSC_VER
#include <io.h>
bool IsStdioInteractive(void)
{
    return _isatty(_fileno(stdout));
}
#else
bool IsStdioInteractive(void)
{
    return isatty(STDOUT_FILENO);
}
#endif