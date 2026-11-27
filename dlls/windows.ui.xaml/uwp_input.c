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

static struct uwp_element *uwp_hit_test_button( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0 || !el->is_hit_test_visible) return NULL;
    if (el->has_bounds && el->kind != UWP_KIND_POPUP &&
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
    if (el->template_root && el->template_root != el &&
        !uwp_is_combobox( el ) && !uwp_is_expander( el ) && !uwp_is_nav_view( el ) &&
        !uwp_is_tabview( el ) && !uwp_is_toggle_switch( el ))
    {
        struct uwp_element *hit = uwp_hit_test_button( el->template_root, x, y );
        if (hit) return hit;
    }
    else if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = uwp_hit_test_button( ch, x, y );
        if (hit) return hit;
    }

    if (el->has_bounds && el->is_enabled &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         uwp_is_toggle_switch( el ) ||
         UWP_EV_CNT( el, click_reg ) > 0 || el->command != NULL || el->flyout != NULL ||
         uwp_is_menubar_item( el )))
    {
        if (uwp_paint_is_invisible_shortcut_button( el ))
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

    if (ch == 27 && uwp_popup_handle_escape())
        return;

    if (g_focused_textbox && g_focused_textbox->visibility == 0 && g_focused_textbox->is_enabled)
    {
        const WCHAR *cur_txt = uwp_elem_text( g_focused_textbox );
        size_t len = wcslen( cur_txt );
        BOOL changed = FALSE;
        TRACE( "uwp_keyboard_press_char: ch=%#x focused=%s cur=%s\n",
               (unsigned)ch, debugstr_w(g_focused_textbox->name), debugstr_w(cur_txt) );
        if (ch == L'\b')
        {
            if (len > 0)
            {
                uwp_elem_set_text_n( g_focused_textbox, cur_txt, len - 1 );
                changed = TRUE;
            }
        }
        else if (ch == 27)
        {
            g_focused_textbox->is_open = FALSE;
            g_focused_textbox->is_dropdown_open = FALSE;
            if (len > 0) { uwp_elem_clear_text( g_focused_textbox ); changed = TRUE; }
            else
            {
                struct uwp_element *prev = g_focused_textbox;
                g_focused_textbox = NULL;
                uwp_ui_fire_lost_focus( prev );
            }
        }
        else if (ch == L'\r')
        {
            struct uwp_element *sub_tb = g_focused_textbox;
            sub_tb->is_open = FALSE;
            sub_tb->is_dropdown_open = FALSE;
            g_focused_textbox = NULL;
            uwp_fire_asb_recursive( sub_tb, uwp_elem_text( sub_tb ), TRUE );
            sub_tb->is_open = FALSE;
            sub_tb->is_dropdown_open = FALSE;
        }
        else if (ch >= 0x20)
        {
            WCHAR add[2] = { ch, 0 };
            uwp_elem_append_text( g_focused_textbox, add );
            changed = TRUE;
        }
        if (changed && g_focused_textbox)
        {
            if (!uwp_has_text( g_focused_textbox ))
            {
                g_focused_textbox->is_open = FALSE;
                g_focused_textbox->is_dropdown_open = FALSE;
            }
            uwp_fire_asb_recursive( g_focused_textbox, uwp_elem_text( g_focused_textbox ), FALSE );
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
    if (uwp_is_teaching_tip( el ) && !el->is_open) return FALSE;

    if (el->events)
    {
        void **handlers = NULL;
        unsigned int hcnt = xaml_event_registry_snapshot_alloc( &el->events->preview_keydown_reg, &handlers );
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

    TRACE( "accelerator inv: %c%s%s%s (vk=%#x mods=%#x target=%s %s)\n",
           ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) ? (char)vk : '?',
           (mods & 1) ? " ctrl" : "",
           (mods & 2) ? " alt" : "",
           (mods & 4) ? " shift" : "",
           vk, mods,
           target ? debugstr_w(target->class_name) : "?",
           target ? debugstr_w(target->name) : "" );

    if (accel && UWP_EV_CNT( accel, accel_invoked_reg ) > 0)
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
            uwp_fire_event_registry( &accel->events->accel_invoked_reg, sender,
                                     &args->IInspectable_iface, "KeyboardAccelerator::Invoked" );
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

    WARN( "uwp_invoke_button: %s (%s, text=%s, cmd=%p, param=%p, dc=%p, clicks=%u, tapped=%d, ptrP=%d, ptrR=%d, dp=%d)\n",
          debugstr_w(btn->class_name), debugstr_w(btn->name), debugstr_w(uwp_elem_text( btn )),
          btn->command, btn->command_param, btn->data_context, UWP_EV_CNT( btn, click_reg ),
          btn->events ? (int)btn->events->tapped_reg.next_token : 0,
          btn->events ? (int)btn->events->ptr_pressed_reg.next_token : 0,
          btn->events ? (int)btn->events->ptr_released_reg.next_token : 0, btn->dp_cnt );
    for (i = 0; i < btn->dp_cnt; i++)
    {
        const WCHAR *dpn = xaml_dp_get_name( btn->dp_store[i].dp );
        WARN( "  dp[%d] name=%s val=%p\n", i,
              dpn[0] ? debugstr_w(dpn) : "?", btn->dp_store[i].value );
    }

    /* 0. Toggle IsChecked on ToggleButton / IsOn on ToggleSwitch before firing Click/Command */
    if (uwp_is_toggle_switch( btn ))
    {
        btn->is_checked = (btn->is_checked == 1) ? 0 : 1;
        uwp_invalidate_window();
    }
    else if (btn->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        BOOL is_radio = !wcscmp( btn->class_name, L"Windows.UI.Xaml.Controls.RadioButton" );
        int target_chk = is_radio ? 1 : ((btn->is_checked == 1) ? 0 : 1);
        uwp_toggle_button_set_checked( btn, target_chk, TRUE );
        if (btn->parent &&
            ((btn->parent->class_flags & (UWP_CF_RADIO_BUTTON | UWP_CF_ITEMS_REPEATER | UWP_CF_ITEMS_CONTROL |
                                          UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW | UWP_CF_LISTBOX)) ||
             is_radio || UWP_EV_CNT( btn->parent, sel_changed_reg ) > 0) &&
            btn->parent->children_coll)
        {
            struct uwp_element *rb = btn->parent;
            struct uwp_vec_view *children = CONTAINING_RECORD( rb->children_coll, struct uwp_vec_view, IInspectable_iface );
            INT32 sel_idx = -1;
            IInspectable *sel_item = NULL;
            unsigned int k;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *sib = uwp_elem_from_any( children->items[k] );
                if (!sib) continue;
                if (sib == btn)
                {
                    if (btn->is_checked == 1)
                    {
                        sel_idx = (INT32)k;
                        sel_item = btn->data_context ? btn->data_context : &btn->IInspectable_iface;
                    }
                }
                else if (btn->is_checked == 1 && sib->is_checked != 0)
                {
                    uwp_toggle_button_set_checked( sib, 0, FALSE );
                }
            }
            uwp_selector_set_selection( rb, sel_idx, sel_item, sel_idx >= 0, TRUE );
        }
    }

    /* 0b. Open attached Flyout / MenuFlyout on Button / DropDownButton */
    if (btn->flyout && !uwp_is_menubar_item( btn ))
    {
        struct uwp_element *flyout_el = uwp_elem_from_any( btn->flyout );
        if (flyout_el && UWP_EV_CNT( flyout_el, flyout_opening_reg ) > 0)
        {
            IInspectable *fsender = flyout_el->outer ? flyout_el->outer : &flyout_el->IFlyout_iface;
            uwp_fire_event_registry( &flyout_el->events->flyout_opening_reg, fsender, NULL, "Flyout::Opening" );
        }
        g_popup_owner_el = btn;
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
    if (UWP_EV_CNT( btn, click_reg ) > 0)
    {
        void **handlers = NULL;
        unsigned int hi, cnt = xaml_event_registry_snapshot_alloc( &btn->events->click_reg, &handlers );
        IInspectable *sender = btn->outer ? btn->outer : &btn->IInspectable_iface;
        for (hi = 0; hi < cnt; hi++)
        {
            __TRY
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                HMODULE mod = NULL;
                void *inv = h->lpVtbl->Invoke;
                HRESULT chr;
                IInspectable *args = (IInspectable *)g_routed_args_for( btn );
                GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)inv, &mod );
                chr = h->lpVtbl->Invoke( h, sender, args );
                if (args) IInspectable_Release( args );
                WARN( "uwp_invoke_button: Click[%u] h=%p inv=%p (mod=%p +0x%lx) sender=%p -> hr=%#lx\n",
                      hi, h, inv, mod, mod ? (unsigned long)((const char *)inv - (const char *)mod) : 0ul,
                      sender, (unsigned long)chr );
            }
            __EXCEPT_ALL
            {
                WARN( "uwp_invoke_button: Click handler %u faulted\n", hi );
            }
            __ENDTRY;
            IUnknown_Release( (IUnknown *)handlers[hi] );
        }
        free( handlers );
    }
    if (UWP_EV_ANY( btn, tapped_reg ) && UWP_EV_CNT( btn, click_reg ) == 0 && !btn->command)
        uwp_fire_tapped_bubble( btn, btn->bounds.left + 8, btn->bounds.top + 8 );

    /* 3. If CloseButton inside an open Popup was clicked and Popup is still open, close it */
    if (!wcscmp( btn->name, L"CloseButton" ))
    {
        struct uwp_element *pops[16];
        int pcnt = uwp_get_open_popups( pops, ARRAY_SIZE(pops) ), pi;
        for (pi = pcnt - 1; pi >= 0; pi--)
        {
            struct uwp_element *pop = pops[pi];
            struct uwp_element *pch = uwp_elem_from_any( pop->content );
            struct uwp_element *anc;
            for (anc = btn; anc; anc = anc->parent)
            {
                if (anc == pop || (pch && anc == pch))
                {
                    if (pop->is_open) uwp_popup_set_open( pop, FALSE );
                    break;
                }
            }
        }
    }

    uwp_invalidate_window();
}

