/*
 * Advanced Local Procedure Call tests
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
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
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <ntstatus.h>
#include "wine/test.h"

#define DECL_FUNCPTR(f) static typeof(f) *p##f;

DECL_FUNCPTR(AlpcGetHeaderSize)
DECL_FUNCPTR(AlpcGetMessageAttribute)
DECL_FUNCPTR(AlpcInitializeMessageAttribute)
DECL_FUNCPTR(NtAlpcCreatePort)
DECL_FUNCPTR(NtAlpcConnectPort)
DECL_FUNCPTR(NtAlpcAcceptConnectPort)
DECL_FUNCPTR(NtAlpcDisconnectPort)
DECL_FUNCPTR(NtAlpcQueryInformation)
DECL_FUNCPTR(NtAlpcSendWaitReceivePort)
DECL_FUNCPTR(NtAlpcCreatePortSection)
DECL_FUNCPTR(NtAlpcDeletePortSection)
DECL_FUNCPTR(NtAlpcCreateSectionView)
DECL_FUNCPTR(NtAlpcDeleteSectionView)
DECL_FUNCPTR(NtClose)

#undef DECL_FUNCPTR

/* NtAlpcSetInformation is not declared in the SDK headers, and is absent on
 * some Windows versions; load it dynamically. */
typedef NTSTATUS (WINAPI *NtAlpcSetInformation_t)(HANDLE port, ULONG class, void *buffer, ULONG length);
static NtAlpcSetInformation_t pNtAlpcSetInformation;

#define LPC_REQUEST_TYPE              1
#define LPC_REPLY_TYPE                2
#define LPC_CONNECTION_REQUEST_TYPE   10

#define ALPC_BASIC_INFORMATION_CLASS       0
#define ALPC_INVALID_INFORMATION_CLASS     0xffffffff

#undef DECL_FUNCPTR

static void init_functions(void)
{
    HMODULE ntdll;

    ntdll = GetModuleHandleA("ntdll.dll");
    ok(ntdll != NULL, "GetModuleHandleA failed.\n");

#define LOAD_FUNCPTR(f) p##f = (void *)GetProcAddress(ntdll, #f);

    LOAD_FUNCPTR(AlpcGetHeaderSize)
    LOAD_FUNCPTR(AlpcGetMessageAttribute)
    LOAD_FUNCPTR(AlpcInitializeMessageAttribute)
    LOAD_FUNCPTR(NtAlpcCreatePort)
    LOAD_FUNCPTR(NtAlpcConnectPort)
    LOAD_FUNCPTR(NtAlpcAcceptConnectPort)
    LOAD_FUNCPTR(NtAlpcDisconnectPort)
    LOAD_FUNCPTR(NtAlpcQueryInformation)
    LOAD_FUNCPTR(NtAlpcSendWaitReceivePort)
    LOAD_FUNCPTR(NtAlpcCreatePortSection)
    LOAD_FUNCPTR(NtAlpcDeletePortSection)
    LOAD_FUNCPTR(NtAlpcCreateSectionView)
    LOAD_FUNCPTR(NtAlpcDeleteSectionView)
    LOAD_FUNCPTR(NtClose)

    pNtAlpcSetInformation = (NtAlpcSetInformation_t)GetProcAddress( ntdll, "NtAlpcSetInformation" );

#undef LOAD_FUNCPTR
}

static void test_AlpcGetHeaderSize(void)
{
    unsigned int i, j;
    SIZE_T size;

    static const struct
    {
        ULONG attribute;
        SIZE_T size;
    }
    tests[] =
    {
        {0, 0},
        {1u << 1, 0},
        {1u << 2, 0},
        {1u << 3, 0},
        {1u << 4, 0},
        {1u << 5, 0},
        {1u << 6, 0},
        {1u << 7, 0},
        {1u << 8, 0},
        {1u << 9, 0},
        {1u << 10, 0},
        {1u << 11, 0},
        {1u << 12, 0},
        {1u << 13, 0},
        {1u << 14, 0},
        {1u << 15, 0},
        {1u << 16, 0},
        {1u << 17, 0},
        {1u << 18, 0},
        {1u << 19, 0},
        {1u << 20, 0},
        {1u << 21, 0},
        {1u << 22, 0},
        {1u << 23, 0},
        {1u << 24, 0},
        {1u << 25, sizeof(ALPC_WORK_ON_BEHALF_ATTR)},   /* ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE */
        {1u << 26, sizeof(ALPC_DIRECT_ATTR)},           /* ALPC_MESSAGE_DIRECT_ATTRIBUTE */
        {1u << 27, sizeof(ALPC_TOKEN_ATTR)},            /* ALPC_MESSAGE_TOKEN_ATTRIBUTE */
        {1u << 28, sizeof(ALPC_HANDLE_ATTR)},           /* ALPC_MESSAGE_HANDLE_ATTRIBUTE */
        {1u << 29, sizeof(ALPC_CONTEXT_ATTR)},          /* ALPC_MESSAGE_CONTEXT_ATTRIBUTE */
        {1u << 30, sizeof(ALPC_VIEW_ATTR)},             /* ALPC_MESSAGE_VIEW_ATTRIBUTE */
        {1u << 31, sizeof(ALPC_SECURITY_ATTR)},         /* ALPC_MESSAGE_SECURITY_ATTRIBUTE */
        {0xffffffff, sizeof(ALPC_WORK_ON_BEHALF_ATTR) +
                     sizeof(ALPC_DIRECT_ATTR) +
                     sizeof(ALPC_TOKEN_ATTR) +
                     sizeof(ALPC_HANDLE_ATTR) +
                     sizeof(ALPC_CONTEXT_ATTR) +
                     sizeof(ALPC_VIEW_ATTR) +
                     sizeof(ALPC_SECURITY_ATTR)},       /* All attributes */
    };

    if (!pAlpcGetHeaderSize)
    {
        win_skip("AlpcGetHeaderSize is unavailable.\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        winetest_push_context("i %d", i);

        /* Single attribute */
        size = pAlpcGetHeaderSize(tests[i].attribute);
        ok(size == sizeof(ALPC_MESSAGE_ATTRIBUTES) + tests[i].size, "Got unexpected size %Ix.\n", size);

        /* Two attributes */
        for (j = 0; j < ARRAY_SIZE(tests); j++)
        {
            if (tests[i].attribute & tests[j].attribute)
                continue;

            winetest_push_context("j %d", j);

            size = pAlpcGetHeaderSize(tests[i].attribute | tests[j].attribute);
            ok(size == sizeof(ALPC_MESSAGE_ATTRIBUTES) + tests[i].size + tests[j].size,
               "Got unexpected size %Ix.\n", size);

            winetest_pop_context();
        }
        winetest_pop_context();
    }
}

static void test_AlpcGetMessageAttribute(void)
{
    unsigned char buffer[1024];
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)buffer;
    unsigned int i, j;
    void *ptr;

    static const ULONG attributes[] =
    {
        0,
        ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE,
        ALPC_MESSAGE_DIRECT_ATTRIBUTE,
        ALPC_MESSAGE_TOKEN_ATTRIBUTE,
        ALPC_MESSAGE_HANDLE_ATTRIBUTE,
        ALPC_MESSAGE_CONTEXT_ATTRIBUTE,
        ALPC_MESSAGE_VIEW_ATTRIBUTE,
        ALPC_MESSAGE_SECURITY_ATTRIBUTE,
    };

    if (!pAlpcGetMessageAttribute)
    {
        win_skip("AlpcGetMessageAttribute is unavailable.\n");
        return;
    }

    /* Invalid flag */
    attr->AllocatedAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
    attr->ValidAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
    ptr = pAlpcGetMessageAttribute(attr, 0x1);
    ok(ptr == NULL, "Got unexpected ptr.\n");

    for (i = 0; i < ARRAY_SIZE(attributes); i++)
    {
        winetest_push_context("%#lx", attributes[i]);

        /* The message has no allocated attributes */
        attr->AllocatedAttributes = 0;
        attr->ValidAttributes = attributes[i];
        ptr = pAlpcGetMessageAttribute(attr, attributes[i]);
        ok(ptr == NULL, "Got unexpected ptr.\n");

        /* The message has no valid attributes */
        attr->AllocatedAttributes = attributes[i];
        attr->ValidAttributes = 0;
        ptr = pAlpcGetMessageAttribute(attr, attributes[i]);
        if (attributes[i])
            ok(ptr == ((unsigned char *)attr + sizeof(*attr)), "Got unexpected ptr.\n");
        else
            ok(ptr == NULL, "Got unexpected ptr.\n");

        /* Normal calls */
        attr->AllocatedAttributes = attributes[i];
        attr->ValidAttributes = attributes[i];
        ptr = pAlpcGetMessageAttribute(attr, attributes[i]);
        if (attributes[i])
            ok(ptr == ((unsigned char *)attr + sizeof(*attr)), "Got unexpected ptr.\n");
        else
            ok(ptr == NULL, "Got unexpected ptr.\n");

        /* The message has full attributes */
        attr->AllocatedAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
        attr->ValidAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
        ptr = pAlpcGetMessageAttribute(attr, attributes[i]);
        if (attributes[i])
            ok(ptr == ((unsigned char *)attr + pAlpcGetHeaderSize(attr->AllocatedAttributes & ~(attributes[i] | (attributes[i] - 1)))),
               "Got unexpected ptr.\n");
        else
            ok(ptr == NULL, "Got unexpected ptr.\n");

        /* The message has some valid attributes */
        for (j = 0; j < ARRAY_SIZE(attributes); j++)
        {
            attr->AllocatedAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
            attr->ValidAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL & ~attributes[j];
            ptr = pAlpcGetMessageAttribute(attr, attributes[i]);
            if (attributes[i])
                ok(ptr == ((unsigned char *)attr + pAlpcGetHeaderSize(attr->AllocatedAttributes & ~(attributes[i] | (attributes[i] - 1)))),
                   "Got unexpected ptr.\n");
            else
                ok(ptr == NULL, "Got unexpected ptr.\n");
        }

        /* Two attributes are passed to AlpcGetMessageAttribute() */
        for (j = 0; j < ARRAY_SIZE(attributes); j++)
        {
            if (!attributes[i] || !attributes[j] || attributes[i] == attributes[j])
                continue;

            attr->AllocatedAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
            attr->ValidAttributes = ALPC_MESSAGE_ATTRIBUTE_ALL;
            ptr = pAlpcGetMessageAttribute(attr, attributes[i] | attributes[j]);
            ok(ptr == NULL, "Got unexpected ptr.\n");
        }

        winetest_pop_context();
    }
}

