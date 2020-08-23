﻿#include "contextMgr.hpp"

#include "WinIOMonitor_W32API.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

CTX_GLOBAL_DATA GlobalContext;

///////////////////////////////////////////////////////////////////////////////

NTSTATUS CtxAllocateContext( PFLT_FILTER Filter, FLT_CONTEXT_TYPE ContextType, PFLT_CONTEXT* Context )
{
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if( Filter == NULL )
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        ULONG ctxSize = 0;

        switch( ContextType )
        {
            case FLT_VOLUME_CONTEXT: {
                ctxSize = CTX_VOLUME_CONTEXT_SIZE;
            } break;
            case FLT_INSTANCE_CONTEXT: {
                ctxSize = CTX_INSTANCE_CONTEXT_SIZE;
            } break;
            case FLT_FILE_CONTEXT: {
                ctxSize = CTX_FILE_CONTEXT_SIZE;
            } break;
            case FLT_STREAM_CONTEXT: {
                ctxSize = CTX_STREAM_CONTEXT_SIZE;
            } break;
            case FLT_STREAMHANDLE_CONTEXT: {
                ctxSize = CTX_STREAMHANDLE_CONTEXT_SIZE;
            } break;
            case FLT_TRANSACTION_CONTEXT: {
                ctxSize = CTX_TRANSACTION_CONTEXT_SIZE;
            } break;
        }

        Status = FltAllocateContext( Filter, ContextType, ctxSize, NonPagedPool, Context );
        if( NT_SUCCESS( Status ) )
        {
            RtlZeroMemory( *Context, ctxSize );
        }

    } while( false );

    return Status;
}

NTSTATUS CtxSetContext( PCFLT_RELATED_OBJECTS FltObjects, PVOID Target, FLT_CONTEXT_TYPE ContextType, PFLT_CONTEXT NewContext, PFLT_CONTEXT* OldContext )
{
    switch( ContextType )
    {
        case FLT_INSTANCE_CONTEXT: {
            return FltSetInstanceContext( FltObjects->Instance, FLT_SET_CONTEXT_KEEP_IF_EXISTS, 
                                          NewContext, OldContext );
        } break;
        case FLT_VOLUME_CONTEXT: {
            return FltSetVolumeContext( FltObjects->Volume, FLT_SET_CONTEXT_KEEP_IF_EXISTS, 
                                        NewContext, OldContext );
        } break;
        case FLT_FILE_CONTEXT: {
            return nsW32API::FltMgrAPIMgr.pfnFltSetFileContext( FltObjects->Instance, (PFILE_OBJECT)Target,
                                                         FLT_SET_CONTEXT_KEEP_IF_EXISTS, 
                                                         NewContext, OldContext );
        } break;
        case FLT_STREAM_CONTEXT: {
            return FltSetStreamContext( FltObjects->Instance, ( PFILE_OBJECT )Target,
                                        FLT_SET_CONTEXT_KEEP_IF_EXISTS,
                                        NewContext, OldContext );
        } break;
        case FLT_STREAMHANDLE_CONTEXT: {
            return FltSetStreamHandleContext( FltObjects->Instance, (PFILE_OBJECT)Target, 
                                              FLT_SET_CONTEXT_KEEP_IF_EXISTS, 
                                              NewContext, OldContext );
        } break;
        case FLT_TRANSACTION_CONTEXT: {
            return nsW32API::FltMgrAPIMgr.pfnFltSetTransactionContext( FltObjects->Instance, ( PKTRANSACTION )Target,
                                                                FLT_SET_CONTEXT_KEEP_IF_EXISTS,
                                                                NewContext, OldContext );
        } break;
    }

    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL, "[WinIOMon] %s %s ContextType=0x%08x\n",
                 __FUNCTION__, "Unexpected Context Type", ContextType ) );
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS CtxGetContext( PCFLT_RELATED_OBJECTS FltObjects, PVOID Target, FLT_CONTEXT_TYPE ContextType, PFLT_CONTEXT* Context )
{
    switch( ContextType )
    {
        case FLT_INSTANCE_CONTEXT: {
            return FltGetInstanceContext( FltObjects->Instance, Context );
        } break;
        case FLT_VOLUME_CONTEXT:
        {
            return FltGetVolumeContext( FltObjects->Filter, FltObjects->Volume, Context );
        } break;
        case FLT_FILE_CONTEXT:
        {
            return nsW32API::FltMgrAPIMgr.pfnFltGetFileContext( FltObjects->Instance, ( PFILE_OBJECT )Target, Context );
        } break;
        case FLT_STREAM_CONTEXT:
        {
            return FltGetStreamContext( FltObjects->Instance, ( PFILE_OBJECT )Target, Context );
        } break;
        case FLT_STREAMHANDLE_CONTEXT:
        {
            return FltGetStreamHandleContext( FltObjects->Instance, ( PFILE_OBJECT )Target, Context );
        } break;
        case FLT_TRANSACTION_CONTEXT:
        {
            return nsW32API::FltMgrAPIMgr.pfnFltGetTransactionContext( FltObjects->Instance, ( PKTRANSACTION )Target, Context );
        } break;
    }

    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL, "[WinIOMon] %s %s ContextType=0x%08x\n",
                 __FUNCTION__, "Unexpected Context Type", ContextType ) );
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS CtxGetOrSetContext( PCFLT_RELATED_OBJECTS FltObjects, PVOID Target, PFLT_CONTEXT* Context, FLT_CONTEXT_TYPE ContextType )
{
    NTSTATUS status;
    PFLT_CONTEXT newContext;
    PFLT_CONTEXT oldContext;

    PAGED_CODE();

    ASSERT( NULL != Context );

    newContext = *Context;

    //
    //  Is there already a context attached to the target?
    //

    status = CtxGetContext( FltObjects, Target, ContextType, &oldContext );

    if( STATUS_NOT_FOUND == status )
    {

        //
        //  There is no attached context. This means we have to either attach the
        //  one provided by the caller or allocate a new one and attach it.
        //

        if( NULL == newContext )
        {

            //
            //  No provided context. Allocate one.
            //

            status = CtxAllocateContext( FltObjects->Filter, ContextType, &newContext );

            if( !NT_SUCCESS( status ) )
            {

                //
                //  We failed to allocate.
                //

                return status;
            }
        }

    }
    else if( !NT_SUCCESS( status ) )
    {

        //
        //  We failed trying to get a context from the target.
        //

        return status;

    }
    else
    {

        //
        //  There is already a context attached to the target, so return
        //  that context.
        //
        //  If a context was provided by the caller, release it if it's not
        //  the one attached to the target.
        //

        //
        //  The caller is not allowed to set the same context on the target
        //  twice.
        //
        ASSERT( newContext != oldContext );

        if( NULL != newContext )
        {

            FltReleaseContext( newContext );
        }

        *Context = oldContext;
        return status;
    }

    //
    //  At this point we should have a context to set on the target (newContext).
    //

    status = CtxSetContext( FltObjects, Target, ContextType, newContext, &oldContext );

    if( !NT_SUCCESS( status ) )
    {

        //
        //  FltSetStreamContext failed so we must release the new context.
        //

        FltReleaseContext( newContext );

        if( STATUS_FLT_CONTEXT_ALREADY_DEFINED == status )
        {

            //
            //  We're racing with some other call which managed to set the
            //  context before us. We will return that context instead, which
            //  will be in oldContext.
            //

            *Context = oldContext;
            return STATUS_SUCCESS;

        }
        else
        {

            //
            //  Failed to set the context. Return NULL.
            //

            *Context = NULL;
            return status;
        }
    }

    //
    //  If this is setting a transaction context, we want to enlist in the
    //  transaction as well.
    //

    if( FLT_TRANSACTION_CONTEXT == ContextType )
    {
        status = nsW32API::FltMgrAPIMgr.pfnFltEnlistInTransaction( FltObjects->Instance,
                                                                   ( PKTRANSACTION )Target,
                                                                   newContext,
                                                                   ( TRANSACTION_NOTIFY_COMMIT_FINALIZE | TRANSACTION_NOTIFY_ROLLBACK ) );
    }

    //
    //  Setting the context was successful so just return newContext.
    //

    *Context = newContext;
    return status;
}

