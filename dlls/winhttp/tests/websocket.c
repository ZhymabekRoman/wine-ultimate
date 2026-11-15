/*
 * WinHTTP - WebSocket tests
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windef.h>
#include <winbase.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <winhttp.h>

#include "wine/test.h"

static DWORD (WINAPI *pWinHttpWebSocketClose)(HINTERNET,USHORT,void*,DWORD);
static HINTERNET (WINAPI *pWinHttpWebSocketCompleteUpgrade)(HINTERNET,DWORD_PTR);
static DWORD (WINAPI *pWinHttpWebSocketQueryCloseStatus)(HINTERNET,USHORT*,void*,DWORD,DWORD*);
static DWORD (WINAPI *pWinHttpWebSocketReceive)(HINTERNET,void*,DWORD,DWORD*,WINHTTP_WEB_SOCKET_BUFFER_TYPE*);
static DWORD (WINAPI *pWinHttpWebSocketSend)(HINTERNET,WINHTTP_WEB_SOCKET_BUFFER_TYPE,void*,DWORD);
static DWORD (WINAPI *pWinHttpWebSocketShutdown)(HINTERNET,USHORT,void*,DWORD);

static void create_websocket_accept(const char *key, char *accept, DWORD accept_len)
{
    static const char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    HCRYPTPROV prov;
    HCRYPTHASH hash;
    BYTE sha1[20];
    DWORD len = sizeof(sha1);
    BOOL ret;

    ret = CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    ok(ret, "CryptAcquireContextA failed: %lu\n", GetLastError());
    ret = CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash);
    ok(ret, "CryptCreateHash failed: %lu\n", GetLastError());
    ret = CryptHashData(hash, (const BYTE *)key, strlen(key), 0);
    ok(ret, "CryptHashData failed: %lu\n", GetLastError());
    ret = CryptHashData(hash, (const BYTE *)magic, sizeof(magic) - 1, 0);
    ok(ret, "CryptHashData failed: %lu\n", GetLastError());
    ret = CryptGetHashParam(hash, HP_HASHVAL, sha1, &len, 0);
    ok(ret, "CryptGetHashParam failed: %lu\n", GetLastError());
    ret = CryptBinaryToStringA(sha1, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, accept, &accept_len);
    ok(ret, "CryptBinaryToStringA failed: %lu\n", GetLastError());
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
}

static int recv_exact(SOCKET s, void *buf, int len)
{
    int total = 0;
    while (total < len)
    {
        int r = recv(s, (char *)buf + total, len - total, 0);
        if (r <= 0) return -1;
        total += r;
    }
    return total;
}

static int send_exact(SOCKET s, const void *buf, int len)
{
    int total = 0;
    while (total < len)
    {
        int r = send(s, (const char *)buf + total, len - total, 0);
        if (r <= 0) return -1;
        total += r;
    }
    return total;
}

static int ws_build_server_frame(unsigned char *out, BOOL fin, unsigned char opcode, const void *payload, DWORD len)
{
    int off = 2;
    out[0] = (fin ? 0x80 : 0) | (opcode & 0x0f);
    if (len < 126)
    {
        out[1] = (unsigned char)len;
    }
    else if (len < 65536)
    {
        out[1] = 126;
        out[2] = (len >> 8) & 0xff;
        out[3] = len & 0xff;
        off += 2;
    }
    else
    {
        out[1] = 127;
        out[2] = out[3] = out[4] = out[5] = 0;
        out[6] = (len >> 24) & 0xff;
        out[7] = (len >> 16) & 0xff;
        out[8] = (len >> 8) & 0xff;
        out[9] = len & 0xff;
        off += 8;
    }
    if (len) memcpy(out + off, payload, len);
    return off + len;
}

static BOOL ws_send_server_frame(SOCKET s, BOOL fin, unsigned char opcode, const void *payload, DWORD len)
{
    unsigned char *buf = malloc(10 + len);
    int frame_len;
    BOOL ret;

    if (!buf) return FALSE;
    frame_len = ws_build_server_frame(buf, fin, opcode, payload, len);
    ret = (send_exact(s, buf, frame_len) == frame_len);
    free(buf);
    return ret;
}

static BOOL ws_recv_client_frame(SOCKET s, BOOL *fin, unsigned char *opcode, char **payload_out, DWORD *len_out)
{
    unsigned char hdr[2], mask[4];
    DWORD len;
    char *payload = NULL;
    DWORD i;

    if (recv_exact(s, hdr, 2) != 2) return FALSE;
    *fin = !!(hdr[0] & 0x80);
    *opcode = hdr[0] & 0x0f;
    ok(!!(hdr[1] & 0x80), "client frame not masked (hdr[1]=%#x)\n", hdr[1]);

    len = hdr[1] & 0x7f;
    if (len == 126)
    {
        unsigned char ext[2];
        if (recv_exact(s, ext, 2) != 2) return FALSE;
        len = ((DWORD)ext[0] << 8) | ext[1];
    }
    else if (len == 127)
    {
        unsigned char ext[8];
        if (recv_exact(s, ext, 8) != 8) return FALSE;
        ok(!ext[0] && !ext[1] && !ext[2] && !ext[3], "unexpected >4GB frame\n");
        len = ((DWORD)ext[4] << 24) | ((DWORD)ext[5] << 16) | ((DWORD)ext[6] << 8) | ext[7];
    }

    if (recv_exact(s, mask, 4) != 4) return FALSE;
    if (len)
    {
        payload = malloc(len + 1);
        if (!payload) return FALSE;
        if (recv_exact(s, payload, len) != (int)len)
        {
            free(payload);
            return FALSE;
        }
        for (i = 0; i < len; i++)
            payload[i] ^= mask[i % 4];
        payload[len] = 0;
    }

    *payload_out = payload;
    *len_out = len;
    return TRUE;
}

struct ws_server_info
{
    HANDLE ready;
    int port;
    SOCKET listen_sock;
};

static DWORD CALLBACK ws_server_thread(LPVOID param)
{
    struct ws_server_info *info = param;
    SOCKET c;

    SetEvent(info->ready);

    while ((c = accept(info->listen_sock, NULL, NULL)) != INVALID_SOCKET)
    {
        char req[2048];
        int req_len = 0;
        const char *pos;
        char key[32], accept_val[64], resp[512];
        BOOL fin;
        unsigned char opcode;
        char *payload = NULL;
        DWORD plen = 0;

        while (req_len < (int)sizeof(req) - 1)
        {
            int r = recv(c, req + req_len, sizeof(req) - 1 - req_len, 0);
            if (r <= 0) break;
            req_len += r;
            req[req_len] = 0;
            if (strstr(req, "\r\n\r\n")) break;
        }

        pos = strstr(req, "Sec-WebSocket-Key: ");
        if (!pos || !strstr(req, "Upgrade: websocket\r\n") || !strstr(req, "Connection: Upgrade\r\n"))
        {
            closesocket(c);
            continue;
        }

        memcpy(key, pos + 19, 24);
        key[24] = 0;
        create_websocket_accept(key, accept_val, sizeof(accept_val));

        sprintf(resp,
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n"
                "\r\n",
                accept_val);

        if (strstr(req, "GET /buffered"))
        {
            /* Send 101 response + initial WS frame in the same packet */
            unsigned char combined[600];
            int resp_len = strlen(resp);
            int frame_len;
            memcpy(combined, resp, resp_len);
            frame_len = ws_build_server_frame(combined + resp_len, TRUE, 0x1, "buffered-init", 13);
            send_exact(c, combined, resp_len + frame_len);

            /* Expect close */
            if (ws_recv_client_frame(c, &fin, &opcode, &payload, &plen))
            {
                ok(opcode == 0x8, "expected close, got %#x\n", opcode);
                free(payload);
                /* Reply with empty close frame (0-byte payload -> 1005) */
                ws_send_server_frame(c, TRUE, 0x8, NULL, 0);
            }
            closesocket(c);
            continue;
        }

        send_exact(c, resp, strlen(resp));

        if (strstr(req, "GET /echo_sync"))
        {
            /* 1. Receive masked "hello" and echo back */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv hello failed\n");
            ok(fin && opcode == 0x1 && plen == 5 && !memcmp(payload, "hello", 5),
               "unexpected hello frame fin=%d op=%x len=%lu\n", fin, opcode, plen);
            ws_send_server_frame(c, TRUE, 0x1, payload, plen);
            free(payload);

            /* 2. Send PING with payload + unsolicited PONG with payload + TEXT "after-ping" */
            ws_send_server_frame(c, TRUE, 0x9, "ping-data", 9);
            /* Client auto-replies PONG with "ping-data" while inside WinHttpWebSocketReceive */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv pong failed\n");
            ok(fin && opcode == 0xa && plen == 9 && !memcmp(payload, "ping-data", 9),
               "unexpected pong frame fin=%d op=%x len=%lu\n", fin, opcode, plen);
            free(payload);

            ws_send_server_frame(c, TRUE, 0xa, "unsolicited", 11);
            ws_send_server_frame(c, TRUE, 0x1, "after-ping", 10);

            /* 3. Receive 1000-byte message (16-bit extended length 126) and echo back */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv 1000B failed\n");
            ok(fin && opcode == 0x2 && plen == 1000, "unexpected 1000B frame op=%x len=%lu\n", opcode, plen);
            ws_send_server_frame(c, TRUE, 0x2, payload, plen);
            free(payload);

            /* 4. Receive 70KB message (64-bit extended length 127) and echo back */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv 70KB failed\n");
            ok(fin && opcode == 0x2 && plen == 70 * 1024, "unexpected 70KB frame op=%x len=%lu\n", opcode, plen);
            ws_send_server_frame(c, TRUE, 0x2, payload, plen);
            free(payload);

            /* 5. Receive 2-fragment UTF8 message ("frag1-" + "frag2") */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv frag1 failed\n");
            ok(!fin && opcode == 0x1 && plen == 6 && !memcmp(payload, "frag1-", 6),
               "unexpected frag1 fin=%d op=%x len=%lu\n", fin, opcode, plen);
            free(payload);

            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv frag2 failed\n");
            ok(fin && opcode == 0x0 && plen == 5 && !memcmp(payload, "frag2", 5),
               "unexpected frag2 fin=%d op=%x len=%lu\n", fin, opcode, plen);
            free(payload);

            /* Send fragmented UTF8 response with interleaved PING in the middle */
            ws_send_server_frame(c, FALSE, 0x1, "srv1-", 5);
            ws_send_server_frame(c, TRUE, 0x9, "mid", 3);
            ws_send_server_frame(c, TRUE, 0x0, "srv2", 4);

            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv mid pong failed\n");
            ok(fin && opcode == 0xa && plen == 3 && !memcmp(payload, "mid", 3),
               "unexpected mid pong fin=%d op=%x len=%lu\n", fin, opcode, plen);
            free(payload);

            /* 6. Close handshake */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "recv close failed\n");
            ok(fin && opcode == 0x8 && plen == 5, "unexpected close op=%x len=%lu\n", opcode, plen);
            ws_send_server_frame(c, TRUE, 0x8, payload, plen);
            free(payload);
        }
        else if (strstr(req, "GET /echo_async"))
        {
            /* Async test: echo 70KB + ping-with-payload + close */
            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "async recv 70KB failed\n");
            ok(fin && opcode == 0x2 && plen == 70 * 1024, "async unexpected 70KB op=%x len=%lu\n", opcode, plen);
            ws_send_server_frame(c, TRUE, 0x9, "async-ping", 10);
            ws_send_server_frame(c, TRUE, 0x2, payload, plen);
            free(payload);

            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "async recv pong failed\n");
            ok(fin && opcode == 0xa && plen == 10 && !memcmp(payload, "async-ping", 10),
               "async unexpected pong op=%x len=%lu\n", opcode, plen);
            free(payload);

            ok(ws_recv_client_frame(c, &fin, &opcode, &payload, &plen), "async recv close failed\n");
            ok(fin && opcode == 0x8, "async unexpected close op=%x\n", opcode);
            ws_send_server_frame(c, TRUE, 0x8, payload, plen);
            free(payload);
        }

        closesocket(c);
    }

    return 0;
}

