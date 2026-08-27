// =============================================================================
// core/update.inc  —  auto-update shared core: SHA-256, UPD_ASSETS asset
// table, upd_check_finish, upd_auto_tick, upd_apply, upd_recheck_due.
// Proof: updinfo, updcheck. One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: an update is never a player decision — upd_auto_tick applies
// itself under four guards (home && menu_open && !online && entry screen)
// and nowhere else; there is no rollback beyond the <exe>.old link nothing
// reads.
// =============================================================================
// ---------------------------------------------------------------------------
// Auto-update — shared core. Everything here is pure data-in/data-out (asset
// table, SHA-256, the SHA256SUMS comparison rules, the status line) so the harness can
// prove it without touching the network; only the fetch (curl subprocess / WinHTTP),
// the swap and the check thread are per-platform, below the split.
// ---------------------------------------------------------------------------

#ifndef BUILD_VERSION
#define BUILD_VERSION "dev"     // Makefile default: a dev build NEVER updates
#endif
#ifndef BUILD_COMMIT
#define BUILD_COMMIT "unknown"
#endif

// The FULL own path, not exe_dir(): composing "dir + assumed name" swaps whatever
// carries that name, and the player may have renamed the binary.
static int exe_path(char *out, size_t n) {
#ifdef _WIN32
  DWORD len = GetModuleFileNameA(NULL, out, (DWORD)n);
  return len > 0 && len < (DWORD)n;
#else
  // readlink neither NUL-terminates nor reports truncation, so a full buffer is
  // indistinguishable from a path that fits — and a shortened path swaps the
  // wrong file.
  ssize_t len = readlink("/proc/self/exe", out, n - 1);
  if (len <= 0 || (size_t)len >= n - 1) { out[0] = '\0'; return 0; }
  out[len] = '\0';
  return 1;
#endif
}

