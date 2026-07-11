#include "vb_runtime_package.h"

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dfs_posix.h>

#define VB_PACKAGE_MAX_APP_ID 16
#define VB_PACKAGE_MAX_PATH 160
#define VB_PACKAGE_MAX_REL_PATH 96
#define VB_PACKAGE_MAX_MANIFEST 16384
#define VB_PACKAGE_SHA256_BYTES 32
#define VB_PACKAGE_SHA256_HEX_LEN 64
#define VB_PACKAGE_HASH_BUFFER 512

typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} vb_sha256_ctx_t;

static const uint32_t vb_sha256_k[64] =
{
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

static uint32_t vb_sha256_rotr(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void vb_sha256_transform(vb_sha256_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t m[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;

    for (i = 0; i < 16; i++)
    {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++)
    {
        uint32_t s0 = vb_sha256_rotr(m[i - 15], 7) ^ vb_sha256_rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = vb_sha256_rotr(m[i - 2], 17) ^ vb_sha256_rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++)
    {
        uint32_t s1 = vb_sha256_rotr(e, 6) ^ vb_sha256_rotr(e, 11) ^ vb_sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + vb_sha256_k[i] + m[i];
        uint32_t s0 = vb_sha256_rotr(a, 2) ^ vb_sha256_rotr(a, 13) ^ vb_sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void vb_sha256_init(vb_sha256_ctx_t *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
}

static void vb_sha256_update(vb_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
    {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64)
        {
            vb_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void vb_sha256_final(vb_sha256_ctx_t *ctx, uint8_t hash[VB_PACKAGE_SHA256_BYTES])
{
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56)
    {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    }
    else
    {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        vb_sha256_transform(ctx, ctx->data);
        rt_memset(ctx->data, 0, 56);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8U;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    vb_sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; i++)
    {
        hash[i]      = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xff);
        hash[i + 4]  = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xff);
        hash[i + 8]  = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xff);
        hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xff);
        hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xff);
        hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xff);
        hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xff);
        hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xff);
    }
}

static int vb_package_file_exists(const char *path)
{
    return access(path, 0) == 0;
}

