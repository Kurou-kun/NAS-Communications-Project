#pragma once
#include <windows.h>
#include <string>
#include "RainmeterAPI.h"

enum class DebugLevel : int
{
    ErrorsOnly = 0,
    Trace      = 1,
    DeviceList = 2
};

void NASCP_Log(void* rm, int level, const wchar_t* func, int line, const wchar_t* msg);
void NASCP_LogDevice(void* rm, const wchar_t* func, int line, const wchar_t* msg);

#define DEBUG_INFO(rm, debugLevel, msg)                                         \
    do {                                                                        \
        if ((debugLevel) >= static_cast<int>(DebugLevel::Trace))                \
            NASCP_Log((rm), LOG_NOTICE, __FUNCTIONW__, __LINE__, (msg));        \
    } while(0)

#define DEBUG_ERROR(rm, debugLevel, msg)                                        \
    do {                                                                        \
        if ((debugLevel) >= static_cast<int>(DebugLevel::ErrorsOnly))           \
            NASCP_Log((rm), LOG_ERROR, __FUNCTIONW__, __LINE__, (msg));         \
    } while(0)

#define DEBUG_DEVICE(rm, debugLevel, msg)                                       \
    do {                                                                        \
        if ((debugLevel) >= static_cast<int>(DebugLevel::DeviceList))           \
            NASCP_LogDevice((rm), __FUNCTIONW__, __LINE__, (msg));              \
    } while(0)