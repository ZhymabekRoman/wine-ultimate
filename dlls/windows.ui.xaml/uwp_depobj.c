/* WinRT Windows.UI.Xaml Global Element Registry & IDependencyObject
 *
 * Copyright (C) 2026 Wine project
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <math.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "weakreference.h"
#include "uwp_private.h"
#include "dp_vsm_private.h"
#include "xbf_known_types.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* ============================================================================
 * 4. UWP Visual Element & Distinct Per-Interface Sub-Objects
 * ============================================================================ */

/* Global registry of live uwp_elements so we can resolve from any interface or outer.
 * Linked list (newest first): static-array registry overflowed at 2048 entries with
 * App.xbf style/setter/visualstate churn, silently dropping the Page root. */
struct uwp_element *g_all_uwp_elems_head = NULL;
int g_all_uwp_elem_cnt = 0;
SRWLOCK g_all_uwp_elems_lock = SRWLOCK_INIT;
BOOL g_layout_dirty = TRUE;
BOOL g_paint_dirty = TRUE;
UINT32 g_layout_gen = 1;
struct uwp_element *g_open_overlay_nav = NULL;
struct uwp_element *g_captured_elem = NULL;
struct uwp_element *g_hovered_elem = NULL;
/* Last PAGE-kind element created (set by uwp_element_create). Navigate uses it
 * as fallback when the RCW page pointer cannot be resolved to an element. */
struct uwp_element *g_last_created_page = NULL;
/* Page element adopted by the last Frame.Navigate; rendered directly because
 * Frame.content often holds an unresolvable MCG RCW pointer. */
struct uwp_element *g_navigated_page_el = NULL;

HWND g_popup_hwnd = NULL;
HWND g_sub_popup_hwnd = NULL;
struct uwp_element *g_popup_owner_el = NULL;
struct uwp_element *g_last_closed_flyout = NULL;
DWORD g_last_closed_tick = 0;

struct uwp_accel_entry *g_accel_entries = NULL;
int g_accel_cnt = 0;
static int g_accel_cap = 0;

struct uwp_element *uwp_find_owning_page( struct uwp_element *el )
{
    struct uwp_element *cur = el;
    int depth = 0;
    while (cur && depth < 64)
    {
        if (cur->kind == UWP_KIND_PAGE || cur->kind == UWP_KIND_USER_CONTROL)
            return cur;
        if (cur->parent)
            cur = cur->parent;
        else if (cur->owner_component && cur->owner_component != cur)
            cur = cur->owner_component;
        else
            break;
        depth++;
    }
    if (el && el->owner_component) return el->owner_component;
    return g_navigated_page_el ? g_navigated_page_el : g_last_created_page;
}

