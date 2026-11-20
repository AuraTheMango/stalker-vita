#ifndef XRCORE_VITA_FILE_READER_H
#define XRCORE_VITA_FILE_READER_H

#include <cstddef>
#include <cstdio>

class VitaFileReader
{
public:
    VitaFileReader();
    ~VitaFileReader();

    VitaFileReader(const VitaFileReader&) = delete;
    VitaFileReader& operator=(const VitaFileReader&) = delete;

    bool open(const char* path);
    std::size_t read(void* destination, std::size_t bytes);
    long size() const;
    void close();
    bool is_open() const;

private:
    std::FILE* file_;
    long size_;
};

#endif
