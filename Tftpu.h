/** @file
  Header file for 'tftpu' command functions.

  Copyright (c) 2024, Tianocore. All rights reserved. <BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _TFTPU_H_
#define _TFTPU_H_

#include <Uefi.h>

#include <Protocol/HiiPackageList.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/Mtftp4.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ShellLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/HiiLib.h>
#include <Library/NetLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiHiiServicesLib.h>

extern EFI_HII_HANDLE  mTftpuHiiHandle;

typedef struct {
  UINTN    FileSize;
  UINTN    UploadedNbOfBytes;
  UINTN    LastReportedNbOfBytes;
} UPLOAD_CONTEXT;

SHELL_STATUS
RunTftpu (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );

EFI_HII_HANDLE
InitializeHiiPackage (
  EFI_HANDLE  ImageHandle
  );

#endif // _TFTPU_H_