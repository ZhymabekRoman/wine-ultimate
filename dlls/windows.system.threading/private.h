#include <stdarg.h>
#include <stddef.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "combaseapi.h"

#include "activation.h"
#include "inspectable.h"

/*
 * The Windows.Foundation value types used by the Windows.System.Threading
 * interfaces are declared here, the corresponding Wine metadata does not exist.
 * Both are a single 100 ns counter.
 */
typedef struct WindowsFoundation_DateTime
{
    INT64 universal_time;
} WindowsFoundation_DateTime;

typedef struct WindowsFoundation_TimeSpan
{
    INT64 duration;
} WindowsFoundation_TimeSpan;

typedef LONGLONG __MLEVENT_COOKIE;

/*
 * The Windows.System.Threading metadata is not part of Wine, so the interfaces
 * of the ThreadPoolTimer runtime class are declared by hand here.  The slot
 * order follows the metadata: after the IUnknown and IInspectable blocks the
 * declared methods appear in the order the metadata lists them.
 */

typedef struct IThreadPoolTimer IThreadPoolTimer;
typedef struct IThreadPoolTimerVtbl
{
    /*** IUnknown ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IThreadPoolTimer *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IThreadPoolTimer *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IThreadPoolTimer *iface );
    /*** IInspectable ***/
    HRESULT (STDMETHODCALLTYPE *GetIids)( IThreadPoolTimer *iface, ULONG *count, GUID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IThreadPoolTimer *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IThreadPoolTimer *iface, TrustLevel *level );
    /*** IThreadPoolTimer ***/
    HRESULT (STDMETHODCALLTYPE *get_Delay)( IThreadPoolTimer *iface, WindowsFoundation_TimeSpan *value );
    HRESULT (STDMETHODCALLTYPE *get_Period)( IThreadPoolTimer *iface, WindowsFoundation_TimeSpan *value );
    HRESULT (STDMETHODCALLTYPE *Cancel)( IThreadPoolTimer *iface );
    HRESULT (STDMETHODCALLTYPE *add_Completed)( IThreadPoolTimer *iface, IInspectable *handler, __MLEVENT_COOKIE *cookie );
    HRESULT (STDMETHODCALLTYPE *remove_Completed)( IThreadPoolTimer *iface, __MLEVENT_COOKIE cookie );
    HRESULT (STDMETHODCALLTYPE *add_Cancelled)( IThreadPoolTimer *iface, IInspectable *handler, __MLEVENT_COOKIE *cookie );
    HRESULT (STDMETHODCALLTYPE *remove_Cancelled)( IThreadPoolTimer *iface, __MLEVENT_COOKIE cookie );
} IThreadPoolTimerVtbl;

struct IThreadPoolTimer
{
    const struct IThreadPoolTimerVtbl *lpVtbl;
};

typedef struct IThreadPoolTimerStatics IThreadPoolTimerStatics;
typedef struct IThreadPoolTimerStaticsVtbl
{
    /*** IUnknown ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IThreadPoolTimerStatics *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IThreadPoolTimerStatics *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IThreadPoolTimerStatics *iface );
    /*** IInspectable ***/
    HRESULT (STDMETHODCALLTYPE *GetIids)( IThreadPoolTimerStatics *iface, ULONG *count, GUID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IThreadPoolTimerStatics *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IThreadPoolTimerStatics *iface, TrustLevel *level );
    /*** IThreadPoolTimerStatics ***/
    HRESULT (STDMETHODCALLTYPE *CreatePeriodicTimer)( IThreadPoolTimerStatics *iface, IInspectable *handler,
            WindowsFoundation_DateTime due_time, WindowsFoundation_TimeSpan period, IThreadPoolTimer **value );
    HRESULT (STDMETHODCALLTYPE *CreateTimer)( IThreadPoolTimerStatics *iface, IInspectable *handler,
            WindowsFoundation_DateTime due_time, IThreadPoolTimer **value );
    HRESULT (STDMETHODCALLTYPE *CreatePeriodicTimerWithPriority)( IThreadPoolTimerStatics *iface, IInspectable *handler,
            WindowsFoundation_DateTime due_time, WindowsFoundation_TimeSpan period, INT32 priority, IThreadPoolTimer **value );
    HRESULT (STDMETHODCALLTYPE *CreateTimerWithPriority)( IThreadPoolTimerStatics *iface, IInspectable *handler,
            WindowsFoundation_DateTime due_time, INT32 priority, IThreadPoolTimer **value );
} IThreadPoolTimerStaticsVtbl;

struct IThreadPoolTimerStatics
{
    const struct IThreadPoolTimerStaticsVtbl *lpVtbl;
};

/*
 * Interface identifier of Windows.System.Threading.IThreadPoolTimerStatics, the
 * interface the .NET projection asks the class factory for.
 */
static const IID IID_IThreadPoolTimerStatics =
    { 0x1a8a9d02, 0xe482, 0x461b, { 0xb8, 0xc7, 0x8e, 0xfa, 0xd1, 0xcc, 0xe5, 0x90 } };

extern const struct IThreadPoolTimerVtbl threadpool_timer_vtbl;
extern const struct IThreadPoolTimerStaticsVtbl threadpool_timer_statics_vtbl;

struct threadpool_timer
{
    IThreadPoolTimer IThreadPoolTimer_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    IInspectable *handler;
    IInspectable *completed_handler;
    IInspectable *cancelled_handler;
    __MLEVENT_COOKIE completed_cookie;
    __MLEVENT_COOKIE cancelled_cookie;
    INT64 due_time;   /* 100 ns intervals since 1601-01-01, UTC. */
    INT64 period;     /* 100 ns intervals, 0 for a one shot timer. */
    BOOL cancelled;
    HANDLE stop_event;
    HANDLE thread;
};

static inline struct threadpool_timer *impl_from_IThreadPoolTimer( IThreadPoolTimer *iface )
{
    return CONTAINING_RECORD( iface, struct threadpool_timer, IThreadPoolTimer_iface );
}

struct threadpool_statics
{
    IActivationFactory IActivationFactory_iface;
    IThreadPoolTimerStatics IThreadPoolTimerStatics_iface;
    LONG ref;
};

static inline struct threadpool_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct threadpool_statics, IActivationFactory_iface );
}

static inline struct threadpool_statics *impl_from_IThreadPoolTimerStatics( IThreadPoolTimerStatics *iface )
{
    return CONTAINING_RECORD( iface, struct threadpool_statics, IThreadPoolTimerStatics_iface );
}
