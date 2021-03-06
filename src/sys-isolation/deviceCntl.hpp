﻿#ifndef HDR_WINIOMONITOR_DEVICE_CNTL
#define HDR_WINIOMONITOR_DEVICE_CNTL

#include "fltBase.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

EXTERN_C_BEGIN

BOOLEAN FLTAPI FastIoDeviceControl( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait,
                                    IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength,
                                    OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength,
                                    IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus,
                                    IN PDEVICE_OBJECT DeviceObject );

EXTERN_C_END

///////////////////////////////////////////////////////////////////////////////
// IOCTL Handler

BOOLEAN DevIOCntlSetDriverStatus( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlGetDriverStatus( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );

BOOLEAN DevIOCntlSetStubCode( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlGetStubCode( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );

BOOLEAN DevIOCntlAddGlobalPolicy( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlDelGlobalPolicy( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlGetGlobalPolicyCnt( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlRstGlobalPolicy( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );

BOOLEAN DevIOCntlAddProcessPolicy( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );
BOOLEAN DevIOCntlDelProcessPolicyID( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );
BOOLEAN DevIOCntlDelProcessPolicyPID( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );
BOOLEAN DevIOCntlDelProcessPolicyMask( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );
BOOLEAN DevIOCntlAddProcessPolicyItem( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );
BOOLEAN DevIOCntlDelProcessPolicyItem( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );
BOOLEAN DevIOCntlRstProcessPolicy( IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, OUT PIO_STATUS_BLOCK IoStatus );

BOOLEAN DevIOCntlFileGetType( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlFileSetSolutionData( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
BOOLEAN DevIOCntlFileGetSolutionData( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait, IN PVOID InputBuffer OPTIONAL, IN ULONG InputBufferLength, OUT PVOID OutputBuffer OPTIONAL, IN ULONG OutputBufferLength, IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );

#endif // HDR_WINIOMONITOR_DEVICE_CNTL