void uwp_accel_register( struct uwp_element *accel_el, struct uwp_element *target_el,
                                UINT vk, UINT mods, BOOL enabled )
{
    struct uwp_element *page_el;
    int i;

    if (!vk) return;
    if (!target_el && accel_el) target_el = accel_el->accel_target;
    page_el = target_el ? uwp_find_owning_page( target_el ) : (accel_el ? uwp_find_owning_page( accel_el ) : g_navigated_page_el);

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (accel_el)
    {
        for (i = 0; i < g_accel_cnt; i++)
        {
            if (g_accel_entries[i].accel_el == accel_el)
            {
                if (target_el) g_accel_entries[i].target_el = target_el;
                if (page_el) g_accel_entries[i].page_el = page_el;
                g_accel_entries[i].vk = vk;
                g_accel_entries[i].mods = mods;
                g_accel_entries[i].enabled = enabled;
                TRACE( "accel_reg(update): vk=%#x mods=%#x enabled=%d target=%s (%s) page=%s (%s)\n",
                       vk, mods, enabled,
                       g_accel_entries[i].target_el ? debugstr_w(g_accel_entries[i].target_el->class_name) : "?",
                       g_accel_entries[i].target_el ? debugstr_w(g_accel_entries[i].target_el->name) : "",
                       g_accel_entries[i].page_el ? debugstr_w(g_accel_entries[i].page_el->class_name) : "?",
                       g_accel_entries[i].page_el ? debugstr_w(g_accel_entries[i].page_el->name) : "" );
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                return;
            }
        }
    }
    else if (target_el)
    {
        for (i = 0; i < g_accel_cnt; i++)
        {
            if (!g_accel_entries[i].accel_el && g_accel_entries[i].target_el == target_el &&
                g_accel_entries[i].vk == vk && g_accel_entries[i].mods == mods)
            {
                g_accel_entries[i].enabled = enabled;
                if (page_el) g_accel_entries[i].page_el = page_el;
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                return;
            }
        }
    }
    if (xaml_vec_grow( (void **)&g_accel_entries, &g_accel_cap, g_accel_cnt + 1, sizeof(*g_accel_entries), 32 ))
    {
        struct uwp_accel_entry *e = &g_accel_entries[g_accel_cnt++];
        e->accel_el = accel_el;
        e->target_el = target_el;
        e->page_el = page_el;
        e->vk = vk;
        e->mods = mods;
        e->enabled = enabled;
        TRACE( "accel_reg: vk=%#x mods=%#x enabled=%d target=%s (%s) page=%s (%s) total=%d\n",
               vk, mods, enabled,
               target_el ? debugstr_w(target_el->class_name) : "?",
               target_el ? debugstr_w(target_el->name) : "",
               page_el ? debugstr_w(page_el->class_name) : "?",
               page_el ? debugstr_w(page_el->name) : "",
               g_accel_cnt );
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

void uwp_clear_global_elem_refs( struct uwp_element *el )
{
    int i;
    if (!el) return;
    uwp_open_popups_remove( el );
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (g_open_overlay_nav == el) g_open_overlay_nav = NULL;
    if (g_captured_elem == el) g_captured_elem = NULL;
    if (g_hovered_elem == el) g_hovered_elem = NULL;
    if (g_focused_textbox == el) g_focused_textbox = NULL;
    if (g_last_created_page == el) g_last_created_page = NULL;
    if (g_navigated_page_el == el) g_navigated_page_el = NULL;
    if (g_popup_owner_el == el) g_popup_owner_el = NULL;
    if (g_last_closed_flyout == el) g_last_closed_flyout = NULL;
    if (g_open_combobox == el) g_open_combobox = NULL;
    if (g_routed_args_el == el) g_routed_args_el = NULL;
    for (i = 0; i < g_accel_cnt; )
    {
        if (g_accel_entries[i].accel_el == el || g_accel_entries[i].target_el == el || g_accel_entries[i].page_el == el)
            g_accel_entries[i] = g_accel_entries[--g_accel_cnt];
        else
            i++;
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

UINT uwp_parse_vk_string( const WCHAR *s )
{
    if (!s || !s[0]) return 0;
    if (!s[1])
    {
        WCHAR ch = towupper( s[0] );
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return (UINT)ch;
        if (ch == '+') return VK_OEM_PLUS;
        if (ch == '-') return VK_OEM_MINUS;
        if (ch == '.') return VK_OEM_PERIOD;
        if (ch == ',') return VK_OEM_COMMA;
    }
    if (!wcsicmp( s, L"Insert" )) return VK_INSERT;
    if (!wcsicmp( s, L"Delete" )) return VK_DELETE;
    if (!wcsicmp( s, L"Escape" ) || !wcsicmp( s, L"Esc" )) return VK_ESCAPE;
    if (!wcsicmp( s, L"Back" ) || !wcsicmp( s, L"Backspace" )) return VK_BACK;
    if (!wcsicmp( s, L"Enter" ) || !wcsicmp( s, L"Return" )) return VK_RETURN;
    if (!wcsicmp( s, L"Tab" )) return VK_TAB;
    if (!wcsicmp( s, L"Space" )) return VK_SPACE;
    if (!wcsicmp( s, L"Up" )) return VK_UP;
    if (!wcsicmp( s, L"Down" )) return VK_DOWN;
    if (!wcsicmp( s, L"Left" )) return VK_LEFT;
    if (!wcsicmp( s, L"Right" )) return VK_RIGHT;
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9')
    {
        int fn = _wtoi( s + 1 );
        if (fn >= 1 && fn <= 24) return (UINT)(VK_F1 + fn - 1);
    }
    return 0;
}

struct uwp_rd_bucket *uwp_rd_bucket_for_el( struct uwp_element *el, BOOL create )
{
    struct uwp_rd_bucket *b;
    if (!el) return NULL;
    if (el->rd_bucket) return el->rd_bucket;
    if (el->resources_rd)
    {
        b = get_rd_bucket( el->resources_rd, create );
        if (b) { b->owner_el = el; el->rd_bucket = b; }
        return b;
    }
    if (el->kind == UWP_KIND_RESOURCE_DICT)
    {
        b = get_rd_bucket( &el->IInspectable_iface, create );
        if (b) { b->owner_el = el; el->rd_bucket = b; }
        return b;
    }
    if (!create) return NULL;
    xaml_resource_dictionary_create_for_uwp( (void **)&el->resources_rd );
    if (!el->resources_rd) return NULL;
    b = get_rd_bucket( el->resources_rd, TRUE );
    if (b) { b->owner_el = el; el->rd_bucket = b; }
    return b;
}

/* --- Live engine: StandardCalculatorViewModel (CalcViewModel.dll) ---
 * Slot 7 = get_DisplayValue(HSTRING*), slot 79 = SendCommandToCalcManager(int).
 * Init is async (Concurrency task): m_calculatorManager lands at this+392
 * ~100ms after activation, so pump messages before the first SendCommand. */

const GUID IID_INotifyPropertyChanged_UWP =
    { 0xcf75d69c, 0xf2f4, 0x486b, { 0xb3, 0x02, 0xbb, 0x4c, 0x09, 0xba, 0xeb, 0xfa } };
const GUID IID_IPropertyChangedEventHandler_UWP =
    { 0x50f19c16, 0x0a22, 0x4d8e, { 0xa0, 0x89, 0x1e, 0xa9, 0x95, 0x16, 0x57, 0xd2 } };
const GUID IID_IPropertyChangedEventArgs_UWP =
    { 0x4f33a9a0, 0x5cf4, 0x47a4, { 0xb1, 0x6f, 0xd7, 0xfa, 0xaf, 0x17, 0x45, 0x7e } };
const GUID IID_IReference_NumbersAndOperatorsEnum =
    { 0x487a747c, 0x8529, 0x548a, { 0x92, 0x6f, 0x3b, 0xe3, 0x9f, 0x3e, 0xcb, 0xbf } };
const GUID IID_IDependencyPropertyChangedCallback_UWP =
    { 0x49c85e2a, 0xf407, 0x48e8, { 0x84, 0x54, 0x3e, 0x3f, 0x98, 0x31, 0xe9, 0xb9 } };


HRESULT xaml_dp_get_default_value( void *dp, IInspectable **out )
{
    IPropertyMetadata *meta = xaml_dp_get_metadata( dp );
    if (!out) return E_POINTER;
    *out = NULL;
    if (meta)
        return IPropertyMetadata_get_DefaultValue( meta, out );
    return S_OK;
}

/* --- IDependencyObject (slots 6=GetValue, 7=SetValue, 8=ClearValue, 9=ReadLocalValue, 10=GetAnimationBaseValue, 11=get_Dispatcher) --- */
HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !dp) return S_OK;
    if (g_in_tap_diag)
        WARN( "TAP GetValue on %s (%s) dp=%p\n", debugstr_w(impl->class_name), debugstr_w(impl->name), dp );

    {
        const WCHAR *dpn = xaml_dp_get_name( dp );
        if (dpn[0] && !wcscmp( dpn, L"DataContext" ))
        {
            struct uwp_element *cur;
            for (cur = impl; cur; cur = cur->parent)
            {
                if (cur->data_context)
                {
                    *out = cur->data_context;
                    IInspectable_AddRef( *out );
                    return S_OK;
                }
            }
        }
        if (dpn[0] && !wcscmp( dpn, L"Text" ) && uwp_has_text( impl ) &&
            uwp_is_text_input( impl ))
        {
            *out = box_string_value( uwp_elem_text( impl ) );
            if (*out) return S_OK;
        }
        if (dpn[0] && !wcscmp( dpn, L"Flyout" ) && impl->flyout)
        {
            *out = impl->flyout;
            IInspectable_AddRef( *out );
            return S_OK;
        }
        if (dpn[0] && !wcscmp( dpn, L"IsChecked" ))
        {
            *out = box_bool_value( impl->is_checked == 1 ? TRUE : FALSE );
            if (*out) return S_OK;
        }
    }

    AcquireSRWLockShared( &impl->dp_lock );
    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            *out = impl->dp_store[i].value;
            if (*out) IInspectable_AddRef( *out );
            ReleaseSRWLockShared( &impl->dp_lock );
            return S_OK;
        }
    }
    ReleaseSRWLockShared( &impl->dp_lock );

    /* Fall back to IPropertyMetadata::get_DefaultValue if DP has metadata */
    {
        IPropertyMetadata *meta = xaml_dp_get_metadata( dp );
        if (meta)
            IPropertyMetadata_get_DefaultValue( meta, out );
    }
    return S_OK;
}

