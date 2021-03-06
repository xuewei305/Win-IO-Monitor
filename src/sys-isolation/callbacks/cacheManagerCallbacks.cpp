﻿#include "cacheManagerCallbacks.hpp"


#include "irpContext.hpp"
#include "privateFCBMgr.hpp"
#include "privateFCBMgr_Defs.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

BOOLEAN CcAcquireForLazyWrite( PVOID Context, BOOLEAN Wait )
{
    auto Fcb = ( FCB* )Context;
    UNREFERENCED_PARAMETER( Wait );
    BOOLEAN bRet = FALSE;

    auto EvtID = CreateEvtID();
    KdPrint( ( "[WinIOSol] >> EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , Fcb->FileFullPath.Buffer
               ) );

    if( !BooleanFlagOn( Fcb->Flags, FCB_STATE_PGIO_SHARED ) &&
        !BooleanFlagOn( Fcb->Flags, FCB_STATE_PGIO_EXCLUSIVE ) )
    {
        FsRtlEnterFileSystem();

        bRet = ExAcquireResourceSharedLite( &Fcb->PagingIoResource, Wait );

        FsRtlExitFileSystem();

        if( bRet )
            SetFlag( Fcb->Flags, FCB_STATE_PGIO_SHARED );
    }

    KdPrint( ( "[WinIOSol] << EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Acquired=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , bRet
               , Fcb->FileFullPath.Buffer
               ) );

    return bRet;
}

void CcReleaseFromLazyWrite( PVOID Context )
{
    auto Fcb = ( FCB* )Context;

    auto EvtID = CreateEvtID();
    KdPrint( ( "[WinIOSol] >> EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , Fcb->FileFullPath.Buffer
               ) );

    if( BooleanFlagOn( Fcb->Flags, FCB_STATE_PGIO_SHARED ) ||
        BooleanFlagOn( Fcb->Flags, FCB_STATE_PGIO_EXCLUSIVE ) )
    {
        FsRtlEnterFileSystem();
        ExReleaseResourceLite( &Fcb->PagingIoResource );
        FsRtlExitFileSystem();
        ClearFlag( Fcb->Flags, FCB_STATE_PGIO_SHARED );
        ClearFlag( Fcb->Flags, FCB_STATE_PGIO_EXCLUSIVE );
    }

    KdPrint( ( "[WinIOSol] << EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , Fcb->FileFullPath.Buffer
               ) );
}

BOOLEAN CcAcquireForReadAhead( PVOID Context, BOOLEAN Wait )
{
    auto Fcb = ( FCB* )Context;
    UNREFERENCED_PARAMETER( Wait );
    BOOLEAN bRet = FALSE;

    auto EvtID = CreateEvtID();
    KdPrint( ( "[WinIOSol] >> EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , Fcb->FileFullPath.Buffer
               ) );

    if( !BooleanFlagOn( Fcb->Flags, FCB_STATE_MAIN_SHARED ) &&
        !BooleanFlagOn( Fcb->Flags, FCB_STATE_MAIN_EXCLUSIVE ) )
    {
        KeEnterCriticalRegion();

        bRet = ExAcquireResourceSharedLite( &Fcb->MainResource, Wait );

        KeLeaveCriticalRegion();

        if( bRet )
            SetFlag( Fcb->Flags, FCB_STATE_MAIN_SHARED );
    }

    KdPrint( ( "[WinIOSol] << EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Acquired=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , bRet
               , Fcb->FileFullPath.Buffer
               ) );

    return bRet;
}

void CcReleaseFromReadAhead( PVOID Context )
{
    auto Fcb = ( FCB* )Context;

    auto EvtID = CreateEvtID();
    KdPrint( ( "[WinIOSol] >> EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , Fcb->FileFullPath.Buffer
               ) );

    if( BooleanFlagOn( Fcb->Flags, FCB_STATE_MAIN_SHARED ) ||
        BooleanFlagOn( Fcb->Flags, FCB_STATE_MAIN_EXCLUSIVE ) )
    {
        FsRtlEnterFileSystem();
        ExReleaseResourceLite( &Fcb->MainResource );
        FsRtlExitFileSystem();
        ClearFlag( Fcb->Flags, FCB_STATE_MAIN_SHARED );
        ClearFlag( Fcb->Flags, FCB_STATE_MAIN_EXCLUSIVE );
    }

    KdPrint( ( "[WinIOSol] << EvtID=%09d %s Thread=%p Open=%d Clean=%d Ref=%d Src=%ws\n"
               , EvtID, __FUNCTION__
               , PsGetCurrentThread()
               , Fcb->OpnCount, Fcb->ClnCount, Fcb->RefCount
               , Fcb->FileFullPath.Buffer
               ) );
}
