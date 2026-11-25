/* WinRT Windows.UI.Xaml UWP Visual Tree, PRI/XBF Loader, Layout & Input Host
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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "weakreference.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);



/* Split from uwp_xaml.c: hit-testing, pointer/mouse/tapped dispatch, keyboard & accelerators. */

static struct uwp_element *uwp_resolve_spotlight_card( struct uwp_element *spot_el, int target_idx )
{
    struct uwp_element *page_root = spot_el;
    struct uwp_element *cards[4] = {0};
    WCHAR titles[4][128];
    struct uwp_element *stack[128];
    int sp = 0, cnt = 0;

    if (!spot_el || target_idx < 0 || target_idx >= 4) return NULL;
    while (page_root->parent) page_root = page_root->parent;
    memset( titles, 0, sizeof(titles) );
    stack[sp++] = page_root;
    while (sp > 0 && cnt < 4)
    {
        struct uwp_element *cur = stack[--sp];
        if (!cur) continue;
        if (cur != spot_el && wcsstr( cur->class_name, L"Card" ) && cur->text[0])
        {
            const WCHAR *nl = wcschr( cur->text, '\n' );
            size_t tlen = nl ? (size_t)(nl - cur->text) : wcslen( cur->text );
            int dup = 0, d;
            WCHAR tbuf[128] = {0};
            if (tlen >= ARRAY_SIZE(tbuf)) tlen = ARRAY_SIZE(tbuf) - 1;
            memcpy( tbuf, cur->text, tlen * sizeof(WCHAR) );
            tbuf[tlen] = 0;
            for (d = 0; d < cnt; d++)
                if (!wcscmp( titles[d], tbuf )) { dup = 1; break; }
            if (!dup && tbuf[0])
            {
                lstrcpynW( titles[cnt], tbuf, ARRAY_SIZE(titles[0]) );
                cards[cnt++] = cur;
            }
        }
        if (cur->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( cur->children_coll, struct uwp_vec_view, IInspectable_iface );
            int k;
            for (k = (int)v->count - 1; k >= 0 && sp < (int)ARRAY_SIZE(stack); k--)
            {
                struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
                if (ch && ch != cur) stack[sp++] = ch;
            }
        }
        if (cur->content && sp < (int)ARRAY_SIZE(stack))
        {
            struct uwp_element *ch = uwp_elem_from_any( cur->content );
            if (ch && ch != cur) stack[sp++] = ch;
        }
    }
    if (cnt == 0) return NULL;
    return cards[min( cnt - 1, target_idx )];
}

static struct uwp_element *uwp_hit_test_button( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0 || !el->is_hit_test_visible) return NULL;
    if (el->has_bounds && wcsstr( el->class_name, L"ScrollViewer" ) &&
        (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom))
        return NULL;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_button( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = uwp_hit_test_button( ch, x, y );
        if (hit) return hit;
    }

    if (el->has_bounds && el->is_enabled &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        wcsstr( el->class_name, L"DynamicSpotlightControl" ))
    {
        RECT rc = el->bounds;
        int avail_w = rc.right - rc.left, avail_h = rc.bottom - rc.top;
        if (avail_w >= 480 && avail_h >= 180)
        {
            int pad = 12, gap = 12;
            int hero_w = ((avail_w - pad * 2 - gap * 2) * 58) / 100;
            int hx0 = rc.left + pad, hx1 = hx0 + hero_w;
            int prev_w = max( 1, (avail_w - pad * 2 - hero_w - gap * 2) / 2 );
            int idx = (x <= hx1) ? 0 : (1 + max( 0, min( 1, (x - (hx1 + gap)) / max( 1, prev_w + gap ) ) ));
            struct uwp_element *spot_card = uwp_resolve_spotlight_card( el, idx );
            if (spot_card) return spot_card;
        }
    }

    if (el->has_bounds && el->is_enabled &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         el->click_cnt > 0 || el->command != NULL ||
         wcsstr( el->class_name, L"MenuBarItem" )))
    {
        if (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" ))
            return NULL;
        return el;
    }
    return NULL;
}

BOOL uwp_has_focused_textbox( void )
{
    return g_focused_textbox && g_focused_textbox->visibility == 0 && g_focused_textbox->is_enabled;
}