static struct uwp_element *uwp_find_named_descendant( struct uwp_element *el, const WCHAR *name, int depth )
{
    if (!el || !name || !name[0] || depth > 8) return NULL;
    if (el->name[0] && !wcscmp( el->name, name )) return el;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r = (ch && ch != el) ? uwp_find_named_descendant( ch, name, depth + 1 ) : NULL;
        if (r) return r;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            struct uwp_element *r = (ch && ch != el) ? uwp_find_named_descendant( ch, name, depth + 1 ) : NULL;
            if (r) return r;
        }
    }
    return NULL;
}

static BOOL uwp_replace_iface( IInspectable **slot, IInspectable *val )
{
    if (*slot == val) return FALSE;
    if (*slot) IInspectable_Release( *slot );
    *slot = val;
    if (*slot) IInspectable_AddRef( *slot );
    return TRUE;
}

static BOOL uwp_wcs_in_table( const WCHAR *s, const WCHAR *const *table, size_t count )
{
    size_t i;
    for (i = 0; i < count; i++)
        if (!wcscmp( s, table[i] )) return TRUE;
    return FALSE;
}

static BOOL uwp_substr_in_table( const WCHAR *s, const WCHAR *const *table, size_t count )
{
    size_t i;
    for (i = 0; i < count; i++)
        if (wcsstr( s, table[i] )) return TRUE;
    return FALSE;
}

static void uwp_sync_template_binding_tree( struct uwp_element *owner, struct uwp_element *node,
                                             const WCHAR *dpname, IInspectable *val, int depth )
{
    if (!owner || !node || !dpname || !dpname[0] || depth > 16) return;
    if (node->templated_parent == owner && node->style_anim && node->style_anim->tpl_bind_src_prop[0] &&
        !wcscmp( node->style_anim->tpl_bind_src_prop, dpname ))
    {
        WORD tpid = node->style_anim->tpl_bind_target_pid;
        if (tpid == XBF_PROP_TEXTBLOCK_TEXT ||
            tpid == XBF_PROP_CONTENT ||
            tpid == XBF_PROP_FONTICON_GLYPH)
        {
            WCHAR sbuf[256] = {0};
            if (val && uwp_unbox_string( val, sbuf, ARRAY_SIZE(sbuf) ))
            {
                uwp_elem_set_text( node, sbuf );
                uwp_invalidate_window();
            }
        }
        else if (tpid == XBF_PROP_WIDTH)
        {
            double dv = 0.0;
            if (val && uwp_unbox_double( val, &dv ) && !isnan( dv ) && dv >= 0.0)
            {
                node->width = (dv > 0.1) ? dv : 0.1;
                g_layout_dirty = TRUE;
                uwp_invalidate_window();
            }
        }
        else if (tpid == XBF_PROP_FRAMEWORKELEMENT_HEIGHT ||
                 tpid == XBF_PROP_FLOW_DIRECTION)
        {
            double dv = 0.0;
            if (val && uwp_unbox_double( val, &dv ) && !isnan( dv ) && dv >= 0.0)
            {
                node->height = (dv > 0.1) ? dv : 0.1;
                g_layout_dirty = TRUE;
                uwp_invalidate_window();
            }
        }
    }
    if (node->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( node->content );
        if (ch && ch != node) uwp_sync_template_binding_tree( owner, ch, dpname, val, depth + 1 );
    }
    if (node->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( node->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != node) uwp_sync_template_binding_tree( owner, ch, dpname, val, depth + 1 );
        }
    }
}

static void uwp_sync_template_presenter( struct uwp_element *impl, const WCHAR *dpname, IInspectable *val )
{
    static const struct { const WCHAR *dp; const WCHAR *target; } repeater_forwards[] = {
        { L"ItemsSource",          L"ItemsSource"  },
        { L"ItemTemplate",         L"ItemTemplate" },
        { L"ItemTemplateSelector", L"ItemTemplate" },
    };
    static const struct { const WCHAR *dp; const WCHAR *part; } presenter_forwards[] = {
        { L"Header",              L"HeaderPresenter"       },
        { L"IslandHeaderContent", L"IslandHeaderPresenter" },
        { L"IslandContent",       L"IslandPresenter"       },
    };
    struct uwp_element *pres;
    size_t k;

    if (!impl || !dpname || !dpname[0]) return;
    if (impl->template_root)
        uwp_sync_template_binding_tree( impl, impl->template_root, dpname, val, 0 );
    for (k = 0; k < ARRAY_SIZE(repeater_forwards); k++)
    {
        struct uwp_element *rep;
        int j;
        if (wcscmp( dpname, repeater_forwards[k].dp )) continue;
        if (!val || (impl->class_flags & UWP_CF_RESP_ITEMS_REPEATER_LIST)) return;
        rep = uwp_find_named_descendant( impl, L"Repeater", 0 );
        if (!rep || rep == impl || !uwp_is_items_repeater( rep )) return;
        {
            void *target_dp = NULL;
            AcquireSRWLockShared( &rep->dp_lock );
            for (j = 0; j < rep->dp_cnt; j++)
            {
                const WCHAR *rdpn = xaml_dp_get_name( rep->dp_store[j].dp );
                if (rdpn[0] && !wcscmp( rdpn, repeater_forwards[k].target ))
                {
                    if (rep->dp_store[j].value != val)
                    {
                        target_dp = rep->dp_store[j].dp;
                        IUnknown_AddRef( (IUnknown *)target_dp );
                    }
                    break;
                }
            }
            ReleaseSRWLockShared( &rep->dp_lock );
            if (target_dp)
            {
                WARN( "uwp_sync_template_presenter: forwarding %s -> Repeater.%s val=%p\n",
                      debugstr_w(dpname), debugstr_w(repeater_forwards[k].target), val );
                uwp_do_SetValue( &rep->IDependencyObject_iface, target_dp, val );
                IUnknown_Release( (IUnknown *)target_dp );
            }
        }
        return;
    }
    for (k = 0; k < ARRAY_SIZE(presenter_forwards); k++)
    {
        if (wcscmp( dpname, presenter_forwards[k].dp )) continue;
        pres = uwp_find_named_descendant( impl, presenter_forwards[k].part, 0 );
        if (pres && uwp_replace_iface( &pres->content, val ))
            uwp_invalidate_window();
        return;
    }
}

