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
@ stdcall RDSRecoverRequests()
@ stdcall AppxPreStageCleanupRunTask()
@ stdcall AppxPreRegisterPackage(wstr wstr wstr ptr)
@ stdcall ReArmAppxPreStageCleanupTask()
@ stub AppxAddPackageToAllUserStoreForPbr
@ stub GetPackageApplicabilityForUserLogon
16 stub @
@ stdcall AppxStagePackage(wstr wstr wstr ptr wstr ptr)
@ stdcall AppxGetPackageInstalledLocation(wstr ptr)
@ stdcall AppxDestagePackage(wstr)
@ stub GetApplicability2
@ stdcall AppxGetStagedPackageFullNameFromFamilyName(wstr ptr)
@ stdcall AppxIsStagedPackageStoreSigned(wstr ptr)
@ stdcall RegisterNotification(wstr long)
@ stdcall UnregisterNotification(wstr)
@ stdcall GetNotificationPayload(wstr ptr)
@ stdcall NotifyPackageStatusChanged(wstr long)
@ stdcall VerifyPackage(wstr wstr)
@ stdcall AppxPreRegisterAllInboxPackages()
@ stdcall AppxValidatePackagesWithOptions(wstr wstr long)
@ stub AppxRemovePackageForAllUsers
@ stub RequestContentGroups
@ stub GetApplicability4
@ stub RequestContentGroupsForFullTrust
@ stub GeneratePreInstalledPriFiles
@ stub PopulateProtocolAndFTA
@ stub FixJunctionsForAppsIfNecessary
@ stdcall AppxRemovePackageForUserSid(wstr wstr)
@ stub CreateCanonicalPriFile
@ stdcall RegisterNotificationForUser(wstr wstr long)
@ stdcall UnregisterNotificationForUser(wstr wstr)
@ stdcall GetNotificationPayloadForUser(wstr wstr ptr)
@ stdcall AppxCleanupWCIReparsePoints()
@ stdcall RepairPackageFileAcls()
@ stdcall EnsurePackageFamilyIsRegisteredBeforeActivation(wstr wstr ptr)
@ stdcall AppxCleanupSystemAppsMigratedToFOD()
@ stdcall AppxCleanupOrphanPackages()
@ stub CheckComCallerHasCapabilities
@ stub GetBundleApplicablePackages
@ stub GetApplicability5
@ stub DeleteApplicabilityInfoArray
@ stub UpdateAgentCreateDownload
@ stub UpdateAgentGetDownloadRanges
@ stdcall UpdateAgentCancelAllDownloads()
@ stub UpdateDataSourceRegister
@ stub UpdateDataSourceAddRange
@ stub UpdateDataSourceRun
@ stub UpdateDataSourceCancelRun
@ stub GetPackageRegistrationStatusForUser
@ stub IsSharedAppsEnabled
@ stdcall AppInstallerUpdateAllTask()
@ stdcall CheckAppInstallerUpdateAvailability(ptr)
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
@ stdcall AppxValidatePackages(wstr wstr)
@ stdcall CheckForUpdatesAndWaitForInstallerIfNeeded(long)
@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetActivationFactory(ptr ptr)
@ stdcall -private DllGetClassObject(ptr ptr ptr)
