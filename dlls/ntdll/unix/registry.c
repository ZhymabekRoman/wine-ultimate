/*
 * Registry functions
 *
 * Copyright 1999 Juergen Schmied
 * Copyright 2000 Alexandre Julliard
 * Copyright 2005 Ivan Leo Puoti, Laurent Pinchart
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
 *
 */

#if 0
#pragma makedep unix
#endif

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntstatus.h"
#include "winternl.h"
#include "unix_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(reg);

/* maximum length of a value name in bytes (without terminating null) */
#define MAX_VALUE_LENGTH (16383 * sizeof(WCHAR))

#define REG_SHM_MAGIC          0x52475348u
#define REG_SHM_VERSION        3u
#define REG_SHM_VAL_BUCKETS    2048u
#define REG_SHM_VAL_WAYS       4u
#define REG_SHM_VAL_SLOTS      (REG_SHM_VAL_BUCKETS * REG_SHM_VAL_WAYS)
#define REG_SHM_HANDLE_BUCKETS 1024u
#define REG_SHM_HANDLE_WAYS    4u
#define REG_SHM_HANDLE_SLOTS   (REG_SHM_HANDLE_BUCKETS * REG_SHM_HANDLE_WAYS)
#define REG_SHM_NEG_BUCKETS    512u
#define REG_SHM_NEG_WAYS       4u
#define REG_SHM_NEG_SLOTS      (REG_SHM_NEG_BUCKETS * REG_SHM_NEG_WAYS)
#define REG_SHM_VAL_MAX_NAME   128u
#define REG_SHM_VAL_MAX_DATA   256u
#define REG_SHM_NEG_MAX_NAME   256u
#define REG_SHM_ENUM_BUCKETS   8u
#define REG_SHM_ENUM_WAYS      2u
#define REG_SHM_ENUM_SLOTS     (REG_SHM_ENUM_BUCKETS * REG_SHM_ENUM_WAYS)
#define REG_SHM_ENUM_MAX_NAME  128u
#define REG_SHM_ENUM_MAX_CLASS 128u
#define REG_SHM_ENUM_MAX_DATA  1024u
#define REG_SHM_ENUM_MAX_VALS  3072u
#define REG_SHM_ENUM_MAX_SUBKEYS 24u
#define REG_SHM_KEY_BUCKETS    2048u
#define REG_SHM_KEY_WAYS       4u
#define REG_SHM_KEY_SLOTS      (REG_SHM_KEY_BUCKETS * REG_SHM_KEY_WAYS)
#define REG_SHM_ENUM_FLAG_VALS    0x01u
#define REG_SHM_ENUM_FLAG_SUBKEYS 0x02u
#define REG_SHM_ENUM_FLAG_SELF    0x04u

struct reg_shm_val_slot
{
    uint32_t seq;
    uint32_t status;
    uint64_t key_id;
    uint64_t epoch;
    uint32_t type;
    uint32_t name_len;
    uint32_t data_len;
    uint32_t pad;
    WCHAR    name[REG_SHM_VAL_MAX_NAME / sizeof(WCHAR)];
    BYTE     data[REG_SHM_VAL_MAX_DATA];
};

struct reg_shm_handle_slot
{
    uint32_t seq;
    uint32_t pid;
    uint32_t handle;
    uint32_t can_query;
    uint64_t key_id;
};

struct reg_shm_neg_open_slot
{
    uint32_t seq;
    uint32_t wow_flags;
    uint64_t parent_key_id;
    uint64_t tree_epoch;
    uint32_t name_len;
    uint32_t pad;
    WCHAR    name[REG_SHM_NEG_MAX_NAME / sizeof(WCHAR)];
};

struct reg_shm_enum_val
{
    uint32_t type;
    uint32_t name_len;
    uint32_t data_len;
    uint32_t pad;
    WCHAR    name[REG_SHM_ENUM_MAX_NAME / sizeof(WCHAR)];
    BYTE     data[REG_SHM_ENUM_MAX_DATA];
};

struct reg_shm_enum_sub
{
    int64_t  modif;
    uint32_t name_len;
    uint32_t class_len;
    WCHAR    name[REG_SHM_ENUM_MAX_NAME / sizeof(WCHAR)];
    WCHAR    class[REG_SHM_ENUM_MAX_CLASS / sizeof(WCHAR)];
};

struct reg_shm_key_slot
{
    uint32_t seq;
    uint32_t val_epoch;
    uint32_t tree_epoch;
    uint32_t pad;
    uint64_t key_id;
    uint64_t val_bloom;
    uint64_t subkey_bloom;
};

struct reg_shm_enum_slot
{
    uint32_t seq;
    uint32_t flags;
    uint64_t key_id;
    uint32_t val_epoch;
    uint32_t tree_epoch;
    int64_t  modif;
    uint32_t val_count;
    uint32_t subkey_count;
    uint32_t max_subkey;
    uint32_t max_class;
    uint32_t max_value;
    uint32_t max_data;
    uint32_t self_name_len;
    uint32_t self_class_len;
    uint32_t pad;
    WCHAR    self_name[REG_SHM_ENUM_MAX_NAME / sizeof(WCHAR)];
    WCHAR    self_class[REG_SHM_ENUM_MAX_CLASS / sizeof(WCHAR)];
    struct reg_shm_enum_val vals[REG_SHM_ENUM_MAX_VALS];
    struct reg_shm_enum_sub subkeys[REG_SHM_ENUM_MAX_SUBKEYS];
};

struct reg_shm
{
    uint32_t magic;
    uint32_t version;
    uint64_t val_epoch;
    uint64_t tree_epoch;
    uint32_t rr_val;
    uint32_t rr_handle;
    uint32_t rr_neg;
    uint32_t rr_enum;
    uint32_t rr_key;
    uint64_t key_gen;
    uint32_t pad[4];
    struct reg_shm_val_slot      val_table[REG_SHM_VAL_SLOTS];
    struct reg_shm_handle_slot   handle_map[REG_SHM_HANDLE_SLOTS];
    struct reg_shm_neg_open_slot neg_open_cache[REG_SHM_NEG_SLOTS];
    struct reg_shm_enum_slot     enum_table[REG_SHM_ENUM_SLOTS];
    struct reg_shm_key_slot      key_table[REG_SHM_KEY_SLOTS];
};

static const struct reg_shm *reg_shm = NULL;
static pthread_once_t reg_shm_once = PTHREAD_ONCE_INIT;

static void init_reg_shm(void)
{
    struct stat st;
    char path[1024];
    int fd;
    const struct reg_shm *ptr;

    if (!config_dir || stat( config_dir, &st ) == -1) return;
#ifdef __ANDROID__
    snprintf( path, sizeof(path), "%s/.wineserver/server-%llx-%llx/reg_shm",
              config_dir, (unsigned long long)st.st_dev, (unsigned long long)st.st_ino );
#else
    snprintf( path, sizeof(path), "/tmp/.wine-%u/server-%llx-%llx/reg_shm",
              getuid(), (unsigned long long)st.st_dev, (unsigned long long)st.st_ino );
#endif
    fd = open( path, O_RDONLY | O_CLOEXEC );
    if (fd == -1) return;
    if (fstat( fd, &st ) == -1 || st.st_size < (off_t)sizeof(struct reg_shm))
    {
        close( fd );
        return;
    }
    ptr = mmap( NULL, sizeof(struct reg_shm), PROT_READ, MAP_SHARED, fd, 0 );
    close( fd );
    if (ptr == MAP_FAILED) return;
    if (__atomic_load_n( &ptr->magic, __ATOMIC_ACQUIRE ) != REG_SHM_MAGIC ||
        ptr->version != REG_SHM_VERSION)
    {
        munmap( (void *)ptr, sizeof(struct reg_shm) );
        return;
    }
    __atomic_store_n( &reg_shm, ptr, __ATOMIC_RELEASE );
}

static inline const struct reg_shm *get_reg_shm(void)
{
    pthread_once( &reg_shm_once, init_reg_shm );
    return __atomic_load_n( &reg_shm, __ATOMIC_ACQUIRE );
}

static inline uint64_t reg_shm_hash_name_icase( uint64_t seed, const WCHAR *str, uint32_t len_bytes )
{
    uint64_t h = seed;
    uint32_t count = len_bytes / sizeof(WCHAR);

    while (count--)
    {
        WCHAR ch = *str++;
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        else if (ch >= 0x80) ch = 0x80;
        h ^= ch;
        h *= 0x100000001b3ULL;
    }
    return h;
}

