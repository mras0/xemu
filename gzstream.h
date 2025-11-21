#ifndef GZSTREAM_H
#define GZSTREAM_H

#include <istream>
#include <vector>

class GZInputStream : private std::streambuf, public std::istream {
public:
    explicit GZInputStream(const char* filename);
    ~GZInputStream();

private:
    struct gzFile_s* /*gzFile*/ f_;
    std::vector<char> buffer_;

    int underflow() override;
};


#endif
