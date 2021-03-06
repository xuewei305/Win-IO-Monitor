﻿#ifndef HDR_W32API
#define HDR_W32API

#include "fltBase.hpp"

#if defined(_MSC_VER)
#   pragma execution_character_set( "utf-8" )
#endif

#include "W32API_Base.hpp"
#include "W32API_NtOsKrnl.hpp"
#include "W32API_FltMgr.hpp"
#include "W32API_NTSTATUS.hpp"
#include "W32API_DBGHelper.hpp"

namespace nsW32API
{
    /**
     * @brief The FltIsVolumeWritable routine determines whether the disk device that corresponds to a volume or minifilter driver instance is writable. 
     * @param FltObject  Volume or Instance Object
     * @param IsWritable 
     * @return
     *
     * Support WinXP
    */
    NTSTATUS IsVolumeWritable( __in PVOID FltObject, __out PBOOLEAN IsWritable );

    NTSTATUS FltCreateFileEx( __in      PFLT_FILTER Filter,
                              __in_opt  PFLT_INSTANCE Instance,
                              __out     PHANDLE FileHandle,
                              __out     PFILE_OBJECT* FileObject,
                              __in      ACCESS_MASK DesiredAccess,
                              __in      POBJECT_ATTRIBUTES ObjectAttributes,
                              __out     PIO_STATUS_BLOCK IoStatusBlock,
                              __in_opt  PLARGE_INTEGER AllocationSize,
                              __in      ULONG FileAttributes,
                              __in      ULONG ShareAccess,
                              __in      ULONG CreateDisposition,
                              __in      ULONG CreateOptions,
                              __in_opt  PVOID EaBuffer,
                              __in      ULONG EaLength,
                              __in      ULONG Flags
    );

    /*!
        if os support FltCreateFileEx2, then call native FltCreateFileEx2
        , if not then call FltCreateFileEx or FltCreateFile and ObReferenceObjectByHandle

        전달받은 FileHandle 및 FileObject 는 더이상 필요가 없을 때 반드시 FltClose, ObDereferenceObject 를 호출하여 해제해야한다. 

        Support WinXP, code from MS Namechanger sample code
    */
    NTSTATUS FltCreateFileEx2( __in PFLT_FILTER                   Filter,
                               __in_opt PFLT_INSTANCE             Instance,
                               __out PHANDLE                      FileHandle,
                               __out_opt PFILE_OBJECT*            FileObject,
                               __in ACCESS_MASK                   DesiredAccess,
                               __in POBJECT_ATTRIBUTES            ObjectAttributes,
                               __out PIO_STATUS_BLOCK             IoStatusBlock,
                               __in_opt PLARGE_INTEGER            AllocationSize,
                               __in ULONG                         FileAttributes,
                               __in ULONG                         ShareAccess,
                               __in ULONG                         CreateDisposition,
                               __in ULONG                         CreateOptions,
                               __in_bcount_opt( EaLength ) PVOID  EaBuffer,
                               __in ULONG                         EaLength,
                               __in ULONG                         Flags,
                               __in_opt PIO_DRIVER_CREATE_CONTEXT DriverContext
    );

    /*!

        Support WinXP
    */
    NTSTATUS FltQueryDirectoryFile( __in       PFLT_INSTANCE Instance,
                                    __in       PFILE_OBJECT FileObject,
                                    __out      PVOID FileInformation,
                                    __in       ULONG Length,
                                    __in       FILE_INFORMATION_CLASS FileInformationClass,
                                    __in       BOOLEAN ReturnSingleEntry,
                                    __in_opt   PUNICODE_STRING FileName,
                                    __in       BOOLEAN RestartScan,
                                    __out_opt  PULONG LengthReturned
    );

    NTSTATUS FltQueryEaFile( __in PFLT_INSTANCE Instance,
                             __in PFILE_OBJECT FileObject,
                             __out_bcount_part( Length, *LengthReturned ) PVOID ReturnedEaData,
                             __in ULONG Length,
                             __in BOOLEAN ReturnSingleEntry,
                             __in_bcount_opt( EaListLength ) PVOID EaList,
                             __in ULONG EaListLength,
                             __in_opt PULONG EaIndex,
                             __in BOOLEAN RestartScan,
                             __out_opt PULONG LengthReturned
    );

    NTSTATUS FltSetEaFile( __in PFLT_INSTANCE Instance,
                           __in PFILE_OBJECT FileObject,
                           __in_bcount( Length ) PVOID EaBuffer,
                           __in ULONG Length
    );

    FLT_PREOP_CALLBACK_STATUS FltProcessFileLock( __in PFILE_LOCK FileLock,
                                                  __in PFLT_CALLBACK_DATA CallbackData,
                                                  __in_opt PVOID Context );

    /*!
        The IoReplaceFileObjectName routine replaces the name of a file object.

        Support WinXP
    */
    NTSTATUS IoReplaceFileObjectName( __in PFILE_OBJECT FileObject, __in PWSTR NewFileName, __in USHORT FileNameLength );

} // nsW32API

#endif // HDR_W32API