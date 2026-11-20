#include "stdafx.h"
#pragma hdrstop

#include "FileSystem.h"
#include "LocatorAPI.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

EFS_Utils* xr_EFS = NULL;

EFS_Utils::EFS_Utils() {}

EFS_Utils::~EFS_Utils()
{
	for (HANDLEPairIt it = m_LockFiles.begin(); it != m_LockFiles.end(); ++it)
	{
		int fd = static_cast<int>(reinterpret_cast<intptr_t>(it->second));
		close(fd);
	}
}

xr_string EFS_Utils::ExtractFileName(LPCSTR src)
{
	if (!src)
		return xr_string();
	const char* name = strrchr(src, '/');
	const char* backslash = strrchr(src, '\\');
	if (!name || (backslash && backslash > name))
		name = backslash;
	return xr_string(name ? name + 1 : src);
}

xr_string EFS_Utils::ExtractFileExt(LPCSTR src)
{
	if (!src)
		return xr_string();
	const char* base = src;
	const char* slash = strrchr(src, '/');
	const char* backslash = strrchr(src, '\\');
	if (slash && slash + 1 > base) base = slash + 1;
	if (backslash && backslash + 1 > base) base = backslash + 1;
	const char* dot = strrchr(base, '.');
	return dot ? xr_string(dot) : xr_string();
}

xr_string EFS_Utils::ExtractFilePath(LPCSTR src)
{
	if (!src)
		return xr_string();
	const char* slash = strrchr(src, '/');
	const char* backslash = strrchr(src, '\\');
	const char* last = slash;
	if (!last || (backslash && backslash > last)) last = backslash;
	return last ? xr_string(src, static_cast<size_t>(last - src + 1)) : xr_string();
}

xr_string EFS_Utils::ExcludeBasePath(LPCSTR full_path, LPCSTR excl_path)
{
	if (!full_path || !excl_path)
		return xr_string(full_path ? full_path : "");
	const char* sub = strstr(full_path, excl_path);
	return xr_string(sub ? sub + xr_strlen(excl_path) : full_path);
}

xr_string EFS_Utils::ChangeFileExt(LPCSTR src, LPCSTR ext)
{
	if (!src)
		return xr_string();
	xr_string result(src);
	const size_t slash = result.find_last_of("/\\");
	const size_t dot = result.find_last_of('.');
	if (dot != xr_string::npos && (slash == xr_string::npos || dot > slash))
		result.erase(dot);
	if (ext)
		result += ext;
	return result;
}

xr_string EFS_Utils::ChangeFileExt(const xr_string& src, LPCSTR ext)
{
	return ChangeFileExt(src.c_str(), ext);
}

bool EFS_Utils::GetOpenName(LPCSTR, string_path&, int, bool, LPCSTR, int)
{
	return false;
}

bool EFS_Utils::GetOpenName(LPCSTR, xr_string&, bool, LPCSTR, int)
{
	return false;
}

bool EFS_Utils::GetSaveName(LPCSTR, string_path&, LPCSTR, int)
{
	return false;
}

bool EFS_Utils::GetSaveName(LPCSTR, xr_string&, LPCSTR, int)
{
	return false;
}

void EFS_Utils::MarkFile(LPCSTR fn, bool delete_source)
{
	const xr_string extension = ExtractFileExt(fn);
	xr_string backup_extension(extension);
	if (backup_extension.empty())
		backup_extension = "~";
	else
		backup_extension.insert(1, "~");
	const xr_string backup = ChangeFileExt(fn, backup_extension.c_str());
	if (delete_source)
		FS.file_rename(fn, backup.c_str(), true);
	else
		FS.file_copy(fn, backup.c_str());
}

xr_string EFS_Utils::AppendFolderToName(xr_string& tex_name, int depth, BOOL full_name)
{
	string1024 buffer;
	strcpy_s(buffer, sizeof(buffer), tex_name.c_str());
	tex_name = AppendFolderToName(buffer, depth, full_name);
	return tex_name;
}