static void test_websocket_sync(int port)
{
    HINTERNET session, connect, request, socket;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    DWORD err, size, status, ret_len;
    char buf[4096], *big_send, *big_recv;
    unsigned char *ubuf;
    USHORT close_status;
    BOOL ret;
    DWORD i, offset;

    session = WinHttpOpen(L"winetest-ws", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    ok(session != NULL, "WinHttpOpen failed: %lu\n", GetLastError());

    connect = WinHttpConnect(session, L"127.0.0.1", port, 0);
    ok(connect != NULL, "WinHttpConnect failed: %lu\n", GetLastError());

    request = WinHttpOpenRequest(connect, L"GET", L"/echo_sync", NULL, NULL, NULL, 0);
    ok(request != NULL, "WinHttpOpenRequest failed: %lu\n", GetLastError());

    ret = WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    ok(ret, "WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET failed: %lu\n", GetLastError());

    ret = WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0);
    ok(ret, "WinHttpSendRequest failed: %lu\n", GetLastError());

    ret = WinHttpReceiveResponse(request, NULL);
    ok(ret, "WinHttpReceiveResponse failed: %lu\n", GetLastError());

    size = sizeof(status);
    ret = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL);
    ok(ret && status == 101, "expected 101, got ret=%d status=%lu\n", ret, status);

    socket = pWinHttpWebSocketCompleteUpgrade(request, 0);
    ok(socket != NULL, "WinHttpWebSocketCompleteUpgrade failed: %lu\n", GetLastError());
    WinHttpCloseHandle(request);

    /* 1. Send masked "hello" & receive echo */
    err = pWinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void *)"hello", 5);
    ok(!err, "send hello failed: %lu\n", err);

    err = pWinHttpWebSocketReceive(socket, buf, sizeof(buf), &ret_len, &type);
    ok(!err, "recv hello failed: %lu\n", err);
    ok(ret_len == 5 && type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !memcmp(buf, "hello", 5),
       "unexpected hello echo len=%lu type=%u\n", ret_len, type);

    /* 2. Receive after server PING (with payload) and unsolicited PONG */
    err = pWinHttpWebSocketReceive(socket, buf, sizeof(buf), &ret_len, &type);
    ok(!err, "recv after-ping failed: %lu\n", err);
    ok(ret_len == 10 && type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !memcmp(buf, "after-ping", 10),
       "unexpected after-ping len=%lu type=%u\n", ret_len, type);

    /* 3. 1000-byte message (16-bit extended payload length) */
    for (i = 0; i < 1000; i++) buf[i] = (char)(i ^ 0x5a);
    err = pWinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, buf, 1000);
    ok(!err, "send 1000B failed: %lu\n", err);

    memset(buf + 1000, 0, 1000);
    err = pWinHttpWebSocketReceive(socket, buf + 1000, 1000, &ret_len, &type);
    ok(!err, "recv 1000B failed: %lu\n", err);
    ok(ret_len == 1000 && type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
       "unexpected 1000B len=%lu type=%u\n", ret_len, type);
    ok(!memcmp(buf, buf + 1000, 1000), "1000B echo mismatch\n");

    /* 4. 70KB message (64-bit extended length + > send_buffer_size 32KB) */
    big_send = malloc(70 * 1024);
    big_recv = calloc(1, 70 * 1024);
    ubuf = (unsigned char *)big_send;
    for (i = 0; i < 70 * 1024; i++) ubuf[i] = (unsigned char)((i * 7 + 13) & 0xff);

    err = pWinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, big_send, 70 * 1024);
    ok(!err, "send 70KB failed: %lu\n", err);

    offset = 0;
    do
    {
        err = pWinHttpWebSocketReceive(socket, big_recv + offset, 16384, &ret_len, &type);
        ok(!err, "recv 70KB chunk failed: %lu\n", err);
        ok(ret_len > 0, "zero-byte 70KB chunk\n");
        offset += ret_len;
        ok(offset <= 70 * 1024, "overflow offset %lu\n", offset);
    } while (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE);

    ok(type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, "expected BINARY_MESSAGE, got %u\n", type);
    ok(offset == 70 * 1024, "expected 71680 bytes, got %lu\n", offset);
    ok(!memcmp(big_send, big_recv, 70 * 1024), "70KB echo data mismatch\n");
    free(big_send);
    free(big_recv);

    /* 5. Fragmented send + fragmented receive with interleaved PING */
    err = pWinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE, (void *)"frag1-", 6);
    ok(!err, "send frag1 failed: %lu\n", err);
    err = pWinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void *)"frag2", 5);
    ok(!err, "send frag2 failed: %lu\n", err);

    err = pWinHttpWebSocketReceive(socket, buf, sizeof(buf), &ret_len, &type);
    ok(!err, "recv srv1 failed: %lu\n", err);
    ok(ret_len == 5 && type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && !memcmp(buf, "srv1-", 5),
       "unexpected srv1 len=%lu type=%u\n", ret_len, type);

    err = pWinHttpWebSocketReceive(socket, buf, sizeof(buf), &ret_len, &type);
    ok(!err, "recv srv2 failed: %lu\n", err);
    ok(ret_len == 4 && type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !memcmp(buf, "srv2", 4),
       "unexpected srv2 len=%lu type=%u\n", ret_len, type);

    /* 6. Close with status + reason */
    err = pWinHttpWebSocketClose(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, (void *)"bye", 3);
    ok(!err, "WinHttpWebSocketClose failed: %lu\n", err);

    close_status = 0;
    ret_len = 0;
    memset(buf, 0, sizeof(buf));
    err = pWinHttpWebSocketQueryCloseStatus(socket, &close_status, buf, sizeof(buf), &ret_len);
    ok(!err, "QueryCloseStatus failed: %lu\n", err);
    ok(close_status == 1000 && ret_len == 3 && !memcmp(buf, "bye", 3),
       "unexpected close status=%u len=%lu\n", close_status, ret_len);

    WinHttpCloseHandle(socket);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

