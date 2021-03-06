﻿#include "irpContext.hpp"

#include "privateFCBMgr.hpp"

#include "utilities/procNameMgr.hpp"
#include "utilities/contextMgr.hpp"
#include "utilities/contextMgr_Defs.hpp"
#include "utilities/fltUtilities.hpp"
#include "utilities/volumeMgr.hpp"

#include "policies/GlobalFilter.hpp"
#include "policies/ProcessFilter.hpp"

#include "fltCmnLibs.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

LONG volatile GlobalEvtID;

///////////////////////////////////////////////////////////////////////////////

LONG CreateEvtID()
{
    return InterlockedIncrement( &GlobalEvtID );
}

PIRP_CONTEXT CreateIrpContext( __in PFLT_CALLBACK_DATA Data, __in PCFLT_RELATED_OBJECTS FltObjects )
{
    NTSTATUS                Status = STATUS_SUCCESS;
    PIRP_CONTEXT            IrpContext = NULLPTR;
    const auto&             MajorFunction = Data->Iopb->MajorFunction;
    auto                    IsPreIO = BooleanFlagOn( Data->Flags, FLTFL_CALLBACK_DATA_POST_OPERATION ) == FALSE;
    CTX_INSTANCE_CONTEXT*   InstanceContext = NULLPTR;
    PFLT_FILE_NAME_INFORMATION DestinationFileName = NULLPTR;

    __try
    {
        IrpContext = ( PIRP_CONTEXT )ExAllocateFromNPagedLookasideList( &GlobalContext.IrpContextLookasideList );
        if( IrpContext == NULLPTR )
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            __leave;
        }

        RtlZeroMemory( IrpContext, sizeof( IRP_CONTEXT ) );

        IrpContext->EvtID           = CreateEvtID();
        IrpContext->DebugText       = ( CHAR* )ExAllocateFromNPagedLookasideList( &GlobalContext.DebugLookasideList );
        RtlZeroMemory( IrpContext->DebugText, DEBUG_TEXT_SIZE * sizeof( CHAR ) );

        IrpContext->Data            = Data;
        IrpContext->FltObjects      = FltObjects;
        IrpContext->IsOwnObject     = IsOwnFileObject( FltObjects->FileObject );

        if( IrpContext->IsOwnObject == true )
        {
            IrpContext->Fcb = ( FCB* )FltObjects->FileObject->FsContext;
            IrpContext->Ccb = ( CCB* )FltObjects->FileObject->FsContext2;

            if( IrpContext->Ccb != NULLPTR )
            {
                IrpContext->ProcessId = IrpContext->Ccb->ProcessId;
                IrpContext->ProcessFullPath = CloneBuffer( &IrpContext->Ccb->ProcessFileFullPath );
                IrpContext->ProcessFileName = nsUtils::ReverseFindW( IrpContext->ProcessFullPath.Buffer, L'\\' );
                if( IrpContext->ProcessFileName != NULLPTR )
                    IrpContext->ProcessFileName++;
            }

            CtxGetContext( FltObjects, IrpContext->Fcb->LowerFileObject, FLT_STREAM_CONTEXT, ( PFLT_CONTEXT* )&IrpContext->StreamContext );

            InstanceContext = IrpContext->Fcb->InstanceContext;
            FltReferenceContext( InstanceContext );

            if( IrpContext->Fcb->MetaDataInfo != NULLPTR )
            {
                if( IrpContext->Fcb->MetaDataInfo->MetaData.Type != METADATA_STB_TYPE )
                    IrpContext->SrcFileFullPath = CloneBuffer( &IrpContext->Fcb->FileFullPath );
                else
                {
                    if( IrpContext->Fcb->PretendFileFullPath.Buffer != NULLPTR )
                        IrpContext->SrcFileFullPath = CloneBuffer( &IrpContext->Fcb->PretendFileFullPath );
                    else
                        IrpContext->SrcFileFullPath = CloneBuffer( &IrpContext->Fcb->FileFullPath );
                }
            }
        }
        else
        {
            if( MajorFunction != IRP_MJ_CREATE )
                CtxGetContext( FltObjects, FltObjects->FileObject, FLT_STREAM_CONTEXT, (PFLT_CONTEXT*)&IrpContext->StreamContext );
        }

        if( InstanceContext != NULLPTR )
        {
            Status = CtxGetContext( FltObjects, NULLPTR, FLT_INSTANCE_CONTEXT, ( PFLT_CONTEXT* )&InstanceContext );
            if( !NT_SUCCESS( Status ) || InstanceContext == NULLPTR )
            {
                Status = STATUS_INTERNAL_ERROR;
                __leave;
            }
        }

        IrpContext->InstanceContext = InstanceContext;
        FltReferenceContext( IrpContext->InstanceContext );

        if( IrpContext->ProcessFullPath.Buffer == NULLPTR )
        {
            IrpContext->ProcessId = FltGetRequestorProcessId( Data );
            SearchProcessInfo( IrpContext->ProcessId, &IrpContext->ProcessFullPath, &IrpContext->ProcessFileName );
        }

        if( IrpContext->SrcFileFullPath.Buffer == NULLPTR )
        {
            if( MajorFunction == IRP_MJ_CREATE && FlagOn( IrpContext->Data->Iopb->Parameters.Create.Options, FILE_OPEN_BY_FILE_ID ) )
            {
                PFLT_FILE_NAME_INFORMATION fni = NULLPTR;

                do
                {
                    // FltGetFileNameInformation return volume path not driver letter
                    auto Ret = FltGetFileNameInformation( Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT, &fni );

                    if( !NT_SUCCESS( Ret ) )
                    {
                        KdPrint( ( "[WinIOSol] >> EvtID=%09d %s %s Status=0x%08x,%s\n"
                                   , IrpContext->EvtID, __FUNCTION__, "FltGetFileNameInformation(FILE_OPEN_BY_FILE_ID) FAILED"
                                   , Ret, ntkernel_error_category::find_ntstatus( Ret )->message
                                   ) );
                        break;
                    }

                    IrpContext->SrcFileFullPath = AllocateBuffer<WCHAR>( BUFFER_FILENAME, fni->Name.MaximumLength );
                    if( InstanceContext->DriveLetter != L'\0' )
                    {
                        IrpContext->SrcFileFullPath.Buffer[ 0 ] = InstanceContext->DriveLetter;
                        IrpContext->SrcFileFullPath.Buffer[ 1 ] = L':';

                        auto DeviceNameLength = nsUtils::strlength( InstanceContext->DeviceNameBuffer ) * sizeof( WCHAR );
                        RtlStringCbCatW( &IrpContext->SrcFileFullPath.Buffer[ 2 ],
                                         fni->Name.Length - ( DeviceNameLength * sizeof( WCHAR ) ) + sizeof( WCHAR ),
                                         &fni->Name.Buffer[ DeviceNameLength ] );
                    }

                } while( false );

                if( fni != NULLPTR )
                    FltReleaseFileNameInformation( fni );
            }
            else
            {
                IrpContext->SrcFileFullPath = nsUtils::ExtractFileFullPath( FltObjects->FileObject, InstanceContext,
                                                                            ( MajorFunction == IRP_MJ_CREATE ) && ( IsPreIO == true ) );
            }
        }

        IrpContext->UserBuffer = FltMapUserBuffer( Data );

        switch( MajorFunction )
        {
            case IRP_MJ_SET_INFORMATION: {
                if( IsPreIO == false )
                    break;

                const auto& FileInformationClass = ( nsW32API::FILE_INFORMATION_CLASS )Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
                switch( FileInformationClass )
                {
                    case nsW32API::FileRenameInformation:
                    case nsW32API::FileRenameInformationEx: {

                        if( FileInformationClass == nsW32API::FileRenameInformation )
                        {
                            auto InfoBuffer = ( nsW32API::FILE_RENAME_INFORMATION* )( Data->Iopb->Parameters.SetFileInformation.InfoBuffer );
                            Status = FltGetDestinationFileNameInformation( FltObjects->Instance, FltObjects->FileObject, InfoBuffer->RootDirectory,
                                                                           InfoBuffer->FileName, InfoBuffer->FileNameLength,
                                                                           FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                                                           &DestinationFileName
                            );
                        }
                        else if( FileInformationClass == nsW32API::FileRenameInformationEx )
                        {
                            auto InfoBuffer = ( nsW32API::FILE_RENAME_INFORMATION_EX* )( Data->Iopb->Parameters.SetFileInformation.InfoBuffer );
                            Status = FltGetDestinationFileNameInformation( FltObjects->Instance, FltObjects->FileObject, InfoBuffer->RootDirectory,
                                                                           InfoBuffer->FileName, InfoBuffer->FileNameLength,
                                                                           FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                                                           &DestinationFileName
                            );
                        }

                        if( !NT_SUCCESS( Status ) )
                            break;

                        IrpContext->DstFileFullPath = AllocateBuffer<WCHAR>( BUFFER_FILENAME, DestinationFileName->Name.Length + sizeof( WCHAR ) +
                                                                             ( CONTAINOR_SUFFIX_MAX * sizeof( WCHAR ) ) );
                        if( IrpContext->DstFileFullPath.Buffer == NULLPTR )
                        {
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                            break;
                        }

                        RtlStringCbCopyNW( IrpContext->DstFileFullPath.Buffer, IrpContext->DstFileFullPath.BufferSize, DestinationFileName->Name.Buffer, DestinationFileName->Name.Length );
                        VolumeMgr_Replace( IrpContext->DstFileFullPath.Buffer, IrpContext->DstFileFullPath.BufferSize );

                    } break;

                    case nsW32API::FileLinkInformation:
                    case nsW32API::FileLinkInformationEx: {

                        if( FileInformationClass == nsW32API::FileLinkInformation )
                        {
                            auto InfoBuffer = ( FILE_LINK_INFORMATION* )( Data->Iopb->Parameters.SetFileInformation.InfoBuffer );
                            Status = FltGetDestinationFileNameInformation( FltObjects->Instance, FltObjects->FileObject, InfoBuffer->RootDirectory,
                                                                           InfoBuffer->FileName, InfoBuffer->FileNameLength,
                                                                           FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                                                           &DestinationFileName
                            );
                        }
                        else if( FileInformationClass == nsW32API::FileLinkInformationEx )
                        {
                            auto InfoBuffer = ( nsW32API::FILE_LINK_INFORMATION_EX* )( Data->Iopb->Parameters.SetFileInformation.InfoBuffer );
                            Status = FltGetDestinationFileNameInformation( FltObjects->Instance, FltObjects->FileObject, InfoBuffer->RootDirectory,
                                                                           InfoBuffer->FileName, InfoBuffer->FileNameLength,
                                                                           FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                                                           &DestinationFileName
                            );
                        }

                        if( !NT_SUCCESS( Status ) )
                            break;

                        IrpContext->DstFileFullPath = AllocateBuffer<WCHAR>( BUFFER_FILENAME, DestinationFileName->Name.Length + sizeof( WCHAR ) +
                                                                             ( CONTAINOR_SUFFIX_MAX * sizeof( WCHAR ) ) );
                        if( IrpContext->DstFileFullPath.Buffer == NULLPTR )
                        {
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                            break;
                        }

                        RtlStringCbCopyNW( IrpContext->DstFileFullPath.Buffer, IrpContext->DstFileFullPath.BufferSize, DestinationFileName->Name.Buffer, DestinationFileName->Name.Length );
                        VolumeMgr_Replace( IrpContext->DstFileFullPath.Buffer, IrpContext->DstFileFullPath.BufferSize );

                    } break;
                } // switch FileInformationClass

            } break;
        } // switch MajorFunction

        if( IrpContext->SrcFileFullPath.Buffer != NULLPTR )
        {
            if( IrpContext->SrcFileFullPathWOVolume == NULLPTR )
            {
                if( IrpContext->SrcFileFullPath.Buffer[ 1 ] == L':' )
                    IrpContext->SrcFileFullPathWOVolume = &IrpContext->SrcFileFullPath.Buffer[ 2 ];
                else
                {
                    auto cchDeviceName = nsUtils::strlength( InstanceContext->DeviceNameBuffer );
                    if( cchDeviceName > 0 )
                        IrpContext->SrcFileFullPathWOVolume = &IrpContext->SrcFileFullPath.Buffer[ cchDeviceName ];
                }

                if( IrpContext->SrcFileFullPathWOVolume == NULLPTR )
                    IrpContext->SrcFileFullPathWOVolume = IrpContext->SrcFileFullPath.Buffer;
            }

            if( IrpContext->SrcFileName == NULLPTR )
            {
                IrpContext->SrcFileName = nsUtils::ReverseFindW( IrpContext->SrcFileFullPath.Buffer, L'\\' );

                if( IrpContext->SrcFileName == NULLPTR )
                    IrpContext->SrcFileFullPath.Buffer;
                else
                    IrpContext->SrcFileName++;
            }
        }

        if( IrpContext->DstFileFullPath.Buffer != NULLPTR )
        {
            if( IrpContext->DstFileFullPathWOVolume == NULLPTR )
            {
                if( IrpContext->DstFileFullPath.Buffer[ 1 ] == L':' )
                    IrpContext->DstFileFullPathWOVolume = &IrpContext->DstFileFullPath.Buffer[ 2 ];
                else
                {
                    auto cchDeviceName = nsUtils::strlength( InstanceContext->DeviceNameBuffer );
                    if( cchDeviceName > 0 )
                        IrpContext->DstFileFullPathWOVolume = &IrpContext->DstFileFullPath.Buffer[ cchDeviceName ];
                }

                if( IrpContext->DstFileFullPathWOVolume == NULLPTR )
                    IrpContext->DstFileFullPathWOVolume = IrpContext->DstFileFullPath.Buffer;
            }

            if( IrpContext->DstFileName == NULLPTR )
            {
                IrpContext->DstFileName = nsUtils::ReverseFindW( IrpContext->DstFileFullPath.Buffer, L'\\' );

                if( IrpContext->DstFileName == NULLPTR )
                    IrpContext->DstFileFullPath.Buffer;
                else
                    IrpContext->DstFileName++;
            }
        }

        CheckEventTo( IrpContext );
    }
    __finally
    {
        CtxReleaseContext( InstanceContext );

        if( DestinationFileName != NULLPTR )
            FltReleaseFileNameInformation( DestinationFileName );
    }

    return IrpContext;
}