static BOOL uwp_fire_tapped( struct uwp_element *el, int x, int y )
{
    void **handlers = NULL;
    unsigned int count, i;
    struct uwp_tapped_args *args;
    IInspectable *sender;

    if (!UWP_EV_ANY( el, tapped_reg )) return FALSE;
    count = xaml_event_registry_snapshot_alloc( &el->events->tapped_reg, &handlers );
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
           debugstr_w(el->class_name), debugstr_w(el->name), debugstr_w(uwp_elem_text( el )), count );
    for (i = 0; i < count; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, &args->ITappedRoutedEventArgs_iface );
        }
        __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    free( handlers );
    uwp_tap_Release( &args->ITappedRoutedEventArgs_iface );
    return TRUE;
}

static struct uwp_element *g_pressed_combobox = NULL;
static BOOL g_cb_popup_down = FALSE;
static struct uwp_element *g_pressed_cdp = NULL;
static struct uwp_element *g_pressed_expander = NULL;

static struct uwp_element *uwp_hit_test_cdp( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0 || !el->is_hit_test_visible) return NULL;
    if (el->has_bounds && el->kind != UWP_KIND_POPUP &&
        (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom))
        return NULL;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_cdp( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = (ch && ch != el) ? uwp_hit_test_cdp( ch, x, y ) : NULL;
        if (hit) return hit;
    }
    if (el->has_bounds && el->is_enabled && uwp_is_calendar_date_picker( el ) &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom)
        return el;
    return NULL;
}