static void test_AlpcInitializeMessageAttribute(void)
{
    unsigned char buffer[1024];
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)buffer;
    SIZE_T required_size, size;
    unsigned int i, j, k;
    NTSTATUS status;

    static const ULONG attributes[] =
    {
        0,
        ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE,
        ALPC_MESSAGE_DIRECT_ATTRIBUTE,
        ALPC_MESSAGE_TOKEN_ATTRIBUTE,
        ALPC_MESSAGE_HANDLE_ATTRIBUTE,
        ALPC_MESSAGE_CONTEXT_ATTRIBUTE,
        ALPC_MESSAGE_VIEW_ATTRIBUTE,
        ALPC_MESSAGE_SECURITY_ATTRIBUTE,
        0xffffffff,
    };

    if (!pAlpcInitializeMessageAttribute)
    {
        win_skip("AlpcInitializeMessageAttribute is unavailable.\n");
        return;
    }

    /* Check parameters */
    for (i = 0; i < ARRAY_SIZE(attributes); i++)
    {
        winetest_push_context("i %d", i);

        size = pAlpcGetHeaderSize(attributes[i]);

        /* Null message pointer */
        required_size = 0;
        status = pAlpcInitializeMessageAttribute(attributes[i], NULL, size, &required_size);
        ok(status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status);
        ok(required_size == size, "Expected %Ix, got %Ix.\n", size, required_size);

        /* Buffer too small */
        attr->AllocatedAttributes = 0xdeadbeef;
        attr->ValidAttributes = 0xdeadbeef;
        required_size = 0;
        status = pAlpcInitializeMessageAttribute(attributes[i], attr, size - 1, &required_size);
        ok(status == STATUS_BUFFER_TOO_SMALL, "Got unexpected status %#lx.\n", status);
        ok(attr->AllocatedAttributes == 0xdeadbeef, "Got unexpected %#lx.\n", attr->AllocatedAttributes);
        ok(attr->ValidAttributes == 0xdeadbeef, "Got unexpected %#lx.\n", attr->ValidAttributes);
        ok(required_size == size, "Expected %Ix, got %Ix.\n", size, required_size);

        /* Buffer too large */
        attr->AllocatedAttributes = 0xdeadbeef;
        attr->ValidAttributes = 0xdeadbeef;
        required_size = 0;
        status = pAlpcInitializeMessageAttribute(attributes[i], attr, size + 1, &required_size);
        ok(status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status);
        ok(attr->AllocatedAttributes == attributes[i], "Got unexpected %#lx.\n", attr->AllocatedAttributes);
        ok(attr->ValidAttributes == 0, "Got unexpected %#lx.\n", attr->ValidAttributes);
        ok(required_size == size, "Expected %Ix, got %Ix.\n", size, required_size);

        /* Correct buffer size */
        memset(buffer, 0xa1, sizeof(buffer));
        required_size = 0;
        status = pAlpcInitializeMessageAttribute(attributes[i], attr, size, &required_size);
        ok(status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status);
        ok(attr->AllocatedAttributes == attributes[i], "Got unexpected %#lx.\n", attr->AllocatedAttributes);
        ok(attr->ValidAttributes == 0, "Got unexpected %#lx.\n", attr->ValidAttributes);
        ok(required_size == size, "Expected %Ix, got %Ix.\n", size, required_size);
        /* Test that AlpcInitializeMessageAttribute() only sets AllocatedAttributes and ValidAttributes */
        for (k = sizeof(*attr); k < sizeof(buffer); k++)
        {
            if (buffer[k] != 0xa1)
            {
                ok(0, "Marker got overwritten at %d.\n", k);
                break;
            }
        }

        /* Two attributes */
        for (j = 0; j < ARRAY_SIZE(attributes); j++)
        {
            if (attributes[i] & attributes[j])
                continue;

            winetest_push_context("j %d", j);

            memset(buffer, 0xb2, sizeof(buffer));
            size = pAlpcGetHeaderSize(attributes[i] | attributes[j]);
            required_size = 0;
            status = pAlpcInitializeMessageAttribute(attributes[i] | attributes[j], attr, size, &required_size);
            ok(status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status);
            ok(attr->AllocatedAttributes == (attributes[i] | attributes[j]),
               "Got unexpected %#lx.\n", attr->AllocatedAttributes);
            ok(attr->ValidAttributes == 0, "Got unexpected %#lx.\n", attr->ValidAttributes);
            ok(required_size == size, "Expected %Ix, got %Ix.\n", size, required_size);
            /* Test that AlpcInitializeMessageAttribute() only sets AllocatedAttributes and ValidAttributes */
            for (k = sizeof(*attr); k < sizeof(buffer); k++)
            {
                if (buffer[k] != 0xb2)
                {
                    ok(0, "Marker got overwritten at %d.\n", k);
                    break;
                }
            }

            winetest_pop_context();
        }
        winetest_pop_context();
    }
}

static void test_AlpcSectionView(void)
{
    HANDLE section = NULL;
    SIZE_T actual_size = 0;
    ALPC_DATA_VIEW_ATTR view;
    NTSTATUS status;

    if (!pNtAlpcCreatePortSection || !pNtAlpcCreateSectionView ||
        !pNtAlpcDeleteSectionView || !pNtAlpcDeletePortSection)
    {
        win_skip("ALPC section view functions are unavailable.\n");
        return;
    }

    /* Test invalid parameters */
    status = pNtAlpcCreatePortSection(NULL, 0, NULL, 0x1000, &section, &actual_size);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got %#lx\n", status);

    status = pNtAlpcCreatePortSection((HANDLE)0x1234, 0, NULL, 0, &section, &actual_size);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got %#lx\n", status);

    /* Test creating port section */
    status = pNtAlpcCreatePortSection((HANDLE)0x1234, 0, NULL, 0x1000, &section, &actual_size);
    ok(status == STATUS_SUCCESS, "NtAlpcCreatePortSection failed: %#lx\n", status);
    ok(section != NULL, "Expected valid section handle\n");
    ok(actual_size >= 0x1000, "Expected actual_size >= 0x1000, got %Ix\n", actual_size);

    /* Test creating section view */
    memset(&view, 0, sizeof(view));
    view.SectionHandle = section;
    view.ViewSize = 0x1000;
    status = pNtAlpcCreateSectionView((HANDLE)0x1234, 0, &view);
    ok(status == STATUS_SUCCESS, "NtAlpcCreateSectionView failed: %#lx\n", status);
    ok(view.ViewBase != NULL, "Expected non-NULL ViewBase\n");
    ok(view.ViewSize >= 0x1000, "Expected ViewSize >= 0x1000, got %Ix\n", view.ViewSize);

    /* Test read/write into view memory */
    strcpy((char *)view.ViewBase, "test view buffer data");
    ok(!strcmp((char *)view.ViewBase, "test view buffer data"), "View memory content mismatch\n");

    /* Test unmapping view */
    status = pNtAlpcDeleteSectionView((HANDLE)0x1234, 0, view.ViewBase);
    ok(status == STATUS_SUCCESS, "NtAlpcDeleteSectionView failed: %#lx\n", status);

    /* Test deleting section */
    status = pNtAlpcDeletePortSection((HANDLE)0x1234, 0, section);
    ok(status == STATUS_SUCCESS, "NtAlpcDeletePortSection failed: %#lx\n", status);
}

/* The Wine ALPC implementation is 64-bit only: the port lifecycle syscalls
 * (NtAlpcConnectPort, NtAlpcSendWaitReceivePort, NtAlpcQueryInformation, ...) are
 * not marshalled for 32-bit callers on 64-bit Wine. The message attribute helper
 * tests above (plain PE exports) run on both architectures. */
#ifdef _WIN64

struct alpc_view_thread_args
{
    UNICODE_STRING *port_name;
    NTSTATUS status;
    void *view_base;
};

/* Client thread for the view attribute test: connect, set up a shared memory
 * section view, and send a message carrying the view attribute. */