VOID CloseIrpContext( __in PIRP_CONTEXT IrpContext )
{
    if( IrpContext == NULLPTR )
        return;

    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IrpContext->Data->IoStatus.Status = IrpContext->Status;

    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IrpContext->Data->IoStatus.Information = IrpContext->Information;

    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_FREE_LOWER_FILEOBJECT ) )
    {
        
    }

    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_FREE_PGIO_RSRC ) )
        FltReleaseResource( &IrpContext->Fcb->PagingIoResource );

    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_FREE_MAIN_RSRC ) )
        FltReleaseResource( &IrpContext->Fcb->MainResource );

    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_FREE_INST_RSRC ) )
        FltReleaseResource( &IrpContext->InstanceContext->VcbLock );

    if( IrpContext->DebugText != NULLPTR )
        ExFreeToNPagedLookasideList( &GlobalContext.DebugLookasideList, IrpContext->DebugText );

    if( IrpContext->ProcessFilter != NULLPTR )
    {
        ProcessFilter_Close( IrpContext->ProcessFilter );
        IrpContext->ProcessFilter = NULLPTR;
        IrpContext->ProcessFilterEntry = NULLPTR;
    }

    DeallocateBuffer( &IrpContext->ProcessFullPath );
    DeallocateBuffer( &IrpContext->SrcFileFullPath );
    DeallocateBuffer( &IrpContext->DstFileFullPath );

    CtxReleaseContext( IrpContext->StreamContext );
    CtxReleaseContext( IrpContext->InstanceContext );

    ExFreeToNPagedLookasideList( &GlobalContext.IrpContextLookasideList, IrpContext );
}