static BOOL uwp_should_sync_display_text( const struct uwp_element *impl, const WCHAR *dpn )
{
    static const WCHAR *const meta_dps[] = {
        L"AutomationId", L"Name", L"HelpText", L"ItemStatus", L"LocalizedControlType", L"FullDescription",
    };
    static const WCHAR *const text_class_substrs[] = {
        L"OverflowTextBlock", L"TabsBarItem", L"TabViewItem", L"MenuBarItem",
    };
    static const WCHAR *const text_class_exact[] = {
        L"Windows.UI.Xaml.Controls.Control", L"Windows.UI.Xaml.Controls.ListViewItem",
    };

    if (uwp_wcs_in_table( dpn, meta_dps, ARRAY_SIZE(meta_dps) ))
        return FALSE;
    if (uwp_is_tooltip( impl ) || uwp_is_nav_settings_item( impl ))
        return FALSE;
    return uwp_is_display_result_element( impl ) ||
           uwp_substr_in_table( impl->class_name, text_class_substrs, ARRAY_SIZE(text_class_substrs) ) ||
           uwp_wcs_in_table( impl->class_name, text_class_exact, ARRAY_SIZE(text_class_exact) ) ||
           (impl->kind == UWP_KIND_TEXT_BLOCK && !wcscmp( dpn, L"Text" ));
}

static void uwp_tabview_handle_dp( struct uwp_element *impl, const WCHAR *dpname, IInspectable *val )
{
    static const struct { const WCHAR *dp; size_t off; BOOL invalidate; } tab_dps[] = {
        { L"TabItemsSource",              offsetof(struct uwp_nav_ext, tab_items_source),  TRUE  },
        { L"AddTabButtonCommand",         offsetof(struct uwp_nav_ext, tab_add_cmd),       FALSE },
        { L"AddTabButtonCommandParameter",offsetof(struct uwp_nav_ext, tab_add_cmd_param), FALSE },
    };
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    size_t k;

    if (!nav) return;
    TRACE( "TabView SetValue dp=%s val=%p\n", debugstr_w(dpname), val );
    for (k = 0; k < ARRAY_SIZE(tab_dps); k++)
    {
        if (!wcscmp( dpname, tab_dps[k].dp ))
        {
            IInspectable **slot = (IInspectable **)((char *)nav + tab_dps[k].off);
            if (uwp_replace_iface( slot, val ) && tab_dps[k].invalidate)
                uwp_invalidate_window();
            return;
        }
    }
    if (val && (ULONG_PTR)val >= 0x10000)
    {
        void *vm = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_ITabItemViewModel, &vm )) && vm)
        {
            IUnknown_Release( (IUnknown *)vm );
            if (uwp_replace_iface( &nav->selected_item, val ))
                uwp_invalidate_window();
        }
    }
}

void uwp_sync_all_template_presenters( struct uwp_element *impl )
{
    int i;
    if (!impl) return;
    for (i = 0; i < impl->dp_cnt; i++)
    {
        const WCHAR *dpn = xaml_dp_get_name( impl->dp_store[i].dp );
        if (dpn[0])
            uwp_sync_template_presenter( impl, dpn, impl->dp_store[i].value );
    }
}

static BOOL uwp_dp_values_equal( IInspectable *a, IInspectable *b )
{
    boolean ba = FALSE, bb = FALSE;
    INT32 ia = 0, ib = 0;
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    if (uwp_unbox_bool( a, &ba ) && uwp_unbox_bool( b, &bb ))
        return (ba != 0) == (bb != 0);
    if (uwp_unbox_int32( a, &ia ) && uwp_unbox_int32( b, &ib ))
        return ia == ib;
    return FALSE;
}

static void uwp_nav_sync_selection_tree( struct uwp_element *el, struct uwp_element *selected_nvi,
                                         IInspectable *selected_val, int depth )
{
    if (!el || depth > 6) return;
    if (uwp_is_nav_item( el ))
    {
        if (selected_nvi)
        {
            el->is_checked = (el == selected_nvi) ? 1 : 0;
        }
        else if (selected_val)
        {
            BOOL match = ((el->content && el->content == selected_val) ||
                          (el->data_context && el->data_context == selected_val) ||
                          (el->outer && el->outer == selected_val) ||
                          &el->IInspectable_iface == selected_val);
            el->is_checked = match ? 1 : 0;
        }
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el && !uwp_is_nav_view( ch ))
                uwp_nav_sync_selection_tree( ch, selected_nvi, selected_val, depth + 1 );
        }
    }
}