NTSTATUS CtxReleaseContext( PFLT_CONTEXT Context )
{
    if( Context == NULLPTR )
        return STATUS_SUCCESS;

    FltReleaseContext( Context );
    return STATUS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

void CtxInstanceContextCleanupCallback( PCTX_INSTANCE_CONTEXT InstanceContext, FLT_CONTEXT_TYPE ContextType )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s ContextType=%d Context=%p\n", 
                 __FUNCTION__, ContextType, InstanceContext ) );
}

void CtxVolumeContextCleanupCallback( PCTX_VOLUME_CONTEXT VolumeContext, FLT_CONTEXT_TYPE ContextType )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s ContextType=%d Context=%p\n",
                 __FUNCTION__, ContextType, VolumeContext ) );
}

void CtxFileContextCleanupCallback( PCTX_FILE_CONTEXT FileContext, FLT_CONTEXT_TYPE ContextType )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s ContextType=%d Context=%p\n",
                 __FUNCTION__, ContextType, FileContext ) );
}

void CtxStreamContextCleanupCallback( PCTX_STREAM_CONTEXT StreamContext, FLT_CONTEXT_TYPE ContextType )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s ContextType=%d Context=%p\n",
                 __FUNCTION__, ContextType, StreamContext ) );
}

void CtxStreamHandleContextCleanupCallback( PCTX_STREAMHANDLE_CONTEXT StreamHandleContext, FLT_CONTEXT_TYPE ContextType )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s ContextType=%d Context=%p\n",
                 __FUNCTION__, ContextType, StreamHandleContext ) );
}

void CtxTransactionContextCleanupCallback( PCTX_TRANSACTION_CONTEXT TransactionContext, FLT_CONTEXT_TYPE ContextType )
{
    KdPrintEx( ( DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[WinIOMon] %s ContextType=%d Context=%p\n",
                 __FUNCTION__, ContextType, TransactionContext ) );
}
