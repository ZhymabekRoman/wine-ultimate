/*
 * Copyright (C) 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _DISMAPI_H_
#define _DISMAPI_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT DismSession;

typedef void (CALLBACK *DISM_PROGRESS_CALLBACK)(UINT Current, UINT Total, PVOID UserData);

#define DISM_ONLINE_IMAGE L"DISM_{53BFAE52-B167-4E2F-A258-0A37B57FF845}"
#define DISM_SESSION_DEFAULT 0

#define DISM_MOUNT_READWRITE        0x00000000
#define DISM_MOUNT_READONLY         0x00000001
#define DISM_MOUNT_OPTIMIZE         0x00000002
#define DISM_MOUNT_CHECK_INTEGRITY  0x00000004

#define DISM_COMMIT_IMAGE           0x00000000
#define DISM_DISCARD_IMAGE          0x00000001

#define DISM_RESERVED_STORAGE_DISABLED 0x00000000
#define DISM_RESERVED_STORAGE_ENABLED  0x00000001

/* Error codes */
#define DISMAPI_E_DISMAPI_ALREADY_INITIALIZED 0xC0040002
#define DISMAPI_E_OPEN_SESSION_HANDLES        0xC0040003
#define DISMAPI_E_INVALID_DISM_SESSION        0xC0040004
#define DISMAPI_E_MUST_SPECIFY_ONLINE_IMAGE   0xC004000E
#define DISMAPI_E_UNKNOWN_FEATURE             0x800f080c
#define DISMAPI_E_BUSY                        0x800f0902

typedef enum _DismLogLevel
{
    DismLogErrors = 0,
    DismLogErrorsWarnings,
    DismLogErrorsWarningsInfo
} DismLogLevel;

typedef enum _DismPackageIdentifier
{
    DismPackageNone = 0,
    DismPackageName,
    DismPackagePath
} DismPackageIdentifier, DismPackageType;

typedef enum _DismPackageFeatureState
{
    DismStateNotPresent = 0,
    DismStateUninstallPending,
    DismStateStaged,
    DismStateResolved,
    DismStateRemoved = DismStateResolved,
    DismStateInstalled,
    DismStateInstallPending,
    DismStateSuperseded,
    DismStatePartiallyInstalled
} DismPackageFeatureState;

typedef enum _DismReleaseType
{
    DismReleaseTypeCriticalUpdate = 0,
    DismReleaseTypeDriver,
    DismReleaseTypeFeaturePack,
    DismReleaseTypeHotfix,
    DismReleaseTypeSecurityUpdate,
    DismReleaseTypeSoftwareUpdate,
    DismReleaseTypeUpdate,
    DismReleaseTypeUpdateRollup,
    DismReleaseTypeLanguagePack,
    DismReleaseTypeFoundation,
    DismReleaseTypeServicePack,
    DismReleaseTypeProduct,
    DismReleaseTypeLocalPack,
    DismReleaseTypeOther,
    DismReleaseTypeOnDemandPack
} DismReleaseType;

typedef enum _DismRestartType
{
    DismRestartNo = 0,
    DismRestartPossible,
    DismRestartRequired
} DismRestartType;

typedef enum _DismFullyOfflineInstallableType
{
    DismFullyOfflineInstallable = 0,
    DismFullyOfflineNotInstallable,
    DismFullyOfflineInstallableUndetermined
} DismFullyOfflineInstallableType;

#pragma pack(push, 1)

typedef struct _DismCustomProperty
{
    PCWSTR Name;
    PCWSTR Value;
    PCWSTR Path;
} DismCustomProperty;

typedef struct _DismFeature
{
    PCWSTR FeatureName;
    DismPackageFeatureState State;
} DismFeature;

typedef struct _DismFeatureInfo
{
    PCWSTR FeatureName;
    DismPackageFeatureState FeatureState;
    PCWSTR DisplayName;
    PCWSTR Description;
    DismRestartType RestartRequired;
    DismCustomProperty *CustomProperty;
    UINT CustomPropertyCount;
} DismFeatureInfo;

typedef struct _DismString
{
    PCWSTR Value;
} DismString;

#pragma pack(pop)

HRESULT WINAPI DismInitialize(DismLogLevel LogLevel, PCWSTR LogFilePath, PCWSTR ScratchDirectory);
HRESULT WINAPI DismShutdown(void);
HRESULT WINAPI DismOpenSession(PCWSTR ImagePath, PCWSTR WindowsDirectory, PCWSTR SystemDrive, DismSession *Session);
HRESULT WINAPI DismCloseSession(DismSession Session);
HRESULT WINAPI DismGetFeatures(DismSession Session, PCWSTR Identifier, DismPackageType PackageType, DismFeature **Feature, UINT *Count);
HRESULT WINAPI DismGetFeatureInfo(DismSession Session, PCWSTR FeatureName, PCWSTR Identifier, DismPackageType PackageType, DismFeatureInfo **FeatureInfo);
HRESULT WINAPI DismEnableFeature(DismSession Session, PCWSTR FeatureName, PCWSTR Identifier, DismPackageType PackageType, BOOL LimitAccess, PCWSTR *SourcePaths, UINT SourcePathCount, BOOL EnableAllDependencies, HANDLE CancelEvent, DISM_PROGRESS_CALLBACK Progress, PVOID UserContext);
HRESULT WINAPI DismDisableFeature(DismSession Session, PCWSTR FeatureName, PCWSTR PackageName, BOOL RemovePayload, HANDLE CancelEvent, DISM_PROGRESS_CALLBACK Progress, PVOID UserContext);
HRESULT WINAPI DismDelete(VOID *DismStructure);

#ifdef __cplusplus
}
#endif

#endif /* _DISMAPI_H_ */