static struct uwp_element *uwp_hit_test_expander_header( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0 || !el->is_hit_test_visible) return NULL;
    if (el->has_bounds && el->kind != UWP_KIND_POPUP &&
        (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom))
        return NULL;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_expander_header( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->template_root && el->template_root != el &&
        !uwp_is_combobox( el ) && !uwp_is_expander( el ) && !uwp_is_nav_view( el ) &&
        !uwp_is_tabview( el ) && !uwp_is_toggle_switch( el ))
    {
        struct uwp_element *hit = uwp_hit_test_expander_header( el->template_root, x, y );
        if (hit) return hit;
    }
    else if (el->content && !uwp_is_expander( el ))
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = (ch && ch != el) ? uwp_hit_test_expander_header( ch, x, y ) : NULL;
        if (hit) return hit;
    }
    if (el->has_bounds && el->is_enabled && uwp_is_expander( el ))
    {
        float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        int hdr_h = (int)(64 * sc + 0.5f);
        struct uwp_element *he = el->pane ? uwp_elem_from_any( el->pane ) : NULL;
        if (he && he != el && he->has_bounds && he->bounds.bottom + (int)(10 * sc) - el->bounds.top > hdr_h)
            hdr_h = he->bounds.bottom + (int)(10 * sc) - el->bounds.top;
        if (hdr_h > el->bounds.bottom - el->bounds.top) hdr_h = el->bounds.bottom - el->bounds.top;
        if (x >= el->bounds.left && x < el->bounds.right &&
            y >= el->bounds.top && y < el->bounds.top + hdr_h)
            return el;
    }
    return NULL;
}