VOID PrintIrpContext( __in PIRP_CONTEXT IrpContext, __in bool isForceResult /* = false */ )
{
    if( IrpContext == NULLPTR ) return;

    if( IrpContext->DebugText == NULLPTR )
    {
        IrpContext->DebugText = ( CHAR* )ExAllocateFromNPagedLookasideList( &GlobalContext.DebugLookasideList );
        RtlZeroMemory( IrpContext->DebugText, 1024 * sizeof( CHAR ) );
    }

    if( IrpContext->DebugText == NULLPTR )
        return;

    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;

    const auto& IsPreIO = BooleanFlagOn( Data->Flags, FLTFL_CALLBACK_DATA_POST_OPERATION ) == FALSE;
    bool IsResultMode = ( IsPreIO == FALSE ) || ( isForceResult == true );

    switch( MajorFunction )
    {
        case IRP_MJ_CREATE:                     { PrintIrpContextCREATE( IrpContext, IsResultMode ); } break;
        case IRP_MJ_READ:                       { PrintIrpContextREAD( IrpContext, IsResultMode ); } break;
        case IRP_MJ_WRITE:                      { PrintIrpContextWRITE( IrpContext, IsResultMode ); } break;
        case IRP_MJ_QUERY_INFORMATION:          { PrintIrpContextQUERY_INFORMATION( IrpContext, IsResultMode ); } break;
        case IRP_MJ_SET_INFORMATION:            { PrintIrpContextSET_INFORMATION( IrpContext, IsResultMode ); } break;
        case IRP_MJ_QUERY_SECURITY:             { PrintIrpContextQUERY_SECURITY( IrpContext, IsResultMode ); } break;
        case IRP_MJ_SET_SECURITY:               { PrintIrpContextSET_SECURITY( IrpContext, IsResultMode ); } break;
        case IRP_MJ_DIRECTORY_CONTROL:          { PrintIrpContextDIRECTORY_CONTROL( IrpContext, IsResultMode ); } break;
        case IRP_MJ_CLEANUP:                    { PrintIrpContextCLEANUP( IrpContext, IsResultMode ); } break;
        case IRP_MJ_CLOSE:                      { PrintIrpContextCLOSE( IrpContext, IsResultMode ); } break;

        case IRP_MJ_QUERY_VOLUME_INFORMATION:   { PrintIrpContextQUERY_VOLUME_INFORMATION( IrpContext, IsResultMode ); } break;
        case IRP_MJ_FILE_SYSTEM_CONTROL:        { PrintIrpContextFILE_SYSTEM_CONTROL( IrpContext, IsResultMode ); } break;
        case IRP_MJ_LOCK_CONTROL:               { PrintIrpContextLOCK_CONTROL( IrpContext, IsResultMode ); } break;

        default: {
            KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                       , IsResultMode == false ? ">>" : "<<"
                       , IrpContext->EvtID
                       , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                       , PsGetCurrentThread()
                       , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                       , IrpContext->SrcFileFullPath.Buffer
                       ) );
        } break;
    }
}