static inline int reg_shm_eq_icase( const WCHAR *a, const WCHAR *b, uint32_t len_bytes )
{
    uint32_t count = len_bytes / sizeof(WCHAR);

    while (count--)
    {
        WCHAR ca = *a++;
        WCHAR cb = *b++;
        if (ca != cb && towlower(ca) != towlower(cb)) return 0;
    }
    return 1;
}

static inline uint64_t reg_shm_lookup_handle( const struct reg_shm *shm, HANDLE handle, uint32_t *can_query )
{
    uint32_t pid = HandleToULong( NtCurrentTeb()->ClientId.UniqueProcess );
    obj_handle_t hval = wine_server_obj_handle( handle );
    uint64_t h = ((uint64_t)pid * 0x9e3779b97f4a7c15ULL) ^ ((uint64_t)hval * 0xbf58476d1ce4e5b9ULL);
    uint32_t bucket = (uint32_t)(h & (REG_SHM_HANDLE_BUCKETS - 1)) * REG_SHM_HANDLE_WAYS;
    uint32_t i;

    if (!hval) return 0;
    for (i = 0; i < REG_SHM_HANDLE_WAYS; i++)
    {
        const struct reg_shm_handle_slot *slot = &shm->handle_map[bucket + i];
        int tries;
        for (tries = 0; tries < 4; tries++)
        {
            uint32_t s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
            uint32_t spid, shandle, scan;
            uint64_t skey_id;
            uint32_t s2;

            if (s1 & 1) continue;
            spid = slot->pid;
            shandle = slot->handle;
            scan = slot->can_query;
            skey_id = slot->key_id;
            __atomic_thread_fence( __ATOMIC_ACQUIRE );
            s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
            if (s1 != s2) continue;
            if (spid == pid && shandle == hval && skey_id)
            {
                if (can_query) *can_query = scan;
                return skey_id;
            }
            break;
        }
    }
    return 0;
}

static inline int reg_shm_lookup_neg_open( const struct reg_shm *shm, HANDLE root_dir,
                                           ACCESS_MASK access, const UNICODE_STRING *name )
{
    uint64_t parent_key_id = 0;
    uint32_t wow_flags, bucket, i;
    uint64_t epoch, h;

    if (!name || !name->Buffer || !name->Length || name->Length > REG_SHM_NEG_MAX_NAME) return 0;
    if (root_dir)
    {
        parent_key_id = reg_shm_lookup_handle( shm, root_dir, NULL );
        if (!parent_key_id) return 0;
    }

    wow_flags = (access & (KEY_WOW64_32KEY | KEY_WOW64_64KEY)) | (is_wow64() ? 1u : 0u);
    epoch = __atomic_load_n( &shm->tree_epoch, __ATOMIC_ACQUIRE );
    h = reg_shm_hash_name_icase( parent_key_id ^ ((uint64_t)wow_flags * 0x9e3779b97f4a7c15ULL),
                                 name->Buffer, name->Length );
    bucket = (uint32_t)(h & (REG_SHM_NEG_BUCKETS - 1)) * REG_SHM_NEG_WAYS;

    for (i = 0; i < REG_SHM_NEG_WAYS; i++)
    {
        const struct reg_shm_neg_open_slot *slot = &shm->neg_open_cache[bucket + i];
        int tries;
        for (tries = 0; tries < 4; tries++)
        {
            uint32_t s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
            uint32_t swow, snamelen, s2;
            uint64_t sparent, sepoch;
            WCHAR sname[REG_SHM_NEG_MAX_NAME / sizeof(WCHAR)];

            if (s1 & 1) continue;
            swow = slot->wow_flags;
            sparent = slot->parent_key_id;
            sepoch = slot->tree_epoch;
            snamelen = slot->name_len;
            if (snamelen > REG_SHM_NEG_MAX_NAME) break;
            if (snamelen) memcpy( sname, slot->name, snamelen );
            __atomic_thread_fence( __ATOMIC_ACQUIRE );
            s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
            if (s1 != s2) continue;
            if (sepoch == epoch && sparent == parent_key_id && swow == wow_flags &&
                snamelen == name->Length && reg_shm_eq_icase( sname, name->Buffer, snamelen ))
                return 1;
            break;
        }
    }
    return 0;
}

static inline const struct reg_shm_enum_slot *reg_shm_find_enum( const struct reg_shm *shm, uint64_t key_id )
{
    uint64_t h = key_id * 0x9e3779b97f4a7c15ULL;
    uint32_t bucket = (uint32_t)(h & (REG_SHM_ENUM_BUCKETS - 1)) * REG_SHM_ENUM_WAYS;
    uint32_t i;

    for (i = 0; i < REG_SHM_ENUM_WAYS; i++)
    {
        const struct reg_shm_enum_slot *slot = &shm->enum_table[bucket + i];
        if (__atomic_load_n( &slot->key_id, __ATOMIC_RELAXED ) == key_id) return slot;
    }
    return NULL;
}

static inline uint64_t reg_shm_bloom_mask( const WCHAR *str, uint32_t len_bytes )
{
    uint64_t h = reg_shm_hash_name_icase( 0x517cc1b727220a95ULL, str, len_bytes );
    return (1ULL << (h & 63)) | (1ULL << ((h >> 6) & 63));
}

static inline int reg_shm_key_info( const struct reg_shm *shm, uint64_t key_id,
                                    uint32_t *val_epoch, uint32_t *tree_epoch,
                                    uint64_t *val_bloom, uint64_t *subkey_bloom )
{
    uint64_t h = key_id * 0x9e3779b97f4a7c15ULL;
    uint32_t bucket = (uint32_t)(h & (REG_SHM_KEY_BUCKETS - 1)) * REG_SHM_KEY_WAYS;
    const struct reg_shm_key_slot *slot;
    uint32_t i, s1, s2;

    for (i = 0; i < REG_SHM_KEY_WAYS; i++)
    {
        int tries;
        slot = &shm->key_table[bucket + i];
        if (__atomic_load_n( &slot->key_id, __ATOMIC_RELAXED ) != key_id) continue;
        for (tries = 0; tries < 8; tries++)
        {
            s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
            if (s1 & 1) continue;
            __atomic_thread_fence( __ATOMIC_ACQUIRE );
            if (slot->key_id != key_id) break;
            if (val_epoch) *val_epoch = slot->val_epoch;
            if (tree_epoch) *tree_epoch = slot->tree_epoch;
            if (val_bloom) *val_bloom = slot->val_bloom;
            if (subkey_bloom) *subkey_bloom = slot->subkey_bloom;
            __atomic_thread_fence( __ATOMIC_ACQUIRE );
            s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
            if (s1 == s2) return 1;
        }
    }
    return 0;
}

static inline int reg_shm_key_epochs( const struct reg_shm *shm, uint64_t key_id,
                                      uint32_t *val_epoch, uint32_t *tree_epoch )
{
    return reg_shm_key_info( shm, key_id, val_epoch, tree_epoch, NULL, NULL );
}


NTSTATUS open_hkcu_key( const char *path, HANDLE *key )
{
    NTSTATUS status;
    char buffer[256];
    WCHAR bufferW[256];
    DWORD_PTR sid_data[(sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE) / sizeof(DWORD_PTR)];
    DWORD i, len = sizeof(sid_data);
    SID *sid;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attr;

    status = NtQueryInformationToken( GetCurrentThreadEffectiveToken(), TokenUser, sid_data, len, &len );
    if (status) return status;

    sid = ((TOKEN_USER *)sid_data)->User.Sid;
    len = snprintf( buffer, sizeof(buffer), "\\Registry\\User\\S-%u-%u", sid->Revision,
                   MAKELONG( MAKEWORD( sid->IdentifierAuthority.Value[5], sid->IdentifierAuthority.Value[4] ),
                             MAKEWORD( sid->IdentifierAuthority.Value[3], sid->IdentifierAuthority.Value[2] )));
    for (i = 0; i < sid->SubAuthorityCount; i++)
        len += snprintf( buffer + len, sizeof(buffer) - len, "-%u", sid->SubAuthority[i] );
    len += snprintf( buffer + len, sizeof(buffer) - len, "\\%s", path );

    ascii_to_unicode( bufferW, buffer, len + 1 );
    init_unicode_string( &name, bufferW );
    InitializeObjectAttributes( &attr, &name, OBJ_CASE_INSENSITIVE, 0, NULL );
    return NtCreateKey( key, KEY_ALL_ACCESS, &attr, 0, NULL, 0, NULL );
}