void uwp_keyboard_press_char( WCHAR ch )
{
    WCHAR buf[2] = { ch, 0 };
    struct uwp_element *root = g_navigated_page_el, *hit;

    if (g_focused_textbox && g_focused_textbox->visibility == 0 && g_focused_textbox->is_enabled)
    {
        size_t len = wcslen( g_focused_textbox->text );
        BOOL changed = FALSE;
        TRACE( "uwp_keyboard_press_char: ch=%#x focused=%s cur=%s\n",
               (unsigned)ch, debugstr_w(g_focused_textbox->name), debugstr_w(g_focused_textbox->text) );
        if (ch == L'\b')
        {
            if (len > 0) { g_focused_textbox->text[len - 1] = 0; changed = TRUE; }
        }
        else if (ch == 27)
        {
            g_focused_textbox->is_open = FALSE;
            g_focused_textbox->is_dropdown_open = FALSE;
            if (len > 0) { g_focused_textbox->text[0] = 0; changed = TRUE; }
            else
            {
                struct uwp_element *prev = g_focused_textbox;
                g_focused_textbox = NULL;
                uwp_ui_fire_lost_focus( prev );
            }
        }
        else if (ch == L'\r')
        {
            g_focused_textbox->is_open = FALSE;
            g_focused_textbox->is_dropdown_open = FALSE;
            uwp_fire_asb_recursive( g_focused_textbox, g_focused_textbox->text, FALSE );
            uwp_fire_asb_recursive( g_focused_textbox, g_focused_textbox->text, TRUE );
        }
        else if (ch >= 0x20 && len + 1 < ARRAY_SIZE(g_focused_textbox->text))
        {
            g_focused_textbox->text[len] = ch;
            g_focused_textbox->text[len + 1] = 0;
            changed = TRUE;
        }
        if (changed && g_focused_textbox)
        {
            if (!g_focused_textbox->text[0])
            {
                g_focused_textbox->is_open = FALSE;
                g_focused_textbox->is_dropdown_open = FALSE;
            }
            uwp_fire_asb_recursive( g_focused_textbox, g_focused_textbox->text, FALSE );
        }
        uwp_invalidate_window();
        return;
    }

    if (!root) return;
    if (ch == L'\r') buf[0] = L'=';
    if ((hit = uwp_find_button_by_text( root, buf ))) uwp_invoke_button( hit );
}

BOOL uwp_fire_preview_keydown_tree( struct uwp_element *el, struct uwp_key_routed_args *args )
{
    if (!el || !args || el->visibility != 0) return FALSE;
    if (wcsstr( el->class_name, L"TeachingTip" ) && !el->is_open) return FALSE;

    {
        void **handlers = NULL;
        unsigned int hcnt = xaml_event_registry_snapshot_alloc( &el->preview_keydown_reg, &handlers );
        if (hcnt > 0)
        {
            IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
            unsigned int hi;
            for (hi = 0; hi < hcnt; hi++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                if (h)
                {
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                    IUnknown_Release( (IUnknown *)handlers[hi] );
                }
            }
            free( handlers );
            if (args->handled) return TRUE;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_fire_preview_keydown_tree( ch, args ))
            return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && uwp_fire_preview_keydown_tree( ch, args ))
                return TRUE;
        }
    }
    return FALSE;
}

BOOL uwp_invoke_accel_entry( const struct uwp_accel_entry *e )
{
    struct uwp_element *accel = e->accel_el;
    struct uwp_element *target = e->target_el;
    UINT vk = e->vk, mods = e->mods;
    int i;

    TRACE( "accelerator inv: %c%s%s%s (vk=%#x mods=%#x target=%s %s)\n",
           ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) ? (char)vk : '?',
           (mods & 1) ? " ctrl" : "",
           (mods & 2) ? " alt" : "",
           (mods & 4) ? " shift" : "",
           vk, mods,
           target ? debugstr_w(target->class_name) : "?",
           target ? debugstr_w(target->name) : "" );

    if (accel && accel->accel_invoked_cnt > 0)
    {
        struct uwp_ka_invoked_args *args = calloc( 1, sizeof(*args) );
        if (args)
        {
            IInspectable *sender = accel->outer ? accel->outer : &accel->IKeyboardAccelerator_iface;
            boolean handled = FALSE;
            args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ka_args_vtbl;
            args->ref = 1;
            args->handled = FALSE;
            if (target)
            {
                args->element = target->outer ? target->outer : &target->IDependencyObject_iface;
                IInspectable_AddRef( args->element );
            }
            for (i = 0; i < accel->accel_invoked_cnt; i++)
            {
                if (accel->accel_invoked_handlers[i])
                {
                    struct xaml_event_handler *h = (struct xaml_event_handler *)accel->accel_invoked_handlers[i];
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                }
            }
            handled = args->handled;
            uwp_ka_args_Release( &args->IInspectable_iface );
            if (handled) return TRUE;
        }
    }

    if (target)
    {
        uwp_invoke_button( target );
        return TRUE;
    }
    return FALSE;
}

