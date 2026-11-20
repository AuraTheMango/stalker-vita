#ifndef TYPES_H
#define TYPES_H

#if defined(__vita__)

#ifndef VERIFY
#define VERIFY(expr) do {} while (0)
#endif

#include <math.h>
#include <sys/syslimits.h>
#include <string.h>
#ifndef CopyMemory
#define CopyMemory(destination, source, length) memcpy((destination), (source), (length))
#endif
#include <strings.h>
#include <limits>
#include <ctype.h>

#ifndef _copysign
#define _copysign copysign
#endif

#ifndef _MAX_PATH
#define _MAX_PATH PATH_MAX
#endif

#ifndef IC
#define IC inline
#endif

#ifndef ICF
#define ICF inline
#endif

#ifndef ICN
#define ICN __attribute__((noinline))
#endif

#endif

// Type defs
typedef	signed		char	s8;
typedef	unsigned	char	u8;

typedef	signed		short	s16;
typedef	unsigned	short	u16;

typedef	signed		int		s32;
typedef	unsigned	int		u32;
                                         
#if defined(__vita__)
typedef signed long long s64;
typedef unsigned long long u64;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef long LONG;
typedef long HRESULT;
typedef void* HANDLE;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char TCHAR;
typedef const char* LPCTSTR;
typedef union LARGE_INTEGER {
	struct {
		u32 LowPart;
		s32 HighPart;
	};
	s64 QuadPart;
} LARGE_INTEGER;
typedef LARGE_INTEGER* PLARGE_INTEGER;
#else
typedef signed __int64 s64;
typedef unsigned __int64 u64;
#endif

typedef float				f32;
typedef double				f64;

typedef char*				pstr;
typedef const char*			pcstr;

// windoze stuff
#ifndef _WINDOWS_
	typedef	int				BOOL;
	typedef pstr			LPSTR;
	typedef pcstr			LPCSTR;
	#define TRUE			true
	#define FALSE			false
#endif

// Type limits
#define type_max(T)		(std::numeric_limits<T>::max())
#define type_min(T)		(-std::numeric_limits<T>::max())
#define type_zero(T)	(std::numeric_limits<T>::min())
#define type_epsilon(T)	(std::numeric_limits<T>::epsilon())

#define int_max			type_max(int)
#define int_min			type_min(int)
#define int_zero		type_zero(int)

#define flt_max			type_max(float)
#define flt_min			type_min(float)
//#define FLT_MAX         3.402823466e+38F        /* max value */
//#define FLT_MIN         1.175494351e-38F        /* min positive value */
#define FLT_MAX			flt_max
#define FLT_MIN			flt_min

#define flt_zero		type_zero(float)
#define flt_eps			type_epsilon(float)

#define dbl_max			type_max(double)
#define dbl_min			type_min(double)
#define dbl_zero		type_zero(double)
#define dbl_eps			type_epsilon(double)

typedef	char	string16	[16];
typedef	char	string32	[32];
typedef	char	string64	[64];
typedef	char	string128	[128];
typedef	char	string256	[256];
typedef	char	string512	[512];
typedef	char	string1024	[1024];
typedef	char	string2048	[2048];
typedef	char	string4096	[4096];

typedef	char	string_path	[2*_MAX_PATH];

#endif
