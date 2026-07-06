/** @file
  Entrypoint of "tftpu" shell standalone application.

  Copyright (c) 2024, Tianocore. All rights reserved. <BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include "Tftpu.h"

GLOBAL_REMOVE_IF_UNREFERENCED EFI_STRING_ID  mStringHelpTokenId = STRING_TOKEN (STR_GET_HELP_TFTPU);

EFI_STATUS
EFIAPI
TftpuAppInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  mTftpuHiiHandle = InitializeHiiPackage (ImageHandle);
  if (mTftpuHiiHandle == NULL) {
    return EFI_ABORTED;
  }

  Status = (EFI_STATUS)RunTftpu (ImageHandle, SystemTable);
  HiiRemovePackages (mTftpuHiiHandle);
  return Status;
}