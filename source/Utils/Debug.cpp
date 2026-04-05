#include <winsock2.h>
#include <windows.h>
#include "Debug.h"
#include <sstream>

static std::wstring Format(const wchar_t* func, int line, const wchar_t* msg)
{
    std::wostringstream ss;
    ss << L"[NASCP] (" << func << L") line " << line << L": " << msg;
    return ss.str();
}

void NASCP_Log(void* rm, int level, const wchar_t* func, int line, const wchar_t* msg)
{
    std::wstring out = Format(func, line, msg);
    RmLog(rm, (LOGLEVEL)level, out.c_str());
}

void NASCP_LogDevice(void* rm, const wchar_t* func, int line, const wchar_t* msg)
{
    std::wstring out = Format(func, line, msg);
    RmLog(rm, LOG_NOTICE, out.c_str());
}