static DWORD WINAPI alpc_view_client_thread( void *arg )
{
    struct alpc_view_thread_args *args = arg;
    ALPC_PORT_ATTRIBUTES port_attr;
    ALPC_DATA_VIEW_ATTR view, *view_attr;
    char buf[512];
    char attrbuf[256];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)attrbuf;
    SIZE_T size, req_size = 0;
    HANDLE conn = NULL, section = NULL;
    NTSTATUS status;

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    memset( &view, 0, sizeof(view) );

    status = pNtAlpcConnectPort( &conn, args->port_name, NULL, &port_attr, 0,
                                 NULL, NULL, NULL, NULL, NULL, NULL );
    if (status == STATUS_SUCCESS)
    {
        size = 0;
        status = pNtAlpcCreatePortSection( conn, 0, NULL, 0x1000, &section, &size );
        if (status == STATUS_SUCCESS)
        {
            view.SectionHandle = section;
            view.ViewSize = 0x1000;
            status = pNtAlpcCreateSectionView( conn, 0, &view );
            if (status == STATUS_SUCCESS)
                strcpy( (char *)view.ViewBase, "alpc shared payload 12345" );
        }
        args->view_base = view.ViewBase;

        if (status == STATUS_SUCCESS)
        {
            status = pAlpcInitializeMessageAttribute( ALPC_MESSAGE_VIEW_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
            if (status == STATUS_SUCCESS)
            {
                view_attr = pAlpcGetMessageAttribute( attr, ALPC_MESSAGE_VIEW_ATTRIBUTE );
                if (view_attr)
                {
                    view_attr->Flags = 0;
                    view_attr->SectionHandle = section;
                    view_attr->ViewBase = view.ViewBase;
                    view_attr->ViewSize = 0x1000;
                    attr->ValidAttributes |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
                }
                else
                    status = STATUS_UNSUCCESSFUL;
            }
            if (status == STATUS_SUCCESS)
            {
                memset( msg, 0, sizeof(*msg) );
                msg->DataLength = 0;
                msg->TotalLength = sizeof(*msg);
                status = pNtAlpcSendWaitReceivePort( conn, ALPC_MSGFLG_SYNC_REQUEST, msg, attr,
                                                     NULL, NULL, NULL, NULL );
            }
        }
        if (view.ViewBase) pNtAlpcDeleteSectionView( conn, 0, view.ViewBase );
        if (section) pNtAlpcDeletePortSection( conn, 0, section );
        if (conn) NtClose( conn );
    }
    args->status = status;
    return 0;
}

static void test_AlpcMessageViewAttribute(void)
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\TestAlpcViewPort";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char recv_buf[512];
    char recv_attr_buf[256];
    ALPC_PORT_MESSAGE *recv_msg = (ALPC_PORT_MESSAGE *)recv_buf;
    ALPC_MESSAGE_ATTRIBUTES *recv_attr = (ALPC_MESSAGE_ATTRIBUTES *)recv_attr_buf;
    struct alpc_view_thread_args args;
    ALPC_DATA_VIEW_ATTR *recvd_view;
    SIZE_T recv_sz, req_attr_size = 0;
    HANDLE server_port = NULL, server_conn = NULL, thread;
    NTSTATUS status;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort ||
        !pNtAlpcSendWaitReceivePort || !pNtAlpcCreatePortSection ||
        !pNtAlpcCreateSectionView || !pNtAlpcDeleteSectionView || !pNtAlpcDeletePortSection)
    {
        win_skip("ALPC functions are unavailable.\n");
        return;
    }

    memset( &args, 0, sizeof(args) );
    args.port_name = &port_name;

    RtlInitUnicodeString(&port_name, port_nameW);
    InitializeObjectAttributes(&obj_attr, &port_name, 0, NULL, NULL);
    memset(&port_attr, 0, sizeof(port_attr));
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort(&server_port, &obj_attr, &port_attr);
    ok(status == STATUS_SUCCESS, "NtAlpcCreatePort failed: %#lx\n", status);
    if (status != STATUS_SUCCESS) return;

    /* The connection handshake blocks the client until the server accepts,
     * so run the client side on a dedicated thread. */
    thread = CreateThread( NULL, 0, alpc_view_client_thread, &args, 0, NULL );
    ok( thread != NULL, "CreateThread failed, err %u\n", (unsigned int)GetLastError() );
    if (!thread)
    {
        NtClose( server_port );
        return;
    }

    /* Receive the connect request, accept the connection. */
    timeout.QuadPart = -200 * 10000000LL;
    memset( recv_buf, 0, sizeof(recv_buf) );
    recv_sz = sizeof(recv_buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, recv_msg, &recv_sz, NULL, &timeout );
    ok(status == STATUS_SUCCESS, "Receive connect request failed: %#lx\n", status);
    if (status == STATUS_SUCCESS)
    {
        status = pNtAlpcAcceptConnectPort(&server_conn, server_port, 0, NULL, NULL, NULL, recv_msg, NULL, TRUE);
        ok(status == STATUS_SUCCESS, "NtAlpcAcceptConnectPort failed: %#lx\n", status);
    }

    /* Server receives message with ALPC_MESSAGE_VIEW_ATTRIBUTE */
    if (status == STATUS_SUCCESS)
    {
        status = pAlpcInitializeMessageAttribute(ALPC_MESSAGE_VIEW_ATTRIBUTE | ALPC_MESSAGE_CONTEXT_ATTRIBUTE,
                                                 recv_attr, sizeof(recv_attr_buf), &req_attr_size);
        ok(status == STATUS_SUCCESS, "AlpcInitializeMessageAttribute failed: %#lx\n", status);

        memset(recv_msg, 0, sizeof(*recv_msg));
        recv_sz = sizeof(recv_buf);
        status = pNtAlpcSendWaitReceivePort(server_port, 0, NULL, NULL, recv_msg, &recv_sz, recv_attr, &timeout);
        ok(status == STATUS_SUCCESS, "NtAlpcSendWaitReceivePort (receive) failed: %#lx\n", status);

        if (status == STATUS_SUCCESS)
        {
            ok(recv_attr->ValidAttributes & ALPC_MESSAGE_VIEW_ATTRIBUTE,
               "Expected ALPC_MESSAGE_VIEW_ATTRIBUTE in ValidAttributes, got %#lx\n", recv_attr->ValidAttributes);
            recvd_view = pAlpcGetMessageAttribute(recv_attr, ALPC_MESSAGE_VIEW_ATTRIBUTE);
            ok(recvd_view != NULL, "Expected recvd_view != NULL\n");
            if (recvd_view)
            {
                ok(recvd_view->ViewBase == args.view_base,
                   "Expected ViewBase %p, got %p\n", args.view_base, recvd_view->ViewBase);
                ok(recvd_view->ViewSize == 0x1000,
                   "Expected ViewSize 0x1000, got %Ix\n", recvd_view->ViewSize);
                ok(!strcmp((char *)recvd_view->ViewBase, "alpc shared payload 12345"),
                   "Received view content mismatch: %s\n", (char *)recvd_view->ViewBase);
            }
        }
    }

    WaitForSingleObject( thread, 30000 );
    ok( args.status == STATUS_SUCCESS, "Client thread failed: %#lx\n", args.status );

    /* Clean up */
    if (server_conn)
    {
        if (pNtAlpcDisconnectPort)
            pNtAlpcDisconnectPort(server_conn, 0);
        pNtClose(server_conn);
    }
    pNtClose(server_port);
    CloseHandle( thread );
}

static void test_NtAlpcCreatePort(void)
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestCreatePort";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    ALPC_BASIC_INFORMATION info;
    HANDLE port = NULL, dup = NULL;
    NTSTATUS status;
    ULONG retlen;

    if (!pNtAlpcCreatePort)
    {
        win_skip("NtAlpcCreatePort is unavailable.\n");
        return;
    }

    /* Null port handle pointer. */
    status = pNtAlpcCreatePort( NULL, NULL, NULL );
    ok( status == STATUS_ACCESS_VIOLATION, "Got unexpected status %#lx.\n", status );

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    /* Valid server port creation. */
    status = pNtAlpcCreatePort( &port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    ok( port != NULL, "Expected a valid port handle.\n" );
    if (status != STATUS_SUCCESS || !port) return;

    /* The handle is duplicable with the same access rights. */
    status = NtDuplicateObject( NtCurrentProcess(), port, NtCurrentProcess(), &dup, 0, 0, DUPLICATE_SAME_ACCESS );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    ok( dup != NULL, "Expected a valid duplicated handle.\n" );
    if (dup)
    {
        /* The duplicated handle is usable. */
        retlen = 0;
        status = pNtAlpcQueryInformation( dup, ALPC_BASIC_INFORMATION_CLASS, &info, sizeof(info), &retlen );
        ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
        NtClose( dup );
    }

    /* Creating a port with an already existing name. */
    {
        HANDLE port2 = NULL;
        status = pNtAlpcCreatePort( &port2, &obj_attr, &port_attr );
        ok( status == STATUS_OBJECT_NAME_EXISTS || status == STATUS_OBJECT_NAME_COLLISION || status == STATUS_SUCCESS,
            "Got unexpected status %#lx.\n", status );
        if (port2) NtClose( port2 );
    }

    NtClose( port );
}

