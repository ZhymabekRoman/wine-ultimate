@ stub DismAddDriver
@ stdcall DismAddPackage(long wstr long long long ptr ptr)
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
@ stdcall DismGetPackageInfo(long wstr long ptr)
@ stdcall DismGetPackages(long ptr ptr)
@ stdcall DismGetSession(ptr)
@ stdcall DismGetSessionImagePath(long ptr)
@ stdcall DismInitialize(long wstr wstr)
@ stub DismMountImage
@ stdcall DismOpenSession(wstr wstr wstr ptr)
@ stub DismRemountImage
@ stub DismRemoveDriver
@ stdcall DismRemovePackage(long wstr long ptr ptr ptr)
@ stub DismRestoreImageHealth
@ stdcall DismShutdown()
@ stub DismUnmountImage
