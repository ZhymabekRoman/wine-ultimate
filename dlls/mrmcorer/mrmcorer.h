/*
 * MRT Core builtin: WinRT C ABI surface for Microsoft.Windows.ApplicationModel.Resources.
 */
#ifndef __WINE_MRMCORER_H
#define __WINE_MRMCORER_H

#include <windows.h>
#include <objbase.h>
#include "winstring.h"
#include "activation.h"


#define COBJMACROS

typedef IInspectable *Uri;

/* --- parameterized collections (plain vtables; IIDs via RoGetParameterized) --- */

static const GUID IID_IVectorView_HSTRING_ =
    {0x5be2af1c, 0x844a, 0x4942, {0x86, 0x51, 0x82, 0x82, 0x93, 0xac, 0xbc, 0xb8}};
static const GUID IID_IMapView_HSTRING_HSTRING_ =
    {0xe85add24, 0x4991, 0x4d2c, {0xa3, 0x52, 0xab, 0x83, 0x3b, 0x0c, 0xa0, 0x16}};
static const GUID IID_IMapView_HSTRING_NAMEDRESOURCE_ =
    {0x84cc9e13, 0x1887, 0x519a, {0x85, 0x09, 0x64, 0xc5, 0x10, 0xa2, 0x4d, 0x38}};
static const GUID IID_IKeyValuePair_HSTRING_NAMEDRESOURCE_ =
    {0x9c03e588, 0xc79f, 0x5233, {0xb2, 0x40, 0x4f, 0x4b, 0x04, 0xdc, 0x64, 0xd0}};
static const GUID IID_IKeyValuePair_HSTRING_RESOURCE_ =
    {0x0813a644, 0x6f52, 0x5dfb, {0xb1, 0x46, 0x5c, 0x47, 0x4e, 0x6d, 0x1d, 0x62}};
static const GUID IID_IVectorView_ResourceCandidateState_ =
    {0x373115e8, 0xbde3, 0x511d, {0x91, 0x1e, 0x31, 0x5d, 0xb3, 0xb5, 0xc9, 0xbf}};

/* MWR v1 */
static const GUID IID_IResourceContext           = {0x2fa22f4b, 0x707e, 0x4b27, {0xad, 0x0d, 0xd0, 0xd8, 0xcd, 0x46, 0x8f, 0xd2}};
static const GUID IID_IResourceMap               = {0xbbd9b099, 0x4e4a, 0x7bdf, {0x5c, 0x81, 0x2c, 0x7e, 0x7b, 0xb0, 0xc2, 0x23}};
static const GUID IID_IResourceCandidate         = {0x2f3b672d, 0x33f1, 0x51f8, {0xa1, 0x8c, 0x30, 0x9b, 0x1d, 0xa1, 0x1a, 0x40}};
static const GUID IID_INamedResource             = {0x87f4b128, 0x6b28, 0x5390, {0xb1, 0x88, 0xf3, 0x96, 0x80, 0xc7, 0x56, 0x39}};
static const GUID IID_IResourceManager           = {0xac2291ef, 0x81be, 0x5c99, {0xa0, 0xae, 0xbc, 0xee, 0x01, 0x80, 0xb8, 0xa8}};

typedef enum ResourceCandidateState
{
    ResourceCandidateState_None = 0,
    ResourceCandidateState_Matched = 1,
    ResourceCandidateState_Fallback = 2,
    ResourceCandidateState_Candidate = 3,
    ResourceCandidateState_SemanticMatch = 4,
} ResourceCandidateState;

typedef interface IVectorView_HSTRING_ IVectorView_HSTRING_;
typedef interface IMapView_HSTRING_HSTRING_ IMapView_HSTRING_HSTRING_;
typedef interface IMapView_HSTRING_NAMEDRESOURCE_ IMapView_HSTRING_NAMEDRESOURCE_;
typedef interface IKeyValuePair_HSTRING_HSTRING_ IKeyValuePair_HSTRING_HSTRING_;
typedef interface IKeyValuePair_HSTRING_RESOURCE_ IKeyValuePair_HSTRING_RESOURCE_;
typedef interface IKeyValuePair_HSTRING_NAMEDRESOURCE_ IKeyValuePair_HSTRING_NAMEDRESOURCE_;
typedef interface IVectorView_ResourceCandidateState_ IVectorView_ResourceCandidateState_;
typedef interface IResourceContext IResourceContext;
typedef interface IResourceMap IResourceMap;
typedef interface IResourceCandidate IResourceCandidate;
typedef interface INamedResource INamedResource;
typedef interface IResourceManager IResourceManager;

