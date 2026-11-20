#ifndef XRCORE_VITA_NATIVE_FILE_READER_H
#define XRCORE_VITA_NATIVE_FILE_READER_H

#include <cstddef>
#include <psp2/io/fcntl.h>

class NativeVitaFileReader
{
public:
    NativeVitaFileReader();
    ~NativeVitaFileReader();

    NativeVitaFileReader(const NativeVitaFileReader&) = delete;
    NativeVitaFileReader& operator=(const NativeVitaFileReader&) = delete;

    bool open(const char* path);
    std::size_t read(void* destination, std::size_t bytes);
    bool seek(SceOff offset);
    SceOff size() const;
    void close();
    bool is_open() const;

private:
    SceUID file_;
    SceOff size_;
};

#endif
