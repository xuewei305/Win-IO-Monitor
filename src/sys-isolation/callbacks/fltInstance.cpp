﻿#include "fltInstance.hpp"

#include "fltCmnLibs.hpp"

#include "utilities/contextMgr.hpp"
#include "utilities/contextMgr_Defs.hpp"
#include "utilities/osInfoMgr.hpp"

#include "utilities/volumeMgr.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

NTSTATUS FLTAPI InstanceSetup( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, ULONG VolumeDeviceType,
                               FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if( VolumeFilesystemType == FLT_FSTYPE_RAW )
        {
            // 지원하지 않는 파일시스템이므로 필터를 해당 파일시스템에 붙이지 않는다
            Status = STATUS_FLT_DO_NOT_ATTACH;
            break;
        }

        Status = CreateInstanceContext( FltObjects, Flags, VolumeDeviceType, VolumeFilesystemType );

    } while( false );

    return Status;
}

NTSTATUS FLTAPI InstanceQueryTeardown( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags )
{
    KdPrint( ( "[WinIOSol] >> %s|IRQL=%d|Thread=%p|Instance=%p|Flags=%d\n", __FUNCTION__, KeGetCurrentIrql(), PsGetCurrentThread(), FltObjects->Instance, Flags ) );

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    return STATUS_SUCCESS;
}

void FLTAPI InstanceTeardownStart( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags )
{
    KdPrint( ( "[WinIOSol] >> %s|IRQL=%d|Thread=%p|Instance=%p|Flags=%d\n", __FUNCTION__, KeGetCurrentIrql(), PsGetCurrentThread(), FltObjects->Instance, Flags ) );

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    if( Flags == FLTFL_INSTANCE_TEARDOWN_INTERNAL_ERROR )
        return;

    // TODO: Close Instance's Own FileObject
    // TODO: Call FltCancelIO any I/O
    CTX_INSTANCE_CONTEXT* CtxInstanceContext = NULLPTR;
    NTSTATUS Status = CtxGetContext( FltObjects, NULLPTR, FLT_INSTANCE_CONTEXT, ( PVOID* )&CtxInstanceContext );

    if( NT_SUCCESS( Status ) )
    {
        VolumeMgr_Remove( CtxInstanceContext->DeviceNameBuffer );
        CtxReleaseContext( CtxInstanceContext );
    }
}

void FLTAPI InstanceTeardownComplete( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags )
{
    KdPrint( ( "[WinIOSol] >> %s|IRQL=%d|Thread=%p|Instance=%p|Flags=%d\n", __FUNCTION__, KeGetCurrentIrql(), PsGetCurrentThread(), FltObjects->Instance, Flags ) );

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
}

///////////////////////////////////////////////////////////////////////////////

NTSTATUS CreateInstanceContext( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags,
                                ULONG VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    CTX_INSTANCE_CONTEXT* InstanceContext = NULLPTR;

    do
    {
        Status = CtxAllocateContext( FltObjects->Filter, FLT_INSTANCE_CONTEXT, ( PFLT_CONTEXT* )&InstanceContext );
        if( !NT_SUCCESS( Status ) )
        {
            KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "[WinIOSol] %s %s Status=0x%08x,%s\n",
                         __FUNCTION__, "CtxAllocateContext FAILED", Status, ntkernel_error_category::find_ntstatus( Status )->message ) );
            break;
        }

        Status = CtxSetContext( FltObjects, NULLPTR, FLT_INSTANCE_CONTEXT, InstanceContext, NULLPTR );
        if( !NT_SUCCESS( Status ) )
        {
            KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "[WinIOSol] %s %s Status=0x%08x InstanceContext=%p\n",
                         __FUNCTION__, "FltSetInstanceContext FAILED", Status, InstanceContext ) );
            break;
        }

        InstanceContext->Filter = FltObjects->Filter;
        InstanceContext->Instance = FltObjects->Instance;
        InstanceContext->Volume = FltObjects->Volume;

        RtlInitEmptyUnicodeString( &InstanceContext->DeviceName, InstanceContext->DeviceNameBuffer,
                                   sizeof( WCHAR ) * _countof( InstanceContext->DeviceNameBuffer ) );

        Status = FltGetVolumeName( FltObjects->Volume, &InstanceContext->DeviceName, NULL );
        if( !NT_SUCCESS( Status ) )
        {
            KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "[WinIOSol] %s %s Status=0x%08x\n",
                         __FUNCTION__, "FltGetVolumeName FAILED", Status ) );
            break;
        }

        nsUtils::FindDriveLetterByDeviceName( &InstanceContext->DeviceName, &InstanceContext->DriveLetter );
        InstanceContext->DeviceNameCch = nsUtils::strlength( InstanceContext->DeviceNameBuffer );

        InstanceContext->VolumeFileSystemType = VolumeFilesystemType;
        RtlInitEmptyUnicodeString( &InstanceContext->VolumeGUIDName, InstanceContext->VolumeGUIDNameBuffer, sizeof( WCHAR ) * _countof( InstanceContext->VolumeGUIDNameBuffer ) );

        if( nsUtils::VerifyVersionInfoEx( 6, ">=" ) == true )
        {
            // On Windows Vista and later, a minifilter driver can safely call FltGetVolumeGuidName from its InstanceSetupCallback routine ( PFLT_INSTANCE_SETUP_CALLBACK) because the callback is called on the first I/O operation for a new volume after all of the mount processing is completed.
            // On Windows operating systems earlier than Windows Vista, FltGetVolumeGuidName cannot safely be called from an InstanceSetupCallback routine because the mount manager might issue a file I/O operation while holding a lock, which can cause a deadlock.

            FltGetVolumeGuidName( FltObjects->Volume, &InstanceContext->VolumeGUIDName, NULL );
        }

        nsW32API::IsVolumeWritable( FltObjects->Volume, &InstanceContext->IsWritable );

        ExInitializeResourceLite( &InstanceContext->VcbLock );
        InitializeListHead( &InstanceContext->FcbListHead );

        KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[WinIOSol] %s DeviceName=%wZ DriveLetter=%lc\n",
                     __FUNCTION__, &InstanceContext->DeviceName, InstanceContext->DriveLetter == L'\0' ? L' ' : InstanceContext->DriveLetter ) );

        if( InstanceContext->DriveLetter != L'\0' )
            VolumeMgr_Add( InstanceContext->DeviceNameBuffer, InstanceContext->DriveLetter, InstanceContext );

    } while( false );

    CtxReleaseContext( InstanceContext );

    return Status;
}