struct IVectorView_HSTRING_ { const struct IVectorView_HSTRINGVtbl *lpVtbl; };
struct IMapView_HSTRING_HSTRING_ { const struct IMapView_HSTRING_HSTRINGVtbl *lpVtbl; };
struct IKeyValuePair_HSTRING_HSTRING_ { const struct IKeyValuePair_HSTRING_HSTRINGVtbl *lpVtbl; };
struct IKeyValuePair_HSTRING_RESOURCE_ { const struct IKeyValuePair_HSTRING_RESOURCEVtbl *lpVtbl; };
struct IKeyValuePair_HSTRING_NAMEDRESOURCE_ { const struct IKeyValuePair_HSTRING_NAMEDRESOURCEVtbl *lpVtbl; };
struct IMapView_HSTRING_NAMEDRESOURCE_ { const struct IMapView_HSTRING_NAMEDRESOURCEVtbl *lpVtbl; };
struct IVectorView_ResourceCandidateState_ { const struct IVectorView_ResourceCandidateStateVtbl *lpVtbl; };
struct IResourceContext { const struct IResourceContextVtbl *lpVtbl; };
struct IResourceCandidate { const struct IResourceCandidateVtbl *lpVtbl; };
struct INamedResource { const struct INamedResourceVtbl *lpVtbl; };
struct IResourceMap { const struct IResourceMapVtbl *lpVtbl; };
struct IResourceManager { const struct IResourceManagerVtbl *lpVtbl; };

struct IVectorView_HSTRINGVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IVectorView_HSTRING_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IVectorView_HSTRING_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IVectorView_HSTRING_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IVectorView_HSTRING_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IVectorView_HSTRING_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IVectorView_HSTRING_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *GetAt)(IVectorView_HSTRING_ *This, UINT32 index, HSTRING *item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(IVectorView_HSTRING_ *This, UINT32 *size);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(IVectorView_HSTRING_ *This, HSTRING item, UINT32 *index, boolean *found);
};

struct IMapView_HSTRING_HSTRINGVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMapView_HSTRING_HSTRING_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IMapView_HSTRING_HSTRING_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IMapView_HSTRING_HSTRING_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IMapView_HSTRING_HSTRING_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IMapView_HSTRING_HSTRING_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IMapView_HSTRING_HSTRING_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *GetAt)(IMapView_HSTRING_HSTRING_ *This, UINT32 index, IKeyValuePair_HSTRING_HSTRING_ **item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(IMapView_HSTRING_HSTRING_ *This, UINT32 *size);
    HRESULT (STDMETHODCALLTYPE *HasKey)(IMapView_HSTRING_HSTRING_ *This, HSTRING key, boolean *found);
    HRESULT (STDMETHODCALLTYPE *Lookup)(IMapView_HSTRING_HSTRING_ *This, HSTRING key, HSTRING *value);
};

struct IKeyValuePair_HSTRING_HSTRINGVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IKeyValuePair_HSTRING_HSTRING_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IKeyValuePair_HSTRING_HSTRING_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IKeyValuePair_HSTRING_HSTRING_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IKeyValuePair_HSTRING_HSTRING_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IKeyValuePair_HSTRING_HSTRING_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IKeyValuePair_HSTRING_HSTRING_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Key)(IKeyValuePair_HSTRING_HSTRING_ *This, HSTRING *key);
    HRESULT (STDMETHODCALLTYPE *get_Value)(IKeyValuePair_HSTRING_HSTRING_ *This, HSTRING *value);
};

struct IKeyValuePair_HSTRING_RESOURCEVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IKeyValuePair_HSTRING_RESOURCE_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IKeyValuePair_HSTRING_RESOURCE_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IKeyValuePair_HSTRING_RESOURCE_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IKeyValuePair_HSTRING_RESOURCE_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IKeyValuePair_HSTRING_RESOURCE_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IKeyValuePair_HSTRING_RESOURCE_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Key)(IKeyValuePair_HSTRING_RESOURCE_ *This, HSTRING *key);
    HRESULT (STDMETHODCALLTYPE *get_Value)(IKeyValuePair_HSTRING_RESOURCE_ *This, IUnknown **value);
};

struct IKeyValuePair_HSTRING_NAMEDRESOURCEVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Key)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This, HSTRING *key);
    HRESULT (STDMETHODCALLTYPE *get_Value)(IKeyValuePair_HSTRING_NAMEDRESOURCE_ *This, INamedResource **value);
};

struct IMapView_HSTRING_NAMEDRESOURCEVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMapView_HSTRING_NAMEDRESOURCE_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IMapView_HSTRING_NAMEDRESOURCE_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IMapView_HSTRING_NAMEDRESOURCE_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IMapView_HSTRING_NAMEDRESOURCE_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IMapView_HSTRING_NAMEDRESOURCE_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IMapView_HSTRING_NAMEDRESOURCE_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *GetAt)(IMapView_HSTRING_NAMEDRESOURCE_ *This, UINT32 index, IKeyValuePair_HSTRING_NAMEDRESOURCE_ **item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(IMapView_HSTRING_NAMEDRESOURCE_ *This, UINT32 *size);
    HRESULT (STDMETHODCALLTYPE *HasKey)(IMapView_HSTRING_NAMEDRESOURCE_ *This, HSTRING key, boolean *found);
    HRESULT (STDMETHODCALLTYPE *Lookup)(IMapView_HSTRING_NAMEDRESOURCE_ *This, HSTRING key, INamedResource **value);
};