static void test_websocket_buffered_and_empty_close(int port)
{
    HINTERNET session, connect, request, socket;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    DWORD err, ret_len;
    char buf[64];
    USHORT close_status;
    BOOL ret;

    session = WinHttpOpen(L"winetest-ws", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    connect = WinHttpConnect(session, L"127.0.0.1", port, 0);
    request = WinHttpOpenRequest(connect, L"GET", L"/buffered", NULL, NULL, NULL, 0);

    ret = WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    ok(ret, "set option failed: %lu\n", GetLastError());
    ret = WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0);
    ok(ret, "send failed: %lu\n", GetLastError());
    ret = WinHttpReceiveResponse(request, NULL);
    ok(ret, "recv resp failed: %lu\n", GetLastError());

    socket = pWinHttpWebSocketCompleteUpgrade(request, 0);
    ok(socket != NULL, "CompleteUpgrade failed: %lu\n", GetLastError());
    WinHttpCloseHandle(request);

    err = pWinHttpWebSocketReceive(socket, buf, sizeof(buf), &ret_len, &type);
    ok(!err, "recv buffered failed: %lu\n", err);
    ok(ret_len == 13 && type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && !memcmp(buf, "buffered-init", 13),
       "unexpected buffered len=%lu type=%u\n", ret_len, type);

    err = pWinHttpWebSocketClose(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    ok(!err, "close failed: %lu\n", err);

    close_status = 0;
    ret_len = 99;
    err = pWinHttpWebSocketQueryCloseStatus(socket, &close_status, NULL, 0, &ret_len);
    ok(!err, "QueryCloseStatus failed: %lu\n", err);
    ok(close_status == WINHTTP_WEB_SOCKET_EMPTY_CLOSE_STATUS && ret_len == 0,
       "expected 1005/0, got %u/%lu\n", close_status, ret_len);

    WinHttpCloseHandle(socket);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

struct async_ctx
{
    HANDLE event;
    DWORD last_status;
    DWORD last_err;
    DWORD last_bytes;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE last_type;
};

static void CALLBACK async_ws_cb(HINTERNET h, DWORD_PTR ctx, DWORD status, void *info, DWORD len)
{
    struct async_ctx *c = (struct async_ctx *)ctx;

    if (!c) return;
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE || status == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE)
    {
        WINHTTP_WEB_SOCKET_STATUS *ws = info;
        c->last_status = status;
        c->last_bytes = ws->dwBytesTransferred;
        c->last_type = ws->eBufferType;
        SetEvent(c->event);
    }
    else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
             status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
             status == WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE)
    {
        c->last_status = status;
        SetEvent(c->event);
    }
    else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
    {
        WINHTTP_ASYNC_RESULT *r = info;
        c->last_status = status;
        c->last_err = r->dwError;
        SetEvent(c->event);
    }
}

