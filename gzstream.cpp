#include "gzstream.h"
#include <zlib.h>
#include <format>
#include <stdexcept>

GZInputStream::GZInputStream(const char* filename)
    : std::ios(this)
    , std::istream(this)
    , f_ { gzopen(filename, "rb") }
    , buffer_(128 * 1024)
{
    if (!f_)
        throw std::runtime_error { std::format("Could not open gzip file \"{}\"", filename) };
    gzbuffer(f_, static_cast<unsigned>(buffer_.size()));
}

GZInputStream::~GZInputStream()
{
    gzclose(f_);
}

int GZInputStream::underflow()
{
    if (gptr() == egptr()) {
        char* bufp = &buffer_[0];
        int len = gzread(f_, bufp, static_cast<unsigned>(buffer_.size()));
        if (len < 0)
            throw std::runtime_error { "Error reading from gzip file" };
        setg(bufp, bufp, bufp + len);
    }
    return this->gptr() == this->egptr()
        ? std::char_traits<char>::eof()
        : std::char_traits<char>::to_int_type(*this->gptr());
}
