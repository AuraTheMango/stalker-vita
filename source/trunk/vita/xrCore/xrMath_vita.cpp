#include "stdafx.h"
#pragma hdrstop

#include "_compressed_normal.h"
#include <psp2/kernel/processmgr.h>

XRCORE_API Fmatrix Fidentity;
XRCORE_API Dmatrix Didentity;
XRCORE_API CRandom Random;

namespace FPU
{
void m24() {}
void m24r() {}
void m53() {}
void m53r() {}
void m64() {}
void m64r() {}
}

namespace CPU
{
XRCORE_API u64 clk_per_second = 1000000;
XRCORE_API u64 clk_per_milisec = 1000;
XRCORE_API u64 clk_per_microsec = 1;
XRCORE_API u64 clk_overhead = 0;
XRCORE_API float clk_to_seconds = 0.000001f;
XRCORE_API float clk_to_milisec = 0.001f;
XRCORE_API float clk_to_microsec = 1.0f;
XRCORE_API u64 qpc_freq = 1000000;
XRCORE_API u64 qpc_overhead = 0;
XRCORE_API u32 qpc_counter = 0;
XRCORE_API _processor_info ID;

XRCORE_API u64 QPC()
{
	++qpc_counter;
	return static_cast<u64>(sceKernelGetProcessTimeWide());
}

void Detect()
{
	if (!_cpuid(&ID))
		abort();
}
}

void _initialize_cpu()
{
	Msg("* Detected CPU: %s %s", CPU::ID.v_name, CPU::ID.model_name);
	Msg("* CPU Features: ARM");
	Fidentity.identity();
	Didentity.identity();
	pvInitializeStatics();
	Random.seed(static_cast<s32>(CPU::QPC()));
	_initialize_cpu_thread();
}
