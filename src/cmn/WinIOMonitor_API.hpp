﻿#ifndef HDR_WINIOMONITOR_API
#define HDR_WINIOMONITOR_API

#if defined(USE_ON_KERNEL)
#include "fltBase.hpp"
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winternl.h>
#include <fltUser.h>
#include <fltUserStructures.h>
#endif

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

//typedef BOOL( *MessageCallbackRoutine )( IN TyMessageSendPacket* pSendMessage, IN OUT TyMessageReplyPacket* pReplyMessage );
//typedef VOID( *DisconnectCallbackRoutine )( );

DWORD SetTimeOut( __in DWORD TimeOutMs );
DWORD ConnectTo();
void Disconnect();

DWORD StartFiltering();
DWORD StopFiltering();

DWORD SetFilterType( __in DWORD FilterType );

DWORD RegisterCallback();

DWORD AddGlobalFilter( const wchar_t* wszFilterMask, bool isInclude );
DWORD RemoveGlobalFilter( const wchar_t* wszFilterMask );

#endif // HDR_WINIOMONITOR_API