struct IVectorView_ResourceCandidateStateVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IVectorView_ResourceCandidateState_ *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IVectorView_ResourceCandidateState_ *This);
    ULONG (STDMETHODCALLTYPE *Release)(IVectorView_ResourceCandidateState_ *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IVectorView_ResourceCandidateState_ *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IVectorView_ResourceCandidateState_ *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IVectorView_ResourceCandidateState_ *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *GetAt)(IVectorView_ResourceCandidateState_ *This, UINT32 index, enum ResourceCandidateState *item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(IVectorView_ResourceCandidateState_ *This, UINT32 *size);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(IVectorView_ResourceCandidateState_ *This, enum ResourceCandidateState item, UINT32 *index, boolean *found);
};

struct IResourceContextVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IResourceContext *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IResourceContext *This);
    ULONG (STDMETHODCALLTYPE *Release)(IResourceContext *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IResourceContext *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IResourceContext *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IResourceContext *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Languages)(IResourceContext *This, IVectorView_HSTRING_ **value);
    HRESULT (STDMETHODCALLTYPE *get_QualifierValues)(IResourceContext *This, IMapView_HSTRING_HSTRING_ **value);
    HRESULT (STDMETHODCALLTYPE *GetAllQualifierNames)(IResourceContext *This, IVectorView_HSTRING_ **value);
};

struct IResourceCandidateVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IResourceCandidate *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IResourceCandidate *This);
    ULONG (STDMETHODCALLTYPE *Release)(IResourceCandidate *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IResourceCandidate *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IResourceCandidate *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IResourceCandidate *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Qualifiers)(IResourceCandidate *This, IMapView_HSTRING_HSTRING_ **value);
    HRESULT (STDMETHODCALLTYPE *get_Language)(IResourceCandidate *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_CandidateStates)(IResourceCandidate *This, IVectorView_ResourceCandidateState_ **value);
    HRESULT (STDMETHODCALLTYPE *get_Matched)(IResourceCandidate *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_MatchedUri)(IResourceCandidate *This, Uri **value);
    HRESULT (STDMETHODCALLTYPE *get_Score)(IResourceCandidate *This, DOUBLE *value);
    HRESULT (STDMETHODCALLTYPE *get_Text)(IResourceCandidate *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_ValueAsString)(IResourceCandidate *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetAppResourceUri)(IResourceCandidate *This, Uri **value);
    HRESULT (STDMETHODCALLTYPE *GetValueAsString)(IResourceCandidate *This, IResourceContext *context, HSTRING *value);
};

struct INamedResourceVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INamedResource *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(INamedResource *This);
    ULONG (STDMETHODCALLTYPE *Release)(INamedResource *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(INamedResource *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(INamedResource *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(INamedResource *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Name)(INamedResource *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Value)(INamedResource *This, IResourceCandidate **value);
    HRESULT (STDMETHODCALLTYPE *get_ValueAsString)(INamedResource *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetValue)(INamedResource *This, IResourceContext *context, HSTRING *value);
};

struct IResourceMapVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IResourceMap *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IResourceMap *This);
    ULONG (STDMETHODCALLTYPE *Release)(IResourceMap *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IResourceMap *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IResourceMap *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IResourceMap *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *TryGetValueWithContext)(IResourceMap *This, IResourceContext *context, HSTRING key, IResourceCandidate **value);
    HRESULT (STDMETHODCALLTYPE *TryGetValue)(IResourceMap *This, HSTRING key, IResourceCandidate **value);
    HRESULT (STDMETHODCALLTYPE *GetValueByIndexWithContext)(IResourceMap *This, IResourceContext *context, UINT32 index, IKeyValuePair_HSTRING_RESOURCE_ **item);
    HRESULT (STDMETHODCALLTYPE *GetValueByIndex)(IResourceMap *This, UINT32 index, IKeyValuePair_HSTRING_RESOURCE_ **item);
    HRESULT (STDMETHODCALLTYPE *GetValueWithContext)(IResourceMap *This, IResourceContext *context, HSTRING key, IResourceCandidate **value);
    HRESULT (STDMETHODCALLTYPE *GetValue)(IResourceMap *This, HSTRING key, IResourceCandidate **value);
    HRESULT (STDMETHODCALLTYPE *TryGetSubtree)(IResourceMap *This, HSTRING name, IResourceMap **value);
    HRESULT (STDMETHODCALLTYPE *GetSubtree)(IResourceMap *This, HSTRING name, IResourceMap **value);
    HRESULT (STDMETHODCALLTYPE *get_ResourceCount)(IResourceMap *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_Name)(IResourceMap *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Uri)(IResourceMap *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Language)(IResourceMap *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_QualifierValues)(IResourceMap *This, IMapView_HSTRING_HSTRING_ **value);
    HRESULT (STDMETHODCALLTYPE *get_TopLevelResourceKeys)(IResourceMap *This, IMapView_HSTRING_NAMEDRESOURCE_ **value);
};