void uwp_nav_select_item( struct uwp_element *nav, struct uwp_element *nvi )
{
    IInspectable *item_val;
    if (!nvi) return;
    if (!nav)
    {
        struct uwp_element *cur;
        for (cur = nvi->parent; cur; cur = cur->parent)
        {
            if (uwp_is_nav_view( cur )) { nav = cur; break; }
        }
    }
    nvi->is_checked = 1;
    item_val = nvi->data_context ? nvi->data_context
             : ((nvi->content && !uwp_elem_from_any( nvi->content )) ? nvi->content
             : (nvi->outer ? nvi->outer : &nvi->IInspectable_iface));
    if (nav)
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( nav );
        if (ne) uwp_replace_iface( &ne->selected_item, item_val );
        uwp_nav_sync_selection_tree( nav, nvi, NULL, 0 );
    }
    uwp_invalidate_window();
}

static void uwp_setval_sync_display_text( struct uwp_element *impl, const WCHAR *dpn, IInspectable *val )
{
    WCHAR str_val[2048];
    if (!val || !uwp_should_sync_display_text( impl, dpn )) return;
    str_val[0] = 0;
    if (uwp_unbox_string( val, str_val, ARRAY_SIZE(str_val) ) && str_val[0])
    {
        uwp_elem_set_text( impl, str_val );
        if (impl->tpl_normal_output)
            uwp_elem_set_text( impl->tpl_normal_output, str_val );
        TRACE( "SetValue updated display text on %s (%s) -> %s\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_val) );
        uwp_invalidate_window();
    }
}

static void uwp_setval_apply_nav_dp( struct uwp_element *impl, const WCHAR *dpname, IInspectable *val )
{
    if (uwp_is_nav_view( impl ))
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
        INT32 ev = 0;
        boolean bv = FALSE;
        double dv = 0.0;
        if (!ne) return;
        if (!wcscmp( dpname, L"PaneDisplayMode" ) && val && uwp_unbox_enum_int32( val, &ev, TRUE ))
        {
            ne->nav_pane_display_mode = ev;
            ne->nav_pane_display_mode_set = TRUE;
            TRACE( "NavView(%s) SetValue PaneDisplayMode=%d\n", debugstr_w(impl->name), ev );
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"DisplayMode" ) && val && uwp_unbox_enum_int32( val, &ev, TRUE ))
        {
            ne->nav_display_mode = ev;
            ne->nav_display_mode_set = TRUE;
            TRACE( "NavView(%s) SetValue DisplayMode=%d\n", debugstr_w(impl->name), ev );
        }
        else if (!wcscmp( dpname, L"IsPaneOpen" ) && val && uwp_unbox_bool( val, &bv ))
        {
            ne->nav_is_pane_open = (bv != 0);
            uwp_nav_track_open_state( impl );
            if (!ne->nav_is_pane_open) ne->nav_scroll_y = 0;
            TRACE( "NavView(%s) SetValue IsPaneOpen=%d\n", debugstr_w(impl->name), (int)ne->nav_is_pane_open );
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"OpenPaneLength" ) && val && uwp_unbox_double( val, &dv ) && dv > 0.0)
        {
            ne->nav_open_pane_length = dv;
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"CompactPaneLength" ) && val && uwp_unbox_double( val, &dv ) && dv > 0.0)
        {
            ne->nav_compact_pane_length = dv;
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"CompactModeThresholdWidth" ) && val && uwp_unbox_double( val, &dv ) && dv > 0.0)
            ne->nav_compact_threshold = dv;
        else if (!wcscmp( dpname, L"ExpandedModeThresholdWidth" ) && val && uwp_unbox_double( val, &dv ) && dv > 0.0)
            ne->nav_expanded_threshold = dv;
        else if (!wcscmp( dpname, L"IsSettingsVisible" ) && val && uwp_unbox_bool( val, &bv ))
        {
            ne->nav_is_settings_visible = (bv != 0);
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"IsPaneToggleButtonVisible" ) && val && uwp_unbox_bool( val, &bv ))
        {
            ne->nav_is_pane_toggle_visible = (bv != 0);
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"SelectedItem" ))
        {
            uwp_replace_iface( &ne->selected_item, val );
            if (val) uwp_nav_sync_selection_tree( impl, NULL, val, 0 );
            uwp_invalidate_window();
        }
        return;
    }
    if (uwp_is_nav_item( impl ))
    {
        boolean bv = FALSE;
        if (!wcscmp( dpname, L"Icon" ))
        {
            struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
            if (ne && uwp_replace_iface( &ne->nav_icon, val ))
                uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"IsSelected" ) && val && uwp_unbox_bool( val, &bv ))
        {
            if (bv) uwp_nav_select_item( NULL, impl );
            else { impl->is_checked = 0; uwp_invalidate_window(); }
        }
    }
}

