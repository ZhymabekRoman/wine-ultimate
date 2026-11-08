1 stub @
@ stdcall AppxFreeString(ptr)
@ stdcall AppxGetManifestPackageProperties(long long ptr)
@ stdcall AppxGetPackageId(long ptr)
@ stdcall AppxOpenAppxPackageContents(wstr ptr)
@ stub GetApplicability
@ stdcall AppxCloseAppxPackageContents(long)
@ stdcall AppxCreateSharedLocalFolder(wstr wstr)
@ stdcall AppxPackageRepositoryRecoverStagedPackages(ptr)
@ stdcall AppxPackageRepositoryRecoverUserInstalls(ptr)
@ stub AppxRecoverUserInstallsForUpgrade
@ stub AppxDeletePackageFiles
@ stdcall AppxRequestRemovePackageForUser(wstr wstr)
@ stdcall IsPackageInstalled(wstr ptr)
@ stub RDSRecoverRequests
@ stub AppxPreStageCleanupRunTask
@ stdcall AppxPreRegisterPackage(wstr wstr wstr ptr)
@ stub ReArmAppxPreStageCleanupTask
@ stub AppxAddPackageToAllUserStoreForPbr
@ stub GetPackageApplicabilityForUserLogon
16 stub @
@ stdcall AppxStagePackage(wstr wstr wstr ptr wstr ptr)
@ stdcall AppxGetPackageInstalledLocation(wstr ptr)
@ stdcall AppxDestagePackage(wstr)
@ stub GetApplicability2
@ stdcall AppxGetStagedPackageFullNameFromFamilyName(wstr ptr)
@ stdcall AppxIsStagedPackageStoreSigned(wstr ptr)
@ stub RegisterNotification
@ stub UnregisterNotification
@ stub GetNotificationPayload
@ stub NotifyPackageStatusChanged
@ stub VerifyPackage
@ stub AppxPreRegisterAllInboxPackages
@ stub AppxValidatePackagesWithOptions
@ stub AppxRemovePackageForAllUsers
@ stub RequestContentGroups
@ stub GetApplicability4
@ stub RequestContentGroupsForFullTrust
@ stub GeneratePreInstalledPriFiles
@ stub PopulateProtocolAndFTA
@ stub FixJunctionsForAppsIfNecessary
@ stdcall AppxRemovePackageForUserSid(wstr wstr)
@ stub CreateCanonicalPriFile
@ stub RegisterNotificationForUser
@ stub UnregisterNotificationForUser
@ stub GetNotificationPayloadForUser
@ stub AppxCleanupWCIReparsePoints
@ stub RepairPackageFileAcls
@ stub EnsurePackageFamilyIsRegisteredBeforeActivation
@ stub AppxCleanupSystemAppsMigratedToFOD
@ stub AppxCleanupOrphanPackages
@ stub CheckComCallerHasCapabilities
@ stub GetBundleApplicablePackages
@ stub GetApplicability5
@ stub DeleteApplicabilityInfoArray
@ stub UpdateAgentCreateDownload
@ stub UpdateAgentGetDownloadRanges
@ stub UpdateAgentCancelAllDownloads
@ stub UpdateDataSourceRegister
@ stub UpdateDataSourceAddRange
@ stub UpdateDataSourceRun
@ stub UpdateDataSourceCancelRun
@ stub GetPackageRegistrationStatusForUser
@ stub IsSharedAppsEnabled
@ stub AppInstallerUpdateAllTask
@ stub CheckAppInstallerUpdateAvailability
@ stub UpdateAgentFreeDownloadRanges
@ stdcall AppxRegisterPackage(wstr wstr wstr)
@ stub IsPackageMetadataUnderSystemMetadata
@ stub GetMetadataRootForPackage
@ stub ClientGetAllPackagesToBeInstalledForUser
@ stub ClientDeleteAllPackagesFromMainPackageArray
@ stub GetPackageRegistrationStatusForUserAndDefaultAccount
@ stub UpdateAgentGetDownloadingPackageCount
@ stub PreRegisterPackagesInContainer
@ stub EnsurePackageFamiliesAreRegisteredInContainer
@ stub RemovePackageFromContainer
@ stub HasPackageFamilyBeenRegisteredForUser
@ stub CleanupProfileForUser
@ stub AppxRemoveAllPackagesForUserSid
@ stdcall AppxCreateSharedLocalFolderForFamilyName(wstr wstr)
@ stdcall AppxDoesSharedLocalFolderExistForFamilyName(wstr ptr)
@ stub AppxValidatePackages
@ stub CheckForUpdatesAndWaitForInstallerIfNeeded
@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetActivationFactory(ptr ptr)
@ stdcall -private DllGetClassObject(ptr ptr ptr)
