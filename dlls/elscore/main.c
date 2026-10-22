#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(elscore);

HRESULT WINAPI MappingGetServices(void *pOptions, void **prgServices, DWORD *pdwServicesCount)
{
    FIXME("pOptions %p, prgServices %p, pdwServicesCount %p stub!\n", pOptions, prgServices, pdwServicesCount);
    if (!pdwServicesCount) return E_INVALIDARG;
    if (prgServices) *prgServices = NULL;
    *pdwServicesCount = 0;
    return S_OK;
}

HRESULT WINAPI MappingFreeServices(void *pServiceInfo)
{
    TRACE("pServiceInfo %p\n", pServiceInfo);
    return S_OK;
}

HRESULT WINAPI MappingRecognizeText(void *pServiceInfo, const WCHAR *pszText, DWORD dwLength, DWORD dwIndex, void *pOptions, void *pBag)
{
    FIXME("pServiceInfo %p, pszText %s, dwLength %lu, dwIndex %lu, pOptions %p, pBag %p stub!\n",
          pServiceInfo, wine_dbgstr_w(pszText), dwLength, dwIndex, pOptions, pBag);
    return E_NOTIMPL;
}

HRESULT WINAPI MappingFreePropertyBag(void *pBag)
{
    TRACE("pBag %p\n", pBag);
    return S_OK;
}

HRESULT WINAPI MappingDoAction(void *pBag, DWORD dwRangeIndex, const WCHAR *pszActionId)
{
    FIXME("pBag %p, dwRangeIndex %lu, pszActionId %s stub!\n", pBag, dwRangeIndex, wine_dbgstr_w(pszActionId));
    return E_NOTIMPL;
}
