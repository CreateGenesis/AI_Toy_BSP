#include "auth_device.h"
#include <stdio.h>
#include <string.h>

static int uuid_valid(const char *s) {
  size_t i;
  if (!s || strlen(s) != 36)
    return 0;
  for (i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-')
        return 0;
    } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
                 (s[i] >= 'A' && s[i] <= 'F')))
      return 0;
  }
  return 1;
}
static int safe_string(const char *s) {
  size_t i;
  if (!s || !*s)
    return 0;
  for (i = 0; s[i]; i++)
    if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
          (s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '_' ||
          s[i] == '.'))
      return 0;
  return 1;
}
static void wipe(void *p, size_t n) {
  volatile unsigned char *b = (volatile unsigned char *)p;
  while (n--)
    *b++ = 0;
}
int auth_device_base64url(const uint8_t *input, size_t n, char *out,
                          size_t cap) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t i, j = 0, required = (n * 8 + 5) / 6;
  if (!input || !out || n > 128 || cap <= required)
    return AUTH_DEVICE_BUFFER;
  for (i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)input[i] << 16;
    if (i + 1 < n)
      v |= (uint32_t)input[i + 1] << 8;
    if (i + 2 < n)
      v |= input[i + 2];
    out[j++] = table[(v >> 18) & 63];
    out[j++] = table[(v >> 12) & 63];
    if (i + 1 < n)
      out[j++] = table[(v >> 6) & 63];
    if (i + 2 < n)
      out[j++] = table[v & 63];
  }
  out[j] = 0;
  return AUTH_DEVICE_OK;
}
int auth_device_message(const char *id, const char *nonce, char *out,
                        size_t cap) {
  int n;
  if (!uuid_valid(id) || !safe_string(nonce) || strlen(nonce) > 87 || !out)
    return AUTH_DEVICE_ARGUMENT;
  n = snprintf(out, cap, "auth-device-access:v2\n%s\n%s", id, nonce);
  return n < 0 || (size_t)n >= cap ? AUTH_DEVICE_BUFFER : n;
}
/* A bounded parser for the flat JSON protocol. It rejects duplicate fields,
 * nesting, escaped strings, invalid scalar values and trailing input. The
 * protocol's UUID/base64url/JWT strings never require JSON escapes. */