void PrintIrpContextCREATE( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    const auto& Parameters = Data->Iopb->Parameters.Create;
    auto SecurityContext = Parameters.SecurityContext;
    auto CreateOptions = Parameters.Options & 0x00FFFFFF;
    auto CreateDisposition = ( Parameters.Options >> 24 ) & 0x000000ff;

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        RtlStringCbCatA( DebugText, DebugTextSize, "OperationFlags=" );
        nsW32API::FormatOperationFlags( DebugText, DebugTextSize, Data->Iopb->OperationFlags );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "CreateDisposition=" );
        nsW32API::FormatCreateDisposition( DebugText, DebugTextSize, CreateDisposition );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "DesiredAccess=" );
        nsW32API::FormatCreateDesiredAccess( DebugText, DebugTextSize, SecurityContext->DesiredAccess );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "ShareAccess=" );
        nsW32API::FormatCreateShareAccess( DebugText, DebugTextSize, Parameters.ShareAccess );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "Options=" );
        nsW32API::FormatCreateOptions( DebugText, DebugTextSize, CreateOptions );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        %s AllocationSize=%I64d\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , DebugText
                   , Parameters.AllocationSize.QuadPart
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        Status=0x%08x,%s Information=%s Open=%d Clean=%d Ref=%d\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   , nsW32API::ConvertCreateResultInformationTo( IoStatus.Status, IoStatus.Information )
                   , IrpContext->Fcb != NULLPTR ? IrpContext->Fcb->OpnCount : 0
                   , IrpContext->Fcb != NULLPTR ? IrpContext->Fcb->ClnCount : 0
                   , IrpContext->Fcb != NULLPTR ? IrpContext->Fcb->RefCount : 0
                   ) );
    }
}

void PrintIrpContextREAD( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p,%p Proc=%06d,%ws Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread(), IrpContext->Data->Thread
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        RtlStringCbCatA( DebugText, DebugTextSize, "TopLevelIrp=" );
        nsW32API::FormatTopLevelIrp( DebugText, DebugTextSize );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "IrpFlags=" );
        nsW32API::FormatIrpFlags( DebugText, DebugTextSize, Data->Iopb->IrpFlags );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "OpFlags=" );
        nsW32API::FormatOperationFlags( DebugText, DebugTextSize, Data->Iopb->OperationFlags );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        %s Key=%d Length=%d ByteOffset=%I64d Buffer=%p\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , DebugText
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.Key : Data->Iopb->Parameters.Write.Key
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.Length : Data->Iopb->Parameters.Write.Length
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.ByteOffset.QuadPart : Data->Iopb->Parameters.Write.ByteOffset.QuadPart
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.ReadBuffer : Data->Iopb->Parameters.Write.WriteBuffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Status=0x%08x,%s Information=%Id\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   , IoStatus.Information
                   ) );
    }
}

void PrintIrpContextWRITE( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p,%p Proc=%06d,%ws Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread(), IrpContext->Data->Thread
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        RtlStringCbCatA( DebugText, DebugTextSize, "TopLevelIrp=" );
        nsW32API::FormatTopLevelIrp( DebugText, DebugTextSize );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "IrpFlags=" );
        nsW32API::FormatIrpFlags( DebugText, DebugTextSize, Data->Iopb->IrpFlags );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        RtlStringCbCatA( DebugText, DebugTextSize, "OpFlags=" );
        nsW32API::FormatOperationFlags( DebugText, DebugTextSize, Data->Iopb->OperationFlags );
        RtlStringCbCatA( DebugText, DebugTextSize, " " );

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        %s Key=%d Length=%d ByteOffset=%I64d Buffer=%p\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , DebugText
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.Key : Data->Iopb->Parameters.Write.Key
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.Length : Data->Iopb->Parameters.Write.Length
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.ByteOffset.QuadPart : Data->Iopb->Parameters.Write.ByteOffset.QuadPart
                   , MajorFunction == IRP_MJ_READ ? Data->Iopb->Parameters.Read.ReadBuffer : Data->Iopb->Parameters.Write.WriteBuffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Status=0x%08x,%s Information=%Id\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   , IoStatus.Information
                   ) );
    }
}