static struct uwp_element *uwp_hit_test_combobox( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0 || !el->is_hit_test_visible) return NULL;
    if (el->has_bounds && el->kind != UWP_KIND_POPUP &&
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
    if (el->template_root && el->template_root != el &&
        !uwp_is_combobox( el ) && !uwp_is_expander( el ) && !uwp_is_nav_view( el ) &&
        !uwp_is_tabview( el ) && !uwp_is_toggle_switch( el ))
    {
        struct uwp_element *hit = uwp_hit_test_combobox( el->template_root, x, y );
        if (hit) return hit;
    }
    else if (el->content)
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

static struct uwp_element *uwp_get_open_overlay_nav( void )
{
    if (g_open_overlay_nav && g_open_overlay_nav->visibility == 0 &&
        uwp_nav_is_overlay_open( g_open_overlay_nav ))
        return g_open_overlay_nav;
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
    if (el->has_bounds && uwp_is_scroll_viewer( el ) &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        el->scroll_extent_h > (el->bounds.bottom - el->bounds.top))
        return el;
    return NULL;
}

BOOL uwp_hit_test_interactive_client( IInspectable *root_content, int x, int y )
{
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    struct uwp_element *pops[16];
    if (g_popup_owner_el) return TRUE;
    if (uwp_get_open_popups( pops, ARRAY_SIZE(pops) ) > 0) return TRUE;
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

    if (g_popup_owner_el && !(g_popup_hwnd && IsWindow( g_popup_hwnd )))
    {
        static BOOL g_flyout_down = FALSE;
        struct uwp_element *owner = g_popup_owner_el;
        struct uwp_element *flyout = owner->flyout ? uwp_elem_from_any( owner->flyout ) : owner;
        struct uwp_vec_view *children = (flyout && flyout->children_coll)
            ? CONTAINING_RECORD( flyout->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        BOOL in_popup = (flyout && flyout->has_bounds &&
                         x >= flyout->bounds.left && x < flyout->bounds.right &&
                         y >= flyout->bounds.top && y < flyout->bounds.bottom);

        if (msg == WM_MOUSEMOVE && children)
        {
            BOOL changed = FALSE;
            unsigned int i;
            for (i = 0; i < children->count; i++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[i] );
                BOOL hov;
                if (!ch || ch->visibility != 0 || uwp_is_separator( ch )) continue;
                hov = (ch->has_bounds && x >= ch->bounds.left && x < ch->bounds.right &&
                       y >= ch->bounds.top && y < ch->bounds.bottom);
                if (ch->is_hovered != hov) { ch->is_hovered = hov; changed = TRUE; }
            }
            if (changed)
            {
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
            }
            return in_popup;
        }
        if (msg == WM_LBUTTONDOWN)
        {
            if (in_popup)
                g_flyout_down = TRUE;
            else
            {
                g_flyout_down = FALSE;
                uwp_close_flyout_popup( TRUE );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            }
            return TRUE;
        }
        if (msg == WM_LBUTTONUP)
        {
            if (!g_flyout_down && g_pressed_btn)
                return FALSE;
            if (g_flyout_down)
            {
                g_flyout_down = FALSE;
                if (in_popup && children)
                {
                    int hit_idx = -1, i;
                    for (i = 0; i < (int)children->count; i++)
                    {
                        struct uwp_element *ch = uwp_elem_from_any( children->items[i] );
                        if (!ch || ch->visibility != 0 || !ch->is_enabled || uwp_is_separator( ch )) continue;
                        if (ch->has_bounds && x >= ch->bounds.left && x < ch->bounds.right &&
                            y >= ch->bounds.top && y < ch->bounds.bottom)
                        {
                            hit_idx = i;
                            break;
                        }
                    }
                    if (hit_idx >= 0)
                    {
                        struct uwp_element *clicked_el = uwp_elem_from_any( children->items[hit_idx] );
                        if (clicked_el->class_flags & UWP_CF_TOGGLE_MENU_ITEM)
                            uwp_toggle_button_set_checked( clicked_el, clicked_el->is_checked ? 0 : 1, TRUE );
                        else if (clicked_el->class_flags & UWP_CF_RADIO_MENU_ITEM)
                        {
                            int j;
                            for (j = hit_idx - 1; j >= 0; j--)
                            {
                                struct uwp_element *sib = uwp_elem_from_any( children->items[j] );
                                if (!sib) continue;
                                if (uwp_is_separator( sib )) break;
                                if (sib->class_flags & UWP_CF_RADIO_MENU_ITEM)
                                    uwp_toggle_button_set_checked( sib, 0, FALSE );
                            }
                            for (j = hit_idx + 1; j < (int)children->count; j++)
                            {
                                struct uwp_element *sib = uwp_elem_from_any( children->items[j] );
                                if (!sib) continue;
                                if (uwp_is_separator( sib )) break;
                                if (sib->class_flags & UWP_CF_RADIO_MENU_ITEM)
                                    uwp_toggle_button_set_checked( sib, 0, FALSE );
                            }
                            uwp_toggle_button_set_checked( clicked_el, 1, TRUE );
                        }
                        uwp_close_flyout_popup( TRUE );
                        uwp_invoke_button( clicked_el );
                        if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                        return TRUE;
                    }
                }
                uwp_close_flyout_popup( TRUE );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            }
            return TRUE;
        }
    }

    {
        struct uwp_element *pops[16];
        int pcnt = uwp_get_open_popups( pops, ARRAY_SIZE(pops) );
        if (pcnt > 0)
        {
            static struct uwp_element *g_popup_close_down = NULL;
            struct uwp_element *pop = pops[pcnt - 1];
            struct uwp_element *ch = uwp_elem_from_any( pop->content );
            BOOL in_popup = (pop->has_bounds &&
                             x >= pop->bounds.left && x < pop->bounds.right &&
                             y >= pop->bounds.top && y < pop->bounds.bottom);

            if (msg == WM_MOUSEWHEEL && ch)
            {
                struct uwp_element *sv = uwp_find_scroll_viewer_at( ch, x, y, 0 );
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
                    }
                }
                return TRUE;
            }
            if (msg == WM_MOUSEMOVE && ch)
            {
                struct uwp_element *hit = in_popup ? uwp_hit_test_button( ch, x, y ) : NULL;
                if (hit != g_hovered_elem)
                {
                    if (g_hovered_elem) g_hovered_elem->is_hovered = FALSE;
                    g_hovered_elem = hit;
                    if (hit) hit->is_hovered = TRUE;
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                }
                return TRUE;
            }
            if (msg == WM_LBUTTONDOWN)
            {
                if (!in_popup)
                {
                    g_popup_close_down = NULL;
                    uwp_popup_set_open( pop, FALSE );
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    return TRUE;
                }
                if (ch)
                {
                    struct uwp_element *btn = uwp_hit_test_button( ch, x, y );
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
                        if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                        uwp_invalidate_window();
                        return TRUE;
                    }
                }
                if (x >= pop->bounds.right - 44 && y <= pop->bounds.top + 44)
                    g_popup_close_down = pop;
                return TRUE;
            }
            if (msg == WM_LBUTTONUP)
            {
                if (g_pressed_btn && ch)
                {
                    struct uwp_element *btn = g_pressed_btn;
                    struct uwp_element *hit = uwp_hit_test_button( ch, x, y );
                    btn->is_pressed = FALSE;
                    g_pressed_btn = NULL;
                    if (hit == btn)
                    {
                        struct xaml_pointer_info info = {0};
                        info.x = (float)x;
                        info.y = (float)y;
                        info.pointer_id = 1;
                        info.is_left = TRUE;
                        info.is_in_contact = FALSE;
                        info.is_in_window = TRUE;
                        uwp_fire_element_pointer( btn, XAML_PTR_RELEASED, &info, TRUE );
                        uwp_invoke_button( btn );
                        if (!wcscmp( btn->name, L"CloseButton" ) && pop->is_open)
                            uwp_popup_set_open( pop, FALSE );
                    }
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                    return TRUE;
                }
                if (g_popup_close_down == pop)
                {
                    g_popup_close_down = NULL;
                    if (x >= pop->bounds.right - 44 && x < pop->bounds.right &&
                        y >= pop->bounds.top && y <= pop->bounds.top + 44)
                    {
                        uwp_popup_set_open( pop, FALSE );
                        if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    }
                    return TRUE;
                }
                return TRUE;
            }
        }
    }

    if (g_open_cdp && g_open_cdp->is_dropdown_open)
    {
        struct uwp_element *cdp = g_open_cdp;
        RECT prc;
        BOOL has_rect = uwp_cdp_compute_popup_rect( cdp, width, height, &prc );
        BOOL in_popup = has_rect && x >= prc.left && x < prc.right && y >= prc.top && y < prc.bottom;
        if (msg == WM_LBUTTONDOWN)
        {
            if (!in_popup)
            {
                uwp_cdp_set_open( cdp, FALSE );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                /* Fall through so clicking another picker or button still works */
            }
            else
                return TRUE;
        }
        else if (msg == WM_LBUTTONUP && in_popup)
        {
            struct uwp_nav_ext *ne = uwp_elem_ensure_nav( cdp );
            float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
            int hdr_h = (int)(36 * sc + 0.5f);
            int cell_w = (prc.right - prc.left - (int)(16 * sc)) / 7;
            int cell_h = (int)(34 * sc + 0.5f);
            int grid_top = prc.top + hdr_h + (int)(30 * sc);
            if (ne)
            {
                int vy = (ne->cdp_view_year > 0) ? ne->cdp_view_year : 2026;
                int vm = (ne->cdp_view_month >= 1 && ne->cdp_view_month <= 12) ? ne->cdp_view_month : 11;
                if (y < prc.top + hdr_h)
                {
                    if (x >= prc.right - (int)(64 * sc) && x < prc.right - (int)(34 * sc))
                    {
                        if (--vm < 1) { vm = 12; vy--; }
                        ne->cdp_view_year = vy; ne->cdp_view_month = vm;
                    }
                    else if (x >= prc.right - (int)(34 * sc))
                    {
                        if (++vm > 12) { vm = 1; vy++; }
                        ne->cdp_view_year = vy; ne->cdp_view_month = vm;
                    }
                }
                else if (y >= grid_top && cell_w > 0 && cell_h > 0)
                {
                    int col = (x - (prc.left + (int)(8 * sc))) / cell_w;
                    int row = (y - grid_top) / cell_h;
                    if (col >= 0 && col < 7 && row >= 0 && row < 6)
                    {
                        int fdow = uwp_cdp_first_dow( vy, vm );
                        int dim = uwp_cdp_days_in_month( vy, vm );
                        int day = row * 7 + col - fdow + 1;
                        if (day >= 1 && day <= dim)
                        {
                            INT64 ticks = uwp_cdp_ymd_to_ticks( vy, vm, day );
                            uwp_cdp_set_date( cdp, ticks, TRUE, TRUE );
                            uwp_cdp_set_open( cdp, FALSE );
                        }
                    }
                }
            }
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
    }

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

    if (uwp_is_autosuggest( g_focused_textbox ) &&
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
                    uwp_invalidate_paint_only();
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
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
                            uwp_elem_set_text( asb, prim );
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
        struct uwp_element *nav = uwp_get_open_overlay_nav();
        if (nav && nav->nav && nav->nav->nav_pane_bounds.right > nav->nav->nav_pane_bounds.left)
        {
            RECT prc = nav->nav->nav_pane_bounds;
            BOOL in_pane = (x >= prc.left && x < prc.right && y >= prc.top && y < prc.bottom);
            if (msg == WM_MOUSEWHEEL && in_pane)
            {
                int ns = nav->nav->nav_scroll_y + ((wheel_delta > 0) ? -40 : 40);
                if (ns < 0) ns = 0;
                nav->nav->nav_scroll_y = ns;
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
        (g_layout_dirty || !root_el->has_bounds ||
         (root_el->bounds.right - root_el->bounds.left) != width ||
         (root_el->bounds.bottom - root_el->bounds.top) != height))
    {
        RECT rc = { 0, 0, width, height };
        g_layout_dirty = FALSE;
        uwp_arrange_element( root_el, rc );
    }

    if (msg == WM_LBUTTONDOWN)
    {
        struct uwp_element *ov_nav = uwp_get_open_overlay_nav();
        if (ov_nav && ov_nav->nav && ov_nav->nav->nav_pane_bounds.right > ov_nav->nav->nav_pane_bounds.left)
        {
            RECT prc = ov_nav->nav->nav_pane_bounds;
            if (x >= prc.left && x < prc.right && y >= prc.top && y < prc.bottom)
            {
                struct uwp_element *pane_btn = uwp_hit_test_button( ov_nav, x, y );
                if (pane_btn && !wcscmp( pane_btn->name, L"TogglePaneButton" ))
                {
                    struct xaml_pointer_info info = {0};
                    g_pressed_btn = pane_btn;
                    pane_btn->is_pressed = TRUE;
                    info.x = (float)x; info.y = (float)y; info.pointer_id = 1;
                    info.is_left = TRUE; info.is_in_contact = TRUE; info.is_in_window = TRUE;
                    uwp_fire_element_pointer( pane_btn, XAML_PTR_PRESSED, &info, TRUE );
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                }
                return TRUE;
            }
        }
        {
        struct uwp_element *tv = uwp_hit_test_tabview( root_el, x, y );
        struct uwp_element *cb = tv ? NULL : uwp_hit_test_combobox( root_el, x, y );
        struct uwp_element *cdp = (tv || cb) ? NULL : uwp_hit_test_cdp( root_el, x, y );
        struct uwp_element *tb = (tv || cb || cdp) ? NULL : uwp_hit_test_textbox( root_el, x, y );
        struct uwp_element *btn = (tv || cb || cdp || tb) ? NULL : uwp_hit_test_button( root_el, x, y );
        struct uwp_element *exp = (tv || cb || cdp || tb || btn) ? NULL : uwp_hit_test_expander_header( root_el, x, y );
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
        if (cdp)
        {
            g_pressed_cdp = cdp;
            return TRUE;
        }
        if (exp)
        {
            g_pressed_expander = exp;
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
        if (uwp_is_menubar_item( btn ))
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
                   x, y, debugstr_w(btn->name), debugstr_w(uwp_elem_text( btn )) );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        }
    }
    else if (msg == WM_MOUSEMOVE)
    {
        static struct uwp_element *s_hovered_tv = NULL;
        struct uwp_element *tv = uwp_hit_test_tabview( root_el, x, y );
        struct uwp_element *hit = tv ? NULL : uwp_hit_test_button( root_el, x, y );
        BOOL tv_changed = FALSE;
        if (s_hovered_tv && s_hovered_tv != tv && uwp_elem_from_any( &s_hovered_tv->IInspectable_iface ) == s_hovered_tv && s_hovered_tv->magic == UWP_ELEM_MAGIC)
        {
            if (s_hovered_tv->nav && (s_hovered_tv->nav->tab_hovered_idx != -1 || s_hovered_tv->nav->tab_hovered_close))
            {
                s_hovered_tv->nav->tab_hovered_idx = -1;
                s_hovered_tv->nav->tab_hovered_close = FALSE;
                tv_changed = TRUE;
            }
        }
        s_hovered_tv = tv;
        if (tv)
        {
            struct uwp_nav_ext *nav = uwp_elem_ensure_nav( tv );
            if (nav)
            {
                IInspectable *items[64];
                int count = uwp_tabview_get_items( tv, items, ARRAY_SIZE(items) );
                int n = max( 1, count ), i;
                int new_idx = -1;
                BOOL new_close = FALSE;
                POINT pt = { x, y };
                RECT pill, close_rc, add_rc;
                uwp_tabview_compute_rects( &tv->bounds, n, 0, &pill, &close_rc, &add_rc );
                if (PtInRect( &add_rc, pt ))
                {
                    new_idx = -2;
                }
                else
                {
                    for (i = 0; i < count; i++)
                    {
                        uwp_tabview_compute_rects( &tv->bounds, n, i, &pill, &close_rc, &add_rc );
                        if (PtInRect( &pill, pt ))
                        {
                            new_idx = i;
                            new_close = PtInRect( &close_rc, pt );
                            break;
                        }
                    }
                }
                for (i = 0; i < count; i++) IInspectable_Release( items[i] );
                if (nav->tab_hovered_idx != new_idx || nav->tab_hovered_close != new_close)
                {
                    nav->tab_hovered_idx = new_idx;
                    nav->tab_hovered_close = new_close;
                    tv_changed = TRUE;
                }
            }
        }
        if (hit != g_hovered_elem || tv_changed)
        {
            if (g_hovered_elem) g_hovered_elem->is_hovered = FALSE;
            g_hovered_elem = hit;
            if (hit) hit->is_hovered = TRUE;
            if (uwp_is_menubar_item( hit ) &&
                uwp_is_menubar_item( g_popup_owner_el ) &&
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
            uwp_invalidate_paint_only();
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
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
        if (g_pressed_cdp)
        {
            struct uwp_element *cdp = g_pressed_cdp;
            struct uwp_element *hit = uwp_hit_test_cdp( root_el, x, y );
            g_pressed_cdp = NULL;
            if (hit == cdp)
            {
                uwp_cdp_set_open( cdp, !cdp->is_dropdown_open );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            }
            return TRUE;
        }
        if (g_pressed_expander)
        {
            struct uwp_element *exp = g_pressed_expander;
            struct uwp_element *hit = uwp_hit_test_expander_header( root_el, x, y );
            g_pressed_expander = NULL;
            if (hit == exp)
            {
                BOOL next_open = !exp->is_open;
                int di;
                exp->is_open = next_open;
                for (di = 0; di < exp->dp_cnt; di++)
                {
                    if (!wcscmp( xaml_dp_get_name( exp->dp_store[di].dp ), L"IsExpanded" ))
                    {
                        IInspectable *bv = box_bool_value( (boolean)next_open );
                        if (bv)
                        {
                            uwp_do_SetValue( &exp->IDependencyObject_iface, exp->dp_store[di].dp, bv );
                            IInspectable_Release( bv );
                        }
                        break;
                    }
                }
                g_layout_dirty = TRUE;
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
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
                BOOL prev_open = uwp_elem_nav_is_pane_open( nav );
                struct xaml_pointer_info info = {0};
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = FALSE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( btn, XAML_PTR_RELEASED, &info, TRUE );
                uwp_invoke_button( btn );
                if (nav && uwp_elem_nav_is_pane_open( nav ) == prev_open)
                    uwp_nav_set_pane_open( nav, !prev_open );
            }
            else
                uwp_fire_tapped_bubble( btn, x, y );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        {
            struct uwp_element *nav = uwp_get_open_overlay_nav();
            if (nav)
            {
                struct uwp_element *nav_item = uwp_hit_test_nav_item( nav, x, y );
                if (nav_item)
                {
                    IInspectable *item_obj = nav_item->content ? nav_item->content
                        : (nav_item->outer ? nav_item->outer : &nav_item->IInspectable_iface);
                    uwp_nav_select_item( nav, nav_item );
                    if (UWP_EV_ANY( nav, nav_sel_changed_reg ))
                        uwp_nav_fire( nav, &nav->events->nav_sel_changed_reg, item_obj );
                    if (UWP_EV_ANY( nav, nav_item_invoked_reg ))
                        uwp_nav_fire( nav, &nav->events->nav_item_invoked_reg, item_obj );
                    uwp_nav_set_pane_open( nav, FALSE );
                    if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                    uwp_invalidate_window();
                    return TRUE;
                }
            }
        }
        {
            struct uwp_element *hit_el = uwp_hit_test_element( root_el, x, y );
            if (hit_el && uwp_is_pivot_item( hit_el ) && hit_el->parent && uwp_is_pivot( hit_el->parent ) &&
                hit_el->parent->children_coll)
            {
                struct uwp_element *pivot = hit_el->parent;
                struct uwp_vec_view *children = CONTAINING_RECORD( pivot->children_coll, struct uwp_vec_view, IInspectable_iface );
                unsigned int k;
                for (k = 0; k < children->count; k++)
                {
                    if (uwp_elem_from_any( children->items[k] ) == hit_el)
                    {
                        uwp_selector_set_selection( pivot, (INT32)k, children->items[k], TRUE, TRUE );
                        g_layout_dirty = TRUE;
                        break;
                    }
                }
                uwp_fire_tapped_bubble( hit_el, x, y );
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                return TRUE;
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
        !UWP_EV_ANY( el, tapped_reg ) &&
        !UWP_EV_ANY( el, ptr_pressed_reg ) &&
        !UWP_EV_ANY( el, ptr_released_reg ))
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
          debugstr_w(target->class_name), debugstr_w(target->name), debugstr_w(uwp_elem_text( target )), x, y );
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
                    const struct abi_ctrl_overrides *aco = (const struct abi_ctrl_overrides *)ctrl_ovr;
                    if (aco->lpVtbl->OnTapped)
                    {
                        WARN( "uwp_fire_tapped_bubble: IControlOverrides::OnTapped on %s (%s) txt=%s\n",
                              debugstr_w(cur->class_name), debugstr_w(cur->name), debugstr_w(uwp_elem_text( cur )) );
                        __TRY { aco->lpVtbl->OnTapped( ctrl_ovr, &args->ITappedRoutedEventArgs_iface ); fired = TRUE; }
                        __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
                        __ENDTRY;
                    }
                }
                IUnknown_Release( (IUnknown *)ctrl_ovr );
            }
        }
        if (UWP_EV_ANY( cur, tapped_reg ))
        {
            count = xaml_event_registry_snapshot_alloc( &cur->events->tapped_reg, &handlers );
            for (i = 0; i < count; i++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
                HRESULT thr = S_OK;
                WARN( "uwp_fire_tapped_bubble: invoking tapped_reg[%u/%u] on %s (%s) txt=%s handled=%d\n",
                      i, count, debugstr_w(cur->class_name), debugstr_w(cur->name), debugstr_w(uwp_elem_text( cur )), args->handled );
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

static BOOL uwp_elem_has_ctrl_overrides( struct uwp_element *el )
{
    void *ctrl_ovr = NULL;
    if (!el || !el->outer) return FALSE;
    if (el->has_ctrl_overrides != 0)
        return el->has_ctrl_overrides > 0;
    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IControlOverrides_UWP, &ctrl_ovr )) && ctrl_ovr)
    {
        BOOL is_ext = !((char *)ctrl_ovr >= (char *)el && (char *)ctrl_ovr < (char *)(el + 1));
        IUnknown_Release( (IUnknown *)ctrl_ovr );
        el->has_ctrl_overrides = is_ext ? 1 : -1;
        return is_ext;
    }
    el->has_ctrl_overrides = -1;
    return FALSE;
}

BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble )
{
    struct uwp_pointer_routed_args *args;
    struct uwp_element *cur;
    BOOL fired = FALSE, any_listener = FALSE;
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

    for (cur = target; cur; cur = bubble ? cur->parent : NULL)
    {
        struct xaml_event_registry *reg = uwp_elem_ptr_registry( cur, kind );
        if ((reg && reg->next_token > 0) ||
            (ovr_slot >= 0 && cur->outer && uwp_elem_has_ctrl_overrides( cur )))
        {
            any_listener = TRUE;
            break;
        }
    }
    if (!any_listener) return FALSE;

    args = uwp_pointer_routed_args_create( info, target->outer ? target->outer : &target->IInspectable_iface );
    if (!args) return FALSE;

    for (cur = target; cur && !args->handled; cur = bubble ? cur->parent : NULL)
    {
        struct xaml_event_registry *reg = uwp_elem_ptr_registry( cur, kind );
        void **handlers = NULL;
        unsigned int i, count = (reg && reg->next_token > 0) ? xaml_event_registry_snapshot_alloc( reg, &handlers ) : 0;
        IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;

        if (ovr_slot >= 0 && cur->outer && uwp_elem_has_ctrl_overrides( cur ))
        {
            void *ctrl_ovr = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( cur->outer, &IID_IControlOverrides_UWP, &ctrl_ovr )) && ctrl_ovr)
            {
                if (!((char *)ctrl_ovr >= (char *)cur && (char *)ctrl_ovr < (char *)(cur + 1)))
                {
                    const struct abi_slot_iface *aco = (const struct abi_slot_iface *)ctrl_ovr;
                    HRESULT (WINAPI *on_ptr)(void *, void *) =
                        (ovr_slot >= 0 && ovr_slot < (int)ARRAY_SIZE(aco->lpVtbl->slots)) ? aco->lpVtbl->slots[ovr_slot] : NULL;
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