void uwp_invoke_button( struct uwp_element *btn )
{
    int i;
    if (!btn || !btn->is_enabled) return;

    WARN( "uwp_invoke_button: %s (%s, text=%s, cmd=%p, param=%p, dc=%p, clicks=%d, tapped=%d, ptrP=%d, ptrR=%d, dp=%d)\n",
          debugstr_w(btn->class_name), debugstr_w(btn->name), debugstr_w(btn->text),
          btn->command, btn->command_param, btn->data_context, btn->click_cnt,
          (int)btn->tapped_reg.next_token, (int)btn->ptr_pressed_reg.next_token,
          (int)btn->ptr_released_reg.next_token, btn->dp_cnt );
    for (i = 0; i < btn->dp_cnt; i++)
    {
        struct { IDependencyProperty iface; LONG ref; void *meta; WCHAR name[64]; } *dpl = (void *)btn->dp_store[i].dp;
        WARN( "  dp[%d] name=%s val=%p\n", i,
              (dpl && (ULONG_PTR)dpl >= 0x10000) ? debugstr_w(dpl->name) : "?", btn->dp_store[i].value );
    }

    /* 1. Invoke ICommand::Execute(CommandParameter) if Command is bound */
    if (btn->command)
    {
        __TRY
        {
            void *cmd_iface = NULL;
            HRESULT qhr = IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP, &cmd_iface );
            if (FAILED(qhr))
                qhr = IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP2, &cmd_iface );
            if (SUCCEEDED(qhr) && cmd_iface)
            {
                /* Slot 8: CanExecute(object, boolean*), Slot 9: Execute(object) */
                struct command_iface *cmd = (struct command_iface *)cmd_iface;
                if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, btn->command_param );
                IUnknown_Release( (IUnknown *)cmd_iface );
            }
            else
            {
                /* Direct vtbl call if already on ICommand face */
                struct command_iface *cmd = (struct command_iface *)btn->command;
                HRESULT ehr = E_NOTIMPL;
                if (cmd->lpVtbl->Execute) ehr = cmd->lpVtbl->Execute( cmd, btn->command_param );
                WARN( "uwp_invoke_button: direct Execute qhr=%#lx cmd=%p fn=%p -> hr=%#lx\n",
                      (unsigned long)qhr, cmd, cmd->lpVtbl->Execute, (unsigned long)ehr );
            }
        }
        __EXCEPT_ALL
        {
            WARN( "uwp_invoke_button: ICommand::Execute faulted\n" );
        }
        __ENDTRY;
    }

    /* 2. Invoke registered Click handlers */
    for (i = 0; i < btn->click_cnt; i++)
    {
        if (btn->click_handlers[i])
        {
            __TRY
            {
                IInspectable *sender = btn->outer ? btn->outer : &btn->IInspectable_iface;
                struct xaml_event_handler *h = (struct xaml_event_handler *)btn->click_handlers[i];
                HMODULE mod = NULL;
                void *inv = h->lpVtbl->Invoke;
                HRESULT chr;
                GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)inv, &mod );
                chr = h->lpVtbl->Invoke( h, sender, g_routed_args() );
                WARN( "uwp_invoke_button: Click[%d] h=%p inv=%p (mod=%p +0x%lx) sender=%p -> hr=%#lx\n",
                      i, h, inv, mod, mod ? (unsigned long)((const char *)inv - (const char *)mod) : 0ul,
                      sender, (unsigned long)chr );
            }
            __EXCEPT_ALL
            {
                WARN( "uwp_invoke_button: Click handler %d faulted\n", i );
            }
            __ENDTRY;
        }
    }
    if (btn->tapped_reg.next_token > 0 && btn->click_cnt == 0 && !btn->command)
        uwp_fire_tapped_bubble( btn, btn->bounds.left + 8, btn->bounds.top + 8 );

    /* 3. Resolve CalculatorButton.ButtonId / CommandParameter dynamically via IXamlMetadataProvider */
    if (xaml_get_app_caps()->vm_type[0])
    {
        int code = uwp_resolve_button_calc_code( btn );
        if (code >= 0)
            uwp_calc_vm_send( code, btn->command_param );
    }

    uwp_invalidate_window();
}

static BOOL uwp_fire_tapped( struct uwp_element *el, int x, int y )
{
    void **handlers = NULL;
    unsigned int count, i;
    struct uwp_tapped_args *args;
    IInspectable *sender;

    if (!el || el->tapped_reg.next_token == 0) return FALSE;
    count = xaml_event_registry_snapshot_alloc( &el->tapped_reg, &handlers );
    if (!count) return FALSE;

    sender = el->outer ? el->outer : &el->IInspectable_iface;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return FALSE;
    }
    args->ITappedRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_vtbl;
    args->IRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_routed_vtbl;
    args->ref = 1;
    args->orig_source = sender;
    if (sender) IInspectable_AddRef( sender );
    args->x = (float)x;
    args->y = (float)y;

    TRACE( "uwp_fire_tapped on %s (%s / %s) handlers=%u\n",
           debugstr_w(el->class_name), debugstr_w(el->name), debugstr_w(el->text), count );
    for (i = 0; i < count; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, &args->ITappedRoutedEventArgs_iface );
        }
        __EXCEPT_ALL {}
        __ENDTRY;
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    free( handlers );
    uwp_tap_Release( &args->ITappedRoutedEventArgs_iface );
    return TRUE;
}

static struct uwp_element *g_pressed_combobox = NULL;
static BOOL g_cb_popup_down = FALSE;

static struct uwp_element *uwp_hit_test_combobox( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0 || !el->is_hit_test_visible) return NULL;
    if (el->has_bounds && wcsstr( el->class_name, L"ScrollViewer" ) &&
        (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom))
        return NULL;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_combobox( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = (ch && ch != el) ? uwp_hit_test_combobox( ch, x, y ) : NULL;
        if (hit) return hit;
    }
    if (el->has_bounds && el->is_enabled && uwp_is_combobox( el ) &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom)
        return el;
    return NULL;
}

static struct uwp_element *uwp_find_open_overlay_nav( struct uwp_element *el, int depth )
{
    if (!el || depth > 12 || el->visibility != 0) return NULL;
    if (uwp_nav_is_overlay_open( el )) return el;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *r = (ch && ch != el) ? uwp_find_open_overlay_nav( ch, depth + 1 ) : NULL;
            if (r) return r;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r = (ch && ch != el) ? uwp_find_open_overlay_nav( ch, depth + 1 ) : NULL;
        if (r) return r;
    }
    return NULL;
}

