#include "vita_stream_reader.h"

#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t kCompressedChunkFlag = 0x80000000u;

std::uint32_t read_u32_le(const unsigned char* bytes)
{
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
}

VitaStreamReader::VitaStreamReader()
    : base_offset_(0), length_(0), position_(0)
{
}

bool VitaStreamReader::open(const char* path)
{
    close();

    if (path == nullptr || path[0] == '\0')
        return false;

    if (!file_.open(path))
        return false;

    const SceOff file_size = file_.size();
    if (file_size < 0
        || static_cast<unsigned long long>(file_size)
            > std::numeric_limits<std::uint32_t>::max()) {
        close();
        return false;
    }

    path_ = path;
    base_offset_ = 0;
    length_ = static_cast<std::uint32_t>(file_size);
    position_ = 0;
    return true;
}

bool VitaStreamReader::open_range(
    const std::string& path,
    std::uint32_t base_offset,
    std::uint32_t range_size)
{
    close();

    if (path.empty())
        return false;

    if (!file_.open(path.c_str()))
        return false;

    const SceOff file_size = file_.size();
    if (file_size < 0
        || static_cast<unsigned long long>(base_offset)
            > static_cast<unsigned long long>(file_size)
        || static_cast<unsigned long long>(range_size)
            > static_cast<unsigned long long>(file_size)
                - static_cast<unsigned long long>(base_offset)) {
        close();
        return false;
    }

    if (!file_.seek(static_cast<SceOff>(base_offset))) {
        close();
        return false;
    }

    path_ = path;
    base_offset_ = base_offset;
    length_ = range_size;
    position_ = 0;
    return true;
}

void VitaStreamReader::close()
{
    file_.close();
    path_.clear();
    base_offset_ = 0;
    length_ = 0;
    position_ = 0;
}

bool VitaStreamReader::is_open() const
{
    return file_.is_open();
}

std::size_t VitaStreamReader::read(void* destination, std::size_t bytes)
{
    if (!is_open() || destination == nullptr || bytes == 0)
        return 0;

    const std::uint32_t remaining = length_ - position_;
    if (remaining == 0)
        return 0;

    const std::size_t amount =
        bytes < static_cast<std::size_t>(remaining)
            ? bytes
            : static_cast<std::size_t>(remaining);

    const std::size_t result = file_.read(destination, amount);
    if (result > amount)
        return 0;

    position_ += static_cast<std::uint32_t>(result);
    return result;
}

bool VitaStreamReader::seek(std::uint32_t offset)
{
    if (!is_open() || offset > length_)
        return false;

    const std::uint64_t absolute =
        static_cast<std::uint64_t>(base_offset_) + offset;

    if (absolute > static_cast<std::uint64_t>(
            std::numeric_limits<SceOff>::max()))
        return false;

    if (!file_.seek(static_cast<SceOff>(absolute)))
        return false;

    position_ = offset;
    return true;
}

bool VitaStreamReader::advance(std::int32_t offset)
{
    const std::int64_t target =
        static_cast<std::int64_t>(position_) + offset;

    if (target < 0
        || target > static_cast<std::int64_t>(length_))
        return false;

    return seek(static_cast<std::uint32_t>(target));
}

std::uint32_t VitaStreamReader::tell() const
{
    return position_;
}

std::uint32_t VitaStreamReader::length() const
{
    return length_;
}

std::uint32_t VitaStreamReader::elapsed() const
{
    return length_ - position_;
}

std::unique_ptr<VitaStreamReader> VitaStreamReader::open_chunk(
    std::uint32_t chunk_id)
{
    if (!is_open())
        return nullptr;

    if (!seek(0))
        return nullptr;

    while (elapsed() >= 8) {
        unsigned char header[8];

        if (read(header, sizeof(header)) != sizeof(header))
            return nullptr;

        const std::uint32_t type = read_u32_le(header);
        const std::uint32_t chunk_size = read_u32_le(header + 4);

        if (chunk_size > elapsed())
            return nullptr;

        if ((type & ~kCompressedChunkFlag) == chunk_id) {
            if (chunk_size == 0 || (type & kCompressedChunkFlag) != 0)
                return nullptr;

            const std::uint64_t child_base =
                static_cast<std::uint64_t>(base_offset_) + position_;

            if (child_base > std::numeric_limits<std::uint32_t>::max())
                return nullptr;

            std::unique_ptr<VitaStreamReader> child(
                new VitaStreamReader());

            if (!child->open_range(
                    path_,
                    static_cast<std::uint32_t>(child_base),
                    chunk_size))
                return nullptr;

            return child;
        }

        if (!seek(position_ + chunk_size))
            return nullptr;
    }

    return nullptr;
}