LPCSTR EFS_Utils::AppendFolderToName(LPSTR tex_name, int depth, BOOL full_name)
{
	string256 buffer;
	strcpy_s(buffer, sizeof(buffer), AppendFolderToName(static_cast<LPCSTR>(tex_name), buffer, depth, full_name));
	strcpy_s(tex_name, 256, buffer);
	return tex_name;
}

LPCSTR EFS_Utils::AppendFolderToName(LPCSTR src_name, LPSTR dest_name, int depth, BOOL full_name)
{
	if (!src_name || !dest_name)
		return dest_name;
	const int original_depth = depth;
	const char* src = src_name;
	char* dst = dest_name;
	for (; *src && depth > 0; ++src, ++dst)
	{
		if (*src == '_')
		{
			--depth;
			*dst = '/';
		}
		else
			*dst = *src;
	}
	if (full_name)
	{
		*dst = 0;
		if (depth < original_depth)
			strcat(dest_name, src);
	}
	else
		strcpy(dst, src);
	return dest_name;
}

LPCSTR EFS_Utils::GenerateName(LPCSTR base_path, LPCSTR base_name, LPCSTR def_ext, LPSTR out_name)
{
	if (!out_name)
		return NULL;
	const int capacity = 1024;
	int number = 0;
	string_path candidate;
	if (base_name)
		snprintf(candidate, sizeof(candidate), "%s%s%s", base_path ? base_path : "", base_name, def_ext ? def_ext : "");
	else
		snprintf(candidate, sizeof(candidate), "%s%02d%s", base_path ? base_path : "", number++, def_ext ? def_ext : "");
	while (FS.exist(candidate))
	{
		if (base_name)
			snprintf(candidate, sizeof(candidate), "%s%s%02d%s", base_path ? base_path : "", base_name, number++, def_ext ? def_ext : "");
		else
			snprintf(candidate, sizeof(candidate), "%s%02d%s", base_path ? base_path : "", number++, def_ext ? def_ext : "");
	}
	strncpy(out_name, candidate, capacity - 1);
	out_name[capacity - 1] = 0;
	return out_name;
}

BOOL EFS_Utils::CheckLocking(LPCSTR fname, bool only_self, bool show_message)
{
	if (!fname)
		return FALSE;
	if (only_self)
		return m_LockFiles.find(fname) != m_LockFiles.end();
	// Vita homebrew has no flock() in Newlib. Track locks owned by this process.
	const bool locked = m_LockFiles.find(fname) != m_LockFiles.end();
	if (locked && show_message)
		Msg("#!Access denied. File: '%s' currently locked.", fname);
	return locked ? TRUE : FALSE;
}

BOOL EFS_Utils::LockFile(LPCSTR fname, bool log)
{
	if (!fname || m_LockFiles.find(fname) != m_LockFiles.end())
		return FALSE;
	const int fd = open(fname, O_RDWR);
	if (fd < 0)
	{
		if (fd >= 0) close(fd);
		return FALSE;
	}
	m_LockFiles.insert(mk_pair(xr_string(fname), reinterpret_cast<void*>(static_cast<intptr_t>(fd))));
	if (log)
		WriteAccessLog(fname, "Lock");
	return TRUE;
}

BOOL EFS_Utils::UnlockFile(LPCSTR fname, bool log)
{
	if (!fname)
		return FALSE;
	HANDLEPairIt it = m_LockFiles.find(fname);
	if (it == m_LockFiles.end())
		return FALSE;
	const int fd = static_cast<int>(reinterpret_cast<intptr_t>(it->second));
	m_LockFiles.erase(it);
	if (log)
		WriteAccessLog(fname, "Unlock");
	return close(fd) == 0 ? TRUE : FALSE;
}

void EFS_Utils::WriteAccessLog(LPCSTR, LPCSTR) {}