static struct uwp_element *uwp_hit_test_nav_item( struct uwp_element *nav, int x, int y )
{
    if (!nav || !nav->children_coll) return NULL;
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( nav->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *host = uwp_elem_from_any( children->items[k] );
            if (!host || !host->children_coll) continue;
            if (wcscmp( host->name, L"MenuItemsHost" ) && wcscmp( host->name, L"FooterMenuItemsHost" ))
                continue;
            {
                struct uwp_vec_view *items = CONTAINING_RECORD( host->children_coll, struct uwp_vec_view, IInspectable_iface );
                unsigned int j;
                for (j = 0; j < items->count; j++)
                {
                    struct uwp_element *it = uwp_elem_from_any( items->items[j] );
                    if (!it || !it->has_bounds || uwp_is_nav_header( it ) || uwp_is_nav_separator( it ))
                        continue;
                    if (x >= it->bounds.left && x < it->bounds.right &&
                        y >= it->bounds.top && y < it->bounds.bottom)
                        return it;
                }
            }
        }
    }
    return NULL;
}

static struct uwp_element *uwp_find_scroll_viewer_at( struct uwp_element *el, int x, int y, int depth )
{
    if (!el || depth > 24 || el->visibility != 0) return NULL;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *r = (ch && ch != el) ? uwp_find_scroll_viewer_at( ch, x, y, depth + 1 ) : NULL;
            if (r) return r;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r = (ch && ch != el) ? uwp_find_scroll_viewer_at( ch, x, y, depth + 1 ) : NULL;
        if (r) return r;
    }
    if (el->has_bounds && wcsstr( el->class_name, L"ScrollViewer" ) &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        el->scroll_extent_h > (el->bounds.bottom - el->bounds.top))
        return el;
    return NULL;
}

