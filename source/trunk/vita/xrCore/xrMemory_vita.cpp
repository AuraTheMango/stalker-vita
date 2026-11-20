#include "stdafx.h"
#pragma hdrstop

#include "xrsharedmem.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

xrMemory Memory;
BOOL mem_initialized = FALSE;
bool shared_str_initialized = false;
MEMPOOL mem_pools[mem_pools_count];

namespace
{
void vita_mem_copy(void* dest, const void* src, u32 count)
{
	memcpy(dest, src, count);
}

void vita_mem_fill(void* dest, int value, u32 count)
{
	memset(dest, value, count);
}

void vita_mem_fill32(void* dest, u32 value, u32 count)
{
	u32* words = static_cast<u32*>(dest);
	for (u32 i = 0; i < count; ++i)
		words[i] = value;
}

struct allocation_header
{
	void* raw;
	size_t size;
};

void* allocate_aligned(size_t size)
{
	const size_t requested = size ? size : 1;
	void* raw = malloc(requested + sizeof(allocation_header) + 15);
	if (!raw)
		abort();
	uintptr_t address = reinterpret_cast<uintptr_t>(raw) + sizeof(allocation_header);
	address = (address + 15) & ~uintptr_t(15);
	allocation_header* header = reinterpret_cast<allocation_header*>(address) - 1;
	header->raw = raw;
	header->size = requested;
	return reinterpret_cast<void*>(address);
}
}

xrMemory::xrMemory()
{
	stat_calls = 0;
	stat_counter = 0;
	mem_copy = &vita_mem_copy;
	mem_fill = &vita_mem_fill;
	mem_fill32 = &vita_mem_fill32;
}

void xrMemory::_initialize(BOOL)
{
	if (mem_initialized)
		return;

	stat_calls = 0;
	stat_counter = 0;
	g_pStringContainer = xr_new<str_container>();
	g_pSharedMemoryContainer = xr_new<smem_container>();
	shared_str_initialized = true;
	mem_initialized = TRUE;
}

void xrMemory::_destroy()
{
	if (!mem_initialized)
		return;

	xr_delete(g_pSharedMemoryContainer);
	xr_delete(g_pStringContainer);
	shared_str_initialized = false;
	mem_initialized = FALSE;
}

void xrMemory::mem_compact()
{
	if (g_pStringContainer)
		g_pStringContainer->clean();
	if (g_pSharedMemoryContainer)
		g_pSharedMemoryContainer->clean();
}

void* xrMemory::mem_alloc(size_t size)
{
	++stat_calls;
	return allocate_aligned(size);
}

void* xrMemory::mem_realloc(void* ptr, size_t size)
{
	++stat_calls;
	if (!ptr)
		return allocate_aligned(size);
	allocation_header* old_header = reinterpret_cast<allocation_header*>(ptr) - 1;
	const size_t old_size = old_header->size;
	void* result = allocate_aligned(size);
	memcpy(result, ptr, old_size < (size ? size : 1) ? old_size : (size ? size : 1));
	mem_free(ptr);
	return result;
}

void xrMemory::mem_free(void* ptr)
{
	if (!ptr)
		return;
	++stat_calls;
	allocation_header* header = reinterpret_cast<allocation_header*>(ptr) - 1;
	free(header->raw);
}

u32 xrMemory::mem_usage(u32*, u32*)
{
	return 0;
}

char* xr_strdup(const char* string)
{
	if (!string)
		return NULL;
	const size_t length = strlen(string) + 1;
	char* result = static_cast<char*>(Memory.mem_alloc(length));
	memcpy(result, string, length);
	return result;
}