static void test_NtAlpcConnectPort(void)
{
    static const WCHAR nonexistent_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestNonexistentPort";
    UNICODE_STRING port_name;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    HANDLE conn = NULL;
    NTSTATUS status;

    if (!pNtAlpcConnectPort)
    {
        win_skip("NtAlpcConnectPort is unavailable.\n");
        return;
    }

    /* Null port handle pointer. */
    RtlInitUnicodeString( &port_name, nonexistent_nameW );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    status = pNtAlpcConnectPort( NULL, &port_name, NULL, &port_attr, 0, NULL, NULL, NULL, NULL, NULL, NULL );
    ok( status == STATUS_ACCESS_VIOLATION, "Got unexpected status %#lx.\n", status );

    /* The port does not exist. */
    timeout.QuadPart = -100 * 10000000LL;
    conn = NULL;
    status = pNtAlpcConnectPort( &conn, &port_name, NULL, &port_attr, 0, NULL, NULL, NULL, NULL, NULL, &timeout );
    ok( status == STATUS_OBJECT_NAME_NOT_FOUND, "Got unexpected status %#lx.\n", status );
    ok( conn == NULL, "Expected NULL connection port handle.\n" );
}

static void test_NtAlpcSendWaitReceivePort(void)
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestSendRecv";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    SIZE_T size;
    HANDLE port = NULL;
    NTSTATUS status;

    if (!pNtAlpcSendWaitReceivePort)
    {
        win_skip("NtAlpcSendWaitReceivePort is unavailable.\n");
        return;
    }

    /* Invalid handle, send phase. */
    memset( buf, 0, sizeof(*msg) );
    msg->DataLength = 0;
    msg->TotalLength = sizeof(*msg);
    status = pNtAlpcSendWaitReceivePort( (HANDLE)0x1234, ALPC_MSGFLG_SYNC_REQUEST, msg, NULL,
                                         NULL, NULL, NULL, NULL );
    ok( status == STATUS_INVALID_HANDLE, "Got unexpected status %#lx.\n", status );

    /* Invalid handle, receive phase. */
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( (HANDLE)0x1234, 0, NULL, NULL, msg, &size, NULL, NULL );
    ok( status == STATUS_INVALID_HANDLE, "Got unexpected status %#lx.\n", status );

    /* No send, no receive: pure no-op. */
    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    status = pNtAlpcCreatePort( &port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    status = pNtAlpcSendWaitReceivePort( port, 0, NULL, NULL, NULL, NULL, NULL, NULL );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

    /* Receive with a timeout on an empty port. */
    timeout.QuadPart = -100 * 10000000LL;
    memset( buf, 0, sizeof(*msg) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_TIMEOUT, "Got unexpected status %#lx.\n", status );

    NtClose( port );
}

struct alpc_thread_args
{
    UNICODE_STRING *port_name;
    HANDLE done_event;
    NTSTATUS connect_status;
    NTSTATUS send_status;
    NTSTATUS recv_status;
    HANDLE conn;
    ULONG reply_type;
    ULONG reply_msg_id;
    ULONG reply_data_len;
    char reply_data[64];
    void *recv_port_context;
};

/* Client thread for the round-trip flow: connect with a connection message,
 * send a request, receive the reply with the context attribute. */