/******************************************************************************
 *              NtCreateKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateKey( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                             ULONG index, const UNICODE_STRING *class, ULONG options, ULONG *dispos )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    *key = 0;
    if (attr->Length != sizeof(OBJECT_ATTRIBUTES)) return STATUS_INVALID_PARAMETER;
    if (!attr->ObjectName->Length && !attr->RootDirectory) return STATUS_OBJECT_PATH_SYNTAX_BAD;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;
    objattr->attributes |= OBJ_OPENIF | OBJ_CASE_INSENSITIVE;

    TRACE( "(%p,%s,%s,%x,%x,%p)\n", attr->RootDirectory, debugstr_us(attr->ObjectName),
           debugstr_us(class), options, access, key );

    SERVER_START_REQ( create_key )
    {
        req->access     = access;
        req->options    = options;
        wine_server_add_data( req, objattr, len );
        if (class) wine_server_add_data( req, class->Buffer, class->Length );
        ret = wine_server_call( req );
        *key = wine_server_ptr_handle( reply->hkey );
    }
    SERVER_END_REQ;

    if (ret == STATUS_OBJECT_NAME_EXISTS)
    {
        if (dispos) *dispos = REG_OPENED_EXISTING_KEY;
        ret = STATUS_SUCCESS;
    }
    else if (ret == STATUS_SUCCESS)
    {
        if (dispos) *dispos = REG_CREATED_NEW_KEY;
    }

    TRACE( "<- %p\n", *key );
    free( objattr );
    return ret;
}


/******************************************************************************
 *              NtCreateKeyTransacted  (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateKeyTransacted( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                       ULONG index, const UNICODE_STRING *class, ULONG options,
                                       HANDLE transacted, ULONG *dispos )
{
    FIXME( "(%p,%s,%s,%x,%x,%p,%p)\n", attr->RootDirectory, debugstr_us(attr->ObjectName),
           debugstr_us(class), options, access, transacted, key );
    return STATUS_NOT_IMPLEMENTED;
}


/******************************************************************************
 *              NtOpenKeyEx  (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenKeyEx( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr, ULONG options )
{
    unsigned int ret;
    ULONG attributes;

    *key = 0;
    if (attr->Length != sizeof(*attr)) return STATUS_INVALID_PARAMETER;
    if (attr->ObjectName->Length & 1) return STATUS_OBJECT_NAME_INVALID;

    TRACE( "(%p,%s,%x,%p)\n", attr->RootDirectory, debugstr_us(attr->ObjectName), access, key );

    if (options & ~REG_OPTION_OPEN_LINK) FIXME( "options %x not implemented\n", options );

    attributes = attr->Attributes | OBJ_CASE_INSENSITIVE;

    if (!options && (attr->Attributes & ~(OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE)) == 0)
    {
        const struct reg_shm *shm = get_reg_shm();
        if (shm && reg_shm_lookup_neg_open( shm, attr->RootDirectory, access, attr->ObjectName ))
        {
            TRACE("<- neg_open_cache hit (STATUS_OBJECT_NAME_NOT_FOUND)\n");
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        if (shm && attr->RootDirectory && attr->ObjectName && attr->ObjectName->Buffer &&
            attr->ObjectName->Length > 0 && attr->ObjectName->Buffer[0] != '\\' &&
            !is_wow64() && !(access & KEY_WOW64_32KEY))
        {
            uint64_t parent_key_id = reg_shm_lookup_handle( shm, attr->RootDirectory, NULL );
            uint64_t sbloom;
            if (parent_key_id && reg_shm_key_info( shm, parent_key_id, NULL, NULL, NULL, &sbloom ))
            {
                if (sbloom == 0)
                {
                    TRACE("<- subkey_bloom empty hit (STATUS_OBJECT_NAME_NOT_FOUND)\n");
                    return STATUS_OBJECT_NAME_NOT_FOUND;
                }
                if (sbloom != ~0ULL)
                {
                    uint32_t i, count = attr->ObjectName->Length / sizeof(WCHAR);
                    int has_sep = 0;
                    for (i = 0; i < count; i++)
                    {
                        if (attr->ObjectName->Buffer[i] == '\\') { has_sep = 1; break; }
                    }
                    if (!has_sep)
                    {
                        uint64_t mask = reg_shm_bloom_mask( attr->ObjectName->Buffer, attr->ObjectName->Length );
                        if ((sbloom & mask) != mask)
                        {
                            TRACE("<- subkey_bloom hit (STATUS_OBJECT_NAME_NOT_FOUND)\n");
                            return STATUS_OBJECT_NAME_NOT_FOUND;
                        }
                    }
                }
            }
        }
    }

    SERVER_START_REQ( open_key )
    {
        req->parent     = wine_server_obj_handle( attr->RootDirectory );
        req->access     = access;
        req->attributes = attributes;
        wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *key = wine_server_ptr_handle( reply->hkey );
    }
    SERVER_END_REQ;
    TRACE("<- %p\n", *key);
    return ret;
}


/******************************************************************************
 *              NtOpenKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenKey( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    return NtOpenKeyEx( key, access, attr, 0 );
}


/******************************************************************************
 *              NtOpenKeyTransactedEx  (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenKeyTransactedEx( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                       ULONG options, HANDLE transaction )
{
    FIXME( "(%p %x %p %x %p) semi-stub\n", key, access, attr, options, transaction );
    return NtOpenKeyEx( key, access, attr, options );
}


/******************************************************************************
 *              NtOpenKeyTransacted  (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenKeyTransacted( HANDLE *key, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                     HANDLE transaction )
{
    return NtOpenKeyTransactedEx( key, access, attr, 0, transaction );
}


/******************************************************************************
 *              NtDeleteKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtDeleteKey( HANDLE key )
{
    unsigned int ret;

    TRACE( "(%p)\n", key );

    SERVER_START_REQ( delete_key )
    {
        req->hkey = wine_server_obj_handle( key );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtRenameKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtRenameKey( HANDLE key, UNICODE_STRING *name )
{
    unsigned int ret;

    TRACE( "(%p %s)\n", key, debugstr_us(name) );

    if (!name) return STATUS_ACCESS_VIOLATION;
    if (!name->Buffer || !name->Length) return STATUS_INVALID_PARAMETER;

    SERVER_START_REQ( rename_key )
    {
        req->hkey = wine_server_obj_handle( key );
        wine_server_add_data( req, name->Buffer, name->Length );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *     enumerate_key
 *
 * Implementation of NtQueryKey and NtEnumerateKey
 */
static NTSTATUS enumerate_key( HANDLE handle, int index, KEY_INFORMATION_CLASS info_class,
                               void *info, DWORD length, DWORD *result_len )