static void uwp_setval_apply_control_dp( struct uwp_element *impl, const WCHAR *dpname, IInspectable *val )
{
    double dv = 0.0;
    INT32 ev = 0;

    if (!dpname[0]) return;
    if (uwp_is_nav_view( impl ) || uwp_is_nav_item( impl ))
        uwp_setval_apply_nav_dp( impl, dpname, val );
    else if (impl->class_flags & UWP_CF_RADIO_BUTTON)
    {
        INT32 iv = -1;
        if (!wcscmp( dpname, L"SelectedIndex" ) && val && uwp_unbox_int32( val, &iv ))
            uwp_selector_set_selection( impl, iv, NULL, TRUE, FALSE );
        else if (!wcscmp( dpname, L"SelectedItem" ))
            uwp_selector_set_selection( impl, -1, val, FALSE, FALSE );
    }
    else if (uwp_is_combobox( impl ))
    {
        INT32 iv = -1;
        boolean bv = FALSE;
        WCHAR sbuf[64] = {0};
        if (!wcscmp( dpname, L"SelectedIndex" ) && val && uwp_unbox_int32( val, &iv ))
            uwp_selector_set_selection( impl, iv, NULL, TRUE, FALSE );
        else if (!wcscmp( dpname, L"SelectedItem" ))
            uwp_selector_set_selection( impl, -1, val, FALSE, FALSE );
        else if (!wcscmp( dpname, L"IsDropDownOpen" ) && val && uwp_unbox_bool( val, &bv ))
            uwp_combobox_set_open( impl, bv != 0 );
        else if (!wcscmp( dpname, L"DisplayMemberPath" ) && val && uwp_unbox_string( val, sbuf, ARRAY_SIZE(sbuf) ))
        {
            struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
            if (ne) lstrcpynW( ne->display_member_path, sbuf, ARRAY_SIZE(ne->display_member_path) );
        }
    }

    if (!wcscmp( dpname, L"Value" ) && val && uwp_unbox_double( val, &dv ) && !isnan( dv ))
    {
        impl->range_val = dv;
        uwp_invalidate_window();
    }
    else if (!wcscmp( dpname, L"Minimum" ) && val && uwp_unbox_double( val, &dv ) && !isnan( dv ))
    {
        impl->range_min = dv;
        uwp_invalidate_window();
    }
    else if (!wcscmp( dpname, L"Maximum" ) && val && uwp_unbox_double( val, &dv ) && !isnan( dv ))
    {
        impl->range_max = dv;
        uwp_invalidate_window();
    }
    else if (!wcscmp( dpname, L"HorizontalContentAlignment" ) && val && uwp_unbox_enum_int32( val, &ev, TRUE ))
    {
        impl->h_content_align = ev;
        impl->has_h_content_align = TRUE;
        uwp_invalidate_window();
    }
    else if (!wcscmp( dpname, L"VerticalContentAlignment" ) && val && uwp_unbox_enum_int32( val, &ev, TRUE ))
    {
        impl->v_content_align = ev;
        impl->has_v_content_align = TRUE;
        uwp_invalidate_window();
    }
    else if (!wcscmp( dpname, L"Flyout" ))
    {
        uwp_replace_iface( &impl->flyout, val );
        uwp_invalidate_window();
    }
    else if (!wcscmp( dpname, L"IsChecked" ) || !wcscmp( dpname, L"IsSelected" ))
    {
        boolean bv = FALSE;
        if (!val) impl->is_checked = 0;
        else if (uwp_unbox_bool( val, &bv )) impl->is_checked = bv ? 1 : 0;
        uwp_invalidate_window();
    }
    else if (impl->kind == UWP_KIND_POPUP)
    {
        boolean bv = FALSE;
        if (!wcscmp( dpname, L"IsOpen" ) && val && uwp_unbox_bool( val, &bv ))
            uwp_popup_set_open( impl, bv != 0 );
        else if (!wcscmp( dpname, L"HorizontalOffset" ) && val && uwp_unbox_double( val, &dv ) && !isnan( dv ))
        {
            impl->popup_h_offset = dv;
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"VerticalOffset" ) && val && uwp_unbox_double( val, &dv ) && !isnan( dv ))
        {
            impl->popup_v_offset = dv;
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpname, L"IsLightDismissEnabled" ) && val && uwp_unbox_bool( val, &bv ))
            impl->popup_light_dismiss = (bv != 0);
    }
}

static void uwp_setval_apply_media_source_dp( struct uwp_element *impl, IInspectable *val )
{
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    impl->probe_flags &= ~UWP_PF_NO_IMG;
    if (m && m->img_source != val)
    {
        if (m->img_source) IInspectable_Release( m->img_source );
        m->img_source = val;
        if (m->img_source) IInspectable_AddRef( m->img_source );
    }
    if (val && m)
    {
        WCHAR resolved_src[512] = {0};
        struct uwp_element *src_el = uwp_elem_from_any( val );
        const WCHAR *src_str = uwp_elem_image_src( src_el );
        if (src_str[0])
            lstrcpynW( resolved_src, src_str, ARRAY_SIZE(resolved_src) );
        else if (!uwp_unbox_string( val, resolved_src, ARRAY_SIZE(resolved_src) ))
            uwp_extract_uri_string( val, resolved_src, ARRAY_SIZE(resolved_src) );
        if (resolved_src[0])
        {
            lstrcpynW( m->image_src, resolved_src, ARRAY_SIZE(m->image_src) );
            m->img_loaded = FALSE;
            InterlockedExchange( &m->img_loading, 0 );
            if (impl->parent && impl->parent->kind == UWP_KIND_IMAGE)
            {
                struct uwp_media_ext *pm = uwp_elem_ensure_media( impl->parent );
                impl->parent->probe_flags &= ~UWP_PF_NO_IMG;
                if (pm)
                {
                    lstrcpynW( pm->image_src, resolved_src, ARRAY_SIZE(pm->image_src) );
                    pm->img_loaded = FALSE;
                    InterlockedExchange( &pm->img_loading, 0 );
                }
            }
            uwp_invalidate_window();
        }
    }
    else if (impl->kind == UWP_KIND_IMAGE && m)
    {
        m->image_src[0] = 0;
        m->img_loaded = FALSE;
        InterlockedExchange( &m->img_loading, 0 );
        uwp_invalidate_window();
    }
}

static void uwp_setval_fire_metadata_cb( struct uwp_element *impl, void *dp,
                                         IPropertyMetadata *meta,
                                         IInspectable **old_val, IInspectable *val )
{
    IInspectable *def_val = xaml_pm_get_default_value( meta );
    IPropertyChangedCallback *cb = xaml_pm_get_callback( meta );
    struct uwp_dp_changed_args *args;
    IInspectable *sender;
    HRESULT cb_hr = S_OK;
    IInspectable *nv = NULL;

    if (!*old_val && def_val)
    {
        *old_val = def_val;
        IInspectable_AddRef( *old_val );
    }
    if (!cb || (!*old_val && !val)) return;

    args = calloc( 1, sizeof(*args) );
    if (!args) return;
    sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
    args->vtbl = dp_changed_args_vtbl;
    args->ref = 1;
    args->dp = dp;
    if ((ULONG_PTR)dp >= 0x10000) IUnknown_AddRef( (IUnknown *)dp );
    args->old_val = *old_val;
    if (*old_val) IInspectable_AddRef( *old_val );
    args->new_val = val;
    if (val) IInspectable_AddRef( val );
    TRACE( "uwp_do_SetValue invoking PropertyChangedCallback=%p sender=%p (%s) dp=%p old=%p new=%p\n",
           cb, sender, debugstr_w(impl->class_name), dp, *old_val, val );
    __TRY
    {
        cb_hr = IPropertyChangedCallback_Invoke( cb, (IDependencyObject *)sender,
                                                 (IDependencyPropertyChangedEventArgs *)args );
    }
    __EXCEPT_ALL { cb_hr = E_FAIL; }
    __ENDTRY;
    if (FAILED(cb_hr))
        WARN( "uwp_do_SetValue PropertyChangedCallback=%p sender=%p (%s) dp=%p -> FAILED hr %#lx\n",
              cb, sender, debugstr_w(impl->class_name), dp, cb_hr );
    IDependencyPropertyChangedEventArgs_get_NewValue( (IDependencyPropertyChangedEventArgs *)args, &nv );
    TRACE( "uwp_do_SetValue callback read NewValue -> %p (val %p)\n", (void *)nv, (void *)val );
    if (nv) IInspectable_Release( nv );
    dp_args_Release( args );
}

