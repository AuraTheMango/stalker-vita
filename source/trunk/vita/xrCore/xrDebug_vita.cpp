#include "stdafx.h"
#pragma hdrstop

#include "xrDebug.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>

XRCORE_API xrDebug Debug;

void xrDebug::_initialize(const bool&)
{
	handler = NULL;
	m_on_dialog = NULL;
}

void xrDebug::_destroy() {}

LPCSTR xrDebug::error2string(long)
{
	return "Vita system error";
}

void xrDebug::backend(const char* reason, const char* expression,
	const char* argument0, const char* argument1, const char* file,
	int line, const char* function, bool&)
{
	Msg("*** XRAY ERROR *** %s", reason ? reason : "error");
	if (expression) Msg("Expression: %s", expression);
	if (argument0) Msg("Argument: %s", argument0);
	if (argument1) Msg("Argument: %s", argument1);
	if (file) Msg("Location: %s:%d (%s)", file, line, function ? function : "");
	FlushLog();
	if (handler)
		handler();
	abort();
}

void xrDebug::fail(const char* e1, const char* file, int line,
	const char* function, bool& ignore_always)
{
	backend("assertion failed", e1, NULL, NULL, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const std::string& e2, const char* file,
	int line, const char* function, bool& ignore_always)
{
	backend(e1, e2.c_str(), NULL, NULL, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* file,
	int line, const char* function, bool& ignore_always)
{
	backend(e1, e2, NULL, NULL, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3,
	const char* file, int line, const char* function, bool& ignore_always)
{
	backend(e1, e2, e3, NULL, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3,
	const char* e4, const char* file, int line, const char* function,
	bool& ignore_always)
{
	backend(e1, e2, e3, e4, file, line, function, ignore_always);
}

void xrDebug::error(long code, const char* expression, const char* file,
	int line, const char* function, bool& ignore_always)
{
	backend(error2string(code), expression, NULL, NULL, file, line, function, ignore_always);
}

void xrDebug::error(long code, const char* expression, const char* argument,
	const char* file, int line, const char* function, bool& ignore_always)
{
	backend(error2string(code), expression, argument, NULL, file, line, function, ignore_always);
}

void __cdecl xrDebug::fatal(const char* file, int line, const char* function,
	const char* format, ...)
{
	char message[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	bool ignore_always = false;
	backend("fatal error", message, NULL, NULL, file, line, function, ignore_always);
}

void xrDebug::do_exit(const std::string& message)
{
	Msg("%s", message.c_str());
	FlushLog();
	exit(1);
}

void LogStackTrace(LPCSTR header)
{
	if (header)
		Msg("%s", header);
}