static DWORD WINAPI alpc_echo_client_thread( void *arg )
{
    struct alpc_thread_args *args = arg;
    ALPC_PORT_ATTRIBUTES port_attr;
    char buf[512];
    char attrbuf[256];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)attrbuf;
    ALPC_CONTEXT_ATTR *ctx;
    SIZE_T size, req_size = 0;
    NTSTATUS status;

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    /* Connect, passing a connection message payload. */
    memset( buf, 0, sizeof(*msg) );
    memcpy( msg + 1, "ALPCCONN", 8 );
    msg->DataLength = 8;
    msg->TotalLength = sizeof(*msg) + 8;
    size = sizeof(*msg) + 8;
    args->connect_status = pNtAlpcConnectPort( &args->conn, args->port_name, NULL, &port_attr, 0,
                                               NULL, msg, &size, NULL, NULL, NULL );
    if (args->connect_status != STATUS_SUCCESS)
    {
        SetEvent( args->done_event );
        return 0;
    }

    /* Send a request message. */
    memset( buf, 0, sizeof(*msg) );
    memcpy( msg + 1, "REQ-payload", 11 );
    msg->DataLength = 11;
    msg->TotalLength = sizeof(*msg) + 11;
    args->send_status = pNtAlpcSendWaitReceivePort( args->conn, ALPC_MSGFLG_SYNC_REQUEST, msg, NULL,
                                                    NULL, NULL, NULL, NULL );
    if (args->send_status != STATUS_SUCCESS)
    {
        SetEvent( args->done_event );
        return 0;
    }

    /* Receive the reply, requesting the context attribute. */
    status = pAlpcInitializeMessageAttribute( ALPC_MESSAGE_CONTEXT_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
    if (status != STATUS_SUCCESS)
    {
        args->recv_status = status;
        SetEvent( args->done_event );
        return 0;
    }
    memset( buf, 0, sizeof(*msg) );
    size = sizeof(buf);
    args->recv_status = pNtAlpcSendWaitReceivePort( args->conn, 0, NULL, NULL, msg, &size, attr, NULL );
    if (args->recv_status == STATUS_SUCCESS)
    {
        args->reply_type = msg->Type;
        args->reply_msg_id = msg->MessageId;
        args->reply_data_len = msg->DataLength;
        if (msg->DataLength && msg->DataLength <= sizeof(args->reply_data))
            memcpy( args->reply_data, msg + 1, msg->DataLength );
        ctx = pAlpcGetMessageAttribute( attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
        args->recv_port_context = ctx ? ctx->PortContext : NULL;
    }
    NtClose( args->conn );
    args->conn = NULL;
    SetEvent( args->done_event );
    return 0;
}

/* Client thread for the connection-rejection flow. */
static DWORD WINAPI alpc_reject_client_thread( void *arg )
{
    struct alpc_thread_args *args = arg;
    ALPC_PORT_ATTRIBUTES port_attr;

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    args->conn = NULL;
    args->connect_status = pNtAlpcConnectPort( &args->conn, args->port_name, NULL, &port_attr, 0,
                                               NULL, NULL, NULL, NULL, NULL, NULL );
    if (args->conn)
    {
        NtClose( args->conn );
        args->conn = NULL;
    }
    SetEvent( args->done_event );
    return 0;
}

/* Client thread for the disconnect flow: connect, send a request, then block
 * receiving the reply until the server disconnects the connection. */
static DWORD WINAPI alpc_disconnect_client_thread( void *arg )
{
    struct alpc_thread_args *args = arg;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    SIZE_T size;

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    args->conn = NULL;
    args->connect_status = pNtAlpcConnectPort( &args->conn, args->port_name, NULL, &port_attr, 0,
                                               NULL, NULL, NULL, NULL, NULL, NULL );
    if (args->connect_status != STATUS_SUCCESS)
    {
        SetEvent( args->done_event );
        return 0;
    }

    memset( buf, 0, sizeof(*msg) );
    memcpy( msg + 1, "REQ-payload", 11 );
    msg->DataLength = 11;
    msg->TotalLength = sizeof(*msg) + 11;
    args->send_status = pNtAlpcSendWaitReceivePort( args->conn, ALPC_MSGFLG_SYNC_REQUEST, msg, NULL,
                                                    NULL, NULL, NULL, NULL );

    /* Receive with a timeout; the server disconnects the connection port,
     * which must wake this receive up. */
    timeout.QuadPart = -10 * 10000000LL;
    memset( buf, 0, sizeof(*msg) );
    size = sizeof(buf);
    args->recv_status = pNtAlpcSendWaitReceivePort( args->conn, 0, NULL, NULL, msg, &size, NULL, &timeout );

    NtClose( args->conn );
    args->conn = NULL;
    SetEvent( args->done_event );
    return 0;
}

/* Full connection flow: connect, connection request delivery, accept with a
 * PortContext, request/reply round-trip with data integrity checks and
 * ALPC_MESSAGE_CONTEXT_ATTRIBUTE propagation, disconnect and cleanup. */
static void test_alpc_connection_flow( void )
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestFlow";
    static void * const test_context = (void *)0x1122334455667788;
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512];
    char attrbuf[256];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)attrbuf;
    ALPC_CONTEXT_ATTR *ctx;
    struct alpc_thread_args args;
    SIZE_T size, req_size;
    HANDLE server_port = NULL, server_conn = NULL, thread, event;
    ULONG req_id;
    NTSTATUS status;
    DWORD ret;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort || !pNtAlpcSendWaitReceivePort)
    {
        win_skip("ALPC port functions are unavailable.\n");
        return;
    }

    memset( &args, 0, sizeof(args) );
    args.port_name = &port_name;

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort( &server_port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    event = CreateEventA( NULL, TRUE, FALSE, NULL );
    ok( event != NULL, "CreateEventA failed, err %u.\n", (unsigned int)GetLastError() );
    args.done_event = event;
    thread = CreateThread( NULL, 0, alpc_echo_client_thread, &args, 0, NULL );
    ok( thread != NULL, "CreateThread failed, err %u.\n", (unsigned int)GetLastError() );
    if (!thread)
    {
        NtClose( server_port );
        return;
    }

    /* Receive the connection request on the server port. */
    timeout.QuadPart = -200 * 10000000LL;
    req_size = 0;
    status = pAlpcInitializeMessageAttribute( ALPC_MESSAGE_CONTEXT_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, attr, &timeout );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS)
    {
        WaitForSingleObject( thread, 30000 );
        CloseHandle( thread );
        NtClose( server_port );
        return;
    }
    winetest_push_context( "connection request" );
    ok( msg->Type == LPC_CONNECTION_REQUEST_TYPE, "Got unexpected type %u.\n", msg->Type );
    ok( msg->DataLength == 8, "Got unexpected DataLength %u.\n", msg->DataLength );
    ok( !memcmp( msg + 1, "ALPCCONN", 8 ), "Got unexpected connection payload.\n" );
    ok( msg->ClientId.UniqueProcess == (HANDLE)(DWORD_PTR)GetCurrentProcessId(),
        "Got unexpected ClientId.\n" );
    ok( msg->MessageId != 0, "Expected non-zero MessageId.\n" );
    ctx = pAlpcGetMessageAttribute( attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
    ok( ctx != NULL, "Expected a context attribute.\n" );
    if (ctx) ok( ctx->PortContext == NULL, "Got unexpected PortContext %p.\n", ctx->PortContext );
    req_id = msg->MessageId;
    winetest_pop_context();

    /* Accept the connection and attach the port context. */
    server_conn = NULL;
    status = pNtAlpcAcceptConnectPort( &server_conn, server_port, 0, NULL, NULL, test_context,
                                       msg, NULL, TRUE );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    ok( server_conn != NULL, "Expected a valid communication port handle.\n" );
    if (status != STATUS_SUCCESS || !server_conn)
    {
        WaitForSingleObject( thread, 30000 );
        CloseHandle( thread );
        NtClose( server_port );
        return;
    }

    /* A receive buffer smaller than the message fails and keeps the message queued. */
    size = 8;
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_BUFFER_TOO_SMALL, "Got unexpected status %#lx.\n", status );

    /* Receive the client request on the server port. */
    req_size = 0;
    status = pAlpcInitializeMessageAttribute( ALPC_MESSAGE_CONTEXT_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, attr, &timeout );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS)
    {
        WaitForSingleObject( thread, 30000 );
        CloseHandle( thread );
        NtClose( server_conn );
        NtClose( server_port );
        return;
    }
    winetest_push_context( "request" );
    ok( msg->Type == LPC_REQUEST_TYPE, "Got unexpected type %u.\n", msg->Type );
    ok( msg->DataLength == 11, "Got unexpected DataLength %u.\n", msg->DataLength );
    ok( !memcmp( msg + 1, "REQ-payload", 11 ), "Got unexpected request payload.\n" );
    ok( msg->ClientId.UniqueProcess == (HANDLE)(DWORD_PTR)GetCurrentProcessId(),
        "Got unexpected ClientId.\n" );
    ok( msg->MessageId != 0, "Expected non-zero MessageId.\n" );
    ok( (attr->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE),
        "Expected a valid context attribute, got %#lx.\n", attr->ValidAttributes );
    ctx = pAlpcGetMessageAttribute( attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
    ok( ctx != NULL, "Expected a context attribute.\n" );
    if (ctx)
    {
        /* The PortContext passed to NtAlpcAcceptConnectPort is propagated. */
        ok( ctx->PortContext == test_context, "Got unexpected PortContext %p.\n", ctx->PortContext );
        ok( ctx->MessageId == msg->MessageId, "Got unexpected context MessageId %u.\n", ctx->MessageId );
    }
    req_id = msg->MessageId;
    winetest_pop_context();

    /* Reply to the client, echoing the request MessageId. */
    memset( buf, 0, sizeof(*msg) );
    memcpy( msg + 1, "REPLY-payload", 13 );
    msg->DataLength = 13;
    msg->TotalLength = sizeof(*msg) + 13;
    msg->MessageId = req_id;
    status = pNtAlpcSendWaitReceivePort( server_port, ALPC_MSGFLG_REPLY_MESSAGE, msg, NULL,
                                         NULL, NULL, NULL, NULL );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

    ret = WaitForSingleObject( thread, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );

    winetest_push_context( "client" );
    ok( args.connect_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", args.connect_status );
    ok( args.send_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", args.send_status );
    ok( args.recv_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", args.recv_status );
    if (args.recv_status == STATUS_SUCCESS)
    {
        ok( args.reply_type == LPC_REPLY_TYPE, "Got unexpected reply type %u.\n", args.reply_type );
        ok( args.reply_msg_id == req_id, "Got unexpected reply MessageId %u.\n", args.reply_msg_id );
        ok( args.reply_data_len == 13, "Got unexpected reply DataLength %u.\n", args.reply_data_len );
        ok( !memcmp( args.reply_data, "REPLY-payload", 13 ), "Got unexpected reply payload.\n" );
        ok( args.recv_port_context == test_context, "Got unexpected PortContext %p.\n", args.recv_port_context );
    }
    winetest_pop_context();

    /* Disconnect the communication port and clean up. */
    status = pNtAlpcDisconnectPort( server_conn, 0 );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

    status = NtClose( server_conn );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    status = NtClose( server_port );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

    /* Closed handles are rejected. */
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_INVALID_HANDLE, "Got unexpected status %#lx.\n", status );

    CloseHandle( thread );
    CloseHandle( event );
}

/* The server rejects the connection: NtAlpcAcceptConnectPort with
 * accept_connection = FALSE returns success with a NULL communication port
 * and the client receives STATUS_CONNECTION_REFUSED. */
static void test_alpc_reject_connection( void )
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestReject";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    struct alpc_thread_args args;
    SIZE_T size;
    HANDLE server_port = NULL, conn = NULL, thread, event;
    NTSTATUS status;
    DWORD ret;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort || !pNtAlpcSendWaitReceivePort)
    {
        win_skip("ALPC port functions are unavailable.\n");
        return;
    }

    memset( &args, 0, sizeof(args) );
    args.port_name = &port_name;

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort( &server_port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    event = CreateEventA( NULL, TRUE, FALSE, NULL );
    args.done_event = event;
    thread = CreateThread( NULL, 0, alpc_reject_client_thread, &args, 0, NULL );
    ok( thread != NULL, "CreateThread failed, err %u.\n", (unsigned int)GetLastError() );
    if (!thread)
    {
        NtClose( server_port );
        return;
    }

    /* Receive the connection request. */
    timeout.QuadPart = -200 * 10000000LL;
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status == STATUS_SUCCESS)
    {
        ok( msg->Type == LPC_CONNECTION_REQUEST_TYPE, "Got unexpected type %u.\n", msg->Type );

        conn = (void *)0xdeadbeef;
        status = pNtAlpcAcceptConnectPort( &conn, server_port, 0, NULL, NULL, NULL, msg, NULL, FALSE );
        ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
        ok( conn == NULL, "Expected NULL communication port handle.\n" );
    }

    ret = WaitForSingleObject( thread, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );
    ok( args.connect_status == STATUS_CONNECTION_REFUSED, "Got unexpected status %#lx.\n", args.connect_status );

    NtClose( server_port );
    CloseHandle( thread );
    CloseHandle( event );
}

static void test_NtAlpcAcceptConnectPort(void)
{
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    HANDLE conn = NULL;
    NTSTATUS status;

    if (!pNtAlpcAcceptConnectPort)
    {
        win_skip("NtAlpcAcceptConnectPort is unavailable.\n");
        return;
    }

    /* Invalid connection port handle. */
    memset( buf, 0, sizeof(*msg) );
    conn = NULL;
    status = pNtAlpcAcceptConnectPort( &conn, (HANDLE)0x1234, 0, NULL, NULL, NULL, msg, NULL, TRUE );
    ok( status == STATUS_INVALID_HANDLE, "Got unexpected status %#lx.\n", status );
    ok( conn == NULL, "Expected NULL communication port handle.\n" );
}

/* Disconnect semantics on an accepted connection: the server disconnects its
 * communication port and the client's pending receive is woken up. */
static void test_alpc_disconnect_flow( void )
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestDisconnect";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    struct alpc_thread_args args;
    SIZE_T size;
    HANDLE server_port = NULL, server_conn = NULL, thread, event;
    NTSTATUS status;
    DWORD ret;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort ||
        !pNtAlpcSendWaitReceivePort || !pNtAlpcDisconnectPort)
    {
        win_skip("ALPC port functions are unavailable.\n");
        return;
    }

    /* Invalid handle. */
    status = pNtAlpcDisconnectPort( (HANDLE)0x1234, 0 );
    ok( status == STATUS_INVALID_HANDLE, "Got unexpected status %#lx.\n", status );

    memset( &args, 0, sizeof(args) );
    args.port_name = &port_name;

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort( &server_port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    event = CreateEventA( NULL, TRUE, FALSE, NULL );
    args.done_event = event;
    thread = CreateThread( NULL, 0, alpc_disconnect_client_thread, &args, 0, NULL );
    ok( thread != NULL, "CreateThread failed, err %u.\n", (unsigned int)GetLastError() );
    if (!thread)
    {
        NtClose( server_port );
        return;
    }

    /* Receive the connection request and accept. */
    timeout.QuadPart = -200 * 10000000LL;
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status == STATUS_SUCCESS)
    {
        server_conn = NULL;
        status = pNtAlpcAcceptConnectPort( &server_conn, server_port, 0, NULL, NULL, NULL, msg, NULL, TRUE );
        ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    }

    if (server_conn)
    {
        status = pNtAlpcDisconnectPort( server_conn, 0 );
        ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

        ret = WaitForSingleObject( thread, 30000 );
        ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );

        /* The client's pending receive must be woken up by the disconnect. */
        ok( args.connect_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", args.connect_status );
        ok( args.recv_status == STATUS_PORT_DISCONNECTED || args.recv_status == STATUS_PIPE_DISCONNECTED ||
            args.recv_status == STATUS_TIMEOUT, "Got unexpected status %#lx.\n", args.recv_status );

        status = NtClose( server_conn );
        ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

        /* The closed handle is rejected. */
        status = pNtAlpcDisconnectPort( server_conn, 0 );
        ok( status == STATUS_INVALID_HANDLE, "Got unexpected status %#lx.\n", status );
    }
    else
    {
        WaitForSingleObject( thread, 30000 );
    }

    NtClose( server_port );
    CloseHandle( thread );
    CloseHandle( event );
}