static void uwp_setval_fire_dp_callbacks( struct uwp_element *impl, void *dp )
{
    struct xaml_event_handler *cbs_snap[32];
    int snap_cnt = 0, i;

    AcquireSRWLockShared( &impl->dp_lock );
    for (i = 0; i < impl->dp_cb_cnt && snap_cnt < (int)ARRAY_SIZE(cbs_snap); i++)
    {
        if (impl->dp_cbs[i].dp == dp && impl->dp_cbs[i].callback)
        {
            cbs_snap[snap_cnt] = (struct xaml_event_handler *)impl->dp_cbs[i].callback;
            IUnknown_AddRef( (IUnknown *)cbs_snap[snap_cnt] );
            snap_cnt++;
        }
    }
    ReleaseSRWLockShared( &impl->dp_lock );

    for (i = 0; i < snap_cnt; i++)
    {
        void *sender = impl->outer ? (void *)impl->outer : (void *)&impl->IDependencyObject_iface;
        struct xaml_event_handler *h = cbs_snap[i];
        HRESULT cb_hr = S_OK;
        __TRY { cb_hr = h->lpVtbl->Invoke( h, sender, dp ); }
        __EXCEPT_ALL { cb_hr = E_FAIL; }
        __ENDTRY;
        TRACE( "uwp_do_SetValue dp_cb[%d] h=%p sender=%p (%s) dp=%p -> hr %#lx\n",
               i, h, sender, debugstr_w(impl->class_name), dp, cb_hr );
        IUnknown_Release( (IUnknown *)h );
    }
}

static void uwp_setval_post_commit( struct uwp_element *impl, const WCHAR *dpn, IInspectable *val )
{
    if (uwp_is_items_repeater( impl ))
    {
        struct uwp_element *rep;
        if (impl->in_measure_arrange) return;
        impl->last_measured_w = 0.0f;
        impl->last_measured_h = 0.0f;
        impl->probe_flags = 0;
        rep = uwp_find_named_descendant( impl, L"Repeater", 0 );
        if (rep && rep != impl && !rep->in_measure_arrange)
        {
            rep->last_measured_w = 0.0f;
            rep->last_measured_h = 0.0f;
            rep->probe_flags = 0;
        }
        uwp_invalidate_window();
        return;
    }
    if (!dpn[0]) return;
    if (!wcscmp( dpn, L"ItemsSource" ) || !wcscmp( dpn, L"Content" ) ||
        !wcscmp( dpn, L"DataContext" ) || !wcscmp( dpn, L"Text" ) || !wcscmp( dpn, L"Visibility" ))
    {
        if (!wcscmp( dpn, L"Content" ) || !wcscmp( dpn, L"DataContext" ))
        {
            impl->probe_flags = 0;
            if (!uwp_is_text_input( impl )) uwp_elem_set_placeholder( impl, NULL );
        }
        if (!wcscmp( dpn, L"ItemsSource" ))
        {
            uwp_replace_iface( &impl->items_source, val );
            if (val && !uwp_is_combobox( impl ))
                uwp_items_ctrl_materialize_source( impl, val );
            else if (uwp_is_combobox( impl ) && uwp_elem_selected_item( impl ))
                uwp_selector_set_selection( impl, -1, uwp_elem_selected_item( impl ), FALSE, FALSE );
        }
        uwp_invalidate_window();
    }
}

HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val )
{
    static __thread struct { struct uwp_element *el; void *dp; IInspectable *val; } setval_stack[64];
    static __thread int setval_depth = 0;
    struct uwp_element *impl = uwp_elem_from_any( iface );
    IInspectable *old_val = NULL;
    const WCHAR *dpn;
    IPropertyMetadata *meta;
    BOOL found_existing = FALSE, same_val = FALSE, pushed_setval = FALSE;
    int i, si;

    if (!impl || !dp) return S_OK;
    for (si = 0; si < setval_depth; si++)
    {
        if (setval_stack[si].el == impl &&
            setval_stack[si].dp == dp && uwp_dp_values_equal( setval_stack[si].val, val ))
            return S_OK;
    }
    if (setval_depth >= (int)ARRAY_SIZE(setval_stack))
        return S_OK;

    if ((ULONG_PTR)dp >= 0x10000) IUnknown_AddRef( (IUnknown *)dp );
    dpn = xaml_dp_get_name( dp );

    uwp_setval_sync_display_text( impl, dpn, val );
    if (!wcscmp( dpn, L"DataContext" ))
        uwp_propagate_data_context( impl, val );
    if (uwp_is_tabview( impl ))
        uwp_tabview_handle_dp( impl, dpn, val );
    else
        uwp_setval_apply_control_dp( impl, dpn, val );

    AcquireSRWLockExclusive( &impl->dp_lock );
    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            found_existing = TRUE;
            old_val = impl->dp_store[i].value;
            if (old_val == val) { same_val = TRUE; break; }
            impl->dp_store[i].value = val;
            if (val) IInspectable_AddRef( val );
            break;
        }
    }
    if (!found_existing)
    {
        if (!xaml_vec_grow( (void **)&impl->dp_store, &impl->dp_cap, impl->dp_cnt + 1, sizeof(*impl->dp_store), 24 ))
        {
            ReleaseSRWLockExclusive( &impl->dp_lock );
            if ((ULONG_PTR)dp >= 0x10000) IUnknown_Release( (IUnknown *)dp );
            return E_OUTOFMEMORY;
        }
        impl->dp_store[impl->dp_cnt].dp = dp;
        if ((ULONG_PTR)dp >= 0x10000) IUnknown_AddRef( (IUnknown *)dp );
        impl->dp_store[impl->dp_cnt].value = val;
        if (val) IInspectable_AddRef( val );
        impl->dp_cnt++;
    }
    ReleaseSRWLockExclusive( &impl->dp_lock );

    if (same_val)
    {
        if ((ULONG_PTR)dp >= 0x10000) IUnknown_Release( (IUnknown *)dp );
        return S_OK;
    }

    if (setval_depth < (int)ARRAY_SIZE(setval_stack))
    {
        setval_stack[setval_depth].el = impl;
        setval_stack[setval_depth].dp = dp;
        setval_stack[setval_depth].val = val;
        setval_depth++;
        pushed_setval = TRUE;
    }

    meta = xaml_dp_get_metadata( dp );
    if (dpn[0])
        uwp_sync_template_presenter( impl, dpn, val );
    if (!wcscmp( dpn, L"Style" ))
        uwp_fe_put_Style( &impl->IInspectable_iface, val );
    if (!wcscmp( dpn, L"Source" ) || !wcscmp( dpn, L"UriSource" ) || !wcscmp( dpn, L"ImageSource" ))
        uwp_setval_apply_media_source_dp( impl, val );
    if (meta)
        uwp_setval_fire_metadata_cb( impl, dp, meta, &old_val, val );

    uwp_setval_fire_dp_callbacks( impl, dp );

    if (pushed_setval)
    {
        for (si = setval_depth - 1; si >= 0; si--)
        {
            if (setval_stack[si].el == impl && setval_stack[si].dp == dp)
            {
                setval_stack[si] = setval_stack[--setval_depth];
                break;
            }
        }
    }

    uwp_setval_post_commit( impl, dpn, val );

    if (old_val) IInspectable_Release( old_val );
    if ((ULONG_PTR)dp >= 0x10000) IUnknown_Release( (IUnknown *)dp );
    return S_OK;
}

HRESULT WINAPI uwp_do_ClearValue( void *iface, void *dp )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *dpn = xaml_dp_get_name( dp );
    IInspectable *old_val = NULL;
    void *old_dp = NULL;
    int i;
    if (!impl || !dp) return S_OK;
    if (dpn[0])
    {
        if (!wcscmp( dpn, L"IsSelected" ) || !wcscmp( dpn, L"IsChecked" ))
        {
            impl->is_checked = 0;
            uwp_invalidate_window();
        }
        else if (!wcscmp( dpn, L"SelectedItem" ) && uwp_is_nav_view( impl ))
        {
            if (impl->nav) uwp_replace_iface( &impl->nav->selected_item, NULL );
            uwp_invalidate_window();
        }
    }
    AcquireSRWLockExclusive( &impl->dp_lock );
    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            old_val = impl->dp_store[i].value;
            old_dp = impl->dp_store[i].dp;
            impl->dp_store[i] = impl->dp_store[--impl->dp_cnt];
            break;
        }
    }
    ReleaseSRWLockExclusive( &impl->dp_lock );
    if (old_val) IInspectable_Release( old_val );
    if ((ULONG_PTR)old_dp >= 0x10000) IUnknown_Release( (IUnknown *)old_dp );
    return S_OK;
}

HRESULT WINAPI uwp_do_get_Dispatcher( void *iface, void **value )
{
    ICoreWindow *cw;
    if (!value) return E_POINTER;
    *value = NULL;
    cw = get_current_core_window();
    if (cw) return ICoreWindow_get_Dispatcher( cw, (ICoreDispatcher **)value );
    return S_OK;
}

static IInspectable *g_cached_ui_dispatcher_queue;

HRESULT WINAPI uwp_do_get_DispatcherQueue( void *iface, void **value )
{
    static const GUID IID_IDispatcherQueueStatics_UWP =
        { 0xa96d83d7, 0x9371, 0x4517, { 0x92, 0x45, 0xd0, 0x82, 0x4a, 0xc1, 0x2c, 0x74 } };
    static const GUID IID_IMicrosoftDispatcherQueueStatics_WinUI3 =
        { 0xcd3382ea, 0xa455, 0x5124, { 0xb6, 0x3a, 0xca, 0x40, 0xd3, 0x4c, 0xa2, 0x3c } };
    BOOL is_winui3 = xaml_get_app_caps()->winui3;
    const WCHAR *name = is_winui3
        ? L"Microsoft.UI.Dispatching.DispatcherQueue"
        : L"Windows.System.DispatcherQueue";
    const GUID *statics_iid = is_winui3
        ? &IID_IMicrosoftDispatcherQueueStatics_WinUI3
        : &IID_IDispatcherQueueStatics_UWP;
    HSTRING hstr;
    IInspectable *statics = NULL;
    IInspectable *dq = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (g_cached_ui_dispatcher_queue)
    {
        IInspectable_AddRef( g_cached_ui_dispatcher_queue );
        *value = g_cached_ui_dispatcher_queue;
        return S_OK;
    }
    hr = WindowsCreateString( name, lstrlenW(name), &hstr );
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory( hstr, statics_iid, (void **)&statics );
    WindowsDeleteString( hstr );
    if (SUCCEEDED(hr) && statics)
    {
        const struct abi_dispatcher_queue_statics *dqs = (const struct abi_dispatcher_queue_statics *)statics;
        if (dqs->lpVtbl->GetForCurrentThread) hr = dqs->lpVtbl->GetForCurrentThread( statics, (void **)&dq );
        IInspectable_Release( statics );
    }
    if (dq)
    {
        if (!InterlockedCompareExchangePointer( (void **)&g_cached_ui_dispatcher_queue, dq, NULL ))
            IInspectable_AddRef( dq );
        *value = dq;
        return S_OK;
    }
    if (g_cached_ui_dispatcher_queue)
    {
        IInspectable_AddRef( g_cached_ui_dispatcher_queue );
        *value = g_cached_ui_dispatcher_queue;
        return S_OK;
    }
    return hr;
}