void PrintIrpContextQUERY_INFORMATION( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    const auto& Parameters = Data->Iopb->Parameters.QueryFileInformation;
    
    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Info=%s Thread=%p Proc=%06d,%ws Buffer=%p Length=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , nsW32API::ConvertFileInformationClassTo( (nsW32API::FILE_INFORMATION_CLASS)Parameters.FileInformationClass )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , Parameters.InfoBuffer, Parameters.Length
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Status=0x%08x,%s Information=%Id\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   , IoStatus.Information
                   ) );

        if( !NT_SUCCESS( IoStatus.Status ) )
            return;

        switch( (nsW32API::FILE_INFORMATION_CLASS)Parameters.FileInformationClass )
        {
            case FileBasicInformation: {
                RtlStringCbCatA( DebugText, 1024, "Basic[ " );
                nsW32API::FormatFileBasicInformation( IrpContext->DebugText, 1024, ( PFILE_BASIC_INFORMATION )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " ] " );
            } break;
            case FileStandardInformation: {
                RtlStringCbCatA( DebugText, 1024, "Standard[ " );
                nsW32API::FormatFileStandardInformation( IrpContext->DebugText, 1024, ( PFILE_STANDARD_INFORMATION )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " ] " );
            } break;
            case FileAccessInformation: {
                RtlStringCbCatA( DebugText, 1024, "AccessMask=" );
                nsW32API::FormatFileAccessInformation( IrpContext->DebugText, 1024, (PFILE_ACCESS_INFORMATION)Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileAlignmentInformation: {
                RtlStringCbCatA( DebugText, 1024, "Alignment=" );
                nsW32API::FormatFileAlignmentInformation( IrpContext->DebugText, 1024, ( PFILE_ALIGNMENT_INFORMATION )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileAllInformation: {} break;
            case FileAttributeTagInformation: {} break;
            case FileCompressionInformation: {} break;
            case FileEaInformation: {} break;
            case FileInternalInformation: {} break;
            case FileIoPriorityHintInformation: {} break;
            case FileModeInformation: {} break;
            case FileMoveClusterInformation: {} break;
            case FileNameInformation: {} break;
            case FileNetworkOpenInformation: {
                RtlStringCbCatA( DebugText, 1024, "NetworkOpen[ " );
                nsW32API::FormatFileNetworkOpenInformation( IrpContext->DebugText, 1024, ( PFILE_NETWORK_OPEN_INFORMATION )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " ] " );
            } break;
            case FilePositionInformation: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFilePositionInformation( IrpContext->DebugText, 1024, ( PFILE_POSITION_INFORMATION )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileStreamInformation: {
                ULONG Offset = 0;
                auto InfoBuffer = (PFILE_STREAM_INFORMATION) Parameters.InfoBuffer;
                RtlStringCbCatA( DebugText, 1024, "StreamInfo[ " );

                do
                {
                    Offset = InfoBuffer->NextEntryOffset;

                    auto Length = nsUtils::strlength( DebugText );

                    RtlStringCbPrintfA( &DebugText[ Length ], 1024 - ( Length * sizeof( WCHAR ) ),
                                        "StreamName=%.*S|StreamSize=%I64d|StreamAllocationSize=%I64d",
                                        InfoBuffer->StreamNameLength == 0 ? 7 : InfoBuffer->StreamNameLength / sizeof( WCHAR ), InfoBuffer->StreamName,
                                        InfoBuffer->StreamSize.QuadPart, InfoBuffer->StreamAllocationSize.QuadPart
                    );

                    InfoBuffer = ( PFILE_STREAM_INFORMATION )Add2Ptr( InfoBuffer, Offset );
                    
                } while( Offset != 0 );

                RtlStringCbCatA( DebugText, 1024, " ] " );
            } break;
        }

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        Buffer=%p %s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , Parameters.InfoBuffer
                   , IrpContext->DebugText
                   ) );
    }
}

void PrintIrpContextSET_INFORMATION( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    const auto& Parameters = Data->Iopb->Parameters.SetFileInformation;

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Info=%s Thread=%p Proc=%06d,%ws Buffer=%p Length=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , nsW32API::ConvertFileInformationClassTo( ( nsW32API::FILE_INFORMATION_CLASS )Parameters.FileInformationClass )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , Parameters.InfoBuffer, Parameters.Length
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Status=0x%08x,%s Information=%Id\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   , IoStatus.Information
                   ) );

        if( !NT_SUCCESS( IoStatus.Status ) )
            return;

        PVOID InputBuffer = Parameters.InfoBuffer;

        switch( ( nsW32API::FILE_INFORMATION_CLASS )Parameters.FileInformationClass )
        {
            case FileBasicInformation: {
                RtlStringCbCatA( DebugText, 1024, "Basic[ " );
                nsW32API::FormatFileBasicInformation( IrpContext->DebugText, 1024, ( PFILE_BASIC_INFORMATION )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " ] " );
            } break;
            case FileRenameInformation: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFileRenameInformation( IrpContext->DebugText, 1024, IrpContext->DstFileFullPath.Buffer, ( nsW32API::FILE_RENAME_INFORMATION* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileDispositionInformation: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFileDispositionInformation( IrpContext->DebugText, 1024, ( nsW32API::FILE_DISPOSITION_INFORMATION* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FilePositionInformation: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFilePositionInformation( IrpContext->DebugText, 1024, ( FILE_POSITION_INFORMATION* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileAllocationInformation: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFileAllocationInformation( IrpContext->DebugText, 1024, ( FILE_ALLOCATION_INFORMATION* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileEndOfFileInformation: {
                RtlStringCbPrintfA( DebugText, DebugTextSize, "AdvanceOnly=%d ", Data->Iopb->Parameters.SetFileInformation.AdvanceOnly );
                nsW32API::FormatFileEndOfFileInformation( IrpContext->DebugText, 1024, ( FILE_END_OF_FILE_INFORMATION* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case FileValidDataLengthInformation: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFileValidDataLengthInformation( IrpContext->DebugText, 1024, ( FILE_VALID_DATA_LENGTH_INFORMATION* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case nsW32API::FileRenameInformationEx: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFileRenameInformationEx( IrpContext->DebugText, 1024, IrpContext->DstFileFullPath.Buffer, ( nsW32API::FILE_RENAME_INFORMATION_EX* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
            case nsW32API::FileDispositionInformationEx: {
                RtlStringCbCatA( DebugText, 1024, "" );
                nsW32API::FormatFileDispositionInformationEx( IrpContext->DebugText, 1024, ( nsW32API::FILE_DISPOSITION_INFORMATION_EX* )Parameters.InfoBuffer );
                RtlStringCbCatA( DebugText, 1024, " " );
            } break;
        }

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        Buffer=%p %s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , Parameters.InfoBuffer
                   , IrpContext->DebugText
                   ) );
    }
}

void PrintIrpContextQUERY_SECURITY( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = Data->Iopb->MajorFunction;
    const auto& MinorFunction = Data->Iopb->MinorFunction;
    auto IoStatus = Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    const auto& Parameters = Data->Iopb->Parameters.QuerySecurity;

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        SecurityInformation=%s Length=%d Buffer=%p MDL=%p\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , nsW32API::ConvertSecurityInformationTo( Parameters.SecurityInformation )
                   , Parameters.Length
                   , Parameters.SecurityBuffer
                   , Parameters.MdlAddress
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

void PrintIrpContextSET_SECURITY( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = Data->Iopb->MajorFunction;
    const auto& MinorFunction = Data->Iopb->MinorFunction;
    auto IoStatus = Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    const auto& Parameters = Data->Iopb->Parameters.SetSecurity;

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        SecurityInformation=%s SecurityDescriptor=%p\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , nsW32API::ConvertSecurityInformationTo( Parameters.SecurityInformation )
                   , Parameters.SecurityDescriptor
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

void PrintIrpContextDIRECTORY_CONTROL( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    switch( MinorFunction )
    {
        case IRP_MN_NOTIFY_CHANGE_DIRECTORY: {} break;
        case IRP_MN_QUERY_DIRECTORY: {
            const auto& Parameters = Data->Iopb->Parameters.DirectoryControl.QueryDirectory;
            auto FileInformationClass = Parameters.FileInformationClass;

            if( IsResultMode == false )
            {
                KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Info=%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                           , IsResultMode == false ? ">>" : "<<"
                           , IrpContext->EvtID
                           , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                           , nsW32API::ConvertFileInformationClassTo( ( nsW32API::FILE_INFORMATION_CLASS )FileInformationClass )
                           , PsGetCurrentThread()
                           , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                           , IrpContext->SrcFileFullPath.Buffer
                           ) );
            }
            else
            {
                KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Info=%s Thread=%p Proc=%06d,%ws Src=%ws\n"
                           , IsResultMode == false ? ">>" : "<<"
                           , IrpContext->EvtID
                           , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                           , nsW32API::ConvertFileInformationClassTo( ( nsW32API::FILE_INFORMATION_CLASS )FileInformationClass )
                           , PsGetCurrentThread()
                           , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                           , IrpContext->SrcFileFullPath.Buffer
                           ) );

                switch( FileInformationClass )
                {
                    case FileDirectoryInformation: {} break;
                    case FileFullDirectoryInformation: {} break;
                    case FileBothDirectoryInformation: {

                        ULONG Offset = 0;
                        auto InfoBuffer = ( FILE_BOTH_DIR_INFORMATION* )IrpContext->UserBuffer;
                        UNICODE_STRING FileNameUni;

                        do
                        {
                            Offset = InfoBuffer->NextEntryOffset;

                            if( nsUtils::stricmp( InfoBuffer->FileName, InfoBuffer->FileNameLength, L"." ) != 0 && 
                                nsUtils::stricmp( InfoBuffer->FileName, InfoBuffer->FileNameLength, L".." ) != 0 )
                            {
                                FileNameUni.Buffer = InfoBuffer->FileName;
                                FileNameUni.Length = InfoBuffer->FileNameLength;
                                FileNameUni.MaximumLength = InfoBuffer->FileNameLength;

                                KdPrint( ( "[WinIOSol] %s EvtID=%09d        FileName=%wZ CreationTime=%I64d LastAccessTime=%I64d LastWriteTime=%I64d ChangeTime=%I64d EndOfFile=%I64d AllocationSize=%I64d\n"
                                           , JudgeInOut( IrpContext, IsResultMode ), IrpContext->EvtID
                                           , &FileNameUni
                                           , InfoBuffer->CreationTime.QuadPart
                                           , InfoBuffer->LastAccessTime.QuadPart
                                           , InfoBuffer->LastWriteTime.QuadPart
                                           , InfoBuffer->ChangeTime.QuadPart
                                           , InfoBuffer->EndOfFile.QuadPart
                                           , InfoBuffer->AllocationSize.QuadPart
                                           ) );
                            }

                            InfoBuffer = ( FILE_BOTH_DIR_INFORMATION* )Add2Ptr( InfoBuffer, Offset );

                        } while( Offset != 0 );

                    } break;
                    case FileIdBothDirectoryInformation: {} break;
                    case FileIdFullDirectoryInformation: {} break;
                }
            }
        } break;
    }
}

void PrintIrpContextCLEANUP( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

void PrintIrpContextCLOSE( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb != NULLPTR ? IrpContext->Fcb->OpnCount : 0
                   , IrpContext->Fcb != NULLPTR ? IrpContext->Fcb->ClnCount : 0
                   , IrpContext->Fcb != NULLPTR ? IrpContext->Fcb->RefCount : 0
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

void PrintIrpContextQUERY_VOLUME_INFORMATION( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        auto FsInformationClass = ( nsW32API::FS_INFORMATION_CLASS )IrpContext->Data->Iopb->Parameters.QueryVolumeInformation.FsInformationClass;

        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Info=%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , nsW32API::ConvertFsInformationClassTo( FsInformationClass )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

void PrintIrpContextFILE_SYSTEM_CONTROL( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        ULONG FsControlCode = 0;

        if( MinorFunction == IRP_MN_KERNEL_CALL || MinorFunction == IRP_MN_USER_FS_REQUEST )
            FsControlCode = IrpContext->Data->Iopb->Parameters.FileSystemControl.Common.FsControlCode;

        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s FsControl=0x%08x,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , FsControlCode, FsControlCode == 0 ? "" : nsW32API::ConvertBuiltInFsControlCodeTo( FsControlCode )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

void PrintIrpContextLOCK_CONTROL( PIRP_CONTEXT IrpContext, bool IsResultMode )
{
    const auto& Data = IrpContext->Data;
    const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
    const auto& MinorFunction = IrpContext->Data->Iopb->MinorFunction;
    auto IoStatus = IrpContext->Data->IoStatus;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_STATUS ) )
        IoStatus.Status = IrpContext->Status;
    if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_IOSTATUS_INFORMATION ) )
        IoStatus.Information = IrpContext->Information;

    char* DebugText = IrpContext->DebugText;
    static const int DebugTextSize = 1024 * sizeof( CHAR );

    if( IsResultMode == false )
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Src=%ws\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IrpContext->SrcFileFullPath.Buffer
                   ) );

        auto Length = IrpContext->Data->Iopb->Parameters.LockControl.Length;
        auto Key = IrpContext->Data->Iopb->Parameters.LockControl.Key;
        auto ByteOffset = IrpContext->Data->Iopb->Parameters.LockControl.ByteOffset;
        auto ProcessId = IrpContext->Data->Iopb->Parameters.LockControl.ProcessId;
        auto FailImmediately = IrpContext->Data->Iopb->Parameters.LockControl.FailImmediately;
        auto ExclusiveLock = IrpContext->Data->Iopb->Parameters.LockControl.ExclusiveLock;

        KdPrint( ( "[WinIOSol] %s EvtID=%09d        Length=%I64d Key=%d ByteOffset=%I64d ProcessId=%d FailImmediately=%d ExclusiveLock=%d\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , IrpContext->DebugText
                   , Length->QuadPart, Key, ByteOffset.QuadPart, IrpContext->ProcessId, FailImmediately, ExclusiveLock
                   ) );
    }
    else
    {
        KdPrint( ( "[WinIOSol] %s EvtID=%09d IRP=%s,%s Thread=%p Proc=%06d,%ws Open=%d Clean=%d Ref=%d Status=0x%08x,%s\n"
                   , IsResultMode == false ? ">>" : "<<"
                   , IrpContext->EvtID
                   , nsW32API::ConvertIrpMajorFuncTo( MajorFunction ), nsW32API::ConvertIrpMinorFuncTo( MajorFunction, MinorFunction )
                   , PsGetCurrentThread()
                   , IrpContext->ProcessId, IrpContext->ProcessFileName == NULLPTR ? L"(null)" : IrpContext->ProcessFileName
                   , IrpContext->Fcb->OpnCount, IrpContext->Fcb->ClnCount, IrpContext->Fcb->RefCount
                   , IoStatus.Status, ntkernel_error_category::find_ntstatus( IoStatus.Status )->message
                   ) );
    }
}

VOID AcquireCmnResource( PIRP_CONTEXT IrpContext, LONG RsrcFlags )
{
    //ASSERT( IrpContext != NULLPTR );
    //if( IrpContext == NULLPTR )
    //    return;

    //ASSERT( IrpContext->Fcb != NULLPTR );
    //if( IrpContext->Fcb == NULLPTR )
    //    return;

    if( BooleanFlagOn( RsrcFlags, FCB_MAIN_EXCLUSIVE ) )
    {
        if( BooleanFlagOn( RsrcFlags, FCB_MAIN_SHARED ) || 
            ( ExIsResourceAcquiredSharedLite( &IrpContext->Fcb->MainResource ) > 0 && ExIsResourceAcquiredExclusiveLite( &IrpContext->Fcb->MainResource ) == 0 ) 
             )
        {
            KdBreakPoint();
            ASSERT( false );
            return;
        }

        FltAcquireResourceExclusive( &IrpContext->Fcb->MainResource );
        SetFlag( IrpContext->CompleteStatus, COMPLETE_FREE_MAIN_RSRC );
    }

    if( BooleanFlagOn( RsrcFlags, FCB_MAIN_SHARED ) )
    {
        if( BooleanFlagOn( RsrcFlags, FCB_MAIN_EXCLUSIVE ) )
        {
            KdBreakPoint();
            ASSERT( false );
            return;
        }

        FltAcquireResourceShared( &IrpContext->Fcb->MainResource );
        SetFlag( IrpContext->CompleteStatus, COMPLETE_FREE_MAIN_RSRC );
    }


    if( BooleanFlagOn( RsrcFlags, FCB_PGIO_EXCLUSIVE ) )
    {
        if( BooleanFlagOn( RsrcFlags, FCB_PGIO_SHARED ) || 
            ( ExIsResourceAcquiredSharedLite( &IrpContext->Fcb->PagingIoResource ) > 0 && ExIsResourceAcquiredExclusiveLite( &IrpContext->Fcb->PagingIoResource ) == 0 )
            )
        {
            KdBreakPoint();
            ASSERT( false );
            return;
        }

        FltAcquireResourceExclusive( &IrpContext->Fcb->PagingIoResource );
        SetFlag( IrpContext->CompleteStatus, COMPLETE_FREE_PGIO_RSRC );
    }

    if( BooleanFlagOn( RsrcFlags, FCB_PGIO_SHARED ) )
    {
        if( BooleanFlagOn( RsrcFlags, FCB_PGIO_EXCLUSIVE ) )
        {
            KdBreakPoint();
            ASSERT( false );
            return;
        }

        FltAcquireResourceShared( &IrpContext->Fcb->PagingIoResource );
        SetFlag( IrpContext->CompleteStatus, COMPLETE_FREE_PGIO_RSRC );
    }


    if( BooleanFlagOn( RsrcFlags, INST_EXCLUSIVE ) )
    {
        if( BooleanFlagOn( RsrcFlags, INST_SHARED ) || 
            ( ExIsResourceAcquiredSharedLite( &IrpContext->InstanceContext->VcbLock ) > 0 && ExIsResourceAcquiredExclusiveLite( &IrpContext->InstanceContext->VcbLock ) == 0 )
            )
        {
            KdBreakPoint();
            ASSERT( false );
            return;
        }

        FltAcquireResourceExclusive( &IrpContext->InstanceContext->VcbLock );
        SetFlag( IrpContext->CompleteStatus, COMPLETE_FREE_INST_RSRC );
    }

    if( BooleanFlagOn( RsrcFlags, INST_SHARED ) )
    {
        if( BooleanFlagOn( RsrcFlags, INST_EXCLUSIVE ) )
        {
            KdBreakPoint();
            ASSERT( false );
            return;
        }

        FltAcquireResourceShared( &IrpContext->InstanceContext->VcbLock );
        SetFlag( IrpContext->CompleteStatus, COMPLETE_FREE_INST_RSRC );
    }
}

void ReleaseCmnResource( PIRP_CONTEXT IrpContext, LONG RsrcFlags )
{
    ASSERT( IrpContext != NULLPTR );
    if( IrpContext == NULLPTR )
        return;

    if( BooleanFlagOn( RsrcFlags, INST_SHARED ) || 
        BooleanFlagOn( RsrcFlags, INST_EXCLUSIVE ) )
    {
        if( BooleanFlagOn( IrpContext->CompleteStatus, COMPLETE_FREE_INST_RSRC ) )
        {
            FltReleaseResource( &IrpContext->InstanceContext->VcbLock );
            ClearFlag( IrpContext->CompleteStatus, COMPLETE_FREE_INST_RSRC );
        }
    }
}

NTSTATUS CheckEventTo( IRP_CONTEXT* IrpContext )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        if( IrpContext == NULLPTR )
            break;

        const auto& MajorFunction = IrpContext->Data->Iopb->MajorFunction;
        switch( MajorFunction )
        {
            case IRP_MJ_CREATE: {
                Status = CheckEventToWithCREATE( IrpContext, CHECK_EVENT_GLOBAL_DIR_FILTER | CHECK_EVENT_PROCESS_DIR_FILTER );
            } break;
            case IRP_MJ_DIRECTORY_CONTROL: {
                Status = CheckEventToWithDIRECTORY_CONTROL( IrpContext, CHECK_EVENT_PROCESS_ONLY );
            } break;
            case IRP_MJ_QUERY_INFORMATION: {
                Status = CheckEventToWithQUERY_INFORMATION( IrpContext, CHECK_EVENT_PROCESS_ONLY );
            } break;
            case IRP_MJ_SET_INFORMATION: {
                Status = CheckEventToWithQUERY_INFORMATION( IrpContext, CHECK_EVENT_PROCESS_DIR_FILTER | CHECK_EVENT_IS_ENCRYPTED );
            } break;
            case IRP_MJ_CLEANUP: {} break;
            case IRP_MJ_CLOSE: {} break;
        }
        
    } while( false );

    return Status;
}

NTSTATUS CheckEventToWithCREATE( IRP_CONTEXT* IrpContext, ULONG CheckFlags )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        IrpContext->IsConcerned = false;

        if( FlagOn( CheckFlags, CHECK_EVENT_GLOBAL_DIR_FILTER ) )
        {
            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, false );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = false;
                break;
            }

            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, true );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = true;
            }
        }

        if( IrpContext->ProcessId <= 4 )
            break;

        if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) || FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
        {
            IrpContext->IsConcerned = false;

            Status = ProcessFilter_Match( IrpContext->ProcessId, &IrpContext->ProcessFullPath, &IrpContext->ProcessFilter, &IrpContext->ProcessFilterEntry );

            IrpContext->IsConcerned = IrpContext->ProcessFilter != NULLPTR;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) )
                break;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
            {
                bool isInclude = false;
                Status = ProcessFilter_SubMatch( IrpContext->Data, IrpContext->ProcessFilterEntry, &IrpContext->SrcFileFullPath, &isInclude, &IrpContext->ProcessFilterMaskItem );
                if( Status == STATUS_NO_DATA_DETECTED )
                    break;

                if( Status == STATUS_SUCCESS )
                    IrpContext->IsConcerned = isInclude;
                else
                    IrpContext->IsConcerned = false;
            }
        }
        
    } while( false );

    return Status;
}