static int vb_package_is_safe_app_id(const char *app_id)
{
    int i;
    int len;
    if (!app_id) return 0;
    len = rt_strlen(app_id);
    if (len <= 0 || len >= VB_PACKAGE_MAX_APP_ID) return 0;
    if (app_id[0] < 'a' || app_id[0] > 'z') return 0;
    for (i = 1; i < len; i++)
    {
        char c = app_id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

static int vb_package_is_safe_rel_path(const char *path)
{
    int i;
    int len;
    if (!path) return 0;
    len = rt_strlen(path);
    if (len <= 0 || len >= VB_PACKAGE_MAX_REL_PATH) return 0;
    if (path[0] == '/' || strstr(path, "..") || strstr(path, "//")) return 0;
    for (i = 0; i < len; i++)
    {
        char c = path[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/'))
        {
            return 0;
        }
    }
    return 1;
}

static int vb_package_join_path(char *dst, rt_size_t cap, const char *dir, const char *rel)
{
    int n;
    if (!dst || cap == 0 || !dir || !rel) return -RT_EINVAL;
    n = rt_snprintf(dst, cap, "%s/%s", dir, rel);
    dst[cap - 1] = '\0';
    if (n < 0 || n >= (int)cap) return -RT_EINVAL;
    return RT_EOK;
}

static int vb_package_read_text_file(const char *path, char *dst, rt_size_t cap)
{
    int fd;
    int total = 0;
    if (!path || !dst || cap < 2) return -RT_EINVAL;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -RT_ERROR;
    while (total < (int)cap - 1)
    {
        int got = read(fd, dst + total, (int)cap - 1 - total);
        if (got < 0)
        {
            close(fd);
            return -RT_ERROR;
        }
        if (got == 0) break;
        total += got;
    }
    close(fd);
    dst[total] = '\0';
    if (total >= (int)cap - 1)
    {
        rt_kprintf("[vb_runtime][package] manifest too large: %s\n", path);
        return -RT_EINVAL;
    }
    return total;
}

static const char *vb_package_find_value(const char *begin, const char *end, const char *key)
{
    char needle[48];
    const char *cursor = begin;
    rt_snprintf(needle, sizeof(needle), "\"%s\"", key);
    while (cursor && (!end || cursor < end))
    {
        const char *hit = strstr(cursor, needle);
        if (!hit || (end && hit >= end)) return RT_NULL;
        hit += rt_strlen(needle);
        while ((!end || hit < end) && (*hit == ' ' || *hit == '\t' || *hit == '\r' || *hit == '\n')) hit++;
        if (*hit != ':')
        {
            cursor = hit;
            continue;
        }
        hit++;
        while ((!end || hit < end) && (*hit == ' ' || *hit == '\t' || *hit == '\r' || *hit == '\n')) hit++;
        return hit;
    }
    return RT_NULL;
}

static int vb_package_copy_json_string(const char *begin, const char *end, const char *key,
                                       char *dst, rt_size_t cap)
{
    const char *src;
    rt_size_t used = 0;
    if (!dst || cap == 0) return 0;
    dst[0] = '\0';
    src = vb_package_find_value(begin, end, key);
    if (!src || *src != '"') return 0;
    src++;
    while ((!end || src < end) && *src && *src != '"' && used + 1 < cap)
    {
        if (*src == '\\' && src[1]) src++;
        dst[used++] = *src++;
    }
    dst[used] = '\0';
    return src && (!end || src < end) && *src == '"';
}

static int vb_package_read_json_int(const char *begin, const char *end, const char *key, int *out)
{
    char *tail;
    const char *src = vb_package_find_value(begin, end, key);
    long value;
    if (!src || !out) return 0;
    value = strtol(src, &tail, 10);
    if (tail == src || (end && tail > end)) return 0;
    while ((!end || tail < end) && (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n')) tail++;
    if ((!end || tail < end) && *tail != ',' && *tail != '}' && *tail != ']') return 0;
    if (value < 0 || value > 0x7fffffffL) return 0;
    *out = (int)value;
    return 1;
}

static const char *vb_package_find_char(const char *begin, const char *end, char ch)
{
    const char *p = begin;
    while (p && (!end || p < end))
    {
        if (*p == ch) return p;
        p++;
    }
    return RT_NULL;
}

static int vb_package_hex64(const char *value)
{
    int i;
    if (!value || rt_strlen(value) != VB_PACKAGE_SHA256_HEX_LEN) return 0;
    for (i = 0; i < VB_PACKAGE_SHA256_HEX_LEN; i++)
    {
        char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

static void vb_package_hex_encode(const uint8_t digest[VB_PACKAGE_SHA256_BYTES], char *dst, rt_size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    int i;
    if (!dst || cap < VB_PACKAGE_SHA256_HEX_LEN + 1) return;
    for (i = 0; i < VB_PACKAGE_SHA256_BYTES; i++)
    {
        dst[i * 2] = hex[(digest[i] >> 4) & 0x0f];
        dst[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    dst[VB_PACKAGE_SHA256_HEX_LEN] = '\0';
}

static int vb_package_sha256_file_hex(const char *path, char *hex, rt_size_t hex_cap, unsigned long *size_out)
{
    int fd;
    vb_sha256_ctx_t ctx;
    uint8_t digest[VB_PACKAGE_SHA256_BYTES];
    uint8_t buffer[VB_PACKAGE_HASH_BUFFER];
    unsigned long total = 0;

    if (!path || !hex || hex_cap < VB_PACKAGE_SHA256_HEX_LEN + 1) return -RT_EINVAL;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -RT_ERROR;

    vb_sha256_init(&ctx);
    for (;;)
    {
        int got = read(fd, buffer, sizeof(buffer));
        if (got < 0)
        {
            close(fd);
            return -RT_ERROR;
        }
        if (got == 0) break;
        vb_sha256_update(&ctx, buffer, (uint32_t)got);
        total += (unsigned long)got;
    }
    close(fd);
    vb_sha256_final(&ctx, digest);
    vb_package_hex_encode(digest, hex, hex_cap);
    if (size_out) *size_out = total;
    return RT_EOK;
}

static int vb_package_validate_manifest_file_entry(const char *stage_dir,
                                                   const char *object_start,
                                                   const char *object_end)
{
    char relative_path[VB_PACKAGE_MAX_REL_PATH];
    char expected_sha[VB_PACKAGE_SHA256_HEX_LEN + 1];
    char actual_sha[VB_PACKAGE_SHA256_HEX_LEN + 1];
    char full_path[VB_PACKAGE_MAX_PATH];
    int expected_size = -1;
    unsigned long actual_size = 0;
    int rc;

    if (!vb_package_copy_json_string(object_start, object_end, "path", relative_path, sizeof(relative_path)) ||
        !vb_package_copy_json_string(object_start, object_end, "sha256", expected_sha, sizeof(expected_sha)) ||
        !vb_package_read_json_int(object_start, object_end, "size", &expected_size) ||
        !vb_package_is_safe_rel_path(relative_path) ||
        !vb_package_hex64(expected_sha))
    {
        rt_kprintf("[vb_runtime][package] manifest file entry invalid\n");
        return -RT_EINVAL;
    }

    if (vb_package_join_path(full_path, sizeof(full_path), stage_dir, relative_path) != RT_EOK)
    {
        return -RT_EINVAL;
    }
    if (!vb_package_file_exists(full_path))
    {
        rt_kprintf("[vb_runtime][package] manifest file missing: %s\n", relative_path);
        return -RT_ERROR;
    }

    rc = vb_package_sha256_file_hex(full_path, actual_sha, sizeof(actual_sha), &actual_size);
    if (rc != RT_EOK) return rc;
    if (actual_size != (unsigned long)expected_size)
    {
        rt_kprintf("[vb_runtime][package] manifest size mismatch: %s expected=%lu actual=%lu\n",
                   relative_path, (unsigned long)expected_size, actual_size);
        return -RT_EINVAL;
    }
    if (rt_strcmp(actual_sha, expected_sha) != 0)
    {
        rt_kprintf("[vb_runtime][package] sha256 mismatch: %s expected=%s actual=%s\n",
                   relative_path, expected_sha, actual_sha);
        return -RT_EINVAL;
    }
    return RT_EOK;
}

static int vb_package_validate_manifest_files(const char *stage_dir, const char *json, const char *json_end)
{
    const char *files_value;
    const char *array_start;
    const char *array_end;
    const char *cursor;
    int count = 0;
    int have_main = 0;

    files_value = vb_package_find_value(json, json_end, "files");
    if (!files_value) return RT_EOK;
    if (*files_value != '[')
    {
        rt_kprintf("[vb_runtime][package] manifest files must be an array\n");
        return -RT_EINVAL;
    }
    array_start = files_value;
    array_end = vb_package_find_char(array_start, json_end, ']');
    if (!array_end) return -RT_EINVAL;

    cursor = array_start + 1;
    while (cursor < array_end)
    {
        const char *object_start = vb_package_find_char(cursor, array_end, '{');
        const char *object_end;
        char entry_path[VB_PACKAGE_MAX_REL_PATH];
        int rc;
        if (!object_start) break;
        object_end = vb_package_find_char(object_start, array_end, '}');
        if (!object_end) return -RT_EINVAL;
        if (vb_package_copy_json_string(object_start, object_end, "path", entry_path, sizeof(entry_path)) &&
            rt_strcmp(entry_path, "main.lua") == 0)
        {
            have_main = 1;
        }
        rc = vb_package_validate_manifest_file_entry(stage_dir, object_start, object_end);
        if (rc != RT_EOK) return rc;
        count++;
        cursor = object_end + 1;
    }

    if (count <= 0 || !have_main)
    {
        rt_kprintf("[vb_runtime][package] manifest files must include main.lua\n");
        return -RT_EINVAL;
    }
    rt_kprintf("[vb_runtime][package] manifest integrity checked files=%d\n", count);
    return RT_EOK;
}

static int vb_package_validate_manifest(const char *app_id, const char *stage_dir, const char *manifest_path)
{
    char *json;
    char kind[48];
    char manifest_id[VB_PACKAGE_MAX_APP_ID];
    char entry[VB_PACKAGE_MAX_PATH];
    const char *json_end;
    int version = 0;
    int len;
    int result = RT_EOK;

    json = (char *)rt_malloc(VB_PACKAGE_MAX_MANIFEST);
    if (!json) return -RT_ENOMEM;

    len = vb_package_read_text_file(manifest_path, json, VB_PACKAGE_MAX_MANIFEST);
    if (len <= 0)
    {
        rt_kprintf("[vb_runtime][package] manifest invalid: read failed %s\n", manifest_path);
        rt_free(json);
        return -RT_ERROR;
    }
    json_end = json + len;

    vb_package_copy_json_string(json, json_end, "kind", kind, sizeof(kind));
    vb_package_copy_json_string(json, json_end, "id", manifest_id, sizeof(manifest_id));
    vb_package_copy_json_string(json, json_end, "entry", entry, sizeof(entry));
    if (!(vb_package_read_json_int(json, json_end, "schemaVersion", &version) ||
          vb_package_read_json_int(json, json_end, "version", &version)))
    {
        version = 0;
    }

    if (rt_strcmp(kind, "huangshan-runtime-app-manifest") != 0)
    {
        rt_kprintf("[vb_runtime][package] manifest invalid: kind=%s\n", kind[0] ? kind : "--");
        result = -RT_EINVAL;
    }
    else if (!vb_package_is_safe_app_id(manifest_id) || rt_strcmp(manifest_id, app_id) != 0)
    {
        rt_kprintf("[vb_runtime][package] manifest invalid: id=%s app=%s\n",
                   manifest_id[0] ? manifest_id : "--", app_id);
        result = -RT_EINVAL;
    }
    else if (entry[0] == '\0' || rt_strcmp(entry, "main.lua") != 0)
    {
        rt_kprintf("[vb_runtime][package] manifest invalid: entry=%s\n", entry[0] ? entry : "--");
        result = -RT_EINVAL;
    }
    else if (version != 1)
    {
        rt_kprintf("[vb_runtime][package] manifest invalid: version=%d\n", version);
        result = -RT_EINVAL;
    }
    else
    {
        result = vb_package_validate_manifest_files(stage_dir, json, json_end);
    }

    rt_free(json);
    return result;
}

int vb_runtime_package_validate_stage(const char *app_id, const char *stage_dir)
{
    char manifest_path[VB_PACKAGE_MAX_PATH];
    char appinfo_path[VB_PACKAGE_MAX_PATH];
    char lua_path[VB_PACKAGE_MAX_PATH];
    int have_manifest;

    if (!vb_package_is_safe_app_id(app_id) || !stage_dir || stage_dir[0] == '\0') return -RT_EINVAL;
    if (vb_package_join_path(lua_path, sizeof(lua_path), stage_dir, "main.lua") != RT_EOK ||
        vb_package_join_path(manifest_path, sizeof(manifest_path), stage_dir, "manifest.json") != RT_EOK ||
        vb_package_join_path(appinfo_path, sizeof(appinfo_path), stage_dir, "app.info") != RT_EOK)
    {
        return -RT_EINVAL;
    }

    have_manifest = vb_package_file_exists(manifest_path);
    if (!vb_package_file_exists(lua_path) ||
        (!have_manifest && !vb_package_file_exists(appinfo_path)))
    {
        rt_kprintf("[vb_runtime][package] stage incomplete: %s\n", app_id);
        return -RT_ERROR;
    }
    if (have_manifest)
    {
        return vb_package_validate_manifest(app_id, stage_dir, manifest_path);
    }
    return RT_EOK;
}
