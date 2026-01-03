#ifndef FILEIO_H
#define FILEIO_H

#include <vector>
#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <cstdio>

struct FileCloser {
    void operator()(std::FILE* fp)
    {
        if (fp)
            std::fclose(fp);
    }
};
using FilePointer = std::unique_ptr<std::FILE, FileCloser>;

FilePointer OpenFile(std::string_view filename, const char* mode);

std::vector<std::uint8_t> ReadFile(const std::string& filename);

bool IsStdioInteractive(void);

#endif