NTSTATUS CheckEventToWithDIRECTORY_CONTROL( IRP_CONTEXT* IrpContext, ULONG CheckFlags )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        IrpContext->IsConcerned = false;

        if( FlagOn( CheckFlags, CHECK_EVENT_GLOBAL_DIR_FILTER ) )
        {
            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, false );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = false;
                break;
            }

            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, true );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = true;
            }
        }

        if( IrpContext->ProcessId <= 4 )
            break;

        if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) || FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
        {
            IrpContext->IsConcerned = false;

            Status = ProcessFilter_Match( IrpContext->ProcessId, &IrpContext->ProcessFullPath, &IrpContext->ProcessFilter, &IrpContext->ProcessFilterEntry );

            IrpContext->IsConcerned = IrpContext->ProcessFilter != NULLPTR;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) )
                break;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
            {
                bool isInclude = false;
                Status = ProcessFilter_SubMatch( IrpContext->Data, IrpContext->ProcessFilterEntry, &IrpContext->SrcFileFullPath, &isInclude, &IrpContext->ProcessFilterMaskItem );
                if( Status == STATUS_NO_DATA_DETECTED )
                    break;

                if( Status == STATUS_SUCCESS )
                    IrpContext->IsConcerned = isInclude;
                else
                    IrpContext->IsConcerned = false;
            }
        }

    } while( false );

    return Status;
}