static void test_websocket_async(int port)
{
    struct async_ctx ctx;
    HINTERNET session, connect, request, socket;
    char *big_send, *big_recv;
    DWORD err, offset, i;
    BOOL ret;

    memset(&ctx, 0, sizeof(ctx));
    ctx.event = CreateEventW(NULL, FALSE, FALSE, NULL);

    session = WinHttpOpen(L"winetest-ws-async", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, WINHTTP_FLAG_ASYNC);
    ok(session != NULL, "WinHttpOpen failed\n");
    WinHttpSetStatusCallback(session, async_ws_cb, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);

    connect = WinHttpConnect(session, L"127.0.0.1", port, 0);
    request = WinHttpOpenRequest(connect, L"GET", L"/echo_async", NULL, NULL, NULL, 0);
    WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);

    ret = WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, (DWORD_PTR)&ctx);
    ok(ret, "WinHttpSendRequest failed: %lu\n", GetLastError());
    WaitForSingleObject(ctx.event, 5000);
    ok(ctx.last_status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, "status %#lx\n", ctx.last_status);

    ret = WinHttpReceiveResponse(request, NULL);
    ok(ret, "WinHttpReceiveResponse failed: %lu\n", GetLastError());
    WaitForSingleObject(ctx.event, 5000);
    ok(ctx.last_status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, "status %#lx\n", ctx.last_status);

    socket = pWinHttpWebSocketCompleteUpgrade(request, (DWORD_PTR)&ctx);
    ok(socket != NULL, "CompleteUpgrade failed\n");
    WinHttpCloseHandle(request);

    big_send = malloc(70 * 1024);
    big_recv = calloc(1, 70 * 1024);
    for (i = 0; i < 70 * 1024; i++) big_send[i] = (char)(i & 0xff);

    err = pWinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, big_send, 70 * 1024);
    ok(!err, "async send failed: %lu\n", err);
    WaitForSingleObject(ctx.event, 5000);
    ok(ctx.last_status == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE && ctx.last_bytes == 70 * 1024,
       "write status %#lx bytes %lu\n", ctx.last_status, ctx.last_bytes);

    offset = 0;
    do
    {
        err = pWinHttpWebSocketReceive(socket, big_recv + offset, 32768, NULL, NULL);
        ok(!err, "async recv failed: %lu\n", err);
        WaitForSingleObject(ctx.event, 5000);
        ok(ctx.last_status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE,
           "read status %#lx err %lu\n", ctx.last_status, ctx.last_err);
        offset += ctx.last_bytes;
    } while (ctx.last_type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE);

    ok(ctx.last_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE && offset == 70 * 1024,
       "async recv type=%u offset=%lu\n", ctx.last_type, offset);
    ok(!memcmp(big_send, big_recv, 70 * 1024), "async 70KB mismatch\n");

    err = pWinHttpWebSocketClose(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    ok(!err, "async close failed: %lu\n", err);
    WaitForSingleObject(ctx.event, 5000);
    ok(ctx.last_status == WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE, "close status %#lx\n", ctx.last_status);

    free(big_send);
    free(big_recv);
    WinHttpCloseHandle(socket);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    CloseHandle(ctx.event);
}

