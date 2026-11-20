#include "stdafx.h"
#include "file_stream_reader.h"

void CFileStreamReader::construct	(LPCSTR file_name, const u32 &window_size)
{
#if defined(__vita__)
	(void)window_size;
	inherited::construct	(file_name);
#else
	m_file_handle			=
		CreateFile(
			file_name,
			GENERIC_READ,
			FILE_SHARE_READ,
			0,
			OPEN_EXISTING,
			0,
			0
		);

	VERIFY					(m_file_handle != INVALID_HANDLE_VALUE);
	u32						file_size = (u32)GetFileSize(m_file_handle,NULL);

	HANDLE					file_mapping_handle =
		CreateFileMapping(
			m_file_handle,
			0,
			PAGE_READONLY,
			0,
			0,
			0
		);
	VERIFY					(file_mapping_handle != INVALID_HANDLE_VALUE);

	inherited::construct	(file_mapping_handle,0,file_size,file_size,window_size);
#endif
}

void CFileStreamReader::destroy		()
{
#if defined(__vita__)
	inherited::destroy		();
#else
	HANDLE					file_mapping_handle = this->file_mapping_handle();
	inherited::destroy		();
	CloseHandle				(file_mapping_handle);
	CloseHandle				(m_file_handle);
#endif
}