NTSTATUS CheckEventToWithQUERY_INFORMATION( IRP_CONTEXT* IrpContext, ULONG CheckFlags )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        IrpContext->IsConcerned = false;

        if( FlagOn( CheckFlags, CHECK_EVENT_GLOBAL_DIR_FILTER ) )
        {
            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, false );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = false;
                break;
            }

            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, true );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = true;
            }
        }

        if( IrpContext->ProcessId <= 4 )
            break;

        if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) || FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
        {
            IrpContext->IsConcerned = false;

            Status = ProcessFilter_Match( IrpContext->ProcessId, &IrpContext->ProcessFullPath, &IrpContext->ProcessFilter, &IrpContext->ProcessFilterEntry );

            IrpContext->IsConcerned = IrpContext->ProcessFilter != NULLPTR;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) )
                break;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
            {
                bool isInclude = false;
                Status = ProcessFilter_SubMatch( IrpContext->Data, IrpContext->ProcessFilterEntry, &IrpContext->SrcFileFullPath, &isInclude, &IrpContext->ProcessFilterMaskItem );
                if( Status == STATUS_NO_DATA_DETECTED )
                    break;

                if( Status == STATUS_SUCCESS )
                    IrpContext->IsConcerned = isInclude;
                else
                    IrpContext->IsConcerned = false;
            }
        }

    } while( false );

    return Status;
}