BOOL uwp_hit_test_interactive_client( IInspectable *root_content, int x, int y )
{
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (root_el && root_el->kind == UWP_KIND_FRAME && !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        root_el = g_navigated_page_el;
    if (!root_el) return FALSE;
    if (uwp_hit_test_textbox( root_el, x, y )) return TRUE;
    if (uwp_hit_test_button( root_el, x, y )) return TRUE;
    if (uwp_hit_test_combobox( root_el, x, y )) return TRUE;
    if (uwp_hit_test_tabview( root_el, x, y )) return TRUE;
    return FALSE;
}

BOOL uwp_handle_overlay_mouse_input( HWND hwnd, UINT msg, int x, int y, int wheel_delta,
                                     int width, int height, IInspectable *root_content )
{
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (root_el && root_el->kind == UWP_KIND_FRAME && !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        root_el = g_navigated_page_el;

    if (g_open_combobox && g_open_combobox->is_dropdown_open)
    {
        struct uwp_element *cb = g_open_combobox;
        IInspectable *items[256];
        int count = uwp_selector_get_items( cb, items, ARRAY_SIZE(items) );
        RECT prc;
        int item_h = 32, vis_cnt = 0, pad_y = 4, i;
        BOOL has_rect = (count > 0 && uwp_combobox_compute_popup_rect( cb, count, width, height, &prc, &item_h, &vis_cnt, &pad_y ));
        BOOL in_popup = has_rect && x >= prc.left && x < prc.right && y >= prc.top && y < prc.bottom;
        BOOL handled = FALSE;

        if (msg == WM_MOUSEMOVE)
        {
            int new_hov = -1;
            if (in_popup)
            {
                int row = (y - (prc.top + pad_y)) / max( 1, item_h );
                if (row >= 0 && row < vis_cnt && (cb->dropdown_scroll_offset + row) < count)
                    new_hov = cb->dropdown_scroll_offset + row;
            }
            if (new_hov != cb->hovered_dropdown_idx)
            {
                cb->hovered_dropdown_idx = new_hov;
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                handled = TRUE;
            }
        }
        else if (msg == WM_MOUSEWHEEL)
        {
            if (in_popup && count > vis_cnt)
            {
                int step = (wheel_delta > 0) ? -1 : 1;
                int max_sc = max( 0, count - vis_cnt );
                int ns = cb->dropdown_scroll_offset + step;
                if (ns < 0) ns = 0;
                if (ns > max_sc) ns = max_sc;
                cb->dropdown_scroll_offset = ns;
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                handled = TRUE;
            }
        }
        else if (msg == WM_LBUTTONDOWN)
        {
            if (in_popup)
            {
                g_cb_popup_down = TRUE;
            }
            else
            {
                g_cb_popup_down = FALSE;
                uwp_combobox_set_open( cb, FALSE );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            }
            handled = TRUE;
        }
        else if (msg == WM_LBUTTONUP)
        {
            if (g_cb_popup_down)
            {
                g_cb_popup_down = FALSE;
                if (in_popup)
                {
                    int row = (y - (prc.top + pad_y)) / max( 1, item_h );
                    int idx = cb->dropdown_scroll_offset + row;
                    if (row >= 0 && row < vis_cnt && idx >= 0 && idx < count)
                        uwp_selector_set_selection( cb, idx, NULL, TRUE, TRUE );
                }
                uwp_combobox_set_open( cb, FALSE );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            }
            handled = TRUE;
        }
        for (i = 0; i < count; i++) IInspectable_Release( items[i] );
        return handled;
    }

    if (g_focused_textbox && wcsstr( g_focused_textbox->class_name, L"AutoSuggestBox" ) &&
        g_focused_textbox->is_open)
    {
        static BOOL g_asb_popup_down = FALSE;
        struct uwp_element *asb = g_focused_textbox;
        IInspectable *items[32];
        int count = uwp_asb_get_suggestions( asb, items, ARRAY_SIZE(items) );
        RECT prc;
        int item_h = 38, vis_cnt = 0, pad_y = 6, i;
        BOOL has_rect = (count > 0 && uwp_asb_compute_popup_rect( asb, count, width, height, &prc, &item_h, &vis_cnt, &pad_y ));
        BOOL in_popup = has_rect && x >= prc.left && x < prc.right && y >= prc.top && y < prc.bottom;
        BOOL handled = FALSE;

        if (has_rect)
        {
            if (msg == WM_MOUSEMOVE)
            {
                int new_hov = -1;
                if (in_popup)
                {
                    int row = (y - (prc.top + pad_y)) / max( 1, item_h );
                    if (row >= 0 && row < vis_cnt && (asb->dropdown_scroll_offset + row) < count)
                        new_hov = asb->dropdown_scroll_offset + row;
                }
                if (new_hov != asb->hovered_dropdown_idx)
                {
                    asb->hovered_dropdown_idx = new_hov;
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                    handled = TRUE;
                }
            }
            else if (msg == WM_LBUTTONDOWN && in_popup)
            {
                g_asb_popup_down = TRUE;
                handled = TRUE;
            }
            else if (msg == WM_LBUTTONUP && g_asb_popup_down)
            {
                g_asb_popup_down = FALSE;
                if (in_popup)
                {
                    int row = (y - (prc.top + pad_y)) / max( 1, item_h );
                    int idx = asb->dropdown_scroll_offset + row;
                    if (row >= 0 && row < vis_cnt && idx >= 0 && idx < count)
                    {
                        WCHAR prim[128] = {0};
                        uwp_asb_get_item_label( asb, idx, items[idx], prim, ARRAY_SIZE(prim), NULL, 0 );
                        if (prim[0])
                            lstrcpynW( asb->text, prim, ARRAY_SIZE(asb->text) );
                        asb->is_open = FALSE;
                        asb->is_dropdown_open = FALSE;
                        uwp_asb_fire_suggestion_chosen( asb, items[idx] );
                        uwp_asb_fire_query_submitted_ex( asb, items[idx] );
                    }
                }
                asb->is_open = FALSE;
                asb->is_dropdown_open = FALSE;
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                handled = TRUE;
            }
        }
        for (i = 0; i < count; i++) IInspectable_Release( items[i] );
        if (handled) return TRUE;
    }

    if (root_el)
    {
        struct uwp_element *nav = uwp_find_open_overlay_nav( root_el, 0 );
        if (nav && nav->nav_pane_bounds.right > nav->nav_pane_bounds.left)
        {
            RECT prc = nav->nav_pane_bounds;
            BOOL in_pane = (x >= prc.left && x < prc.right && y >= prc.top && y < prc.bottom);
            if (msg == WM_MOUSEWHEEL && in_pane)
            {
                int ns = nav->nav_scroll_y + ((wheel_delta > 0) ? -40 : 40);
                if (ns < 0) ns = 0;
                nav->nav_scroll_y = ns;
                g_layout_dirty = TRUE;
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                return TRUE;
            }
            if (msg == WM_LBUTTONDOWN && !in_pane)
            {
                uwp_nav_set_pane_open( nav, FALSE );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                return TRUE;
            }
        }
        if (msg == WM_MOUSEWHEEL)
        {
            struct uwp_element *sv = uwp_find_scroll_viewer_at( root_el, x, y, 0 );
            if (sv)
            {
                int vp_h = max( 1, (int)(sv->bounds.bottom - sv->bounds.top) );
                int max_sc = max( 0, sv->scroll_extent_h - vp_h );
                int step = (wheel_delta != 0) ? -(wheel_delta * 120) / WHEEL_DELTA : 0;
                int ns;
                if (step == 0) step = (wheel_delta > 0) ? -120 : 120;
                ns = sv->scroll_y + step;
                if (ns < 0) ns = 0;
                if (max_sc > 0 && ns > max_sc) ns = max_sc;
                if (ns != sv->scroll_y)
                {
                    sv->scroll_y = ns;
                    g_layout_dirty = TRUE;
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content )
{
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (!root_el) return FALSE;
    if (root_el->kind == UWP_KIND_FRAME && !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        root_el = g_navigated_page_el;   /* Frame.content RCW unresolvable; hit the adopted page */

    if (width > 0 && height > 0 &&
        (!root_el->has_bounds ||
         (root_el->bounds.right - root_el->bounds.left) != width ||
         (root_el->bounds.bottom - root_el->bounds.top) != height))
    {
        RECT rc = { 0, 0, width, height };
        uwp_arrange_element( root_el, rc );
    }

    if (msg == WM_LBUTTONDOWN)
    {
        struct uwp_element *tv = uwp_hit_test_tabview( root_el, x, y );
        struct uwp_element *cb = tv ? NULL : uwp_hit_test_combobox( root_el, x, y );
        struct uwp_element *tb = (tv || cb) ? NULL : uwp_hit_test_textbox( root_el, x, y );
        struct uwp_element *btn = (tv || cb || tb) ? NULL : uwp_hit_test_button( root_el, x, y );
        if (tv)
        {
            g_pressed_tabview = tv;
            return TRUE;
        }
        if (cb)
        {
            g_pressed_combobox = cb;
            return TRUE;
        }
        if (tb)
        {
            struct uwp_element *prev = g_focused_textbox;
            g_focused_textbox = tb;
            if (prev && prev != tb)
            {
                prev->is_open = FALSE;
                prev->is_dropdown_open = FALSE;
                uwp_ui_fire_lost_focus( prev );
            }
            if (prev != tb)
                uwp_ui_fire_got_focus( tb );
            TRACE( "uwp_handle_mouse_input: FOCUS TextBox (%d,%d) -> %s\n", x, y, debugstr_w(tb->name) );
            if (hwnd)
                InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        if (g_focused_textbox)
        {
            struct uwp_element *prev = g_focused_textbox;
            prev->is_open = FALSE;
            prev->is_dropdown_open = FALSE;
            g_focused_textbox = NULL;
            uwp_ui_fire_lost_focus( prev );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
        }
        if (btn && wcsstr( btn->class_name, L"MenuBarItem" ))
        {
            if (uwp_popup_is_open_for( btn ))
            {
                uwp_close_flyout_popup( TRUE );
                g_last_closed_flyout = NULL;
                return TRUE;
            }
            if (g_last_closed_flyout == btn && (GetTickCount() - g_last_closed_tick) < 250)
            {
                g_last_closed_flyout = NULL;
                return TRUE;
            }
            {
                struct xaml_pointer_info info = {0};
                POINT spt = { btn->bounds.left, btn->bounds.bottom };
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = TRUE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( btn, XAML_PTR_PRESSED, &info, TRUE );
                if (hwnd) ClientToScreen( hwnd, &spt );
                uwp_show_flyout_popup( btn, hwnd ? GetAncestor( hwnd, GA_ROOT ) : NULL, spt.x, spt.y );
            }
            return TRUE;
        }
        if (btn)
        {
            struct xaml_pointer_info info = {0};
            g_pressed_btn = btn;
            btn->is_pressed = TRUE;
            info.x = (float)x;
            info.y = (float)y;
            info.pointer_id = 1;
            info.is_left = TRUE;
            info.is_in_contact = TRUE;
            info.is_in_window = TRUE;
            uwp_fire_element_pointer( btn, XAML_PTR_PRESSED, &info, TRUE );
            TRACE( "uwp_handle_mouse_input: DOWN (%d,%d) -> %s (%s)\n",
                   x, y, debugstr_w(btn->name), debugstr_w(btn->text) );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
    }
    else if (msg == WM_MOUSEMOVE)
    {
        struct uwp_element *hit = uwp_hit_test_button( root_el, x, y );
        if (hit != g_hovered_elem)
        {
            if (g_hovered_elem) g_hovered_elem->is_hovered = FALSE;
            g_hovered_elem = hit;
            if (hit) hit->is_hovered = TRUE;
            if (hit && wcsstr( hit->class_name, L"MenuBarItem" ) &&
                g_popup_owner_el && wcsstr( g_popup_owner_el->class_name, L"MenuBarItem" ) &&
                hit != g_popup_owner_el)
            {
                struct xaml_pointer_info info = {0};
                POINT spt = { hit->bounds.left, hit->bounds.bottom };
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = TRUE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( hit, XAML_PTR_PRESSED, &info, TRUE );
                if (hwnd) ClientToScreen( hwnd, &spt );
                uwp_show_flyout_popup( hit, hwnd ? GetAncestor( hwnd, GA_ROOT ) : NULL, spt.x, spt.y );
            }
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        return FALSE;
    }
    else if (msg == WM_LBUTTONUP)
    {
        if (g_pressed_tabview)
        {
            struct uwp_element *tv = g_pressed_tabview;
            struct uwp_element *hit = uwp_hit_test_tabview( root_el, x, y );
            g_pressed_tabview = NULL;
            if (hit == tv)
            {
                IInspectable *items[64];
                int count = uwp_tabview_get_items( tv, items, ARRAY_SIZE(items) );
                int n = max( 1, count ), i;
                POINT pt = { x, y };
                RECT pill, close_rc, add_rc;
                uwp_tabview_compute_rects( &tv->bounds, n, 0, &pill, &close_rc, &add_rc );
                if (PtInRect( &add_rc, pt ))
                {
                    uwp_tabview_fire_add_tab( tv );
                }
                else
                {
                    for (i = 0; i < count; i++)
                    {
                        uwp_tabview_compute_rects( &tv->bounds, n, i, &pill, &close_rc, &add_rc );
                        if (PtInRect( &close_rc, pt ))
                        {
                            uwp_tabview_fire_close_tab( tv, items[i] );
                            break;
                        }
                        if (PtInRect( &pill, pt ))
                        {
                            uwp_tabview_select_item( tv, items[i] );
                            break;
                        }
                    }
                }
                for (i = 0; i < count; i++)
                    IInspectable_Release( items[i] );
            }
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        if (g_pressed_combobox)
        {
            struct uwp_element *cb = g_pressed_combobox;
            struct uwp_element *hit = uwp_hit_test_combobox( root_el, x, y );
            g_pressed_combobox = NULL;
            if (hit == cb)
            {
                uwp_combobox_set_open( cb, !cb->is_dropdown_open );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            }
            return TRUE;
        }
        if (g_pressed_btn)
        {
            struct uwp_element *btn = g_pressed_btn;
            struct uwp_element *hit = uwp_hit_test_button( root_el, x, y );
            btn->is_pressed = FALSE;
            g_pressed_btn = NULL;
            WARN( "uwp_handle_mouse_input: UP (%d,%d) pressed=%p(%s) hit=%p(%s)\n",
                  x, y, btn, debugstr_w(btn->class_name), hit, hit ? debugstr_w(hit->class_name) : "(null)" );
            if (hit == btn)
            {
                struct uwp_element *nav = (!wcscmp( btn->name, L"TogglePaneButton" ) && btn->parent && uwp_is_nav_view( btn->parent ))
                    ? btn->parent : NULL;
                BOOL prev_open = nav ? nav->nav_is_pane_open : FALSE;
                struct xaml_pointer_info info = {0};
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = FALSE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( btn, XAML_PTR_RELEASED, &info, TRUE );
                uwp_invoke_button( btn );
                if (nav && nav->nav_is_pane_open == prev_open)
                    uwp_nav_set_pane_open( nav, !prev_open );
            }
            else
                uwp_fire_tapped_bubble( btn, x, y );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        {
            struct uwp_element *nav = uwp_find_open_overlay_nav( root_el, 0 );
            if (nav)
            {
                struct uwp_element *nav_item = uwp_hit_test_nav_item( nav, x, y );
                if (nav_item)
                {
                    IInspectable *item_obj = nav_item->content ? nav_item->content
                        : (nav_item->outer ? nav_item->outer : &nav_item->IInspectable_iface);
                    uwp_nav_select_item( nav, nav_item );
                    if (nav->nav_sel_changed_reg.next_token > 0)
                        uwp_nav_fire( nav, &nav->nav_sel_changed_reg, item_obj );
                    if (nav->nav_item_invoked_reg.next_token > 0)
                        uwp_nav_fire( nav, &nav->nav_item_invoked_reg, item_obj );
                    uwp_nav_set_pane_open( nav, FALSE );
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                    return TRUE;
                }
            }
        }
        {
            struct uwp_element *tap = uwp_hit_test_tappable( root_el, x, y );
            if (tap && uwp_fire_tapped( tap, x, y ))
            {
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                return TRUE;
            }
        }
    }
    return FALSE;
}

struct uwp_element *uwp_hit_test_element( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0 || !el->is_enabled || !el->is_hit_test_visible)
        return NULL;
    if (uwp_is_nav_header( el ) || uwp_is_nav_separator( el ))
        return NULL;
    if (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom)
        return NULL;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit;
            if (ch && !ch->parent) ch->parent = el;
            hit = uwp_hit_test_element( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit;
        if (ch && !ch->parent && ch != el) ch->parent = el;
        hit = uwp_hit_test_element( ch, x, y );
        if (hit) return hit;
    }
    /* In UWP/WinUI XAML, a Border or Panel with Background == null is hit-test
     * transparent so clicks pass through to underlying siblings. */
    if ((el->kind == UWP_KIND_BORDER || el->kind == UWP_KIND_GRID ||
         el->kind == UWP_KIND_STACK_PANEL) &&
        !el->has_bg && !el->comp_brush && !el->fill_brush &&
        el->tapped_reg.next_token == 0 &&
        el->ptr_pressed_reg.next_token == 0 &&
        el->ptr_released_reg.next_token == 0)
        return NULL;
    return el;
}

BOOL uwp_fire_tapped_bubble( struct uwp_element *target, int x, int y )
{
    struct uwp_tapped_args *args;
    struct uwp_element *cur;
    IInspectable *orig_sender;
    BOOL fired = FALSE;

    if (!target) return FALSE;
    WARN( "uwp_fire_tapped_bubble: target=%s (%s) txt=%s (%d,%d)\n",
          debugstr_w(target->class_name), debugstr_w(target->name), debugstr_w(target->text), x, y );
    orig_sender = target->outer ? target->outer : &target->IInspectable_iface;
    args = calloc( 1, sizeof(*args) );
    if (!args) return FALSE;
    args->ITappedRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_vtbl;
    args->IRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_routed_vtbl;
    args->ref = 1;
    args->orig_source = orig_sender;
    if (orig_sender) IInspectable_AddRef( orig_sender );
    args->x = (float)x;
    args->y = (float)y;

    for (cur = target; cur && !args->handled; cur = cur->parent)
    {
        void **handlers = NULL;
        unsigned int count, i;
        IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;

        if (cur->outer)
        {
            void *ctrl_ovr = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( cur->outer, &IID_IControlOverrides_UWP, &ctrl_ovr )) && ctrl_ovr)
            {
                if (!((char *)ctrl_ovr >= (char *)cur && (char *)ctrl_ovr < (char *)(cur + 1)))
                {
                    HRESULT (WINAPI *on_tapped)(void *, void *) = (void *)(*(void ***)ctrl_ovr)[14];
                    if (on_tapped)
                    {
                        WARN( "uwp_fire_tapped_bubble: IControlOverrides::OnTapped on %s (%s) txt=%s\n",
                              debugstr_w(cur->class_name), debugstr_w(cur->name), debugstr_w(cur->text) );
                        __TRY { on_tapped( ctrl_ovr, &args->ITappedRoutedEventArgs_iface ); fired = TRUE; }
                        __EXCEPT_ALL {}
                        __ENDTRY;
                    }
                }
                IUnknown_Release( (IUnknown *)ctrl_ovr );
            }
        }
        if (cur->tapped_reg.next_token > 0)
        {
            count = xaml_event_registry_snapshot_alloc( &cur->tapped_reg, &handlers );
            for (i = 0; i < count; i++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
                HRESULT thr = S_OK;
                WARN( "uwp_fire_tapped_bubble: invoking tapped_reg[%u/%u] on %s (%s) txt=%s handled=%d\n",
                      i, count, debugstr_w(cur->class_name), debugstr_w(cur->name), debugstr_w(cur->text), args->handled );
                g_in_tap_diag = TRUE;
                __TRY { thr = h->lpVtbl->Invoke( h, sender, &args->ITappedRoutedEventArgs_iface ); fired = TRUE; }
                __EXCEPT_ALL { WARN( "uwp_fire_tapped_bubble: tapped_reg[%u] threw!\n", i ); }
                __ENDTRY;
                g_in_tap_diag = FALSE;
                WARN( "uwp_fire_tapped_bubble: after tapped_reg[%u] hr=%#lx handled=%d\n", i, thr, args->handled );
                IUnknown_Release( (IUnknown *)handlers[i] );
            }
            free( handlers );
        }
    }
    uwp_tap_Release( &args->ITappedRoutedEventArgs_iface );
    return fired;
}

BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble )
{
    struct uwp_pointer_routed_args *args;
    struct uwp_element *cur;
    BOOL fired = FALSE;
    int ovr_slot = -1;

    if (!target) return FALSE;
    switch (kind)
    {
    case XAML_PTR_ENTERED:      ovr_slot = 6;  break;
    case XAML_PTR_PRESSED:      ovr_slot = 7;  break;
    case XAML_PTR_MOVED:        ovr_slot = 8;  break;
    case XAML_PTR_RELEASED:     ovr_slot = 9;  break;
    case XAML_PTR_EXITED:       ovr_slot = 10; break;
    case XAML_PTR_CAPTURE_LOST: ovr_slot = 11; break;
    case XAML_PTR_WHEEL:        ovr_slot = 13; break;
    default:                    ovr_slot = -1; break;
    }

    args = uwp_pointer_routed_args_create( info, target->outer ? target->outer : &target->IInspectable_iface );
    if (!args) return FALSE;

    for (cur = target; cur && !args->handled; cur = bubble ? cur->parent : NULL)
    {
        struct xaml_event_registry *reg = uwp_elem_ptr_registry( cur, kind );
        void **handlers = NULL;
        unsigned int i, count = reg ? xaml_event_registry_snapshot_alloc( reg, &handlers ) : 0;
        IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;

        if (ovr_slot >= 0 && cur->outer)
        {
            void *ctrl_ovr = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( cur->outer, &IID_IControlOverrides_UWP, &ctrl_ovr )) && ctrl_ovr)
            {
                if (!((char *)ctrl_ovr >= (char *)cur && (char *)ctrl_ovr < (char *)(cur + 1)))
                {
                    HRESULT (WINAPI *on_ptr)(void *, void *) = (void *)(*(void ***)ctrl_ovr)[ovr_slot];
                    if (on_ptr)
                    {
                        __TRY
                        {
                            on_ptr( ctrl_ovr, &args->IPointerRoutedEventArgs_vtbl );
                            fired = TRUE;
                        }
                        __EXCEPT_ALL
                        {
                            TRACE( "uwp_fire_element_pointer: IControlOverrides[%d] faulted on %s\n",
                                   ovr_slot, debugstr_w(cur->class_name) );
                        }
                        __ENDTRY;
                    }
                }
                IUnknown_Release( (IUnknown *)ctrl_ovr );
            }
        }

        for (i = 0; i < count; i++)
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
            if (h->lpVtbl->Invoke)
            {
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, &args->IPointerRoutedEventArgs_vtbl );
                    fired = TRUE;
                }
                __EXCEPT_ALL
                {
                    TRACE( "uwp_fire_element_pointer: handler %p faulted on %s\n", handlers[i], debugstr_w(cur->name) );
                }
                __ENDTRY;
            }
            IUnknown_Release( (IUnknown *)handlers[i] );
        }
        free( handlers );
    }

    pra_Release( args );
    return fired;
}