typedef struct {
  char key[64];
  const char *start;
  size_t len;
  int string;
} field;
static int parse(const char *s, size_t length, field fields[16],
                 size_t *count) {
  size_t i = 0, n = 0, j;
  *count = 0;
#define WS()                                                                   \
  while (i < length &&                                                         \
         (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))        \
  i++
  WS();
  if (i >= length || s[i++] != '{')
    return AUTH_DEVICE_PROTOCOL;
  WS();
  if (i < length && s[i] == '}') {
    i++;
    WS();
    return i == length ? 0 : AUTH_DEVICE_PROTOCOL;
  }
  while (i < length) {
    size_t k = 0, start;
    if (n >= 16 || s[i++] != '"')
      return AUTH_DEVICE_PROTOCOL;
    while (i < length && s[i] != '"') {
      if ((unsigned char)s[i] < 32 || s[i] == '\\' || k >= 63)
        return AUTH_DEVICE_PROTOCOL;
      fields[n].key[k++] = s[i++];
    }
    if (i >= length)
      return AUTH_DEVICE_PROTOCOL;
    fields[n].key[k] = 0;
    i++;
    for (j = 0; j < n; j++)
      if (strcmp(fields[j].key, fields[n].key) == 0)
        return AUTH_DEVICE_PROTOCOL;
    WS();
    if (i >= length || s[i++] != ':')
      return AUTH_DEVICE_PROTOCOL;
    WS();
    fields[n].string = i < length && s[i] == '"';
    if (fields[n].string)
      i++;
    start = i;
    if (fields[n].string) {
      while (i < length && s[i] != '"') {
        if ((unsigned char)s[i] < 32 || s[i] == '\\')
          return AUTH_DEVICE_PROTOCOL;
        i++;
      }
      if (i >= length)
        return AUTH_DEVICE_PROTOCOL;
    } else {
      while (i < length && s[i] >= '0' && s[i] <= '9')
        i++;
      if (i == start)
        return AUTH_DEVICE_PROTOCOL;
    }
    fields[n].start = s + start;
    fields[n].len = i - start;
    if (fields[n].string)
      i++;
    n++;
    WS();
    if (i >= length)
      return AUTH_DEVICE_PROTOCOL;
    if (s[i] == '}') {
      i++;
      WS();
      if (i != length)
        return AUTH_DEVICE_PROTOCOL;
      *count = n;
      return 0;
    }
    if (s[i++] != ',')
      return AUTH_DEVICE_PROTOCOL;
    WS();
  }
#undef WS
  return AUTH_DEVICE_PROTOCOL;
}
static const field *lookup(const field *f, size_t n, const char *key) {
  size_t i;
  for (i = 0; i < n; i++)
    if (strcmp(f[i].key, key) == 0)
      return &f[i];
  return NULL;
}
static int string_field(const field *f, size_t n, const char *key, char *out,
                        size_t cap) {
  const field *v = lookup(f, n, key);
  if (!v || !v->string || !v->len || v->len >= cap)
    return AUTH_DEVICE_PROTOCOL;
  memcpy(out, v->start, v->len);
  out[v->len] = 0;
  return 0;
}
static int ttl_field(const field *f, size_t n, uint64_t *out) {
  const field *v = lookup(f, n, "expires_in");
  size_t i;
  uint64_t value = 0;
  if (!v || v->string || !v->len || v->len > 3)
    return AUTH_DEVICE_PROTOCOL;
  for (i = 0; i < v->len; i++)
    value = value * 10 + (uint64_t)(v->start[i] - '0');
  if (value < 1 || value > 120)
    return AUTH_DEVICE_PROTOCOL;
  *out = value;
  return 0;
}
static int request_key(auth_device_client *c) {
  uint8_t bytes[16];
  static const char hex[] = "0123456789abcdef";
  size_t i;
  if (c->platform.random(c->platform.context, bytes, sizeof(bytes)))
    return AUTH_DEVICE_SIGN;
  for (i = 0; i < 16; i++) {
    c->request_key[i * 2] = hex[bytes[i] >> 4];
    c->request_key[i * 2 + 1] = hex[bytes[i] & 15];
  }
  c->request_key[32] = 0;
  wipe(bytes, sizeof(bytes));
  return 0;
}
static int send(auth_device_client *c, const char *path, const char *body,
                char *out, size_t *length) {
  int status = 0, rc = 0, attempt;
  for (attempt = 0; attempt < 3; attempt++) {
    *length = 0;
    rc =
        c->platform.http(c->platform.context, c->base_url, path, c->request_key,
                         body, out, AUTH_DEVICE_RESPONSE_CAP, length, &status);
    if (rc == AUTH_DEVICE_BUFFER)
      return AUTH_DEVICE_BUFFER;
    if (rc || status >= 500)
      continue;
    if (*length >= AUTH_DEVICE_RESPONSE_CAP)
      return AUTH_DEVICE_BUFFER;
    out[*length] = 0;
    if (status == 200)
      return 0;
    if (status == 403)
      return AUTH_DEVICE_DENIED;
    if (status == 401)
      return AUTH_DEVICE_REVOKED;
    if (status == 429)
      return AUTH_DEVICE_RATE_LIMIT;
    return AUTH_DEVICE_PROTOCOL;
  }
  return rc ? AUTH_DEVICE_NETWORK : AUTH_DEVICE_DEPENDENCY;
}
/* Origin validation is shared with the no_std SDK: HTTPS, or a numeric
 * loopback development port. Paths, userinfo and control bytes are rejected. */