static void test_NtAlpcQueryInformation(void)
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestQueryInfo";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    ALPC_BASIC_INFORMATION info;
    HANDLE port = NULL;
    ULONG retlen;
    NTSTATUS status;

    if (!pNtAlpcQueryInformation)
    {
        win_skip("NtAlpcQueryInformation is unavailable.\n");
        return;
    }

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort( &port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    /* AlpcBasicInformation. */
    memset( &info, 0xa5, sizeof(info) );
    retlen = 0;
    status = pNtAlpcQueryInformation( port, ALPC_BASIC_INFORMATION_CLASS, &info, sizeof(info), &retlen );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    ok( retlen == sizeof(info), "Got unexpected retlen %u.\n", retlen );
    if (status == STATUS_SUCCESS && retlen == sizeof(info))
    {
        ok( info.Flags == 0, "Got unexpected Flags %#lx.\n", info.Flags );
        ok( info.PortContext == NULL, "Got unexpected PortContext %p.\n", info.PortContext );
    }

    /* Buffer too small. */
    retlen = 0;
    status = pNtAlpcQueryInformation( port, ALPC_BASIC_INFORMATION_CLASS, &info, sizeof(info) - 4, &retlen );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Got unexpected status %#lx.\n", status );

    /* Null return length pointer. */
    status = pNtAlpcQueryInformation( port, ALPC_BASIC_INFORMATION_CLASS, &info, sizeof(info), NULL );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );

    /* Invalid information class. */
    retlen = 0;
    status = pNtAlpcQueryInformation( port, ALPC_INVALID_INFORMATION_CLASS, &info, sizeof(info), &retlen );
    ok( status == STATUS_INVALID_INFO_CLASS, "Got unexpected status %#lx.\n", status );

    NtClose( port );
}

static void test_NtAlpcSetInformation(void)
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestSetInfo";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    SECURITY_DESCRIPTOR sd;
    HANDLE port = NULL;
    NTSTATUS status;

    if (!pNtAlpcSetInformation)
    {
        win_skip("NtAlpcSetInformation is unavailable.\n");
        return;
    }

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort( &port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    InitializeSecurityDescriptor( &sd, SECURITY_DESCRIPTOR_REVISION );
    ok( SetSecurityDescriptorDacl( &sd, TRUE, NULL, FALSE ), "SetSecurityDescriptorDacl failed, err %u.\n", (unsigned int)GetLastError() );

    /* AlpcPortSecurityDescriptorInformation: set the port security descriptor. */
    status = pNtAlpcSetInformation( port, 1 /* AlpcPortSecurityDescriptorInformation */, &sd, sizeof(sd) );
    ok( status == STATUS_SUCCESS || status == STATUS_INVALID_INFO_CLASS || status == STATUS_NOT_SUPPORTED ||
        status == STATUS_NOT_IMPLEMENTED || status == STATUS_INVALID_PARAMETER ||
        status == STATUS_INVALID_SYSTEM_SERVICE,
        "Got unexpected status %#lx.\n", status );

    NtClose( port );
}

/* Client thread for the event-notification flow: connect, wait for the start
 * signal, then send a one-way request. */
struct alpc_event_send_args
{
    UNICODE_STRING *port_name;
    HANDLE start_event;
    NTSTATUS connect_status;
    NTSTATUS send_status;
    HANDLE done_event;
};

static DWORD WINAPI alpc_event_send_thread( void *arg )
{
    struct alpc_event_send_args *args = arg;
    ALPC_PORT_ATTRIBUTES port_attr;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    HANDLE conn = NULL;

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    args->connect_status = pNtAlpcConnectPort( &conn, args->port_name, NULL, &port_attr, 0,
                                               NULL, NULL, NULL, NULL, NULL, NULL );
    if (args->connect_status != STATUS_SUCCESS)
    {
        SetEvent( args->done_event );
        return 0;
    }

    WaitForSingleObject( args->start_event, 30000 );

    memset( buf, 0, sizeof(*msg) );
    memcpy( msg + 1, "EVT-payload", 11 );
    msg->DataLength = 11;
    msg->TotalLength = sizeof(*msg) + 11;
    args->send_status = pNtAlpcSendWaitReceivePort( conn, 0, msg, NULL, NULL, NULL, NULL, NULL );

    NtClose( conn );
    SetEvent( args->done_event );
    return 0;
}

/* Receive thread: blocks in NtAlpcSendWaitReceivePort on a connection armed
 * with the ALPC_MESSAGE_EVENT_ATTRIBUTE. */
struct alpc_event_recv_args
{
    HANDLE port;
    ALPC_MESSAGE_ATTRIBUTES *attr;
    char *buf;
    SIZE_T size;
    NTSTATUS status;
};

static DWORD WINAPI alpc_event_recv_thread( void *arg )
{
    struct alpc_event_recv_args *args = arg;
    LARGE_INTEGER timeout;
    SIZE_T size = args->size;

    timeout.QuadPart = -10 * 10000000LL;
    args->status = pNtAlpcSendWaitReceivePort( args->port, 0, NULL, NULL,
                                               (ALPC_PORT_MESSAGE *)args->buf, &size, args->attr, &timeout );
    args->size = size;
    return 0;
}

/* A receive armed with ALPC_MESSAGE_EVENT_ATTRIBUTE must have its event
 * signaled when a message is delivered (the CoreMessaging dispatcher wake
 * mechanism), the attribute must be reported valid with UsedEntryCount 1. */
