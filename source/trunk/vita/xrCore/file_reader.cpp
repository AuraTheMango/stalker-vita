#include "file_reader.h"

#include <string>

VitaFileReader::VitaFileReader()
    : file_(nullptr), size_(-1)
{
}

VitaFileReader::~VitaFileReader()
{
    close();
}

bool VitaFileReader::open(const char* path)
{
    close();

    if (path == nullptr || path[0] == '\0')
        return false;

    std::string normalized_path(path);
    for (std::size_t i = 0; i != normalized_path.size(); ++i) {
        if (normalized_path[i] == '\\')
            normalized_path[i] = '/';
    }

    file_ = std::fopen(normalized_path.c_str(), "rb");
    if (file_ == nullptr)
        return false;

    if (std::fseek(file_, 0, SEEK_END) != 0) {
        close();
        return false;
    }

    size_ = std::ftell(file_);
    if (size_ < 0 || std::fseek(file_, 0, SEEK_SET) != 0) {
        close();
        return false;
    }

    return true;
}

std::size_t VitaFileReader::read(void* destination, std::size_t bytes)
{
    if (file_ == nullptr || destination == nullptr || bytes == 0)
        return 0;

    return std::fread(destination, 1, bytes, file_);
}

long VitaFileReader::size() const
{
    return size_;
}

void VitaFileReader::close()
{
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }

    size_ = -1;
}

bool VitaFileReader::is_open() const
{
    return file_ != nullptr;
}