static int origin_valid(const char *url) {
  const char *host, *colon, *end;
  size_t n, i;
  unsigned long port = 0;
  int tls;
  if (!url)
    return 0;
  tls = strncmp(url, "https://", 8) == 0;
  if (!tls && strncmp(url, "http://", 7) != 0)
    return 0;
  host = url + (tls ? 8 : 7);
  n = strlen(host);
  if (!n)
    return 0;
  for (i = 0; i < n; i++)
    if ((unsigned char)host[i] <= 32 || (unsigned char)host[i] >= 127 ||
        strchr("/@?#\\", host[i]))
      return 0;
  colon = strchr(host, ':');
  end = colon ? colon : host + n;
  if (host[0] == '[') {
    end = strchr(host, ']');
    if (!end || end == host + 1)
      return 0;
    for (i = 1; host + i < end; i++)
      if (!strchr("0123456789abcdefABCDEF:", host[i]))
        return 0;
    end++;
    colon = *end == ':' ? end : NULL;
    if (*end && !colon)
      return 0;
  } else {
    if (end == host)
      return 0;
    for (i = 0; host + i < end; i++)
      if (!((host[i] >= 'a' && host[i] <= 'z') ||
            (host[i] >= 'A' && host[i] <= 'Z') ||
            (host[i] >= '0' && host[i] <= '9') || host[i] == '.' ||
            host[i] == '-'))
        return 0;
  }
  if (colon) {
    const char *p = colon + 1;
    if (!*p)
      return 0;
    while (*p) {
      if (*p < '0' || *p > '9')
        return 0;
      port = port * 10 + (unsigned)(*p++ - '0');
      if (port > 65535)
        return 0;
    }
    if (!port)
      return 0;
  }
  if (!tls &&
      (!colon ||
       !(((size_t)(end - host) == 9 && memcmp(host, "127.0.0.1", 9) == 0) ||
         ((size_t)(end - host) == 9 && memcmp(host, "localhost", 9) == 0))))
    return 0;
  return 1;
}
int auth_device_init(auth_device_client *c, const auth_device_platform *p,
                     const char *url, const char *alg, const uint8_t *key,
                     size_t key_len, const char *code) {
  int loaded = 1;
  size_t len;
  if (!c || !p || !p->http || !p->random || !p->monotonic_ms || !p->sign ||
      !p->save_identity || !url || !alg || !key)
    return AUTH_DEVICE_ARGUMENT;
  len = strlen(url);
  if (len >= sizeof(c->base_url) || !origin_valid(url))
    return AUTH_DEVICE_ARGUMENT;
  if (!((strcmp(alg, "Ed25519") == 0 && key_len == 32) ||
        (strcmp(alg, "P-256") == 0 && key_len == 65 && key[0] == 4)))
    return AUTH_DEVICE_ARGUMENT;
  if (code && (*code) &&
      (!safe_string(code) || strlen(code) >= AUTH_DEVICE_CODE_CAP))
    return AUTH_DEVICE_ARGUMENT;
  memset(c, 0, sizeof(*c));
  c->platform = *p;
  memcpy(c->base_url, url, len + 1);
  strcpy(c->algorithm, alg);
  if (auth_device_base64url(key, key_len, c->public_key, sizeof(c->public_key)))
    return AUTH_DEVICE_BUFFER;
  if (code)
    strcpy(c->code, code);
  if (p->load_identity) {
    loaded = p->load_identity(p->context, c->device_id);
    if (loaded < 0)
      return AUTH_DEVICE_STORAGE;
    if (loaded == 0 && !uuid_valid(c->device_id))
      return AUTH_DEVICE_STORAGE;
  }
  if (loaded != 0)
    c->device_id[0] = 0;
  return 0;
}
int auth_device_init_sn(auth_device_client *c, const auth_device_platform *p,
                        const char *url, const char *alg, const uint8_t *key,
                        size_t key_len, const char *supplier, const char *sn) {
  size_t i, length;
  int result;
  if (!supplier || !uuid_valid(supplier) || !sn) return AUTH_DEVICE_ARGUMENT;
  length = strlen(sn);
  if (!length || length > 160) return AUTH_DEVICE_ARGUMENT;
  for (i = 0; i < length; i++)
    if ((unsigned char)sn[i] < 33 || (unsigned char)sn[i] > 126)
      return AUTH_DEVICE_ARGUMENT;
  result = auth_device_init(c, p, url, alg, key, key_len, NULL);
  if (result) return result;
  strcpy(c->supplier_id, supplier);
  strcpy(c->sn, sn);
  return AUTH_DEVICE_OK;
}
static void escaped_sn(const char *sn, char output[321]) {
  size_t i, n = 0;
  for (i = 0; sn[i]; i++) {
    if (sn[i] == '"' || sn[i] == '\\') output[n++] = '\\';
    output[n++] = sn[i];
  }
  output[n] = 0;
}
void auth_device_clear_token(auth_device_client *c) {
  if (!c)
    return;
  wipe(c->token, sizeof(c->token));
  c->valid_until_ms = 0;
}
int auth_device_get_token(auth_device_client *c, const char *endpoint,
                          const char **out) {
  char response[AUTH_DEVICE_RESPONSE_CAP], body[1024], message[160], sig[88],
      kind[16], scope[64];
  uint8_t signature[64];
  field fields[16];
  size_t length = 0, count = 0;
  uint64_t ttl = 0, now;
  int rc, n;
  if (!c || !out || !uuid_valid(endpoint))
    return AUTH_DEVICE_ARGUMENT;
  *out = NULL;
  now = c->platform.monotonic_ms(c->platform.context);
  if (strcmp(endpoint, c->endpoint_id) == 0 && now < c->valid_until_ms &&
      c->token[0]) {
    *out = c->token;
    return 0;
  }
  auth_device_clear_token(c);
  if (strcmp(endpoint, c->endpoint_id) != 0) {
    c->pending_token = 0;
    strcpy(c->endpoint_id, endpoint);
  }
  if (c->pending_token != 1) {
    if (c->pending_token == 0) {
      c->challenge_started_ms = now;
      if ((rc = request_key(c)))
        return rc;
      c->pending_token = -1;
    }
    if (!c->device_id[0] && c->sn[0]) {
      char escaped[321];
      escaped_sn(c->sn, escaped);
      n = snprintf(body, sizeof(body),
                   "{\"algorithm\":\"%s\",\"public_key\":\"%s\",\"endpoint_id\":\"%s\",\"supplier_id\":\"%s\",\"sn\":\"%s\"}",
                   c->algorithm, c->public_key, endpoint, c->supplier_id, escaped);
    } else if (c->device_id[0] || !c->code[0])
      n = snprintf(
          body, sizeof(body),
          "{\"algorithm\":\"%s\",\"public_key\":\"%s\",\"endpoint_id\":\"%s\"}",
          c->algorithm, c->public_key, endpoint);
    else
      n = snprintf(body, sizeof(body),
                   "{\"algorithm\":\"%s\",\"public_key\":\"%s\",\"endpoint_"
                   "id\":\"%s\",\"authorization_code\":\"%s\"}",
                   c->algorithm, c->public_key, endpoint, c->code);
    if (n < 0 || (size_t)n >= sizeof(body))
      return AUTH_DEVICE_BUFFER;
    rc = send(c, "/api/v2/devices/challenge", body, response, &length);
    wipe(body, sizeof(body));
    if (rc) {
      if (rc != AUTH_DEVICE_NETWORK && rc != AUTH_DEVICE_DEPENDENCY)
        c->pending_token = 0;
      return rc;
    }
    if (parse(response, length, fields, &count) || count != 3 ||
        string_field(fields, count, "challenge_id", c->challenge_id,
                     sizeof(c->challenge_id)) ||
        string_field(fields, count, "nonce", c->nonce, sizeof(c->nonce)) ||
        ttl_field(fields, count, &ttl) || !uuid_valid(c->challenge_id))
      return AUTH_DEVICE_PROTOCOL;
    n = auth_device_message(c->challenge_id, c->nonce, message,
                            sizeof(message));
    if (n < 0)
      return n;
    if (c->platform.sign(c->platform.context, (const uint8_t *)message,
                         (size_t)n, signature))
      return AUTH_DEVICE_SIGN;
    if (auth_device_base64url(signature, 64, sig, sizeof(sig)))
      return AUTH_DEVICE_BUFFER;
    wipe(signature, sizeof(signature));
    n = snprintf(c->signed_body, sizeof(c->signed_body),
                 "{\"challenge_id\":\"%s\",\"signature\":\"%s\"}",
                 c->challenge_id, sig);
    if (n < 0 || (size_t)n >= sizeof(c->signed_body))
      return AUTH_DEVICE_BUFFER;
    if ((rc = request_key(c)))
      return rc;
    c->pending_token = 1;
  }
  rc = send(c, "/api/v2/devices/token", c->signed_body, response, &length);
  if (rc) {
    if (rc != AUTH_DEVICE_NETWORK && rc != AUTH_DEVICE_DEPENDENCY)
      c->pending_token = 0;
    return rc;
  }
  if (parse(response, length, fields, &count) || count != 5 ||
      string_field(fields, count, "device_id", c->device_id,
                   sizeof(c->device_id)) ||
      !uuid_valid(c->device_id) ||
      string_field(fields, count, "access_token", c->token, sizeof(c->token)) ||
      !safe_string(c->token) ||
      string_field(fields, count, "token_type", kind, sizeof(kind)) ||
      strcmp(kind, "Bearer") || ttl_field(fields, count, &ttl) ||
      string_field(fields, count, "scope", scope, sizeof(scope))) {
    auth_device_clear_token(c);
    return AUTH_DEVICE_PROTOCOL;
  }
  n = snprintf(body, sizeof(body), "endpoint:%s", endpoint);
  if (n < 0 || strcmp(scope, body)) {
    auth_device_clear_token(c);
    return AUTH_DEVICE_PROTOCOL;
  }
  if (c->platform.save_identity(c->platform.context, c->device_id)) {
    auth_device_clear_token(c);
    return AUTH_DEVICE_STORAGE;
  }
  if (c->code[0] && c->platform.clear_code &&
      c->platform.clear_code(c->platform.context)) {
    auth_device_clear_token(c);
    return AUTH_DEVICE_STORAGE;
  }
  wipe(c->code, sizeof(c->code));
  c->pending_token = 0;
  /* Anchor the cache to the start of this exchange, so delayed/replayed
   * responses can never extend the actual token lifetime. */
  c->valid_until_ms = c->challenge_started_ms + (ttl > 5 ? ttl - 5 : 0) * 1000;
  *out = c->token;
  wipe(response, sizeof(response));
  return 0;
}
