#include "native_file_reader.h"

#include <string>

NativeVitaFileReader::NativeVitaFileReader()
    : file_(-1), size_(-1)
{
}

NativeVitaFileReader::~NativeVitaFileReader()
{
    close();
}

bool NativeVitaFileReader::open(const char* path)
{
    close();

    if (path == nullptr || path[0] == '\0')
        return false;

    std::string normalized_path(path);
    for (std::size_t i = 0; i != normalized_path.size(); ++i) {
        if (normalized_path[i] == '\\')
            normalized_path[i] = '/';
    }

    file_ = sceIoOpen(normalized_path.c_str(), SCE_O_RDONLY, 0);
    if (file_ < 0) {
        file_ = -1;
        return false;
    }

    size_ = sceIoLseek(file_, 0, SCE_SEEK_END);
    if (size_ < 0 || sceIoLseek(file_, 0, SCE_SEEK_SET) < 0) {
        close();
        return false;
    }

    return true;
}

std::size_t NativeVitaFileReader::read(void* destination, std::size_t bytes)
{
    if (file_ < 0 || destination == nullptr || bytes == 0)
        return 0;

    const SceSSize result = sceIoRead(file_, destination, static_cast<SceSize>(bytes));

    if (result < 0)
        return 0;

    return static_cast<std::size_t>(result);
}

bool NativeVitaFileReader::seek(SceOff offset)
{
    if (file_ < 0 || offset < 0 || offset > size_)
        return false;

    return sceIoLseek(file_, offset, SCE_SEEK_SET) == offset;
}

SceOff NativeVitaFileReader::size() const
{
    return size_;
}

void NativeVitaFileReader::close()
{
    if (file_ >= 0) {
        sceIoClose(file_);
        file_ = -1;
    }

    size_ = -1;
}

bool NativeVitaFileReader::is_open() const
{
    return file_ >= 0;
}