START_TEST(websocket)
{
    HMODULE mod = GetModuleHandleA("winhttp.dll");
    struct ws_server_info info;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    WSADATA wsa;
    HANDLE thread;

    pWinHttpWebSocketClose = (void *)GetProcAddress(mod, "WinHttpWebSocketClose");
    pWinHttpWebSocketCompleteUpgrade = (void *)GetProcAddress(mod, "WinHttpWebSocketCompleteUpgrade");
    pWinHttpWebSocketQueryCloseStatus = (void *)GetProcAddress(mod, "WinHttpWebSocketQueryCloseStatus");
    pWinHttpWebSocketReceive = (void *)GetProcAddress(mod, "WinHttpWebSocketReceive");
    pWinHttpWebSocketSend = (void *)GetProcAddress(mod, "WinHttpWebSocketSend");
    pWinHttpWebSocketShutdown = (void *)GetProcAddress(mod, "WinHttpWebSocketShutdown");

    if (!pWinHttpWebSocketCompleteUpgrade)
    {
        win_skip("WinHttpWebSocketCompleteUpgrade not supported\n");
        return;
    }

    WSAStartup(MAKEWORD(2, 2), &wsa);

    info.listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    ok(info.listen_sock != INVALID_SOCKET, "socket failed\n");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ok(!bind(info.listen_sock, (struct sockaddr *)&addr, sizeof(addr)), "bind failed\n");
    ok(!listen(info.listen_sock, 4), "listen failed\n");
    ok(!getsockname(info.listen_sock, (struct sockaddr *)&addr, &addrlen), "getsockname failed\n");
    info.port = ntohs(addr.sin_port);

    info.ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, ws_server_thread, &info, 0, NULL);
    ok(thread != NULL, "CreateThread failed\n");
    WaitForSingleObject(info.ready, 5000);

    test_websocket_sync(info.port);
    test_websocket_buffered_and_empty_close(info.port);
    test_websocket_async(info.port);

    closesocket(info.listen_sock);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    CloseHandle(info.ready);
    WSACleanup();
}
