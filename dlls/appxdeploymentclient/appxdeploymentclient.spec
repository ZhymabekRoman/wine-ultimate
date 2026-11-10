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
@ stdcall AppxRecoverUserInstallsForUpgrade(long long)
@ stdcall AppxDeletePackageFiles(wstr)
@ stdcall AppxRequestRemovePackageForUser(wstr wstr)
@ stdcall IsPackageInstalled(wstr ptr)
@ stdcall RDSRecoverRequests()
@ stdcall AppxPreStageCleanupRunTask()
@ stdcall AppxPreRegisterPackage(wstr wstr wstr ptr)
@ stdcall ReArmAppxPreStageCleanupTask()
@ stdcall AppxAddPackageToAllUserStoreForPbr(wstr)
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
@ stdcall AppxRemovePackageForAllUsers(wstr)
@ stdcall RequestContentGroups(long ptr)
@ stub GetApplicability4
@ stdcall RequestContentGroupsForFullTrust(long ptr)
@ stdcall GeneratePreInstalledPriFiles()
@ stdcall PopulateProtocolAndFTA()
@ stdcall FixJunctionsForAppsIfNecessary()
@ stdcall AppxRemovePackageForUserSid(wstr wstr)
@ stdcall CreateCanonicalPriFile(wstr wstr)
@ stdcall RegisterNotificationForUser(wstr wstr long)
@ stdcall UnregisterNotificationForUser(wstr wstr)
@ stdcall GetNotificationPayloadForUser(wstr wstr ptr)
@ stdcall AppxCleanupWCIReparsePoints()
@ stdcall RepairPackageFileAcls()
@ stdcall EnsurePackageFamilyIsRegisteredBeforeActivation(wstr wstr ptr)
@ stdcall AppxCleanupSystemAppsMigratedToFOD()
@ stdcall AppxCleanupOrphanPackages()
@ stdcall CheckComCallerHasCapabilities(long ptr)
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
@ stdcall GetPackageRegistrationStatusForUser(wstr wstr ptr)
@ stdcall IsSharedAppsEnabled(ptr)
@ stdcall AppInstallerUpdateAllTask()
@ stdcall CheckAppInstallerUpdateAvailability(ptr)
@ stub UpdateAgentFreeDownloadRanges
@ stdcall AppxRegisterPackage(wstr wstr wstr)
@ stdcall IsPackageMetadataUnderSystemMetadata(wstr ptr)
@ stdcall GetMetadataRootForPackage(wstr ptr)
@ stub ClientGetAllPackagesToBeInstalledForUser
@ stub ClientDeleteAllPackagesFromMainPackageArray
@ stdcall GetPackageRegistrationStatusForUserAndDefaultAccount(wstr ptr)
@ stub UpdateAgentGetDownloadingPackageCount
@ stub PreRegisterPackagesInContainer
@ stub EnsurePackageFamiliesAreRegisteredInContainer
@ stdcall RemovePackageFromContainer(wstr wstr)
@ stdcall HasPackageFamilyBeenRegisteredForUser(wstr wstr ptr)
@ stdcall CleanupProfileForUser(wstr)
@ stdcall AppxRemoveAllPackagesForUserSid(wstr)
@ stdcall AppxCreateSharedLocalFolderForFamilyName(wstr wstr)
@ stdcall AppxDoesSharedLocalFolderExistForFamilyName(wstr ptr)
@ stdcall AppxValidatePackages(wstr wstr)
@ stdcall CheckForUpdatesAndWaitForInstallerIfNeeded(long)
@ stdcall AppxRegisterPackageManifest(wstr)
@ stdcall AppxUnregisterPackageByName(wstr)
@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetActivationFactory(ptr ptr)
@ stdcall -private DllGetClassObject(ptr ptr ptr)
