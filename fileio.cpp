#include "fileio.h"
#include <stdexcept>
#include <cstdio>
#include <memory>

struct FileCloser {
    void operator()(std::FILE* fp)
    {
        std::fclose(fp);
    }
};
using FilePointer = std::unique_ptr<std::FILE, FileCloser>;

std::vector<std::uint8_t> ReadFile(const std::string& filename)
{
    FilePointer fp { std::fopen(filename.c_str(), "rb") };
    if (!fp)
        throw std::runtime_error{"Could not open " + filename};

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
