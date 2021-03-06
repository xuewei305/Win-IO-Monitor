﻿#include "WinIOMonitor.hpp"

#include "deviceCntl.hpp"
#include "deviceMgmt.hpp"
#include "notifyMgr.hpp"
#include "WinIOMonitor_Filter.hpp"
#include "policies/processFilter.hpp"
#include "utilities/bufferMgr.hpp"

#include "utilities/osInfoMgr.hpp"
#include "utilities/contextMgr.hpp"
#include "utilities/procNameMgr.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

NTSTATUS FLTAPI DriverEntry( PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s DriverObject=%p|RegistryPath=%wZ\n",
                 __FUNCTION__, DriverObject, RegistryPath ) );

    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        nsUtils::InitializeOSInfo();
        nsW32API::InitializeNtOsKrnlAPI( &nsW32API::NtOsKrnlAPIMgr );

        PFAST_IO_DISPATCH FastIODispatch = ( PFAST_IO_DISPATCH )ExAllocatePoolWithTag( NonPagedPool, sizeof( FAST_IO_DISPATCH ), 'abcd' );
        if( FastIODispatch == NULLPTR )
        {
            KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "[WinIOMon] %s %s\n", __FUNCTION__, "ExAllocatePoolWithTag FAILED" ) );
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        IF_FALSE_BREAK( Status, InitializeGlobalContext( DriverObject ) );
        IF_FALSE_BREAK( Status, InitializeFeatures( &GlobalContext ) );
        IF_FALSE_BREAK( Status, InitializeMiniFilter( &GlobalContext ) );

        DriverObject->MajorFunction[ IRP_MJ_CREATE ]    = DeviceCreate;
        DriverObject->MajorFunction[ IRP_MJ_CLEANUP ]   = DeviceCleanup;
        DriverObject->MajorFunction[ IRP_MJ_CLOSE ]     = DeviceClose;

        DriverObject->DriverUnload                      = DriverUnload;

        RtlZeroMemory( FastIODispatch, sizeof( FAST_IO_DISPATCH ) );
        FastIODispatch->SizeOfFastIoDispatch = sizeof( FAST_IO_DISPATCH );
        FastIODispatch->FastIoDeviceControl = FastIoDeviceControl;
        DriverObject->FastIoDispatch = FastIODispatch;

        Status = STATUS_SUCCESS;

    } while( false );

    return Status;
}

void DriverUnload( PDRIVER_OBJECT DriverObject )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s DriverObject=%p\n",
                 __FUNCTION__, DriverObject ) );

    /*!
        1. wehn invoke sc stop, only call DriverUnload
        2. when invoke fltmc unload, first call FilterUnloadcallback then call DriverUnload
    */
    if( GlobalContext.Filter != NULLPTR )
        MiniFilterUnload( 0 );

    CloseNotifyMgr();
    StopProcessNotify();
    CloseProcessFilter();

    RemoveControlDevice( GlobalContext );

    ExDeleteNPagedLookasideList( &GlobalContext.FileNameLookasideList );
    ExDeleteNPagedLookasideList( &GlobalContext.ProcNameLookasideList );
    ExDeleteNPagedLookasideList( &GlobalContext.SendPacketLookasideList );
    ExDeleteNPagedLookasideList( &GlobalContext.ReplyPacketLookasideList );

    if( DriverObject->FastIoDispatch != NULLPTR )
        ExFreePool( DriverObject->FastIoDispatch );
}

NTSTATUS InitializeGlobalContext( PDRIVER_OBJECT DriverObject )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        RtlZeroMemory( &GlobalContext, sizeof( CTX_GLOBAL_DATA ) );
        GlobalContext.DriverObject = DriverObject;
        GlobalContext.TimeOutMs.QuadPart = RELATIVE( MILLISECONDS( 3000 ) );

        IF_FALSE_BREAK( Status, CreateControlDevice( GlobalContext ) );

        ExInitializeNPagedLookasideList( &GlobalContext.FileNameLookasideList,
                                         NULL, NULL, 0,
                                         POOL_FILENAME_SIZE, POOL_FILENAME_TAG, 0 );

        ExInitializeNPagedLookasideList( &GlobalContext.ProcNameLookasideList,
                                         NULL, NULL, 0,
                                         POOL_PROCNAME_SIZE, POOL_PROCNAME_TAG, 0 );

        ExInitializeNPagedLookasideList( &GlobalContext.SendPacketLookasideList,
                                         NULL, NULL, 0,
                                         POOL_MSG_SEND_SIZE, POOL_MSG_SEND_TAG, 0 );

        ExInitializeNPagedLookasideList( &GlobalContext.ReplyPacketLookasideList,
                                         NULL, NULL, 0,
                                         POOL_MSG_REPLY_SIZE, POOL_MSG_REPLY_TAG, 0 );

        AllocateBuffer<WCHAR>( BUFFER_FILENAME );

        Status = STATUS_SUCCESS;

    } while( false );

    KdPrint( ( "[WinIOMon] %s Line=%d Status=0x%08x\n", __FUNCTION__, __LINE__, Status ) );
    return Status;
}

NTSTATUS InitializeFeatures( CTX_GLOBAL_DATA* GlobalContext )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        IF_FALSE_BREAK( Status, InitializeNotifyMgr() );
        IF_FALSE_BREAK( Status, InitializeProcessFilter() );
        IF_FALSE_BREAK( Status, StartProcessNotify() );
        
    } while( false );

    KdPrint( ( "[WinIOMon] %s Line=%d Status=0x%08x\n", __FUNCTION__, __LINE__, Status ) );
    return Status;
}