{
    unsigned int ret;
    void *data_ptr;
    size_t fixed_size;

    switch (info_class)
    {
    case KeyBasicInformation:  data_ptr = ((KEY_BASIC_INFORMATION *)info)->Name; break;
    case KeyFullInformation:   data_ptr = ((KEY_FULL_INFORMATION *)info)->Class; break;
    case KeyNodeInformation:   data_ptr = ((KEY_NODE_INFORMATION *)info)->Name;  break;
    case KeyNameInformation:   data_ptr = ((KEY_NAME_INFORMATION *)info)->Name;  break;
    case KeyCachedInformation: data_ptr = ((KEY_CACHED_INFORMATION *)info)+1;    break;
    default:
        FIXME( "Information class %d not implemented\n", info_class );
        return STATUS_INVALID_PARAMETER;
    }
    fixed_size = (char *)data_ptr - (char *)info;

    if (info_class != KeyNameInformation)
    {
        const struct reg_shm *shm = get_reg_shm();
        uint32_t can_query = 0;
        uint64_t key_id = shm ? reg_shm_lookup_handle( shm, handle, &can_query ) : 0;

        if (key_id && can_query)
        {
            const struct reg_shm_enum_slot *slot = reg_shm_find_enum( shm, key_id );
            int tries;

            for (tries = 0; slot && tries < 8; tries++)
            {
                uint32_t s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
                uint32_t flags, s2, snamelen = 0, sclasslen = 0;
                uint32_t subkey_count = 0, val_count = 0, max_subkey = 0, max_class = 0;
                uint32_t max_value = 0, max_data = 0, reply_size, total = 0;
                uint32_t kve, kte;
                uint64_t vepoch, tepoch;
                int64_t tmodif = 0;
                unsigned int avail;
                int nomore = 0;
                WCHAR sname[REG_SHM_ENUM_MAX_NAME / sizeof(WCHAR)];
                WCHAR sclass[REG_SHM_ENUM_MAX_CLASS / sizeof(WCHAR)];

                if (s1 & 1) continue;
                if ((info_class == KeyFullInformation || info_class == KeyCachedInformation) &&
                    index != (ULONG)-1) break;
                flags = slot->flags;
                vepoch = slot->val_epoch;
                tepoch = slot->tree_epoch;
                tmodif = slot->modif;

                if (index != (ULONG)-1)
                {
                    if (!(flags & REG_SHM_ENUM_FLAG_SUBKEYS)) break;
                    subkey_count = slot->subkey_count;
                    if (index >= subkey_count) { nomore = 1; }
                    else
                    {
                        snamelen = slot->subkeys[index].name_len;
                        sclasslen = slot->subkeys[index].class_len;
                        tmodif = slot->subkeys[index].modif;
                        if (snamelen > REG_SHM_ENUM_MAX_NAME || sclasslen > REG_SHM_ENUM_MAX_CLASS) break;
                        if (snamelen) memcpy( sname, slot->subkeys[index].name, snamelen );
                        if (sclasslen) memcpy( sclass, slot->subkeys[index].class, sclasslen );
                    }
                }
                else
                {
                    if (!(flags & REG_SHM_ENUM_FLAG_SELF)) break;
                    snamelen = slot->self_name_len;
                    sclasslen = slot->self_class_len;
                    if (snamelen > REG_SHM_ENUM_MAX_NAME || sclasslen > REG_SHM_ENUM_MAX_CLASS) break;
                    if (snamelen) memcpy( sname, slot->self_name, snamelen );
                    if (sclasslen) memcpy( sclass, slot->self_class, sclasslen );
                    if (info_class == KeyFullInformation || info_class == KeyCachedInformation)
                    {
                        if ((flags & (REG_SHM_ENUM_FLAG_VALS | REG_SHM_ENUM_FLAG_SUBKEYS)) !=
                            (REG_SHM_ENUM_FLAG_VALS | REG_SHM_ENUM_FLAG_SUBKEYS)) break;
                        val_count = slot->val_count;
                        subkey_count = slot->subkey_count;
                        max_subkey = slot->max_subkey;
                        max_class = slot->max_class;
                        max_value = slot->max_value;
                        max_data = slot->max_data;
                    }
                }
                __atomic_thread_fence( __ATOMIC_ACQUIRE );
                s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
                if (s1 != s2) continue;
                if (!reg_shm_key_epochs( shm, key_id, &kve, &kte )) break;
                if (tepoch != kte) break;
                if ((info_class == KeyFullInformation || info_class == KeyCachedInformation) &&
                    index == (ULONG)-1 && vepoch != kve) break;
                if (nomore) return STATUS_NO_MORE_ENTRIES;

                avail = length > fixed_size ? length - fixed_size : 0;
                switch (info_class)
                {
                case KeyBasicInformation:
                {
                    KEY_BASIC_INFORMATION keyinfo;
                    total = snamelen;
                    keyinfo.LastWriteTime.QuadPart = tmodif;
                    keyinfo.TitleIndex = 0;
                    keyinfo.NameLength = min( total, avail );
                    memcpy( info, &keyinfo, min( length, fixed_size ));
                    memcpy( data_ptr, sname, keyinfo.NameLength );
                    break;
                }

                case KeyNodeInformation:
                {
                    KEY_NODE_INFORMATION keyinfo;
                    total = snamelen + sclasslen;
                    reply_size = min( total, avail );
                    keyinfo.LastWriteTime.QuadPart = tmodif;
                    keyinfo.TitleIndex = 0;
                    keyinfo.NameLength = min( snamelen, reply_size );
                    if (snamelen < reply_size)
                    {
                        keyinfo.ClassLength = reply_size - snamelen;
                        keyinfo.ClassOffset = fixed_size + snamelen;
                    }
                    else
                    {
                        keyinfo.ClassLength = 0;
                        keyinfo.ClassOffset = -1;
                    }
                    memcpy( info, &keyinfo, min( length, fixed_size ));
                    memcpy( data_ptr, sname, keyinfo.NameLength );
                    if (keyinfo.ClassLength) memcpy( (char *)data_ptr + snamelen, sclass, keyinfo.ClassLength );
                    break;
                }

                case KeyFullInformation:
                {
                    KEY_FULL_INFORMATION keyinfo;
                    total = sclasslen;
                    reply_size = min( total, avail );
                    keyinfo.LastWriteTime.QuadPart = tmodif;
                    keyinfo.TitleIndex = 0;
                    keyinfo.ClassLength = reply_size;
                    keyinfo.ClassOffset = reply_size ? fixed_size : -1;
                    keyinfo.SubKeys = subkey_count;
                    keyinfo.MaxNameLen = max_subkey;
                    keyinfo.MaxClassLen = max_class;
                    keyinfo.Values = val_count;
                    keyinfo.MaxValueNameLen = max_value;
                    keyinfo.MaxValueDataLen = max_data;
                    memcpy( info, &keyinfo, min( length, fixed_size ));
                    if (reply_size) memcpy( data_ptr, sclass, reply_size );
                    break;
                }

                case KeyCachedInformation:
                {
                    KEY_CACHED_INFORMATION keyinfo;
                    total = snamelen;
                    keyinfo.LastWriteTime.QuadPart = tmodif;
                    keyinfo.TitleIndex = 0;
                    keyinfo.SubKeys = subkey_count;
                    keyinfo.MaxNameLen = max_subkey;
                    keyinfo.Values = val_count;
                    keyinfo.MaxValueNameLen = max_value;
                    keyinfo.MaxValueDataLen = max_data;
                    keyinfo.NameLength = min( total, avail );
                    memcpy( info, &keyinfo, min( length, fixed_size ));
                    memcpy( data_ptr, sname, keyinfo.NameLength );
                    break;
                }

                default:
                    break;
                }
                *result_len = fixed_size + total;
                if (length < fixed_size) return STATUS_BUFFER_TOO_SMALL;
                if (length < *result_len) return STATUS_BUFFER_OVERFLOW;
                return STATUS_SUCCESS;
            }
        }
    }

    SERVER_START_REQ( enum_key )
    {
        req->hkey       = wine_server_obj_handle( handle );
        req->index      = index;
        req->info_class = info_class;
        if (length > fixed_size) wine_server_set_reply( req, data_ptr, length - fixed_size );
        if (!(ret = wine_server_call( req )))
        {
            switch (info_class)
            {
            case KeyBasicInformation:
            {
                KEY_BASIC_INFORMATION keyinfo;
                keyinfo.LastWriteTime.QuadPart = reply->modif;
                keyinfo.TitleIndex = 0;
                keyinfo.NameLength = reply->namelen;
                memcpy( info, &keyinfo, min( length, fixed_size ) );
            break;
            }

            case KeyFullInformation:
            {
                KEY_FULL_INFORMATION keyinfo;
                keyinfo.LastWriteTime.QuadPart = reply->modif;
                keyinfo.TitleIndex = 0;
                keyinfo.ClassLength = wine_server_reply_size(reply);
                keyinfo.ClassOffset = keyinfo.ClassLength ? fixed_size : -1;
                keyinfo.SubKeys = reply->subkeys;
                keyinfo.MaxNameLen = reply->max_subkey;
                keyinfo.MaxClassLen = reply->max_class;
                keyinfo.Values = reply->values;
                keyinfo.MaxValueNameLen = reply->max_value;
                keyinfo.MaxValueDataLen = reply->max_data;
                memcpy( info, &keyinfo, min( length, fixed_size ) );
                break;
            }

            case KeyNodeInformation:
            {
                KEY_NODE_INFORMATION keyinfo;
                keyinfo.LastWriteTime.QuadPart = reply->modif;
                keyinfo.TitleIndex = 0;
                if (reply->namelen < wine_server_reply_size(reply))
                {
                    keyinfo.ClassLength = wine_server_reply_size(reply) - reply->namelen;
                    keyinfo.ClassOffset = fixed_size + reply->namelen;
                }
                else
                {
                    keyinfo.ClassLength = 0;
                    keyinfo.ClassOffset = -1;
                }
                keyinfo.NameLength = reply->namelen;
                memcpy( info, &keyinfo, min( length, fixed_size ) );
                break;
            }

            case KeyNameInformation:
            {
                KEY_NAME_INFORMATION keyinfo;
                keyinfo.NameLength = reply->namelen;
                memcpy( info, &keyinfo, min( length, fixed_size ) );
                break;
            }

            case KeyCachedInformation:
            {
                KEY_CACHED_INFORMATION keyinfo;
                keyinfo.LastWriteTime.QuadPart = reply->modif;
                keyinfo.TitleIndex = 0;
                keyinfo.SubKeys = reply->subkeys;
                keyinfo.MaxNameLen = reply->max_subkey;
                keyinfo.Values = reply->values;
                keyinfo.MaxValueNameLen = reply->max_value;
                keyinfo.MaxValueDataLen = reply->max_data;
                keyinfo.NameLength = reply->namelen;
                memcpy( info, &keyinfo, min( length, fixed_size ) );
                break;
            }

            default:
                break;
            }
            *result_len = fixed_size + reply->total;
            if (length < fixed_size) ret = STATUS_BUFFER_TOO_SMALL;
            else if (length < *result_len) ret = STATUS_BUFFER_OVERFLOW;
        }
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtEnumerateKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtEnumerateKey( HANDLE handle, ULONG index, KEY_INFORMATION_CLASS info_class,
                                void *info, DWORD length, DWORD *result_len )
{
    /* -1 means query key, so avoid it here */
    if (index == (ULONG)-1) return STATUS_NO_MORE_ENTRIES;
    return enumerate_key( handle, index, info_class, info, length, result_len );
}


/******************************************************************************
 *              NtQueryKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryKey( HANDLE handle, KEY_INFORMATION_CLASS info_class,
                            void *info, DWORD length, DWORD *result_len )
{
    return enumerate_key( handle, -1, info_class, info, length, result_len );
}


/******************************************************************************
 *              NtSetInformationKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtSetInformationKey( HANDLE key, int class, void *info, ULONG length )
{
    FIXME( "(%p,0x%08x,%p,0x%08x) stub\n", key, class, info, length );
    return STATUS_SUCCESS;
}


/* fill the key value info structure for a specific info class */
static void copy_key_value_info( KEY_VALUE_INFORMATION_CLASS info_class, void *info,
                                 DWORD length, int type, int name_len, int data_len )
{
    switch (info_class)
    {
    case KeyValueBasicInformation:
    {
        KEY_VALUE_BASIC_INFORMATION keyinfo;
        keyinfo.TitleIndex = 0;
        keyinfo.Type       = type;
        keyinfo.NameLength = name_len;
        length = min( length, (char *)keyinfo.Name - (char *)&keyinfo );
        memcpy( info, &keyinfo, length );
        break;
    }

    case KeyValueFullInformation:
    {
        KEY_VALUE_FULL_INFORMATION keyinfo;
        keyinfo.TitleIndex = 0;
        keyinfo.Type       = type;
        keyinfo.DataOffset = (char *)keyinfo.Name - (char *)&keyinfo + name_len;
        keyinfo.DataLength = data_len;
        keyinfo.NameLength = name_len;
        length = min( length, (char *)keyinfo.Name - (char *)&keyinfo );
        memcpy( info, &keyinfo, length );
        break;
    }

    case KeyValuePartialInformation:
    {
        KEY_VALUE_PARTIAL_INFORMATION keyinfo;
        keyinfo.TitleIndex = 0;
        keyinfo.Type       = type;
        keyinfo.DataLength = data_len;
        length = min( length, (char *)keyinfo.Data - (char *)&keyinfo );
        memcpy( info, &keyinfo, length );
        break;
    }

    case KeyValuePartialInformationAlign64:
    {
        KEY_VALUE_PARTIAL_INFORMATION_ALIGN64 keyinfo;
        keyinfo.Type       = type;
        keyinfo.DataLength = data_len;
        length = min( length, (char *)keyinfo.Data - (char *)&keyinfo );
        memcpy( info, &keyinfo, length );
        break;
    }

    default:
        break;
    }
}


/******************************************************************************
 *              NtEnumerateValueKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtEnumerateValueKey( HANDLE handle, ULONG index, KEY_VALUE_INFORMATION_CLASS info_class,
                                     void *info, DWORD length, DWORD *result_len )
{
    unsigned int ret;
    void *ptr;
    size_t fixed_size;

    TRACE( "(%p,%u,%d,%p,%d)\n", handle, index, info_class, info, length );

    /* compute the length we want to retrieve */
    switch (info_class)
    {
    case KeyValueBasicInformation:   ptr = ((KEY_VALUE_BASIC_INFORMATION *)info)->Name; break;
    case KeyValueFullInformation:    ptr = ((KEY_VALUE_FULL_INFORMATION *)info)->Name; break;
    case KeyValuePartialInformation: ptr = ((KEY_VALUE_PARTIAL_INFORMATION *)info)->Data; break;
    default:
        FIXME( "Information class %d not implemented\n", info_class );
        return STATUS_INVALID_PARAMETER;
    }
    fixed_size = (char *)ptr - (char *)info;

    {
        const struct reg_shm *shm = get_reg_shm();
        uint32_t can_query = 0;
        uint64_t key_id = shm ? reg_shm_lookup_handle( shm, handle, &can_query ) : 0;

        if (key_id && can_query)
        {
            const struct reg_shm_enum_slot *slot = reg_shm_find_enum( shm, key_id );
            int tries;

            for (tries = 0; slot && tries < 8; tries++)
            {
                uint32_t s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
                uint32_t flags, count, stype, snamelen, sdatalen, s2, total, avail;
                uint32_t kve, kte;
                uint64_t vepoch;
                WCHAR sname[REG_SHM_ENUM_MAX_NAME / sizeof(WCHAR)];
                BYTE sdata[REG_SHM_ENUM_MAX_DATA];

                if (s1 & 1) continue;
                vepoch = slot->val_epoch;
                flags = slot->flags;
                count = slot->val_count;
                if (!reg_shm_key_epochs( shm, key_id, &kve, &kte )) break;
                if (vepoch != kve) break;
                if (!(flags & REG_SHM_ENUM_FLAG_VALS)) break;
                if (index >= count)
                {
                    s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
                    if (s1 == s2) return STATUS_NO_MORE_ENTRIES;
                    continue;
                }
                stype = slot->vals[index].type;
                snamelen = slot->vals[index].name_len;
                sdatalen = slot->vals[index].data_len;
                if (snamelen > REG_SHM_ENUM_MAX_NAME || sdatalen > REG_SHM_ENUM_MAX_DATA) break;
                if (snamelen) memcpy( sname, slot->vals[index].name, snamelen );
                if (sdatalen) memcpy( sdata, slot->vals[index].data, sdatalen );
                __atomic_thread_fence( __ATOMIC_ACQUIRE );
                s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
                if (s1 != s2) continue;

                total = info_class == KeyValueBasicInformation ? snamelen :
                        info_class == KeyValueFullInformation ? snamelen + sdatalen : sdatalen;
                copy_key_value_info( info_class, info, length, stype, snamelen, sdatalen );
                avail = length > fixed_size ? length - fixed_size : 0;
                if (info_class != KeyValuePartialInformation)
                {
                    memcpy( ptr, sname, min( avail, snamelen ));
                    if (info_class == KeyValueFullInformation && avail > snamelen)
                        memcpy( (char *)ptr + snamelen, sdata, min( avail - snamelen, sdatalen ));
                }
                else memcpy( ptr, sdata, min( avail, sdatalen ));
                *result_len = fixed_size + total;
                if (length < *result_len) return STATUS_BUFFER_OVERFLOW;
                return STATUS_SUCCESS;
            }
        }
    }

    SERVER_START_REQ( enum_key_value )
    {
        req->hkey       = wine_server_obj_handle( handle );
        req->index      = index;
        req->info_class = info_class;
        if (length > fixed_size) wine_server_set_reply( req, ptr, length - fixed_size );
        if (!(ret = wine_server_call( req )))
        {
            copy_key_value_info( info_class, info, length, reply->type, reply->namelen,
                                 wine_server_reply_size(reply) - reply->namelen );
            *result_len = fixed_size + reply->total;
            if (length < *result_len) ret = STATUS_BUFFER_OVERFLOW;
        }
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtQueryValueKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryValueKey( HANDLE handle, const UNICODE_STRING *name,
                                 KEY_VALUE_INFORMATION_CLASS info_class,
                                 void *info, DWORD length, DWORD *result_len )
{
    unsigned int ret;
    UCHAR *data_ptr;
    unsigned int fixed_size, min_size;

    TRACE( "(%p,%s,%d,%p,%d)\n", handle, debugstr_us(name), info_class, info, length );

    if (name->Length > MAX_VALUE_LENGTH) return STATUS_OBJECT_NAME_NOT_FOUND;

    /* compute the length we want to retrieve */
    switch(info_class)
    {
    case KeyValueBasicInformation:
    {
        KEY_VALUE_BASIC_INFORMATION *basic_info = info;
        min_size = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name);
        fixed_size = min_size + name->Length;
        if (min_size < length)
            memcpy(basic_info->Name, name->Buffer, min(length - min_size, name->Length));
        data_ptr = NULL;
        break;
    }

    case KeyValueFullInformation:
    {
        KEY_VALUE_FULL_INFORMATION *full_info = info;
        min_size = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name);
        fixed_size = min_size + name->Length;
        if (min_size < length)
            memcpy(full_info->Name, name->Buffer, min(length - min_size, name->Length));
        data_ptr = (UCHAR *)full_info->Name + name->Length;
        break;
    }

    case KeyValuePartialInformation:
        min_size = fixed_size = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
        data_ptr = ((KEY_VALUE_PARTIAL_INFORMATION *)info)->Data;
        break;

    case KeyValuePartialInformationAlign64:
        min_size = fixed_size = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data);
        data_ptr = ((KEY_VALUE_PARTIAL_INFORMATION_ALIGN64 *)info)->Data;
        break;

    default:
        FIXME( "Information class %d not implemented\n", info_class );
        return STATUS_INVALID_PARAMETER;
    }

    {
        const struct reg_shm *shm = get_reg_shm();
        uint32_t can_query = 0, kve = 0;
        uint64_t vbloom = ~0ULL;
        uint64_t key_id = shm ? reg_shm_lookup_handle( shm, handle, &can_query ) : 0;

        if (key_id && can_query && reg_shm_key_info( shm, key_id, &kve, NULL, &vbloom, NULL ))
        {
            if (vbloom != ~0ULL)
            {
                uint64_t mask = reg_shm_bloom_mask( name->Buffer, name->Length );
                if ((vbloom & mask) != mask)
                    return STATUS_OBJECT_NAME_NOT_FOUND;
            }
            if (name->Length <= REG_SHM_VAL_MAX_NAME && kve)
            {
                uint64_t bulk_epoch = __atomic_load_n( &shm->val_epoch, __ATOMIC_ACQUIRE );
                uint64_t epoch = ((uint64_t)(uint32_t)bulk_epoch << 32) | kve;
                uint64_t h = reg_shm_hash_name_icase( key_id, name->Buffer, name->Length );
                uint32_t bucket = (uint32_t)(h & (REG_SHM_VAL_BUCKETS - 1)) * REG_SHM_VAL_WAYS;
                uint32_t i;

                for (i = 0; i < REG_SHM_VAL_WAYS; i++)
                {
                    const struct reg_shm_val_slot *slot = &shm->val_table[bucket + i];
                    int tries;
                    for (tries = 0; tries < 4; tries++)
                    {
                        uint32_t s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
                        uint32_t sstatus, stype, snamelen, sdatalen, s2;
                        uint64_t skey_id, sepoch;
                        WCHAR sname[REG_SHM_VAL_MAX_NAME / sizeof(WCHAR)];
                        BYTE sdata[REG_SHM_VAL_MAX_DATA];

                        if (s1 & 1) continue;
                        sstatus = slot->status;
                        skey_id = slot->key_id;
                        sepoch = slot->epoch;
                        stype = slot->type;
                        snamelen = slot->name_len;
                        sdatalen = slot->data_len;
                        if (snamelen > REG_SHM_VAL_MAX_NAME) break;
                        if (snamelen) memcpy( sname, slot->name, snamelen );
                        if (sstatus == STATUS_SUCCESS && sdatalen && info_class != KeyValueBasicInformation && length > fixed_size)
                            memcpy( sdata, slot->data, min( sdatalen, REG_SHM_VAL_MAX_DATA ) );
                        __atomic_thread_fence( __ATOMIC_ACQUIRE );
                        s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
                        if (s1 != s2) continue;

                        if (sepoch == epoch && skey_id == key_id && snamelen == name->Length &&
                            (!snamelen || reg_shm_eq_icase( sname, name->Buffer, snamelen )))
                        {
                            if (sstatus == STATUS_OBJECT_NAME_NOT_FOUND)
                                return STATUS_OBJECT_NAME_NOT_FOUND;
                            if (sstatus == STATUS_SUCCESS &&
                                (info_class == KeyValueBasicInformation || length <= fixed_size || sdatalen <= REG_SHM_VAL_MAX_DATA))
                            {
                                copy_key_value_info( info_class, info, length, stype, name->Length, sdatalen );
                                *result_len = fixed_size + (info_class == KeyValueBasicInformation ? 0 : sdatalen);
                                if (length < min_size) return STATUS_BUFFER_TOO_SMALL;
                                if (length > fixed_size && data_ptr && sdatalen)
                                    memcpy( data_ptr, sdata, min( length - fixed_size, sdatalen ) );
                                if (length < *result_len) return STATUS_BUFFER_OVERFLOW;
                                return STATUS_SUCCESS;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    SERVER_START_REQ( get_key_value )
    {
        req->hkey = wine_server_obj_handle( handle );
        wine_server_add_data( req, name->Buffer, name->Length );
        if (length > fixed_size && data_ptr) wine_server_set_reply( req, data_ptr, length - fixed_size );
        if (!(ret = wine_server_call( req )))
        {
            copy_key_value_info( info_class, info, length, reply->type,
                                 name->Length, reply->total );
            *result_len = fixed_size + (info_class == KeyValueBasicInformation ? 0 : reply->total);
            if (length < min_size) ret = STATUS_BUFFER_TOO_SMALL;
            else if (length < *result_len) ret = STATUS_BUFFER_OVERFLOW;
        }
    }
    SERVER_END_REQ;
    return ret;
}

#ifdef __x86_64__
static inline void reg_fast_memcpy( void *dst, const void *src, size_t n )
{
    void *d = dst;
    const void *s = src;
    __asm__ volatile( "rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory" );
}

NTSTATUS __attribute__((ms_abi)) fast_leaf_NtQueryValueKey( HANDLE handle, const UNICODE_STRING *name,
                                                            KEY_VALUE_INFORMATION_CLASS info_class,
                                                            void *info, ULONG_PTR _p5, ULONG_PTR _p6,
                                                            ULONG_PTR _p7, ULONG_PTR _p8,
                                                            ULONG_PTR _p9, ULONG_PTR _p10,
                                                            DWORD length, DWORD *result_len )
{
    const struct reg_shm *shm;
    uint32_t can_query = 0, kve = 0;
    uint64_t key_id, epoch, h, vbloom = ~0ULL;
    uint32_t bucket, i;
    UCHAR *data_ptr;
    unsigned int fixed_size, min_size;

    if ((ULONG_PTR)name < 0x10000 || (ULONG_PTR)result_len < 0x10000)
        return STATUS_FAST_LEAF_FALLBACK;
    if (length && (ULONG_PTR)info < 0x10000)
        return STATUS_FAST_LEAF_FALLBACK;
    if (name->Length > MAX_VALUE_LENGTH) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (name->Length > REG_SHM_VAL_MAX_NAME) return STATUS_FAST_LEAF_FALLBACK;
    if (name->Length && (ULONG_PTR)name->Buffer < 0x10000) return STATUS_FAST_LEAF_FALLBACK;

    switch (info_class)
    {
    case KeyValueBasicInformation:
        min_size = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name);
        fixed_size = min_size + name->Length;
        data_ptr = NULL;
        break;
    case KeyValueFullInformation:
        min_size = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name);
        fixed_size = min_size + name->Length;
        data_ptr = (UCHAR *)((KEY_VALUE_FULL_INFORMATION *)info)->Name + name->Length;
        break;
    case KeyValuePartialInformation:
        min_size = fixed_size = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
        data_ptr = ((KEY_VALUE_PARTIAL_INFORMATION *)info)->Data;
        break;
    case KeyValuePartialInformationAlign64:
        min_size = fixed_size = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data);
        data_ptr = ((KEY_VALUE_PARTIAL_INFORMATION_ALIGN64 *)info)->Data;
        break;
    default:
        return STATUS_FAST_LEAF_FALLBACK;
    }

    shm = __atomic_load_n( &reg_shm, __ATOMIC_ACQUIRE );
    if (!shm) return STATUS_FAST_LEAF_FALLBACK;

    key_id = reg_shm_lookup_handle( shm, handle, &can_query );
    if (!key_id || !can_query) return STATUS_FAST_LEAF_FALLBACK;
    if (!reg_shm_key_info( shm, key_id, &kve, NULL, &vbloom, NULL ) || !kve)
        return STATUS_FAST_LEAF_FALLBACK;

    if (vbloom != ~0ULL)
    {
        uint64_t mask = reg_shm_bloom_mask( name->Buffer, name->Length );
        if ((vbloom & mask) != mask)
        {
            if (info_class == KeyValueBasicInformation && min_size < length)
                reg_fast_memcpy( ((KEY_VALUE_BASIC_INFORMATION *)info)->Name, name->Buffer, min( length - min_size, name->Length ) );
            else if (info_class == KeyValueFullInformation && min_size < length)
                reg_fast_memcpy( ((KEY_VALUE_FULL_INFORMATION *)info)->Name, name->Buffer, min( length - min_size, name->Length ) );
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    }

    epoch = ((uint64_t)(uint32_t)__atomic_load_n( &shm->val_epoch, __ATOMIC_ACQUIRE ) << 32) | kve;
    h = reg_shm_hash_name_icase( key_id, name->Buffer, name->Length );
    bucket = (uint32_t)(h & (REG_SHM_VAL_BUCKETS - 1)) * REG_SHM_VAL_WAYS;

    for (i = 0; i < REG_SHM_VAL_WAYS; i++)
    {
        const struct reg_shm_val_slot *slot = &shm->val_table[bucket + i];
        int tries;
        for (tries = 0; tries < 4; tries++)
        {
            uint32_t s1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
            uint32_t sstatus, stype, snamelen, sdatalen, s2;
            uint64_t skey_id, sepoch;
            WCHAR sname[REG_SHM_VAL_MAX_NAME / sizeof(WCHAR)];
            BYTE sdata[REG_SHM_VAL_MAX_DATA];

            if (s1 & 1) continue;
            sstatus = slot->status;
            skey_id = slot->key_id;
            sepoch = slot->epoch;
            stype = slot->type;
            snamelen = slot->name_len;
            sdatalen = slot->data_len;
            if (snamelen > REG_SHM_VAL_MAX_NAME) break;
            if (snamelen) reg_fast_memcpy( sname, slot->name, snamelen );
            if (sstatus == STATUS_SUCCESS && sdatalen && info_class != KeyValueBasicInformation && length > fixed_size)
                reg_fast_memcpy( sdata, slot->data, min( sdatalen, REG_SHM_VAL_MAX_DATA ) );
            __atomic_thread_fence( __ATOMIC_ACQUIRE );
            s2 = __atomic_load_n( &slot->seq, __ATOMIC_RELAXED );
            if (s1 != s2) continue;

            if (sepoch == epoch && skey_id == key_id && snamelen == name->Length &&
                (!snamelen || reg_shm_eq_icase( sname, name->Buffer, snamelen )))
            {
                if (sstatus == STATUS_OBJECT_NAME_NOT_FOUND)
                {
                    if (info_class == KeyValueBasicInformation && min_size < length)
                        reg_fast_memcpy( ((KEY_VALUE_BASIC_INFORMATION *)info)->Name, name->Buffer, min( length - min_size, name->Length ) );
                    else if (info_class == KeyValueFullInformation && min_size < length)
                        reg_fast_memcpy( ((KEY_VALUE_FULL_INFORMATION *)info)->Name, name->Buffer, min( length - min_size, name->Length ) );
                    return STATUS_OBJECT_NAME_NOT_FOUND;
                }
                if (sstatus == STATUS_SUCCESS &&
                    (info_class == KeyValueBasicInformation || length <= fixed_size || sdatalen <= REG_SHM_VAL_MAX_DATA))
                {
                    switch (info_class)
                    {
                    case KeyValueBasicInformation:
                    {
                        KEY_VALUE_BASIC_INFORMATION keyinfo;
                        keyinfo.TitleIndex = 0;
                        keyinfo.Type       = stype;
                        keyinfo.NameLength = name->Length;
                        reg_fast_memcpy( info, &keyinfo, min( length, min_size ) );
                        if (min_size < length)
                            reg_fast_memcpy( ((KEY_VALUE_BASIC_INFORMATION *)info)->Name, name->Buffer, min( length - min_size, name->Length ) );
                        break;
                    }
                    case KeyValueFullInformation:
                    {
                        KEY_VALUE_FULL_INFORMATION keyinfo;
                        keyinfo.TitleIndex = 0;
                        keyinfo.Type       = stype;
                        keyinfo.DataOffset = min_size + name->Length;
                        keyinfo.DataLength = sdatalen;
                        keyinfo.NameLength = name->Length;
                        reg_fast_memcpy( info, &keyinfo, min( length, min_size ) );
                        if (min_size < length)
                            reg_fast_memcpy( ((KEY_VALUE_FULL_INFORMATION *)info)->Name, name->Buffer, min( length - min_size, name->Length ) );
                        break;
                    }
                    case KeyValuePartialInformation:
                    {
                        KEY_VALUE_PARTIAL_INFORMATION keyinfo;
                        keyinfo.TitleIndex = 0;
                        keyinfo.Type       = stype;
                        keyinfo.DataLength = sdatalen;
                        reg_fast_memcpy( info, &keyinfo, min( length, min_size ) );
                        break;
                    }
                    case KeyValuePartialInformationAlign64:
                    {
                        KEY_VALUE_PARTIAL_INFORMATION_ALIGN64 keyinfo;
                        keyinfo.Type       = stype;
                        keyinfo.DataLength = sdatalen;
                        reg_fast_memcpy( info, &keyinfo, min( length, min_size ) );
                        break;
                    }
                    default:
                        break;
                    }
                    *result_len = fixed_size + (info_class == KeyValueBasicInformation ? 0 : sdatalen);
                    if (length < min_size) return STATUS_BUFFER_TOO_SMALL;
                    if (length > fixed_size && data_ptr && sdatalen)
                        reg_fast_memcpy( data_ptr, sdata, min( length - fixed_size, sdatalen ) );
                    if (length < *result_len) return STATUS_BUFFER_OVERFLOW;
                    return STATUS_SUCCESS;
                }
            }
            break;
        }
    }
    return STATUS_FAST_LEAF_FALLBACK;
}
#endif


/******************************************************************************
 *              NtQueryMultipleValueKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryMultipleValueKey( HANDLE key, KEY_MULTIPLE_VALUE_INFORMATION *info,
                                         ULONG count, void *buffer, ULONG length, ULONG *retlen )
{
    FIXME( "(%p,%p,0x%08x,%p,0x%08x,%p) stub!\n", key, info, count, buffer, length, retlen );
    return STATUS_SUCCESS;
}


/******************************************************************************
 *              NtSetValueKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtSetValueKey( HANDLE key, const UNICODE_STRING *name, ULONG index,
                               ULONG type, const void *data, ULONG count )
{
    unsigned int ret;

    TRACE( "(%p,%s,%d,%p,%d)\n", key, debugstr_us(name), type, data, count );

    if (name->Length > MAX_VALUE_LENGTH) return STATUS_INVALID_PARAMETER;

    SERVER_START_REQ( set_key_value )
    {
        req->hkey    = wine_server_obj_handle( key );
        req->type    = type;
        req->namelen = name->Length;
        wine_server_add_data( req, name->Buffer, name->Length );
        wine_server_add_data( req, data, count );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtDeleteValueKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtDeleteValueKey( HANDLE key, const UNICODE_STRING *name )
{
    unsigned int ret;

    TRACE( "(%p,%s)\n", key, debugstr_us(name) );

    if (name->Length > MAX_VALUE_LENGTH) return STATUS_OBJECT_NAME_NOT_FOUND;

    SERVER_START_REQ( delete_key_value )
    {
        req->hkey = wine_server_obj_handle( key );
        wine_server_add_data( req, name->Buffer, name->Length );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtNotifyChangeMultipleKeys  (NTDLL.@)
 */
NTSTATUS WINAPI NtNotifyChangeMultipleKeys( HANDLE key, ULONG count, OBJECT_ATTRIBUTES *attr,
                                            HANDLE event, PIO_APC_ROUTINE apc, void *apc_context,
                                            IO_STATUS_BLOCK *io, ULONG filter, BOOLEAN subtree,
                                            void *buffer, ULONG length, BOOLEAN async )
{
    unsigned int ret;

    TRACE( "(%p,%u,%p,%p,%p,%p,%p,0x%08x, 0x%08x,%p,0x%08x,0x%08x)\n",
           key, count, attr, event, apc, apc_context, io,
           filter, async, buffer, length, subtree );

    if (count || attr || apc || apc_context || buffer || length)
        FIXME( "Unimplemented optional parameter\n" );

    if (!async)
    {
        OBJECT_ATTRIBUTES attr;
        InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
        ret = NtCreateEvent( &event, EVENT_ALL_ACCESS, &attr, SynchronizationEvent, FALSE );
        if (ret) return ret;
    }

    SERVER_START_REQ( set_registry_notification )
    {
        req->hkey    = wine_server_obj_handle( key );
        req->event   = wine_server_obj_handle( event );
        req->subtree = subtree;
        req->filter  = filter;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;

    if (!async)
    {
        if (ret == STATUS_PENDING) ret = NtWaitForSingleObject( event, FALSE, NULL );
        NtClose( event );
    }
    return ret;
}


/******************************************************************************
 *              NtNotifyChangeKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtNotifyChangeKey( HANDLE key, HANDLE event, PIO_APC_ROUTINE apc, void *apc_context,
                                   IO_STATUS_BLOCK *io, ULONG filter, BOOLEAN subtree,
                                   void *buffer, ULONG length, BOOLEAN async )
{
    return NtNotifyChangeMultipleKeys( key, 0, NULL, event, apc, apc_context,
                                       io, filter, subtree, buffer, length, async );
}


/******************************************************************************
 *              NtFlushKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtFlushKey( HANDLE key )
{
    unsigned int ret;

    TRACE( "key=%p\n", key );

    SERVER_START_REQ( flush_key )
    {
	req->hkey = wine_server_obj_handle( key );
	ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtLoadKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtLoadKey( const OBJECT_ATTRIBUTES *attr, OBJECT_ATTRIBUTES *file )
{
    TRACE( "(%p,%p)\n", attr, file );
    return NtLoadKeyEx( attr, file, 0, 0, 0, 0, NULL, NULL );
}

/******************************************************************************
 *              NtLoadKey2  (NTDLL.@)
 */
NTSTATUS WINAPI NtLoadKey2( const OBJECT_ATTRIBUTES *attr, OBJECT_ATTRIBUTES *file, ULONG flags )
{
    return NtLoadKeyEx( attr, file, flags, 0, 0, 0, NULL, NULL );
}

/******************************************************************************
 *              NtLoadKeyEx  (NTDLL.@)
 */
NTSTATUS WINAPI NtLoadKeyEx( const OBJECT_ATTRIBUTES *attr, OBJECT_ATTRIBUTES *file, ULONG flags, HANDLE trustkey,
                             HANDLE event, ACCESS_MASK access, HANDLE *roothandle, IO_STATUS_BLOCK *iostatus )
{
    unsigned int ret;
    HANDLE key;
    data_size_t len;
    struct object_attributes *objattr;
    char *unix_name;
    UNICODE_STRING nt_name;
    OBJECT_ATTRIBUTES new_attr = *file;

    TRACE( "(%p,%p,0x%x,%p,%p,0x%x,%p,%p)\n",
           attr, file, flags, trustkey, event, access, roothandle, iostatus );

    if (flags) FIXME( "flags %x not handled\n", flags );
    if (trustkey) FIXME("trustkey parameter not supported\n");
    if (event) FIXME("event parameter not supported\n");
    if (access) FIXME("access parameter not supported\n");
    if (roothandle) FIXME("roothandle is not filled\n");
    if (iostatus) FIXME("iostatus is not filled\n");

    if (!(ret = get_nt_and_unix_names( &new_attr, &nt_name, &unix_name, FILE_OPEN, FALSE )))
    {
        ret = open_unix_file( &key, unix_name, GENERIC_READ | SYNCHRONIZE,
                              &new_attr, 0, 0, FILE_OPEN, 0, NULL, 0 );
    }
    free( unix_name );
    free( nt_name.Buffer );

    if (ret) return ret;

    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;
    objattr->attributes |= OBJ_OPENIF | OBJ_CASE_INSENSITIVE;

    SERVER_START_REQ( load_registry )
    {
        req->file = wine_server_obj_handle( key );
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        if (ret == STATUS_OBJECT_NAME_EXISTS) ret = STATUS_SUCCESS;
    }
    SERVER_END_REQ;

    NtClose( key );
    free( objattr );
    return ret;
}

/******************************************************************************
 *              NtUnloadKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtUnloadKey( OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    TRACE( "(%p)\n", attr );

    if (!attr || !attr->ObjectName) return STATUS_ACCESS_VIOLATION;
    if (attr->Length != sizeof(*attr)) return STATUS_INVALID_PARAMETER;
    if (attr->ObjectName->Length & 1) return STATUS_OBJECT_NAME_INVALID;

    SERVER_START_REQ( unload_registry )
    {
        req->parent     = wine_server_obj_handle( attr->RootDirectory );
        req->attributes = attr->Attributes;
        wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call(req);
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtSaveKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtSaveKey( HANDLE key, HANDLE file )
{
    unsigned int ret;

    TRACE( "(%p,%p)\n", key, file );

    SERVER_START_REQ( save_registry )
    {
        req->hkey = wine_server_obj_handle( key );
        req->file = wine_server_obj_handle( file );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtRestoreKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtRestoreKey( HANDLE key, HANDLE file, ULONG flags )
{
    FIXME( "(%p,%p,0x%08x) stub\n", key, file, flags );
    return STATUS_SUCCESS;
}


/******************************************************************************
 *              NtReplaceKey  (NTDLL.@)
 */
NTSTATUS WINAPI NtReplaceKey( OBJECT_ATTRIBUTES *attr, HANDLE key, OBJECT_ATTRIBUTES *replace )
{
    FIXME( "(%s,%p,%s),stub!\n", debugstr_us(attr->ObjectName), key, debugstr_us(replace->ObjectName) );
    return STATUS_SUCCESS;
}

/******************************************************************************
 *              NtQueryLicenseValue  (NTDLL.@)
 *
 * NOTES
 *  On Windows all license properties are stored in a single key, but
 *  unless there is some app which explicitly depends on that, there is
 *  no good reason to reproduce that.
 */
NTSTATUS WINAPI NtQueryLicenseValue( const UNICODE_STRING *name, ULONG *type,
                                     void *data, ULONG length, ULONG *retlen )
{
    static const WCHAR nameW[] = {'\\','R','e','g','i','s','t','r','y','\\',
                                  'M','a','c','h','i','n','e','\\',
                                  'S','o','f','t','w','a','r','e','\\',
                                  'W','i','n','e','\\','L','i','c','e','n','s','e',
                                  'I','n','f','o','r','m','a','t','i','o','n',0};
    UNICODE_STRING keyW = RTL_CONSTANT_STRING( nameW );
    KEY_VALUE_PARTIAL_INFORMATION *info;
    NTSTATUS status = STATUS_OBJECT_NAME_NOT_FOUND;
    DWORD info_length, count;
    OBJECT_ATTRIBUTES attr;
    HANDLE key;

    if (!name || !name->Buffer || !name->Length || !retlen) return STATUS_INVALID_PARAMETER;

    info_length = FIELD_OFFSET( KEY_VALUE_PARTIAL_INFORMATION, Data ) + length;
    if (!(info = malloc( info_length ))) return STATUS_NO_MEMORY;

    InitializeObjectAttributes( &attr, &keyW, 0, 0, NULL );

    /* @@ Wine registry key: HKLM\Software\Wine\LicenseInformation */
    if (!NtOpenKey( &key, KEY_READ, &attr ))
    {
        status = NtQueryValueKey( key, name, KeyValuePartialInformation, info, info_length, &count );
        if (!status || status == STATUS_BUFFER_OVERFLOW)
        {
            if (type) *type = info->Type;
            *retlen = info->DataLength;
            if (status == STATUS_BUFFER_OVERFLOW)
                status = STATUS_BUFFER_TOO_SMALL;
            else
                memcpy( data, info->Data, info->DataLength );
        }
        NtClose( key );
    }

    if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        FIXME( "License key %s not found\n", debugstr_w(name->Buffer) );

    free( info );
    return status;
}
