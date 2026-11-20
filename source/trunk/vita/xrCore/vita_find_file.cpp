#include "vita_find_file.h"

#include <cctype>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
struct FindContext
{
    std::vector<_finddata_t> entries;
    std::size_t next_entry;
};

bool wildcard_match(const char* pattern, const char* name)
{
    const char* star = 0;
    const char* retry = 0;

    while (*name) {
        const unsigned char p = static_cast<unsigned char>(*pattern);
        const unsigned char n = static_cast<unsigned char>(*name);
        if (p == '?' || (p != 0 && std::tolower(p) == std::tolower(n))) {
            ++pattern;
            ++name;
        } else if (p == '*') {
            star = pattern++;
            retry = name;
        } else if (star != 0) {
            pattern = star + 1;
            name = ++retry;
        } else {
            return false;
        }
    }

    while (*pattern == '*')
        ++pattern;
    return *pattern == 0;
}

bool fill_find_data(const char* directory, const char* name, _finddata_t& data)
{
    std::memset(&data, 0, sizeof(data));
    std::strncpy(data.name, name, sizeof(data.name) - 1);

    std::string full_path(directory);
    if (!full_path.empty() && full_path[full_path.size() - 1] != '/')
        full_path += '/';
    full_path += name;

    struct stat attributes;
    if (stat(full_path.c_str(), &attributes) != 0)
        return false;

    if (S_ISDIR(attributes.st_mode))
        data.attrib |= _A_SUBDIR;
    if (name[0] == '.')
        data.attrib |= _A_HIDDEN;

    data.time_write = attributes.st_mtime;
    if (attributes.st_size < 0)
        return false;

    const unsigned long long file_size =
        static_cast<unsigned long long>(attributes.st_size);
    const unsigned long long max_size =
        static_cast<unsigned long long>(std::numeric_limits<long>::max());
    if (file_size > max_size)
        data.size = std::numeric_limits<long>::max();
    else
        data.size = static_cast<long>(file_size);
    return true;
}
}

intptr_t _findfirst(const char* input_pattern, _finddata_t* data)
{
    if (input_pattern == 0 || data == 0)
        return static_cast<intptr_t>(-1);

    std::string pattern(input_pattern);
    for (std::size_t i = 0; i != pattern.size(); ++i) {
        if (pattern[i] == '\\')
            pattern[i] = '/';
    }

    const std::size_t slash = pattern.find_last_of('/');
    const std::string directory = slash == std::string::npos
        ? "."
        : (slash == 0 ? "/" : pattern.substr(0, slash));
    std::string name_pattern = slash == std::string::npos
        ? pattern
        : pattern.substr(slash + 1);
    if (name_pattern == "*.*")
        name_pattern = "*";

    DIR* dir = opendir(directory.c_str());
    if (dir == 0)
        return static_cast<intptr_t>(-1);

    FindContext* context = new FindContext();
    context->next_entry = 0;

    struct dirent* entry = 0;
    while ((entry = readdir(dir)) != 0) {
        if (!wildcard_match(name_pattern.c_str(), entry->d_name))
            continue;

        _finddata_t item;
        if (fill_find_data(directory.c_str(), entry->d_name, item))
            context->entries.push_back(item);
    }
    closedir(dir);

    if (context->entries.empty()) {
        delete context;
        return static_cast<intptr_t>(-1);
    }

    *data = context->entries[context->next_entry++];
    return reinterpret_cast<intptr_t>(context);
}

int _findnext(intptr_t handle, _finddata_t* data)
{
    if (handle == static_cast<intptr_t>(-1) || data == 0)
        return -1;

    FindContext* context = reinterpret_cast<FindContext*>(handle);
    if (context->next_entry >= context->entries.size())
        return -1;

    *data = context->entries[context->next_entry++];
    return 0;
}

int _findclose(intptr_t handle)
{
    if (handle == static_cast<intptr_t>(-1))
        return -1;

    delete reinterpret_cast<FindContext*>(handle);
    return 0;
}
