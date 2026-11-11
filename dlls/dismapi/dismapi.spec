@ stub DismAddDriver
@ stub DismAddPackage
@ stub DismApplyUnattend
@ stub DismCheckImageHealth
@ stub DismCleanupMountpoints
@ stdcall DismCloseSession(long)
@ stub DismCommitImage
@ stdcall DismDelete(ptr)
@ stdcall DismDisableFeature(long wstr wstr long ptr ptr ptr)
@ stdcall DismEnableFeature(long wstr wstr long long ptr long long ptr ptr ptr)
@ stub DismGetDriverInfo
@ stub DismGetDrivers
@ stdcall DismGetFeatureInfo(long wstr wstr long ptr)
@ stub DismGetFeatureParent
@ stdcall DismGetFeatures(long wstr long ptr ptr)
@ stub DismGetImageInfo
@ stub DismGetLastErrorMessage
@ stub DismGetMountedImageInfo
@ stub DismGetPackageInfo
@ stub DismGetPackages
@ stdcall DismInitialize(long wstr wstr)
@ stub DismMountImage
@ stdcall DismOpenSession(wstr wstr wstr ptr)
@ stub DismRemountImage
@ stub DismRemoveDriver
@ stub DismRemovePackage
@ stub DismRestoreImageHealth
@ stdcall DismShutdown()
@ stub DismUnmountImage