// --- SHA-256 (FIPS 180-4), self-contained: ~60 lines beat a dependency. ---
typedef struct { uint32_t h[8]; uint64_t n; uint8_t buf[64]; size_t fill; } sha256_t;
static const uint32_t SHA256_K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t sha_rotr(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
static void sha256_block(sha256_t *s, const uint8_t *p) {
  uint32_t w[64], a, b, c, d, e, f, g, h;
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
           (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = sha_rotr(w[i - 15], 7) ^ sha_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = sha_rotr(w[i - 2], 17) ^ sha_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
  e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = sha_rotr(e, 6) ^ sha_rotr(e, 11) ^ sha_rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + SHA256_K[i] + w[i];
    uint32_t S0 = sha_rotr(a, 2) ^ sha_rotr(a, 13) ^ sha_rotr(a, 22);
    uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + mj;
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
  s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha256_init(sha256_t *s) {
  static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                 0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(s->h, iv, sizeof iv); s->n = 0; s->fill = 0;
}
static void sha256_update(sha256_t *s, const void *data, size_t len) {
  const uint8_t *p = data;
  s->n += len;
  while (len) {
    size_t take = 64 - s->fill; if (take > len) take = len;
    memcpy(s->buf + s->fill, p, take);
    s->fill += take; p += take; len -= take;
    if (s->fill == 64) { sha256_block(s, s->buf); s->fill = 0; }
  }
}
static void sha256_hex(sha256_t *s, char out[65]) {
  uint64_t bits = s->n * 8;   // captured BEFORE padding, so padding may grow n
  uint8_t pad = 0x80;
  sha256_update(s, &pad, 1);
  while (s->fill != 56) { uint8_t z = 0; sha256_update(s, &z, 1); }
  uint8_t lenb[8];
  for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - i * 8));
  sha256_update(s, lenb, 8);
  for (int i = 0; i < 8; i++)
    snprintf(out + i * 8, 9, "%08x", s->h[i]);
}

// ONE table, not an #ifdef expression: update_asset_name() picks its own row and
// updinfo prints the WHOLE table for a manual cross-check. The names are LITERAL
// and share no pattern on purpose — they are a contract with the release
// workflow's `mv` lines and docker/entrypoint.sh's ASSET, and a name a reader can
// only match EXACTLY (upd_compare rule 3) is one a clever format string can never
// silently shift. A rename is therefore three edits, and the row order is the one
// upd_own_asset() indexes.
static const char *const UPD_ASSETS[] = {
  "skill-issue-linux",   // 0: linux-amd64 — desktop, SteamOS, dedicated server
  "skill-issue.exe",     // 1: windows-amd64 — also Windows-on-ARM (x64 emulation)
};
#define UPD_NASSETS ((int)(sizeof UPD_ASSETS / sizeof UPD_ASSETS[0]))
static int upd_own_asset(void) {
#if defined(_WIN32)
  return 1;
#elif defined(__x86_64__)
  return 0;
#else
  return -1;   // unreleased platform (incl. aarch64): no asset, updater off
#endif
}
static void upd_asset_name(char *out, size_t n, int i) {
  snprintf(out, n, "%s", UPD_ASSETS[i]);
}
static int update_asset_name(char *out, size_t n) {
  int i = upd_own_asset();
  if (i < 0) { snprintf(out, n, "none"); return 0; }
  upd_asset_name(out, n, i);
  return 1;
}

// The comparison is an ORDER, not an inequality, and positively phrased: an update
// is offered ONLY if every condition holds, so a 404 page, a captive portal or a
// half-read file can never read as "update".
typedef enum {
  UPDV_MALFORMED,   // any parse rule violated — never an update signal
  UPDV_CURRENT,     // same date, same sum
  UPDV_UPDATE,      // remote date strictly newer
  UPDV_RECUT,       // same date, different sum — a same-day re-cut (the common
                    // case now that every push overwrites the one "latest")
  UPDV_DOWNGRADE,   // remote older — happens when starting into the publish
                    // window; NEVER offered
} upd_verdict_t;

static int upd_date_ok(const char *s) {  // strict YYYY-MM-DD
  for (int i = 0; i < 10; i++) {
    if (i == 4 || i == 7) { if (s[i] != '-') return 0; }
    else if (s[i] < '0' || s[i] > '9') return 0;
  }
  int mm = (s[5] - '0') * 10 + (s[6] - '0'), dd = (s[8] - '0') * 10 + (s[9] - '0');
  return mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31;
}
// LOWERCASE ONLY, because every consumer compares the digest as a STRING. Accepting
// an uppercase line here parsed it and then failed the memcmp against our own
// lowercase digest — a 4 MiB download that could only ever end in BAD SHA. Refusing
// the line instead ends the check honestly at "NO ASSET LINE", and sha256sum (which
// writes the release file) emits lowercase.
static int upd_hex64(const char *s) {
  for (int i = 0; i < 64; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
  return 1;
}

// buf: the fetched SHA256SUMS (need not be NUL-terminated), len its size. own_sha
// may be "" — a same-date release then reads as RECUT, erring toward offering.
static upd_verdict_t upd_compare(const char *buf, size_t len,
                                 const char *own_asset, const char *own_sha,
                                 const char *local_date,
                                 char remote_date[11], char remote_sha[65],
                                 const char **why) {
  *why = "";
  remote_date[0] = remote_sha[0] = '\0';
  if (len == 0 || len > 4096) { *why = "SIZE"; return UPDV_MALFORMED; }
  // rule 2: first line is exactly "# version YYYY-MM-DD". len < 21, not 20:
  // buf[20] is the terminator and the buffer need not be NUL-terminated.
  if (len < 21 || memcmp(buf, "# version ", 10) != 0 || !upd_date_ok(buf + 10) ||
      (buf[20] != '\n' && buf[20] != '\r')) { *why = "HEADER"; return UPDV_MALFORMED; }
  memcpy(remote_date, buf + 10, 10);
  remote_date[10] = '\0';
  // rule 3: exactly one well-formed line for the EXACT own asset name
  size_t alen = strlen(own_asset);
  int found = 0;
  for (size_t i = 0; i < len;) {
    size_t e = i;
    while (e < len && buf[e] != '\n') e++;
    // "<64hex>  <name>" — sha256sum's own format, name must end the line
    if (e - i >= 66 + alen && upd_hex64(buf + i) &&
        buf[i + 64] == ' ' && (buf[i + 65] == ' ' || buf[i + 65] == '*')) {
      const char *nm = buf + i + 66;
      size_t nlen = e - i - 66;
      while (nlen && (nm[nlen - 1] == '\r' || nm[nlen - 1] == ' ')) nlen--;
      if (nlen == alen && memcmp(nm, own_asset, alen) == 0) {
        if (found++) { *why = "DUPLICATE"; return UPDV_MALFORMED; }
        memcpy(remote_sha, buf + i, 64);
        remote_sha[64] = '\0';
      }
    }
    i = e + 1;
  }
  if (!found) { *why = "NO ASSET LINE"; return UPDV_MALFORMED; }
  // rule 4: the order. ISO dates compare correctly as strings once validated.
  if (!upd_date_ok(local_date)) { *why = "LOCAL DATE"; return UPDV_MALFORMED; }
  int c = strcmp(remote_date, local_date);
  if (c > 0) return UPDV_UPDATE;
  if (c < 0) { *why = "REMOTE OLDER"; return UPDV_DOWNGRADE; }
  if (own_sha[0] && !strcmp(remote_sha, own_sha)) return UPDV_CURRENT;
  return UPDV_RECUT;
}

// --- update state, published by the check thread, read by UI and updinfo ---
typedef enum {
  UPD_DEV, UPD_OFF, UPD_UNSUPPORTED, UPD_CHECKING,
  UPD_CURRENT, UPD_UPDATE, UPD_RECUT, UPD_APPLYING, UPD_FAILED, UPD_RDONLY,
} upd_state_e;
// The strings are written by the thread BEFORE the seq_cst state store and read
// only after a load sees a completed state — the atomic-volume publish idiom.
static _Atomic int  g_upd_state = UPD_DEV;
static _Atomic int  g_upd_busy;
static _Atomic long g_upd_checked_at;          // time(NULL) of the last result
static char g_upd_remote_date[11];
static char g_upd_remote_sha[65];
static char g_upd_why[32];
// Written by the check thread's fs probe, read by the menu every frame while
// the check is still in flight — it must be atomic like the state above.
static _Atomic int g_upd_writable = -1;        // probed before any offer

// ONE cap for the update download: curl's --max-filesize (stringified below) and
// both apply-path buffers must be the same number.
#define UPD_FETCH_MAX 4194304   // 4 MiB
#define UPD_STR_(x) #x
#define UPD_STR(x) UPD_STR_(x)

static int upd_is_dev(void) { return strcmp(BUILD_VERSION, "dev") == 0; }

// SHA-256 of the running binary itself — the compared identity
// (two release builds can share a date; the sum is what tells them apart).
static int upd_own_sha(char out[65]) {
  char path[512];
  out[0] = '\0';
  if (!exe_path(path, sizeof path)) return 0;
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  sha256_t s;
  sha256_init(&s);
  char buf[16384];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&s, buf, n);
  int ok = !ferror(f);
  fclose(f);
  if (ok) sha256_hex(&s, out);
  return ok;
}

// Shared tail of every check: parse + compare + publish. Runs on the check
// thread; also driven synchronously by the updcheck proof command.
static void upd_check_finish(int fetch_ok, const char *buf, size_t len) {
  if (!fetch_ok) {
    atomic_store(&g_upd_checked_at, (long)time(NULL));
    atomic_store(&g_upd_state, UPD_FAILED);
    return;
  }
  char own[64], own_sha[65], rdate[11], rsha[65];
  const char *why;
  // No asset for this platform means nothing this build could install, so the
  // comparison must not run: "none" is a MATCHABLE name, and a SHA256SUMS
  // carrying a line called "none" would otherwise read as an offer.
  if (!update_asset_name(own, sizeof own)) {
    atomic_store(&g_upd_checked_at, (long)time(NULL));
    atomic_store(&g_upd_state, UPD_UNSUPPORTED);
    return;
  }
  if (!upd_own_sha(own_sha)) own_sha[0] = '\0';
  upd_verdict_t v = upd_compare(buf, len, own, own_sha, BUILD_VERSION,
                                rdate, rsha, &why);
  snprintf(g_upd_remote_date, sizeof g_upd_remote_date, "%s", rdate);
  snprintf(g_upd_remote_sha, sizeof g_upd_remote_sha, "%s", rsha);
  snprintf(g_upd_why, sizeof g_upd_why, "%s", why);
  atomic_store(&g_upd_checked_at, (long)time(NULL));
  int st = v == UPDV_UPDATE ? UPD_UPDATE
         : v == UPDV_RECUT  ? UPD_RECUT
         : v == UPDV_CURRENT || v == UPDV_DOWNGRADE ? UPD_CURRENT
         : UPD_FAILED;
  // No offer without an exit path: an unwritable directory (Program Files,
  // /usr/local/bin, an AppImage mount) becomes the manual-download line BEFORE
  // anything downloads 800 KB per start.
  if ((st == UPD_UPDATE || st == UPD_RECUT) && g_upd_writable == 0) st = UPD_RDONLY;
  atomic_store(&g_upd_state, st);
}

// THE DIAGNOSTIC LINE, and it is on no screen: the player-facing updater is the version
// at the foot of the entry screen plus an UPDATE tile that exists only when there is
// one.
[[maybe_unused]] static void upd_status_line(char *out, size_t n) {
  const char *ver = upd_is_dev() ? "DEV" : BUILD_VERSION;
  char pre[48];
  int r = snprintf(pre, sizeof pre, "VERSION %s+%.7s", ver, BUILD_COMMIT);
  long at = atomic_load(&g_upd_checked_at);
  long mins = at ? (long)(time(NULL) - at) / 60 : 0;
  switch (atomic_load(&g_upd_state)) {
    case UPD_DEV:       r = snprintf(out, n, "%s - DEV BUILD, NO UPDATES", pre); break;
    case UPD_OFF:       r = snprintf(out, n, "%s - UPDATE CHECK OFF", pre); break;
    case UPD_UNSUPPORTED: r = snprintf(out, n, "%s - NO RELEASE FOR THIS PLATFORM", pre); break;
    case UPD_CHECKING:  r = snprintf(out, n, "%s - CHECKING...", pre); break;
    case UPD_CURRENT:   r = snprintf(out, n, "%s - UP TO DATE [CHECKED %ld MIN AGO]", pre, mins); break;
    // ONE line for both offers, naming the build on the other end the way `pre` names
    // this one.
    case UPD_UPDATE:
    case UPD_RECUT:     r = snprintf(out, n, "%s - UPDATE %s+%.7s AVAILABLE", pre,
                                     g_upd_remote_date, g_upd_remote_sha); break;
    case UPD_APPLYING:  r = snprintf(out, n, "%s - UPDATING...", pre); break;
    case UPD_RDONLY:    r = snprintf(out, n, "%s - FOLDER NOT WRITABLE: DOWNLOAD MANUALLY", pre); break;
    default:            r = snprintf(out, n, "%s - CHECK FAILED%s%s", pre,
                                     g_upd_why[0] ? ": " : "", g_upd_why); break;
  }
  if (r < 0) out[0] = '\0';   // encoding error only; truncation is fine here
}

// Platform halves (defined below the split): start the async check thread, and perform
// the swap.
static void upd_check_start(void);
static int  upd_apply(void);          // returns 0 on failure, g_upd_why says why

// A SECOND CHECK, ON THE SCREEN THAT SHOWS THE ANSWER. The boot check is one shot, so a
// client already running when a release went out never learns about it.
#define UPD_RECHECK_S 600
static int upd_recheck_due(long now, long at, int st) {
  if (st == UPD_CHECKING || st == UPD_UPDATE || st == UPD_RECUT ||
      st == UPD_APPLYING) return 0;
  if (at && now - at < UPD_RECHECK_S) return 0;
  return 1;
}
// Called by BOTH native backends from platform_menu_changed, where each already
// knows it is not the harness — the same one-call-site-per-backend shape
// upd_check_start has, and why no proof can reach the network through this.
static void upd_menu_recheck(void) {
  if (!g_menu_open) return;
  if (!upd_recheck_due((long)time(NULL), atomic_load(&g_upd_checked_at),
                       atomic_load(&g_upd_state))) return;
  upd_check_start();
}

// AN UPDATE IS NOT A DECISION THE PLAYER MAKES. With one rolling `latest` re-cut
// several times a day, "there is a newer build" is the normal state of an installed
// client rather than an event, and a player asked every session learns to press past
// it.
static int ui_on_entry_screen(void);

static void upd_arm_apply(void) {
  int st = atomic_load(&g_upd_state);
  if (st != UPD_UPDATE && st != UPD_RECUT) return;
  if (atomic_load(&g_upd_busy)) return;      // a check is still in flight
  atomic_store(&g_upd_state, UPD_APPLYING);
}

// Called once per frame from app_pump, the shared glue BOTH native backends run
// and NOTHING else does: the harness drives app_frame directly and the dedicated
// server has no frame loop, so no proof can reach this.
static void upd_auto_tick(void) {
  if (atomic_load(&g_upd_state) == UPD_APPLYING) {
    if (atomic_load(&g_upd_busy)) return;
    // upd_apply only returns on FAILURE: on success the image is already the
    // new binary (execv) or the process has exited (CreateProcessW + exit).
    if (!upd_apply()) atomic_store(&g_upd_state, UPD_FAILED);
    return;
  }
  // FOUR GUARDS, each excluding a different way of being mid-session: the boot
  // screen, the rail actually being up, not online (--connect kicks a join at
  // boot, so g_home alone does not mean "no session"), and the ROOT page — which
  // is one test covering every modal, rebind capture and held slider in the menu.
  if (!g_home || !g_menu_open || g_online || !ui_on_entry_screen()) return;
  upd_arm_apply();
}

// The genuinely per-OS updater primitives, forward-declared like net_udp_*: the
// transport (curl child / WinHTTP) and the fs probe.
static int  upd_fetch(const char *url, char *dst, size_t cap, size_t *out_len);
static void upd_probe_fs(void);

// The check thread's body — each backend's upd_thread is a two-line wrapper
// in its own thread ABI around this.
static void upd_thread_body(void) {
  upd_probe_fs();
  char url[128 + 64];
  snprintf(url, sizeof url, "%s/releases/latest/download/SHA256SUMS", g_cfg.update_url);
  static char buf[4096];    // one checker at a time (g_upd_busy) — static is safe
  size_t len = 0;
  g_upd_why[0] = '\0';      // a stale why must not survive into a fresh verdict
  int ok = upd_fetch(url, buf, sizeof buf, &len);
  upd_check_finish(ok, buf, len);
  atomic_store(&g_upd_busy, 0);
}

// The boot-time gate: decides whether a check runs at all and flips the state
// machine; returns 1 when the backend should spawn its thread.
static int upd_check_gate(void) {
  if (upd_is_dev())        { atomic_store(&g_upd_state, UPD_DEV); return 0; }
  if (upd_own_asset() < 0) { atomic_store(&g_upd_state, UPD_UNSUPPORTED); return 0; }
  if (!g_cfg.update_check) { atomic_store(&g_upd_state, UPD_OFF); return 0; }
  if (atomic_exchange(&g_upd_busy, 1)) return 0;   // one check at a time
  atomic_store(&g_upd_state, UPD_CHECKING);
  return 1;
}

static void upd_spawn_failed(void) {   // the thread never started: undo the gate
  snprintf(g_upd_why, sizeof g_upd_why, "THREAD");
  atomic_store(&g_upd_busy, 0);
  atomic_store(&g_upd_state, UPD_FAILED);
}

// Download the offered asset to MEMORY and verify its SHA-256 there (the TOCTOU rule:
// the filesystem is touched only after this returns).
static char *upd_download_verified(size_t *out_len) {
  // The ONE legal caller state is APPLYING: upd_arm_apply moved the offer
  // (UPDATE/RECUT) there before upd_auto_tick calls into the apply. Testing
  // for the offer states here instead was the bug that broke every auto-update
  // since the arm flow existed — armed meant "not an offer any more", this
  // returned NULL with no why, and the tick published UPD_FAILED forever.
  int st = atomic_load(&g_upd_state);
  if (st != UPD_APPLYING) {
    snprintf(g_upd_why, sizeof g_upd_why, "STATE");
    return NULL;
  }
  char own[64], url[128 + 128];
  if (!update_asset_name(own, sizeof own)) return NULL;   // no asset: nothing to install
  snprintf(url, sizeof url, "%s/releases/latest/download/%s", g_cfg.update_url, own);
  char *buf = malloc((size_t)UPD_FETCH_MAX);
  if (!buf) { snprintf(g_upd_why, sizeof g_upd_why, "MEM"); return NULL; }
  size_t len = 0;
  g_upd_why[0] = '\0';
  if (!upd_fetch(url, buf, UPD_FETCH_MAX, &len)) { free(buf); return NULL; }
  sha256_t s;
  sha256_init(&s);
  sha256_update(&s, buf, len);
  char hex[65];
  sha256_hex(&s, hex);
  if (strcmp(hex, g_upd_remote_sha) != 0) {
    free(buf);
    snprintf(g_upd_why, sizeof g_upd_why, "BAD SHA");
    return NULL;
  }
  *out_len = len;
  return buf;
}

// UDP socket backend: the ONLY networking below the platform split — POSIX in the
// Linux backend, Winsock in the Windows one. ip is a network-order s_addr, port is
// host-order. recv is non-blocking: >0 bytes, 0 for nothing waiting, -1 on error.
static int      net_udp_open(int port);       // 0 = ephemeral (client); -1 fail
static void     net_udp_close(int fd);
static int      net_udp_send(int fd, uint32_t ip, uint16_t port, const void *b, int n);
static int      net_udp_recv(int fd, uint32_t *ip, uint16_t *port, void *b, int cap);
static uint32_t net_resolve(const char *host);  // name/IP -> network-order s_addr, 0 fail
static uint32_t net_now_ms(void);               // monotonic ms for timeouts/RTT

// ---------------------------------------------------------------------------
// (Netcode wire layer moved below player_t — it needs PITCH_MAX/IN_COUNT.)