NTSTATUS CheckEventToWithSET_INFORMATION( IRP_CONTEXT* IrpContext, ULONG CheckFlags )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    do
    {
        bool isRenameOrLink = false;
        const auto& FileInformationClass = IrpContext->Data->Iopb->Parameters.SetFileInformation.FileInformationClass;

        if( FileInformationClass == FileRenameInformation || 
            FileInformationClass == FileLinkInformation ||
            FileInformationClass == ( nsW32API::FileRenameInformationEx ) ||
            FileInformationClass == nsW32API::FileLinkInformationEx )
            isRenameOrLink = true;

        IrpContext->IsConcerned = false;

        if( FlagOn( CheckFlags, CHECK_EVENT_GLOBAL_DIR_FILTER ) )
        {
            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, false );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = false;
                break;
            }
            
            Status = GlobalFilter_Match( IrpContext->SrcFileFullPath.Buffer, true );
            if( Status == STATUS_SUCCESS )
            {
                IrpContext->IsConcerned = true;
            }

            if( isRenameOrLink == true )
            {
                Status = GlobalFilter_Match( IrpContext->DstFileFullPath.Buffer, true );
                if( Status == STATUS_SUCCESS )
                {
                    IrpContext->IsConcerned = true;
                }
            }
        }

        if( IrpContext->ProcessId <= 4 )
            break;

        if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) || FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
        {
            IrpContext->IsConcerned = false;

            Status = ProcessFilter_Match( IrpContext->ProcessId, &IrpContext->ProcessFullPath, &IrpContext->ProcessFilter, &IrpContext->ProcessFilterEntry );

            IrpContext->IsConcerned = IrpContext->ProcessFilter != NULLPTR;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_ONLY ) )
                break;

            if( FlagOn( CheckFlags, CHECK_EVENT_PROCESS_DIR_FILTER ) )
            {
                bool isInclude = false;
                Status = ProcessFilter_SubMatch( IrpContext->Data, IrpContext->ProcessFilterEntry, &IrpContext->SrcFileFullPath, &isInclude, &IrpContext->ProcessFilterMaskItem );
                if( Status == STATUS_NO_DATA_DETECTED )
                    break;

                if( Status == STATUS_SUCCESS )
                    IrpContext->IsConcerned = isInclude;
                else
                    IrpContext->IsConcerned = false;

                if( isRenameOrLink == true && FlagOn( CheckFlags, CHECK_EVENT_IS_RENAME ) && Status == STATUS_NOT_FOUND )
                {
                    Status = ProcessFilter_SubMatch( IrpContext->Data, IrpContext->ProcessFilterEntry, &IrpContext->DstFileFullPath, &isInclude, &IrpContext->ProcessFilterMaskItem );

                    if( Status == STATUS_SUCCESS )
                        IrpContext->IsConcerned = isInclude;
                    else
                        IrpContext->IsConcerned = false;
                }
            }
        }

    } while( false );

    return Status;
}