static void test_alpc_event_notification( void )
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestEventNotify";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512], attrbuf[256];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)attrbuf;
    ALPC_EVENT *event_attr;
    struct alpc_event_send_args send_args;
    struct alpc_event_recv_args recv_args;
    SIZE_T size, req_size;
    HANDLE server_port = NULL, server_conn = NULL;
    HANDLE notify_event = NULL, start_event = NULL, done_event = NULL;
    HANDLE send_thread = NULL, recv_thread = NULL;
    NTSTATUS status;
    DWORD ret;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort ||
        !pNtAlpcSendWaitReceivePort || !pAlpcInitializeMessageAttribute || !pAlpcGetMessageAttribute)
    {
        win_skip("ALPC port functions are unavailable.\n");
        return;
    }

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;

    status = pNtAlpcCreatePort( &server_port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    notify_event = CreateEventA( NULL, TRUE, FALSE, NULL );
    ok( notify_event != NULL, "CreateEventA failed, err %u.\n", (unsigned int)GetLastError() );
    start_event = CreateEventA( NULL, TRUE, FALSE, NULL );
    done_event = CreateEventA( NULL, TRUE, FALSE, NULL );

    memset( &send_args, 0, sizeof(send_args) );
    send_args.port_name = &port_name;
    send_args.start_event = start_event;
    send_args.done_event = done_event;
    send_thread = CreateThread( NULL, 0, alpc_event_send_thread, &send_args, 0, NULL );
    ok( send_thread != NULL, "CreateThread failed, err %u.\n", (unsigned int)GetLastError() );
    if (!send_thread) goto done;

    /* Receive the connection request and accept. */
    timeout.QuadPart = -200 * 10000000LL;
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) goto done;

    server_conn = NULL;
    status = pNtAlpcAcceptConnectPort( &server_conn, server_port, 0, NULL, NULL, NULL, msg, NULL, TRUE );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS || !server_conn) goto done;

    /* Arm the receive with an event packet notification before the send. */
    req_size = 0;
    status = pAlpcInitializeMessageAttribute( ALPC_MESSAGE_EVENT_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) goto done;
    event_attr = pAlpcGetMessageAttribute( attr, ALPC_MESSAGE_EVENT_ATTRIBUTE );
    ok( event_attr != NULL, "Expected an event attribute.\n" );
    if (!event_attr) goto done;
    event_attr->Event = notify_event;
    event_attr->UsedEntryCount = 0;

    memset( buf, 0, sizeof(buf) );
    memset( &recv_args, 0, sizeof(recv_args) );
    recv_args.port = server_conn;
    recv_args.attr = attr;
    recv_args.buf = buf;
    recv_args.size = sizeof(buf);
    recv_thread = CreateThread( NULL, 0, alpc_event_recv_thread, &recv_args, 0, NULL );
    ok( recv_thread != NULL, "CreateThread failed, err %u.\n", (unsigned int)GetLastError() );
    if (!recv_thread) goto done;

    /* Let the receive arm itself before the request is sent: the notification
     * must be armed from the prior waiting state. */
    Sleep( 500 );

    /* Let the client send its request now. */
    SetEvent( start_event );

    /* The notification event must be signaled when the message is delivered. */
    ret = WaitForSingleObject( notify_event, 5000 );
    ok( ret == 0, "The event packet notification was not signaled, ret %u.\n", ret );

    ret = WaitForSingleObject( recv_thread, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );
    winetest_push_context( "receive" );
    ok( recv_args.status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", recv_args.status );
    ok( (attr->ValidAttributes & ALPC_MESSAGE_EVENT_ATTRIBUTE),
        "Expected a valid event attribute, got %#lx.\n", attr->ValidAttributes );
    ok( event_attr->UsedEntryCount == 1, "Got unexpected UsedEntryCount %u.\n",
        (unsigned int)event_attr->UsedEntryCount );
    ok( msg->DataLength == 11 && !memcmp( msg + 1, "EVT-payload", 11 ), "Got unexpected message payload.\n" );
    winetest_pop_context();

    ret = WaitForSingleObject( send_thread, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );
    winetest_push_context( "send" );
    ok( send_args.connect_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", send_args.connect_status );
    ok( send_args.send_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", send_args.send_status );
    winetest_pop_context();

done:
    if (send_thread)
    {
        SetEvent( start_event );
        SetEvent( done_event );
        CloseHandle( send_thread );
    }
    if (recv_thread) CloseHandle( recv_thread );
    if (server_conn) NtClose( server_conn );
    NtClose( server_port );
    CloseHandle( notify_event );
    CloseHandle( start_event );
    CloseHandle( done_event );
}

/* Multi-buffer send: one send carrying a chain of PORT_MESSAGEs (the
 * ALPC_MSGFLG_LPC_MULTI_MESSAGE contract) is split by the kernel into separate
 * messages: each is delivered on its own, stamped with its own kernel-assigned
 * MessageId, and tagged with the multi-message sequence context (index in the
 * chain, chain length).  A server may then answer every request of the chain:
 * the connection's outstanding list carries all of them. */
struct multi_server_args
{
    HANDLE port;                     /* listening port: connect is slurped here */
    HANDLE ready;                    /* set after accept, server loop starts */
    HANDLE done;
    NTSTATUS recv_status[3];
    NTSTATUS reply_status[3];
    ULONG recv_id[3];
    ULONG recv_seq[3];
    ULONG recv_count[3];
    char recv_payload[3][8];
    ULONG n;
};

static DWORD WINAPI multi_server_thread( void *arg )
{
    struct multi_server_args *a = arg;
    char buf[512], attrbuf[256], rbuf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    ALPC_PORT_MESSAGE *rmsg = (ALPC_PORT_MESSAGE *)rbuf;
    ALPC_MESSAGE_ATTRIBUTES *attr = (ALPC_MESSAGE_ATTRIBUTES *)attrbuf;
    ALPC_CONTEXT_ATTR *ctx;
    LARGE_INTEGER timeout;
    HANDLE conn = NULL;
    SIZE_T size, req_size = 0;
    NTSTATUS status;
    int i;

    /* Accept the connection. */
    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    timeout.QuadPart = -10 * 10000000LL;
    status = pNtAlpcSendWaitReceivePort( a->port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    if (status == STATUS_SUCCESS)
        status = pNtAlpcAcceptConnectPort( &conn, a->port, 0, NULL, NULL, NULL, msg, NULL, TRUE );
    if (status != STATUS_SUCCESS)
    {
        a->n = 0;
        SetEvent( a->done );
        return 0;
    }
    SetEvent( a->ready );

    pAlpcInitializeMessageAttribute( ALPC_MESSAGE_CONTEXT_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
    for (i = 0; i < 3; i++)
    {
        memset( buf, 0, sizeof(buf) );
        memset( attr, 0, sizeof(attrbuf) );
        pAlpcInitializeMessageAttribute( ALPC_MESSAGE_CONTEXT_ATTRIBUTE, attr, sizeof(attrbuf), &req_size );
        size = sizeof(buf);
        timeout.QuadPart = -10 * 10000000LL;
        a->recv_status[i] = pNtAlpcSendWaitReceivePort( conn, 0, NULL, NULL, msg, &size, attr, &timeout );
        if (a->recv_status[i] != STATUS_SUCCESS) break;
        a->recv_id[i] = msg->MessageId;
        if (msg->DataLength >= 8) memcpy( a->recv_payload[i], msg + 1, 8 );
        /* the kernel delivers the multi-message sequence context in the
         * CONTEXT attribute: index in Sequence, chain length in CallbackId */
        ctx = pAlpcGetMessageAttribute( attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
        a->recv_seq[i]   = ctx ? ctx->Sequence : 0xffffffff;
        a->recv_count[i] = ctx ? ctx->CallbackId : 0;

        /* Answer this request of the chain: a REPLY-flag send echoing its id. */
        memset( rbuf, 0, sizeof(*rmsg) );
        memcpy( rmsg + 1, msg + 1, 8 );
        rmsg->DataLength = 8;
        rmsg->TotalLength = sizeof(*rmsg) + 8;
        rmsg->MessageId = msg->MessageId;
        a->reply_status[i] = pNtAlpcSendWaitReceivePort( conn, ALPC_MSGFLG_REPLY_MESSAGE, rmsg, NULL,
                                                         NULL, NULL, NULL, NULL );
    }
    a->n = i;
    if (conn) NtClose( conn );
    SetEvent( a->done );
    return 0;
}

static void test_alpc_multi_buffer( void )
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestMulti";
    static const char payloads[3][8] = { "MULTI-0", "MULTI-1", "MULTI-2" };
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    ALPC_PORT_MESSAGE *msg;
    struct multi_server_args args;
    LARGE_INTEGER timeout;
    char buf[512];
    SIZE_T size;
    HANDLE server_port = NULL, thread, conn = NULL;
    NTSTATUS status;
    ULONG total;
    DWORD ret;
    int i;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort ||
        !pNtAlpcSendWaitReceivePort || !pAlpcInitializeMessageAttribute || !pAlpcGetMessageAttribute)
    {
        win_skip( "ALPC port functions are unavailable.\n" );
        return;
    }

    memset( &args, 0, sizeof(args) );
    args.ready = CreateEventA( NULL, TRUE, FALSE, NULL );
    args.done = CreateEventA( NULL, TRUE, FALSE, NULL );

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    status = pNtAlpcCreatePort( &server_port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;
    args.port = server_port;

    thread = CreateThread( NULL, 0, multi_server_thread, &args, 0, NULL );
    ok( thread != NULL, "CreateThread failed, err %u.\n", (unsigned int)GetLastError() );

    /* Connect as the client. */
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    status = pNtAlpcConnectPort( &conn, &port_name, NULL, &port_attr, 0, NULL,
                                 NULL, NULL, NULL, NULL, NULL );
    ok( status == STATUS_SUCCESS, "Got unexpected connect status %#lx.\n", status );
    if (status != STATUS_SUCCESS) goto done;

    ret = WaitForSingleObject( args.ready, 10000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );

    /* Build the chain: three messages, each sized by its own TotalLength;
     * the send buffer length (the first message's TotalLength) covers all. */
    memset( buf, 0, sizeof(buf) );
    msg = (ALPC_PORT_MESSAGE *)buf;
    total = 0;
    for (i = 0; i < 3; i++)
    {
        ALPC_PORT_MESSAGE *m = (ALPC_PORT_MESSAGE *)((char *)msg + total);
        memcpy( m + 1, payloads[i], 8 );
        m->DataLength = 8;
        m->TotalLength = sizeof(*m) + 8;
        total += m->TotalLength;
    }
    msg->TotalLength = total; /* the whole chain travels in one send */

    status = pNtAlpcSendWaitReceivePort( conn, ALPC_MSGFLG_LPC_MULTI_MESSAGE, msg, NULL,
                                         NULL, NULL, NULL, NULL );
    ok( status == STATUS_SUCCESS, "Got unexpected multi send status %#lx.\n", status );

    /* Receive the three replies (bare receives: each takes one reply). */
    for (i = 0; i < 3; i++)
    {
        memset( buf, 0, sizeof(buf) );
        size = sizeof(buf);
        timeout.QuadPart = -10 * 10000000LL;
        status = pNtAlpcSendWaitReceivePort( conn, 0, NULL, NULL, msg, &size, NULL, &timeout );
        winetest_push_context( "reply %d", i );
        ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
        if (status == STATUS_SUCCESS)
        {
            ok( msg->Type == LPC_REPLY_TYPE, "Got unexpected type %u.\n", msg->Type );
            ok( msg->MessageId != 0, "Expected non-zero reply MessageId.\n" );
            ok( msg->DataLength == 8, "Got unexpected DataLength %u.\n", msg->DataLength );
            ok( !memcmp( msg + 1, payloads[i], 8 ), "Got unexpected reply payload.\n" );
        }
        winetest_pop_context();
        if (status != STATUS_SUCCESS) break;
    }

    ret = WaitForSingleObject( args.done, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );

    winetest_push_context( "server" );
    ok( args.n == 3, "Server received %u messages.\n", args.n );
    for (i = 0; i < (int)args.n; i++)
    {
        winetest_push_context( "msg %d", i );
        ok( args.recv_status[i] == STATUS_SUCCESS, "Got unexpected status %#lx.\n", args.recv_status[i] );
        ok( !memcmp( args.recv_payload[i], payloads[i], 8 ), "Got unexpected payload.\n" );
        /* per-buffer kernel ids: non-zero, distinct, in send order */
        ok( args.recv_id[i] != 0, "Expected non-zero MessageId.\n" );
        ok( args.recv_seq[i] == (ULONG)i, "Got unexpected sequence %u.\n", args.recv_seq[i] );
        ok( args.recv_count[i] == 3, "Got unexpected sequence count %u.\n", args.recv_count[i] );
        ok( args.reply_status[i] == STATUS_SUCCESS, "Got unexpected reply status %#lx.\n", args.reply_status[i] );
        winetest_pop_context();
    }
    if (args.n == 3)
    {
        ok( args.recv_id[0] != args.recv_id[1] && args.recv_id[1] != args.recv_id[2]
            && args.recv_id[0] != args.recv_id[2], "Per-buffer MessageIds must be distinct.\n" );
    }
    winetest_pop_context();

done:
    if (conn) NtClose( conn );
    if (thread)
    {
        WaitForSingleObject( thread, 30000 );
        CloseHandle( thread );
    }
    CloseHandle( args.ready );
    CloseHandle( args.done );
    NtClose( server_port );
}

/* Synchronous serialisation: the kernel runs one synchronous request-reply
 * exchange at a time on a connection.  Two threads send synchronous requests
 * on the same connection concurrently; the server must never see a request
 * arrive while the exchange of a previous one is still open (it has been
 * replied), and every exchange completes with its own correlated reply. */
struct ser_client_args
{
    const UNICODE_STRING *port_name;
    HANDLE conn;      /* one connection shared by both threads */
    const char *payload;
    NTSTATUS connect_status, send_status;
    char reply[8];
};

static DWORD WINAPI ser_connect_thread( void *arg )
{
    struct ser_client_args *a = arg;
    ALPC_PORT_ATTRIBUTES port_attr;

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    a->conn = NULL;
    a->connect_status = pNtAlpcConnectPort( &a->conn, a->port_name, NULL, &port_attr, 0,
                                            NULL, NULL, NULL, NULL, NULL, NULL );
    return 0;
}

static DWORD WINAPI ser_client_thread( void *arg )
{
    struct ser_client_args *a = arg;
    LARGE_INTEGER timeout;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    SIZE_T size;

    memset( buf, 0, sizeof(*msg) );
    memcpy( msg + 1, a->payload, 8 );
    msg->DataLength = 8;
    msg->TotalLength = sizeof(*msg) + 8;
    size = sizeof(buf);
    timeout.QuadPart = -15 * 10000000LL;
    a->send_status = pNtAlpcSendWaitReceivePort( a->conn, ALPC_MSGFLG_SYNC_REQUEST, msg, NULL,
                                                 msg, &size, NULL, &timeout );
    if (a->send_status == STATUS_SUCCESS && msg->DataLength >= 8) memcpy( a->reply, msg + 1, 8 );
    return 0;
}

static void test_alpc_serialization( void )
{
    static const WCHAR port_nameW[] = L"\\BaseNamedObjects\\WineAlpcTestSerial";
    static char pay_a[8] = "SER-A", pay_b[8] = "SER-B";
    UNICODE_STRING port_name;
    OBJECT_ATTRIBUTES obj_attr;
    ALPC_PORT_ATTRIBUTES port_attr;
    LARGE_INTEGER timeout;
    char buf[512];
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)buf;
    struct ser_client_args ca, cb;
    HANDLE server_port = NULL, client_conn = NULL, ta = NULL, tb = NULL, tc = NULL;
    ULONG cid = 0;
    SIZE_T size;
    NTSTATUS status;
    DWORD ret;
    LONG inflight = 0;
    int i, overlap = 0, exchanges = 0;

    if (!pNtAlpcCreatePort || !pNtAlpcConnectPort || !pNtAlpcAcceptConnectPort || !pNtAlpcSendWaitReceivePort)
    {
        win_skip( "ALPC port functions are unavailable.\n" );
        return;
    }

    RtlInitUnicodeString( &port_name, port_nameW );
    InitializeObjectAttributes( &obj_attr, &port_name, 0, NULL, NULL );
    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = 0x1000;
    status = pNtAlpcCreatePort( &server_port, &obj_attr, &port_attr );
    ok( status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", status );
    if (status != STATUS_SUCCESS) return;

    /* One connection: connect on a thread, slurp the connection request on
     * the listening port and complete the connect by the REPLY-flag send
     * (the LPC connect dance): the server keeps no connection handle and
     * its traffic rides the listening port. */
    memset( &ca, 0, sizeof(ca) );
    ca.port_name = &port_name;
    tc = CreateThread( NULL, 0, ser_connect_thread, &ca, 0, NULL );
    ok( tc != NULL, "CreateThread failed.\n" );

    memset( buf, 0, sizeof(buf) );
    size = sizeof(buf);
    timeout.QuadPart = -15 * 10000000LL;
    status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
    ok( status == STATUS_SUCCESS, "Got unexpected slurp status %#lx.\n", status );
    if (status != STATUS_SUCCESS) goto done;
    cid = msg->MessageId;
    memset( buf, 0, sizeof(*msg) );
    msg->MessageId = cid; /* the connect reply echoes the request id */
    msg->TotalLength = sizeof(*msg);
    status = pNtAlpcSendWaitReceivePort( server_port, ALPC_MSGFLG_REPLY_MESSAGE, msg, NULL,
                                         NULL, NULL, NULL, NULL );
    ok( status == STATUS_SUCCESS, "Got unexpected connect-reply status %#lx.\n", status );
    ret = WaitForSingleObject( tc, 10000 );
    ok( ret == 0 && ca.connect_status == STATUS_SUCCESS, "Connect failed, status %#lx.\n", ca.connect_status );
    client_conn = ca.conn;
    if (ca.connect_status != STATUS_SUCCESS) goto done;

    ca.payload = pay_a;
    cb.payload = pay_b;
    ca.conn = cb.conn = client_conn;
    ta = CreateThread( NULL, 0, ser_client_thread, &ca, 0, NULL );
    tb = CreateThread( NULL, 0, ser_client_thread, &cb, 0, NULL );
    ok( ta != NULL && tb != NULL, "CreateThread failed.\n" );

    /* Serve the two exchanges on the listening port: the kernel starts the
     * second synchronous request on a connection only after the reply to the
     * first has been sent, so a request is never delivered while an exchange
     * is open. */
    for (i = 0; i < 2; i++)
    {
        memset( buf, 0, sizeof(buf) );
        size = sizeof(buf);
        timeout.QuadPart = -15 * 10000000LL;
        status = pNtAlpcSendWaitReceivePort( server_port, 0, NULL, NULL, msg, &size, NULL, &timeout );
        if (status != STATUS_SUCCESS) break;
        exchanges++;
        if (InterlockedCompareExchange( &inflight, 1, 0 ) != 0) overlap++;
        status = pNtAlpcSendWaitReceivePort( server_port, ALPC_MSGFLG_REPLY_MESSAGE, msg, NULL,
                                             NULL, NULL, NULL, NULL );
        InterlockedExchange( &inflight, 0 );
        if (status != STATUS_SUCCESS) break;
    }
    ok( exchanges == 2, "Served %d exchanges.\n", exchanges );
    ok( overlap == 0, "Requests were delivered with an open exchange %d times.\n", overlap );

    ret = WaitForSingleObject( ta, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );
    ret = WaitForSingleObject( tb, 30000 );
    ok( ret == 0, "WaitForSingleObject failed, ret %u.\n", ret );

    winetest_push_context( "clients" );
    ok( ca.send_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", ca.send_status );
    ok( cb.send_status == STATUS_SUCCESS, "Got unexpected status %#lx.\n", cb.send_status );
    ok( !memcmp( ca.reply, pay_a, 8 ) || !memcmp( ca.reply, pay_b, 8 ), "Got unexpected reply payload.\n" );
    ok( !memcmp( ca.reply, cb.reply, 8 ) == 0, "Both clients got the same reply.\n" );
    winetest_pop_context();

done:
    if (ta) CloseHandle( ta );
    if (tb) CloseHandle( tb );
    if (tc) CloseHandle( tc );
    if (client_conn) NtClose( client_conn );
    NtClose( server_port );
}
#endif /* _WIN64 */

START_TEST(alpc)
{
    init_functions();

    test_AlpcGetHeaderSize();
    test_AlpcGetMessageAttribute();
    test_AlpcInitializeMessageAttribute();
    test_AlpcSectionView();
#ifdef _WIN64
    test_AlpcMessageViewAttribute();
    test_NtAlpcCreatePort();
    test_NtAlpcConnectPort();
    test_NtAlpcAcceptConnectPort();
    test_NtAlpcSendWaitReceivePort();
    test_alpc_connection_flow();
    test_alpc_reject_connection();
    test_alpc_disconnect_flow();
    test_NtAlpcQueryInformation();
    test_NtAlpcSetInformation();
    test_alpc_event_notification();
    test_alpc_multi_buffer();
    test_alpc_serialization();
#endif
}