struct IResourceManagerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IResourceManager *This, REFIID iid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IResourceManager *This);
    ULONG (STDMETHODCALLTYPE *Release)(IResourceManager *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IResourceManager *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IResourceManager *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IResourceManager *This, enum TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_MainResourceMap)(IResourceManager *This, IResourceMap **value);
    HRESULT (STDMETHODCALLTYPE *get_ResourceContext)(IResourceManager *This, IResourceContext **value);
    HRESULT (STDMETHODCALLTYPE *put_ResourceContext)(IResourceManager *This, IResourceContext *value);
    HRESULT (STDMETHODCALLTYPE *UpdateResourceLocators)(IResourceManager *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetQualifiedResourceMap)(IResourceManager *This, IMapView_HSTRING_HSTRING_ *qualifiers, IResourceMap **value);
    HRESULT (STDMETHODCALLTYPE *CreateResourceContext)(IResourceManager *This, IResourceContext **value);
};


#define IVectorView_HSTRING_Vtbl struct IVectorView_HSTRINGVtbl
#define IMapView_HSTRING_HSTRING_Vtbl struct IMapView_HSTRING_HSTRINGVtbl
#define IKeyValuePair_HSTRING_HSTRING_Vtbl struct IKeyValuePair_HSTRING_HSTRINGVtbl
#define IKeyValuePair_HSTRING_RESOURCE_Vtbl struct IKeyValuePair_HSTRING_RESOURCEVtbl
#define IKeyValuePair_HSTRING_NAMEDRESOURCE_Vtbl struct IKeyValuePair_HSTRING_NAMEDRESOURCEVtbl
#define IMapView_HSTRING_NAMEDRESOURCE_Vtbl struct IMapView_HSTRING_NAMEDRESOURCEVtbl
#define IVectorView_ResourceCandidateState_Vtbl struct IVectorView_ResourceCandidateStateVtbl



/* shared inline IInspectable helpers (defined in main.c) */
/* stubs implemented in main.c */


#ifdef COBJMACROS
#define INamedResource_QueryInterface(This,riid,pv) (This)->lpVtbl->QueryInterface(This,riid,pv)
#define INamedResource_AddRef(This) (This)->lpVtbl->AddRef(This)
#define INamedResource_Release(This) (This)->lpVtbl->Release(This)
#define INamedResource_get_Name(This,p) (This)->lpVtbl->get_Name(This,p)
#define INamedResource_get_ValueAsString(This,p) (This)->lpVtbl->get_ValueAsString(This,p)
#define IResourceCandidate_QueryInterface(This,riid,pv) (This)->lpVtbl->QueryInterface(This,riid,pv)
#define IResourceCandidate_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IResourceCandidate_Release(This) (This)->lpVtbl->Release(This)
#define IResourceCandidate_get_ValueAsString(This,p) (This)->lpVtbl->get_ValueAsString(This,p)
#define IResourceContext_QueryInterface(This,riid,pv) (This)->lpVtbl->QueryInterface(This,riid,pv)
#define IResourceContext_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IResourceContext_Release(This) (This)->lpVtbl->Release(This)
#define IResourceContext_get_Languages(This,p) (This)->lpVtbl->get_Languages(This,p)
#define IResourceMap_QueryInterface(This,riid,pv) (This)->lpVtbl->QueryInterface(This,riid,pv)
#define IResourceMap_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IResourceMap_Release(This) (This)->lpVtbl->Release(This)
#define IResourceMap_get_TopLevelResourceKeys(This,p) (This)->lpVtbl->get_TopLevelResourceKeys(This,p)
#define IResourceManager_QueryInterface(This,riid,pv) (This)->lpVtbl->QueryInterface(This,riid,pv)
#define IResourceManager_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IResourceManager_Release(This) (This)->lpVtbl->Release(This)
#define IResourceManager_get_MainResourceMap(This,p) (This)->lpVtbl->get_MainResourceMap(This,p)
#define IResourceManager_get_ResourceContext(This,p) (This)->lpVtbl->get_ResourceContext(This,p)
#endif

#endif /* __WINE_MRMCORER_H */
