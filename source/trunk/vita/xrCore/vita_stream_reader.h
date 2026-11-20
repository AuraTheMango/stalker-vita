#ifndef XRCORE_VITA_VITA_STREAM_READER_H
#define XRCORE_VITA_VITA_STREAM_READER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "native_file_reader.h"

class VitaStreamReader {
public:
    VitaStreamReader();
    ~VitaStreamReader() = default;

    VitaStreamReader(const VitaStreamReader&) = delete;
    VitaStreamReader& operator=(const VitaStreamReader&) = delete;

    bool open(const char* path);
    bool open_range(const std::string& path,
                    std::uint32_t base_offset,
                    std::uint32_t range_size);
    void close();
    bool is_open() const;

    std::size_t read(void* destination, std::size_t bytes);
    bool seek(std::uint32_t offset);
    bool advance(std::int32_t offset);

    std::uint32_t tell() const;
    std::uint32_t length() const;
    std::uint32_t elapsed() const;

    std::unique_ptr<VitaStreamReader> open_chunk(std::uint32_t chunk_id);

private:
    NativeVitaFileReader file_;
    std::string path_;
    std::uint32_t base_offset_;
    std::uint32_t length_;
    std::uint32_t position_;
};

#endif
