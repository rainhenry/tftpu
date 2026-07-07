/** @file
  The implementation for the 'tftpu' Shell command.

  Copyright (c) 2024, Tianocore. All rights reserved. <BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Tftpu.h"

#define IP4_CONFIG2_INTERFACE_INFO_NAME_LENGTH  32
EFI_HII_HANDLE  mTftpuHiiHandle;

STATIC CONST CHAR16  mTftpuProgressFrame[] = L"[                                        ]";
#define TFTPU_PROGRESS_SLIDER_STEPS  ((sizeof (mTftpuProgressFrame) / sizeof (CHAR16)) - 3)
#define TFTPU_PROGRESS_MESSAGE_SIZE  ((sizeof (mTftpuProgressFrame) / sizeof (CHAR16)) + 12)
STATIC CONST CHAR16  mTftpuProgressDelete[] = L"\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b";

SHELL_FILE_HANDLE  mFileHandle;
CONST CHAR16       *mLocalFilePath;

STATIC
BOOLEAN
StringToUint16 (
  IN   CONST CHAR16  *ValueStr,
  OUT  UINT16        *Value
  );

STATIC
EFI_STATUS
GetNicName (
  IN   EFI_HANDLE  ControllerHandle,
  IN   UINTN       NicNumber,
  OUT  CHAR16      *NicName
  );

STATIC
EFI_STATUS
CreateServiceChildAndOpenProtocol (
  IN   EFI_HANDLE  ControllerHandle,
  IN   EFI_GUID    *ServiceBindingProtocolGuid,
  IN   EFI_GUID    *ProtocolGuid,
  OUT  EFI_HANDLE  *ChildHandle,
  OUT  VOID        **Interface
  );

STATIC
VOID
CloseProtocolAndDestroyServiceChild (
  IN  EFI_HANDLE  ControllerHandle,
  IN  EFI_GUID    *ServiceBindingProtocolGuid,
  IN  EFI_GUID    *ProtocolGuid,
  IN  EFI_HANDLE  ChildHandle
  );

STATIC
EFI_STATUS
GetLocalFileSize (
  IN   CONST CHAR16  *LocalFilePath,
  OUT  UINTN         *FileSize
  );

STATIC
EFI_STATUS
GetFileList (
  OUT  CHAR16   ***FileList,
  OUT  UINTN    *FileCount
  );

STATIC
EFI_STATUS
UploadFile (
  IN   EFI_MTFTP4_PROTOCOL  *Mtftp4,
  IN   CONST CHAR16         *LocalFilePath,
  IN   CONST CHAR8          *AsciiRemoteFilePath,
  IN   UINTN                FileSize,
  IN   UINT16               BlockSize,
  IN   UINT16               WindowSize
  );

STATIC
EFI_STATUS
UploadFileWithNic (
  IN   EFI_HANDLE           ControllerHandle,
  IN   EFI_MTFTP4_CONFIG_DATA  *Mtftp4ConfigData,
  IN   CONST CHAR16         *LocalFilePath,
  IN   CONST CHAR8          *AsciiRemoteFilePath,
  IN   UINTN                FileSize,
  IN   UINT16               BlockSize,
  IN   UINT16               WindowSize,
  IN   CONST CHAR16         *UserNicName,
  OUT  BOOLEAN              *Success
  );

STATIC
EFI_STATUS
EFIAPI
CheckPacket (
  IN EFI_MTFTP4_PROTOCOL  *This,
  IN EFI_MTFTP4_TOKEN     *Token,
  IN UINT16               PacketLen,
  IN EFI_MTFTP4_PACKET    *Packet
  );

STATIC
EFI_STATUS
EFIAPI
PacketNeeded (
  IN  EFI_MTFTP4_PROTOCOL  *This,
  IN  EFI_MTFTP4_TOKEN     *Token,
  IN  OUT UINT16           *Length,
  OUT VOID                 **Buffer
  );

STATIC
VOID
PrintHelp (
  VOID
  );

EFI_MTFTP4_CONFIG_DATA  DefaultMtftp4ConfigData = {
  TRUE,
  {
    { 0, 0, 0, 0 }
  },
  {
    { 0, 0, 0, 0 }
  },
  0,
  {
    { 0, 0, 0, 0 }
  },
  {
    { 0, 0, 0, 0 }
  },
  69,
  6,
  4
};

STATIC CONST SHELL_PARAM_ITEM  ParamList[] = {
  { L"-i", TypeValue },
  { L"-l", TypeValue },
  { L"-r", TypeValue },
  { L"-c", TypeValue },
  { L"-t", TypeValue },
  { L"-s", TypeValue },
  { L"-w", TypeValue },
  { L"-h", TypeFlag  },
  { L"-?", TypeFlag  },
  { NULL,  TypeMax   }
};

#define MTFTP_DEFAULT_BLKSIZE  512
#define MTFTP_MIN_BLKSIZE  8
#define MTFTP_MAX_BLKSIZE  65464
#define MTFTP_DEFAULT_WINDOWSIZE  1
#define MTFTP_MIN_WINDOWSIZE  1
#define MTFTP_MAX_WINDOWSIZE  64

SHELL_STATUS
RunTftpu (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  SHELL_STATUS            ShellStatus;
  EFI_STATUS              Status;
  LIST_ENTRY              *CheckPackage;
  CHAR16                  *ProblemParam;
  UINTN                   ParamCount;
  CONST CHAR16            *UserNicName;
  BOOLEAN                 NicFound;
  CONST CHAR16            *ValueStr;
  CONST CHAR16            *RemoteFilePath;
  CHAR8                   *AsciiRemoteFilePath;
  UINTN                   FilePathSize;
  CONST CHAR16            *Walker;
  EFI_MTFTP4_CONFIG_DATA  Mtftp4ConfigData;
  EFI_HANDLE              *Handles;
  UINTN                   HandleCount;
  UINTN                   NicNumber;
  CHAR16                  NicName[IP4_CONFIG2_INTERFACE_INFO_NAME_LENGTH];
  EFI_HANDLE              ControllerHandle;
  UINTN                   FileSize;
  UINT16                  BlockSize;
  UINT16                  WindowSize;
  BOOLEAN                 BatchMode;
  CHAR16                  **FileList;
  UINTN                   FileCount;

  ShellStatus         = SHELL_INVALID_PARAMETER;
  ProblemParam        = NULL;
  NicFound            = FALSE;
  AsciiRemoteFilePath = NULL;
  Handles             = NULL;
  FileSize            = 0;
  BlockSize           = MTFTP_DEFAULT_BLKSIZE;
  WindowSize          = MTFTP_DEFAULT_WINDOWSIZE;
  BatchMode           = FALSE;
  FileList            = NULL;
  FileCount           = 0;

  Status = ShellInitialize ();
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return SHELL_ABORTED;
  }

  Status = ShellCommandLineParse (ParamList, &CheckPackage, &ProblemParam, TRUE);
  if (EFI_ERROR (Status)) {
    if ((Status == EFI_VOLUME_CORRUPTED) &&
        (ProblemParam != NULL))
    {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_GEN_PROBLEM),
        mTftpuHiiHandle,
        L"tftpu",
        ProblemParam
        );
      FreePool (ProblemParam);
    } else {
      ASSERT (FALSE);
    }

    PrintHelp ();
    goto Error;
  }

  if (ShellCommandLineGetFlag (CheckPackage, L"-h") ||
      ShellCommandLineGetFlag (CheckPackage, L"-?"))
  {
    PrintHelp ();
    ShellStatus = SHELL_SUCCESS;
    goto Error;
  }

  if (gEfiShellParametersProtocol != NULL) {
    for (UINTN Index = 0; Index < gEfiShellParametersProtocol->Argc; Index++) {
      if (StrCmp (gEfiShellParametersProtocol->Argv[Index], L"--help") == 0) {
        PrintHelp ();
        ShellStatus = SHELL_SUCCESS;
        goto Error;
      }
    }
  }

  ParamCount = ShellCommandLineGetCount (CheckPackage);
  if (ParamCount > 4) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_GEN_TOO_MANY),
      mTftpuHiiHandle,
      L"tftpu"
      );
    PrintHelp ();
    goto Error;
  }

  if (ParamCount < 3) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_GEN_TOO_FEW),
      mTftpuHiiHandle,
      L"tftpu"
      );
    PrintHelp ();
    goto Error;
  }

  CopyMem (&Mtftp4ConfigData, &DefaultMtftp4ConfigData, sizeof (EFI_MTFTP4_CONFIG_DATA));

  mLocalFilePath = ShellCommandLineGetRawValue (CheckPackage, 1);

  if (StrCmp (mLocalFilePath, L"*") == 0) {
    BatchMode = TRUE;
  }

  ValueStr = ShellCommandLineGetRawValue (CheckPackage, 2);
  Status   = NetLibStrToIp4 (ValueStr, &Mtftp4ConfigData.ServerIp);
  if (EFI_ERROR (Status)) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_GEN_PARAM_INV),
      mTftpuHiiHandle,
      L"tftpu",
      ValueStr
      );
    PrintHelp ();
    goto Error;
  }

  if (ParamCount == 4) {
    RemoteFilePath = ShellCommandLineGetRawValue (CheckPackage, 3);
  } else {
    RemoteFilePath = NULL;
  }

  UserNicName = ShellCommandLineGetValue (CheckPackage, L"-i");

  ValueStr = ShellCommandLineGetValue (CheckPackage, L"-l");
  if (ValueStr != NULL) {
    if (!StringToUint16 (ValueStr, &Mtftp4ConfigData.LocalPort)) {
      PrintHelp ();
      goto Error;
    }
  }

  ValueStr = ShellCommandLineGetValue (CheckPackage, L"-r");
  if (ValueStr != NULL) {
    if (!StringToUint16 (ValueStr, &Mtftp4ConfigData.InitialServerPort)) {
      PrintHelp ();
      goto Error;
    }
  }

  ValueStr = ShellCommandLineGetValue (CheckPackage, L"-c");
  if (ValueStr != NULL) {
    if (!StringToUint16 (ValueStr, &Mtftp4ConfigData.TryCount)) {
      PrintHelp ();
      goto Error;
    }

    if (Mtftp4ConfigData.TryCount == 0) {
      Mtftp4ConfigData.TryCount = 6;
    }
  }

  ValueStr = ShellCommandLineGetValue (CheckPackage, L"-t");
  if (ValueStr != NULL) {
    if (!StringToUint16 (ValueStr, &Mtftp4ConfigData.TimeoutValue)) {
      PrintHelp ();
      goto Error;
    }

    if (Mtftp4ConfigData.TimeoutValue == 0) {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_GEN_PARAM_INV),
        mTftpuHiiHandle,
        L"tftpu",
        ValueStr
        );
      PrintHelp ();
      goto Error;
    }
  }

  ValueStr = ShellCommandLineGetValue (CheckPackage, L"-s");
  if (ValueStr != NULL) {
    if (!StringToUint16 (ValueStr, &BlockSize)) {
      PrintHelp ();
      goto Error;
    }

    if ((BlockSize < MTFTP_MIN_BLKSIZE) || (BlockSize > MTFTP_MAX_BLKSIZE)) {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_GEN_PARAM_INV),
        mTftpuHiiHandle,
        L"tftpu",
        ValueStr
        );
      PrintHelp ();
      goto Error;
    }
  }

  ValueStr = ShellCommandLineGetValue (CheckPackage, L"-w");
  if (ValueStr != NULL) {
    if (!StringToUint16 (ValueStr, &WindowSize)) {
      PrintHelp ();
      goto Error;
    }

    if ((WindowSize < MTFTP_MIN_WINDOWSIZE) || (WindowSize > MTFTP_MAX_WINDOWSIZE)) {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_GEN_PARAM_INV),
        mTftpuHiiHandle,
        L"tftpu",
        ValueStr
        );
      PrintHelp ();
      goto Error;
    }
  }

  ShellStatus = SHELL_NOT_FOUND;
  Status      = gBS->LocateHandleBuffer (
                       ByProtocol,
                       &gEfiManagedNetworkServiceBindingProtocolGuid,
                       NULL,
                       &HandleCount,
                       &Handles
                       );
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_TFTPU_ERR_NO_NIC),
      mTftpuHiiHandle
      );
    goto Error;
  }

  if (BatchMode) {
    Status = GetFileList (&FileList, &FileCount);
    if (EFI_ERROR (Status)) {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_TFTPU_ERR_BATCH_LIST),
        mTftpuHiiHandle,
        Status
        );
      goto Error;
    }

    if (FileCount == 0) {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_TFTPU_ERR_NO_FILES),
        mTftpuHiiHandle
        );
      goto Error;
    }

    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_TFTPU_BATCH_UPLOAD),
      mTftpuHiiHandle,
      FileCount
      );

    for (UINTN FileIndex = 0; FileIndex < FileCount; FileIndex++) {
      CONST CHAR16  *LocalFile;
      BOOLEAN       Success;

      LocalFile = FileList[FileIndex];

      Status = GetLocalFileSize (LocalFile, &FileSize);
      if (EFI_ERROR (Status)) {
        ShellPrintHiiEx (
          -1,
          -1,
          NULL,
          STRING_TOKEN (STR_TFTPU_ERR_FILE_SIZE),
          mTftpuHiiHandle,
          LocalFile,
          Status
          );
        continue;
      }

      Walker = LocalFile + StrLen (LocalFile);
      while ((--Walker) >= LocalFile) {
        if ((*Walker == L'\\') ||
            (*Walker == L'/'))
        {
          break;
        }
      }

      Walker = LocalFile + StrLen (LocalFile);
      while ((--Walker) >= LocalFile) {
        if ((*Walker == L'\\') ||
            (*Walker == L'/'))
        {
          break;
        }
      }

      CONST CHAR16 *FileNameOnly = Walker + 1;
      CHAR16 *FullRemotePath;

      if (RemoteFilePath != NULL) {
        UINTN RemoteLen = StrLen (RemoteFilePath);
        BOOLEAN IsDirectory = (RemoteLen > 0) &&
                              ((RemoteFilePath[RemoteLen - 1] == L'/') ||
                               (RemoteFilePath[RemoteLen - 1] == L'\\'));

        if (IsDirectory) {
          UINTN FullPathSize = RemoteLen + StrLen (FileNameOnly) + 1;
          FullRemotePath = AllocatePool (FullPathSize * sizeof (CHAR16));
          if (FullRemotePath == NULL) {
            ShellStatus = SHELL_OUT_OF_RESOURCES;
            goto Error;
          }

          UnicodeSPrint (FullRemotePath, FullPathSize * sizeof (CHAR16), L"%s%s", RemoteFilePath, FileNameOnly);
        } else {
          UINTN FullPathSize = StrLen (RemoteFilePath) + 1;
          FullRemotePath = AllocatePool (FullPathSize * sizeof (CHAR16));
          if (FullRemotePath == NULL) {
            ShellStatus = SHELL_OUT_OF_RESOURCES;
            goto Error;
          }

          StrCpyS (FullRemotePath, FullPathSize, RemoteFilePath);
        }
      } else {
        UINTN FullPathSize = StrLen (FileNameOnly) + 1;
        FullRemotePath = AllocatePool (FullPathSize * sizeof (CHAR16));
        if (FullRemotePath == NULL) {
          ShellStatus = SHELL_OUT_OF_RESOURCES;
          goto Error;
        }

        StrCpyS (FullRemotePath, FullPathSize, FileNameOnly);
      }

      FilePathSize = StrLen (FullRemotePath) + 1;
      AsciiRemoteFilePath = AllocatePool (FilePathSize);
      if (AsciiRemoteFilePath == NULL) {
        FreePool (FullRemotePath);
        ShellStatus = SHELL_OUT_OF_RESOURCES;
        goto Error;
      }

      UnicodeStrToAsciiStrS (FullRemotePath, AsciiRemoteFilePath, FilePathSize);
      FreePool (FullRemotePath);

      Success = FALSE;
      for (NicNumber = 0;
           (NicNumber < HandleCount) && (!Success);
           NicNumber++)
      {
        ControllerHandle = Handles[NicNumber];

        Status = GetNicName (ControllerHandle, NicNumber, NicName);
        if (EFI_ERROR (Status)) {
          continue;
        }

        if (UserNicName != NULL) {
          if (StrCmp (NicName, UserNicName) != 0) {
            continue;
          }

          NicFound = TRUE;
        }

        Status = UploadFileWithNic (
                   ControllerHandle,
                   &Mtftp4ConfigData,
                   LocalFile,
                   AsciiRemoteFilePath,
                   FileSize,
                   BlockSize,
                   WindowSize,
                   UserNicName,
                   &Success
                   );
      }

      if (!Success) {
        ShellPrintHiiEx (
          -1,
          -1,
          NULL,
          STRING_TOKEN (STR_TFTPU_ERR_UPLOAD),
          mTftpuHiiHandle,
          LocalFile,
          NicName,
          Status
          );
      }

      if (AsciiRemoteFilePath != NULL) {
        FreePool (AsciiRemoteFilePath);
        AsciiRemoteFilePath = NULL;
      }
    }

    ShellStatus = SHELL_SUCCESS;
  } else {
    Walker = mLocalFilePath + StrLen (mLocalFilePath);
    while ((--Walker) >= mLocalFilePath) {
      if ((*Walker == L'\\') ||
          (*Walker == L'/'))
      {
        break;
      }
    }

    CONST CHAR16 *FileNameOnly = Walker + 1;
    CHAR16 *FullRemotePath;

    if (RemoteFilePath != NULL) {
      UINTN RemoteLen = StrLen (RemoteFilePath);
      BOOLEAN IsDirectory = (RemoteLen > 0) &&
                            ((RemoteFilePath[RemoteLen - 1] == L'/') ||
                             (RemoteFilePath[RemoteLen - 1] == L'\\'));

      if (IsDirectory) {
        UINTN FullPathSize = RemoteLen + StrLen (FileNameOnly) + 1;
        FullRemotePath = AllocatePool (FullPathSize * sizeof (CHAR16));
        if (FullRemotePath == NULL) {
          ShellStatus = SHELL_OUT_OF_RESOURCES;
          goto Error;
        }

        UnicodeSPrint (FullRemotePath, FullPathSize * sizeof (CHAR16), L"%s%s", RemoteFilePath, FileNameOnly);
      } else {
        UINTN FullPathSize = StrLen (RemoteFilePath) + 1;
        FullRemotePath = AllocatePool (FullPathSize * sizeof (CHAR16));
        if (FullRemotePath == NULL) {
          ShellStatus = SHELL_OUT_OF_RESOURCES;
          goto Error;
        }

        StrCpyS (FullRemotePath, FullPathSize, RemoteFilePath);
      }
    } else {
      UINTN FullPathSize = StrLen (FileNameOnly) + 1;
      FullRemotePath = AllocatePool (FullPathSize * sizeof (CHAR16));
      if (FullRemotePath == NULL) {
        ShellStatus = SHELL_OUT_OF_RESOURCES;
        goto Error;
      }

      StrCpyS (FullRemotePath, FullPathSize, FileNameOnly);
    }

    FilePathSize = StrLen (FullRemotePath) + 1;
    AsciiRemoteFilePath = AllocatePool (FilePathSize);
    if (AsciiRemoteFilePath == NULL) {
      FreePool (FullRemotePath);
      ShellStatus = SHELL_OUT_OF_RESOURCES;
      goto Error;
    }

    UnicodeStrToAsciiStrS (FullRemotePath, AsciiRemoteFilePath, FilePathSize);
    FreePool (FullRemotePath);

    Status = GetLocalFileSize (mLocalFilePath, &FileSize);
    if (EFI_ERROR (Status)) {
      ShellPrintHiiEx (
        -1,
        -1,
        NULL,
        STRING_TOKEN (STR_TFTPU_ERR_FILE_SIZE),
        mTftpuHiiHandle,
        mLocalFilePath,
        Status
        );
      goto Error;
    }

    for (NicNumber = 0;
         (NicNumber < HandleCount) && (ShellStatus != SHELL_SUCCESS);
         NicNumber++)
    {
      ControllerHandle = Handles[NicNumber];

      Status = GetNicName (ControllerHandle, NicNumber, NicName);
      if (EFI_ERROR (Status)) {
        ShellPrintHiiEx (
          -1,
          -1,
          NULL,
          STRING_TOKEN (STR_TFTPU_ERR_NIC_NAME),
          mTftpuHiiHandle,
          NicNumber,
          Status
          );
        continue;
      }

      if (UserNicName != NULL) {
        if (StrCmp (NicName, UserNicName) != 0) {
          continue;
        }

        NicFound = TRUE;
      }

      BOOLEAN Success = FALSE;
      Status = UploadFileWithNic (
                 ControllerHandle,
                 &Mtftp4ConfigData,
                 mLocalFilePath,
                 AsciiRemoteFilePath,
                 FileSize,
                 BlockSize,
                 WindowSize,
                 UserNicName,
                 &Success
                 );
      if (Success) {
        ShellStatus = SHELL_SUCCESS;
      }
    }
  }

  if ((UserNicName != NULL) && (!NicFound)) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_TFTPU_ERR_NIC_NOT_FOUND),
      mTftpuHiiHandle,
      UserNicName
      );
  }

Error:
  ShellCommandLineFreeVarList (CheckPackage);
  if (AsciiRemoteFilePath != NULL) {
    FreePool (AsciiRemoteFilePath);
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  if (FileList != NULL) {
    for (UINTN i = 0; i < FileCount; i++) {
      if (FileList[i] != NULL) {
        FreePool (FileList[i]);
      }
    }

    FreePool (FileList);
  }

  if ((ShellStatus != SHELL_SUCCESS) && (EFI_ERROR (Status))) {
    ShellStatus = Status & ~MAX_BIT;
  }

  return ShellStatus;
}

STATIC
BOOLEAN
StringToUint16 (
  IN   CONST CHAR16  *ValueStr,
  OUT  UINT16        *Value
  )
{
  UINTN  Val;

  Val = ShellStrToUintn (ValueStr);
  if (Val > MAX_UINT16) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_GEN_PARAM_INV),
      mTftpuHiiHandle,
      L"tftpu",
      ValueStr
      );
    return FALSE;
  }

  *Value = (UINT16)Val;
  return TRUE;
}

STATIC
EFI_STATUS
GetNicName (
  IN   EFI_HANDLE  ControllerHandle,
  IN   UINTN       NicNumber,
  OUT  CHAR16      *NicName
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    MnpHandle;
  EFI_MANAGED_NETWORK_PROTOCOL  *Mnp;
  EFI_SIMPLE_NETWORK_MODE       SnpMode;

  Status = CreateServiceChildAndOpenProtocol (
             ControllerHandle,
             &gEfiManagedNetworkServiceBindingProtocolGuid,
             &gEfiManagedNetworkProtocolGuid,
             &MnpHandle,
             (VOID **)&Mnp
             );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = Mnp->GetModeData (Mnp, NULL, &SnpMode);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_STARTED)) {
    goto Error;
  }

  UnicodeSPrint (
    NicName,
    IP4_CONFIG2_INTERFACE_INFO_NAME_LENGTH,
    SnpMode.IfType == NET_IFTYPE_ETHERNET ?
    L"eth%d" :
    L"unk%d",
    NicNumber
    );

  Status = EFI_SUCCESS;

Error:
  if (MnpHandle != NULL) {
    CloseProtocolAndDestroyServiceChild (
      ControllerHandle,
      &gEfiManagedNetworkServiceBindingProtocolGuid,
      &gEfiManagedNetworkProtocolGuid,
      MnpHandle
      );
  }

  return Status;
}

STATIC
EFI_STATUS
CreateServiceChildAndOpenProtocol (
  IN   EFI_HANDLE  ControllerHandle,
  IN   EFI_GUID    *ServiceBindingProtocolGuid,
  IN   EFI_GUID    *ProtocolGuid,
  OUT  EFI_HANDLE  *ChildHandle,
  OUT  VOID        **Interface
  )
{
  EFI_STATUS  Status;

  *ChildHandle = NULL;
  Status       = NetLibCreateServiceChild (
                   ControllerHandle,
                   gImageHandle,
                   ServiceBindingProtocolGuid,
                   ChildHandle
                   );
  if (!EFI_ERROR (Status)) {
    Status = gBS->OpenProtocol (
                    *ChildHandle,
                    ProtocolGuid,
                    Interface,
                    gImageHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      NetLibDestroyServiceChild (
        ControllerHandle,
        gImageHandle,
        ServiceBindingProtocolGuid,
        *ChildHandle
        );
      *ChildHandle = NULL;
    }
  }

  return Status;
}

STATIC
VOID
CloseProtocolAndDestroyServiceChild (
  IN  EFI_HANDLE  ControllerHandle,
  IN  EFI_GUID    *ServiceBindingProtocolGuid,
  IN  EFI_GUID    *ProtocolGuid,
  IN  EFI_HANDLE  ChildHandle
  )
{
  gBS->CloseProtocol (
         ChildHandle,
         ProtocolGuid,
         gImageHandle,
         ControllerHandle
         );

  NetLibDestroyServiceChild (
    ControllerHandle,
    gImageHandle,
    ServiceBindingProtocolGuid,
    ChildHandle
    );
}

STATIC
EFI_STATUS
GetLocalFileSize (
  IN   CONST CHAR16  *LocalFilePath,
  OUT  UINTN         *FileSize
  )
{
  EFI_STATUS        Status;
  SHELL_FILE_HANDLE FileHandle;
  UINT64            Size;

  *FileSize = 0;

  Status = ShellOpenFileByName (
             LocalFilePath,
             &FileHandle,
             EFI_FILE_MODE_READ,
             0
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ShellGetFileSize (FileHandle, &Size);
  if (!EFI_ERROR (Status)) {
    *FileSize = (UINTN)Size;
  }

  ShellCloseFile (&FileHandle);
  return Status;
}

STATIC
EFI_STATUS
GetFileList (
  OUT  CHAR16   ***FileList,
  OUT  UINTN    *FileCount
  )
{
  EFI_STATUS           Status;
  SHELL_FILE_HANDLE    DirHandle;
  EFI_FILE_INFO        *FileInfo;
  BOOLEAN              NoFile;
  CHAR16               **List;
  UINTN                Count;
  UINTN                Capacity;

  *FileList  = NULL;
  *FileCount = 0;

  Status = ShellOpenFileByName (
             L".",
             &DirHandle,
             EFI_FILE_MODE_READ,
             0
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Count    = 0;
  Capacity = 16;
  List     = AllocatePool (Capacity * sizeof (CHAR16 *));
  if (List == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  NoFile = FALSE;
  Status = ShellFindFirstFile (DirHandle, &FileInfo);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  while (!NoFile) {
    if ((FileInfo->Attribute & EFI_FILE_DIRECTORY) == 0) {
      if (Count >= Capacity) {
        CHAR16 **NewList;

        Capacity *= 2;
        NewList = ReallocatePool (
                    (Count * sizeof (CHAR16 *)),
                    (Capacity * sizeof (CHAR16 *)),
                    List
                    );
        if (NewList == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
          break;
        }

        List = NewList;
      }

      List[Count] = AllocateCopyPool (
                      (StrLen (FileInfo->FileName) + 1) * sizeof (CHAR16),
                      FileInfo->FileName
                      );
      if (List[Count] == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }

      Count++;
    }

    Status = ShellFindNextFile (DirHandle, FileInfo, &NoFile);
    if (EFI_ERROR (Status)) {
      break;
    }
  }

  if (!EFI_ERROR (Status)) {
    *FileList  = List;
    *FileCount = Count;
    List       = NULL;
  }

Error:
  if (List != NULL) {
    for (UINTN i = 0; i < Count; i++) {
      if (List[i] != NULL) {
        FreePool (List[i]);
      }
    }

    FreePool (List);
  }

  ShellCloseFile (&DirHandle);
  return Status;
}

STATIC
EFI_STATUS
UploadFile (
  IN   EFI_MTFTP4_PROTOCOL  *Mtftp4,
  IN   CONST CHAR16         *LocalFilePath,
  IN   CONST CHAR8          *AsciiRemoteFilePath,
  IN   UINTN                FileSize,
  IN   UINT16               BlockSize,
  IN   UINT16               WindowSize
  )
{
  EFI_STATUS         Status;
  UPLOAD_CONTEXT     *TftpContext;
  EFI_MTFTP4_TOKEN   Mtftp4Token;
  UINT8              BlksizeBuf[10];
  UINT8              WindowsizeBuf[10];

  ZeroMem (&Mtftp4Token, sizeof (EFI_MTFTP4_TOKEN));

  TftpContext = AllocatePool (sizeof (UPLOAD_CONTEXT));
  if (TftpContext == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  TftpContext->FileSize              = FileSize;
  TftpContext->UploadedNbOfBytes     = 0;
  TftpContext->LastReportedNbOfBytes = 0;

  Mtftp4Token.Filename     = (UINT8 *)AsciiRemoteFilePath;
  Mtftp4Token.CheckPacket  = CheckPacket;
  Mtftp4Token.Context      = (VOID *)TftpContext;
  Mtftp4Token.PacketNeeded = PacketNeeded;
  Mtftp4Token.OptionCount  = 0;
  Mtftp4Token.OptionList   = AllocatePool (sizeof (EFI_MTFTP4_OPTION) * 2);
  if (Mtftp4Token.OptionList == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  if (BlockSize != MTFTP_DEFAULT_BLKSIZE) {
    Mtftp4Token.OptionList[Mtftp4Token.OptionCount].OptionStr = (UINT8 *)"blksize";
    AsciiSPrint ((CHAR8 *)BlksizeBuf, sizeof (BlksizeBuf), "%d", BlockSize);
    Mtftp4Token.OptionList[Mtftp4Token.OptionCount].ValueStr = BlksizeBuf;
    Mtftp4Token.OptionCount++;
  }

  if (WindowSize != MTFTP_DEFAULT_WINDOWSIZE) {
    Mtftp4Token.OptionList[Mtftp4Token.OptionCount].OptionStr = (UINT8 *)"windowsize";
    AsciiSPrint ((CHAR8 *)WindowsizeBuf, sizeof (WindowsizeBuf), "%d", WindowSize);
    Mtftp4Token.OptionList[Mtftp4Token.OptionCount].ValueStr = WindowsizeBuf;
    Mtftp4Token.OptionCount++;
  }

  ShellPrintHiiEx (
    -1,
    -1,
    NULL,
    STRING_TOKEN (STR_TFTPU_UPLOADING),
    mTftpuHiiHandle,
    LocalFilePath
    );

  Status = ShellOpenFileByName (
             LocalFilePath,
             &mFileHandle,
             EFI_FILE_MODE_READ,
             0
             );
  if (EFI_ERROR (Status)) {
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_GEN_FILE_OPEN_FAIL),
      mTftpuHiiHandle,
      L"tftpu",
      LocalFilePath
      );
    goto Error;
  }

  Status = Mtftp4->WriteFile (Mtftp4, &Mtftp4Token);
  ShellPrintHiiEx (
    -1,
    -1,
    NULL,
    STRING_TOKEN (STR_GEN_CRLF),
    mTftpuHiiHandle
    );

  ShellCloseFile (&mFileHandle);

Error:
  if (TftpContext != NULL) {
    FreePool (TftpContext);
  }

  if (Mtftp4Token.OptionList != NULL) {
    FreePool (Mtftp4Token.OptionList);
  }

  return Status;
}

STATIC
EFI_STATUS
UploadFileWithNic (
  IN   EFI_HANDLE              ControllerHandle,
  IN   EFI_MTFTP4_CONFIG_DATA  *Mtftp4ConfigData,
  IN   CONST CHAR16            *LocalFilePath,
  IN   CONST CHAR8             *AsciiRemoteFilePath,
  IN   UINTN                   FileSize,
  IN   UINT16                  BlockSize,
  IN   UINT16                  WindowSize,
  IN   CONST CHAR16            *UserNicName,
  OUT  BOOLEAN                 *Success
  )
{
  EFI_STATUS        Status;
  EFI_HANDLE        Mtftp4ChildHandle;
  EFI_MTFTP4_PROTOCOL *Mtftp4;
  CHAR16            NicName[IP4_CONFIG2_INTERFACE_INFO_NAME_LENGTH];

  *Success = FALSE;

  Status = GetNicName (ControllerHandle, 0, NicName);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (UserNicName != NULL) {
    if (StrCmp (NicName, UserNicName) != 0) {
      return EFI_NOT_FOUND;
    }
  }

  Status = CreateServiceChildAndOpenProtocol (
             ControllerHandle,
             &gEfiMtftp4ServiceBindingProtocolGuid,
             &gEfiMtftp4ProtocolGuid,
             &Mtftp4ChildHandle,
             (VOID **)&Mtftp4
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Mtftp4->Configure (Mtftp4, Mtftp4ConfigData);
  if (EFI_ERROR (Status)) {
    CloseProtocolAndDestroyServiceChild (
      ControllerHandle,
      &gEfiMtftp4ServiceBindingProtocolGuid,
      &gEfiMtftp4ProtocolGuid,
      Mtftp4ChildHandle
      );
    return Status;
  }

  Status = UploadFile (Mtftp4, LocalFilePath, AsciiRemoteFilePath, FileSize, BlockSize, WindowSize);
  if (!EFI_ERROR (Status)) {
    *Success = TRUE;
  }

  CloseProtocolAndDestroyServiceChild (
    ControllerHandle,
    &gEfiMtftp4ServiceBindingProtocolGuid,
    &gEfiMtftp4ProtocolGuid,
    Mtftp4ChildHandle
    );

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
CheckPacket (
  IN EFI_MTFTP4_PROTOCOL  *This,
  IN EFI_MTFTP4_TOKEN     *Token,
  IN UINT16               PacketLen,
  IN EFI_MTFTP4_PACKET    *Packet
  )
{
  UINT16   Opcode;
  CHAR16   ErrorMessage[256];

  Opcode = NTOHS (Packet->OpCode);

  if (Opcode == EFI_MTFTP4_OPCODE_ERROR) {
    AsciiStrToUnicodeStrS ((CHAR8 *)Packet->Error.ErrorMessage, ErrorMessage, sizeof (ErrorMessage));
    ShellPrintHiiEx (
      -1,
      -1,
      NULL,
      STRING_TOKEN (STR_TFTPU_ERR_SERVER_ERROR),
      mTftpuHiiHandle,
      NTOHS (Packet->Error.ErrorCode),
      ErrorMessage
      );
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PacketNeeded (
  IN  EFI_MTFTP4_PROTOCOL  *This,
  IN  EFI_MTFTP4_TOKEN     *Token,
  IN  OUT UINT16           *Length,
  OUT VOID                 **Buffer
  )
{
  UPLOAD_CONTEXT  *Context;
  CHAR16          Progress[TFTPU_PROGRESS_MESSAGE_SIZE];
  UINTN           NbOfKb;
  UINTN           Index;
  UINTN           LastStep;
  UINTN           Step;
  EFI_STATUS      Status;
  UINTN           ReadLen;

  Context = (UPLOAD_CONTEXT *)Token->Context;

  if (Context->UploadedNbOfBytes >= Context->FileSize) {
    *Length = 0;
    *Buffer = NULL;
    return EFI_SUCCESS;
  }

  ReadLen = (UINTN)*Length;
  if ((Context->UploadedNbOfBytes + ReadLen) > Context->FileSize) {
    ReadLen = Context->FileSize - Context->UploadedNbOfBytes;
  }

  *Buffer = AllocatePool (ReadLen);
  if (*Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ShellReadFile (mFileHandle, &ReadLen, *Buffer);
  if (EFI_ERROR (Status)) {
    FreePool (*Buffer);
    *Buffer = NULL;
    *Length = 0;
    return Status;
  }

  *Length = (UINT16)ReadLen;

  if (Context->UploadedNbOfBytes == 0) {
    ShellPrintEx (-1, -1, L"%s       0 Kb", mTftpuProgressFrame);
  }

  Context->UploadedNbOfBytes += ReadLen;
  NbOfKb                     = Context->UploadedNbOfBytes / 1024;

  Progress[0] = L'\0';
  LastStep    = (Context->LastReportedNbOfBytes * TFTPU_PROGRESS_SLIDER_STEPS) / Context->FileSize;
  Step        = (Context->UploadedNbOfBytes * TFTPU_PROGRESS_SLIDER_STEPS) / Context->FileSize;

  if (Step <= LastStep) {
    return EFI_SUCCESS;
  }

  ShellPrintEx (-1, -1, L"%s", mTftpuProgressDelete);

  Status = StrCpyS (Progress, TFTPU_PROGRESS_MESSAGE_SIZE, mTftpuProgressFrame);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 1; Index < Step; Index++) {
    Progress[Index] = L'=';
  }

  Progress[Step] = L'>';

  UnicodeSPrint (
    Progress + (sizeof (mTftpuProgressFrame) / sizeof (CHAR16)) - 1,
    sizeof (Progress) - sizeof (mTftpuProgressFrame),
    L" %7d Kb",
    NbOfKb
    );
  Context->LastReportedNbOfBytes = Context->UploadedNbOfBytes;

  ShellPrintEx (-1, -1, L"%s", Progress);

  return EFI_SUCCESS;
}

STATIC
VOID
PrintHelp (
  VOID
  )
{
  ShellPrintHiiEx (
    -1,
    -1,
    NULL,
    STRING_TOKEN (STR_GET_HELP_TFTPU),
    mTftpuHiiHandle
    );
}

EFI_HII_HANDLE
InitializeHiiPackage (
  EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                   Status;
  EFI_HII_PACKAGE_LIST_HEADER  *PackageList;
  EFI_HII_HANDLE               HiiHandle;

  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiHiiPackageListProtocolGuid,
                  (VOID **)&PackageList,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  Status = gHiiDatabase->NewPackageList (
                           gHiiDatabase,
                           PackageList,
                           NULL,
                           &HiiHandle
                           );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  return HiiHandle;
}