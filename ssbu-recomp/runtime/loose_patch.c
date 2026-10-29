// loose_patch.c - PATCH FILES of the loose-file layer (included by loose_files.c). Part of OUR runtime.
// What ARCropolis (reference\ARCropolis src\modfs\handlers\msbt.rs, prc.rs; the prcx crate's patch.rs) does with the
// files of a mod folder that are not whole files but patches of an archive file:
//     <path>.xmsbt                         labels of <path>.msbt: a label the file has gets the patch's text, a new
//                                          label is added; over all patches the LAST one of a label wins
//     <path>.prcxml / .prcx                a partial param tree of <path>.prc (XML / binary): a struct member is
//     (.stdatx / .stdatxml -> .stdat,      merged by its hash (the n-th member of a hash with the n-th one), a list
//      .stprmx / .stprmxml -> .stprm)      item by its position (hash40 "dummy" = unchanged), a value replaced; a
//                                          member or item the file lacks is added; a value of another type fails the
//                                          patch (that patch only is left out, here)
// "+xx_xx" before the extension (msg_name+us_en.xmsbt) limits a patch to that language (ARCropolis' regional suffix).
// The archive file's size in the tables is raised to twice the base's (ARCropolis' patched_size) plus the patches'
// own bytes; when the game has decompressed the file (or the folder brings it whole), the patches are applied over
// those bytes and the result written into the same buffer. The order of patches: mod folders by name, then path.
// Contains no game content.

#define LP_XMSBT 1
#define LP_PRC 2
typedef struct { char *disk, *mod, *rel; uint64_t target; int kind; char region[6]; uint32_t size; } LfPatch;
static LfPatch *g_lp; static int g_lp_count, g_lp_cap;
static const char *g_lp_mod;                 /* the mod folder lf_scan is in (loose_files_scan) */
static const char *const g_lp_regions[] = { "jp_ja", "us_en", "us_fr", "us_es", "eu_en", "eu_fr", "eu_es", "eu_de", "eu_nl", "eu_it", "eu_ru", "kr_ko", "zh_cn", "zh_tw" };

typedef struct { uint8_t *p; size_t n, cap; } LpBuf;
static void lp_put(LpBuf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { size_t c = b->cap ? b->cap : 4096; while (c < b->n + n) c *= 2; b->p = realloc(b->p, c); b->cap = c; }
    if (d) memcpy(b->p + b->n, d, n); else memset(b->p + b->n, 0, n);
    b->n += n;
}
static void lp_put32(LpBuf *b, uint32_t v) { lp_put(b, &v, 4); }
static char *lp_strndup(const char *s, size_t n) { char *d = malloc(n + 1); memcpy(d, s, n); d[n] = 0; return d; }     /* no strndup in the Windows CRT */
static uint32_t lp_g32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t lp_g16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint64_t lp_g64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static uint8_t *lp_read_file(const char *path, size_t *n) {
    FILE *fp = fopen(path, "rb"); if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = sz >= 0 ? malloc((size_t)sz + 1) : NULL;
    if (d && fread(d, 1, (size_t)sz, fp) != (size_t)sz) { free(d); d = NULL; }
    fclose(fp);
    if (d) { d[sz] = 0; *n = (size_t)sz; }
    return d;
}

/* ---- the patch files: found by lf_scan ---- */
// arc path (lower case) of a mod file -> 1 when it is a patch file (recorded, not a loose file)
static int lp_note(const char *rel, const char *disk, uint32_t size) {
    static const struct { const char *ext, *base; int kind; } X[] = {
        { ".xmsbt", ".msbt", LP_XMSBT }, { ".prcxml", ".prc", LP_PRC }, { ".prcx", ".prc", LP_PRC }, { ".stdatxml", ".stdat", LP_PRC },
        { ".stdatx", ".stdat", LP_PRC }, { ".stprmxml", ".stprm", LP_PRC }, { ".stprmx", ".stprm", LP_PRC } };
    const char *dot = strrchr(rel, '.'); if (!dot || strchr(dot, '/')) return 0;
    int k = -1; for (int i = 0; i < (int)(sizeof X / sizeof X[0]); i++) if (!strcmp(dot, X[i].ext)) { k = i; break; }
    if (k < 0) return 0;
    char t[MAX_PATH * 2], reg[6] = { 0 };
    snprintf(t, sizeof t, "%.*s%s", (int)(dot - rel), rel, X[k].base);
    char *bn = strrchr(t, '/'), *plus = strchr(bn ? bn : t, '+');
    if (plus && strlen(plus) >= 6) { memcpy(reg, plus + 1, 5); memmove(plus, plus + 6, strlen(plus + 6) + 1); }   /* ARCropolis' strip_regional */
    if (g_lp_count == g_lp_cap) { g_lp_cap = g_lp_cap ? g_lp_cap * 2 : 1024; g_lp = realloc(g_lp, g_lp_cap * sizeof *g_lp); }
    LfPatch *p = &g_lp[g_lp_count++];
    memset(p, 0, sizeof *p);
    p->disk = _strdup(disk); p->mod = _strdup(g_lp_mod ? g_lp_mod : ""); p->rel = _strdup(rel); p->kind = X[k].kind; p->size = size;
    memcpy(p->region, reg, 6);
    p->target = lf_hash40(t);
    return 1;
}
static char *lp_base_path(const LfPatch *p) {     /* the archive path the patch is for */
    static const struct { const char *ext, *base; } X[] = { { ".xmsbt", ".msbt" }, { ".prcxml", ".prc" }, { ".prcx", ".prc" }, { ".stdatxml", ".stdat" },
        { ".stdatx", ".stdat" }, { ".stprmxml", ".stprm" }, { ".stprmx", ".stprm" } };
    const char *dot = strrchr(p->rel, '.'); char t[MAX_PATH * 2];
    for (int i = 0; i < 7; i++) if (!strcmp(dot, X[i].ext)) { snprintf(t, sizeof t, "%.*s%s", (int)(dot - p->rel), p->rel, X[i].base); break; }
    char *bn = strrchr(t, '/'), *plus = strchr(bn ? bn : t, '+');
    if (plus && strlen(plus) >= 6) memmove(plus, plus + 6, strlen(plus + 6) + 1);
    return _strdup(t);
}
static int lp_cmp(const void *a, const void *b) {
    const LfPatch *x = a, *y = b;
    if (x->target != y->target) return x->target < y->target ? -1 : 1;
    int c = strcmp(x->mod, y->mod); if (c) return c;
    return strcmp(x->rel, y->rel);
}

/* ---- text: patch files are UTF-8 or UTF-16 (by the byte order mark; some declare UTF-16 and are UTF-8) ---- */
static char *lp_utf8(const uint8_t *b, size_t n) {
    int be = 0, u16 = 0;
    if (n >= 2 && b[0] == 0xff && b[1] == 0xfe) { u16 = 1; b += 2; n -= 2; }
    else if (n >= 2 && b[0] == 0xfe && b[1] == 0xff) { u16 = 1; be = 1; b += 2; n -= 2; }
    else if (n >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) { b += 3; n -= 3; }
    else if (n > 1 && b[1] == 0) u16 = 1;
    if (!u16) { char *s = malloc(n + 1); memcpy(s, b, n); s[n] = 0; return s; }
    char *s = malloc(n * 2 + 1), *o = s;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t c = be ? (uint32_t)(b[i] << 8 | b[i + 1]) : (uint32_t)(b[i] | b[i + 1] << 8);
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < n) {
            uint32_t d = be ? (uint32_t)(b[i + 2] << 8 | b[i + 3]) : (uint32_t)(b[i + 2] | b[i + 3] << 8);
            if (d >= 0xdc00 && d < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00); i += 2; }
        }
        if (c < 0x80) *o++ = (char)c;
        else if (c < 0x800) { *o++ = (char)(0xc0 | c >> 6); *o++ = (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { *o++ = (char)(0xe0 | c >> 12); *o++ = (char)(0x80 | (c >> 6 & 0x3f)); *o++ = (char)(0x80 | (c & 0x3f)); }
        else { *o++ = (char)(0xf0 | c >> 18); *o++ = (char)(0x80 | (c >> 12 & 0x3f)); *o++ = (char)(0x80 | (c >> 6 & 0x3f)); *o++ = (char)(0x80 | (c & 0x3f)); }
    }
    *o = 0;
    return s;
}
static void lp_utf16_put(LpBuf *b, uint32_t c) {
    if (c >= 0x10000) { c -= 0x10000; uint16_t h = (uint16_t)(0xd800 + (c >> 10)), l = (uint16_t)(0xdc00 + (c & 0x3ff)); lp_put(b, &h, 2); lp_put(b, &l, 2); }
    else { uint16_t w = (uint16_t)c; lp_put(b, &w, 2); }
}
static void lp_utf8_to_16(LpBuf *b, const char *s, size_t n) {
    const uint8_t *p = (const uint8_t *)s, *e = p + n;
    while (p < e) {
        uint32_t c = *p++;
        if (c >= 0xf0 && e - p >= 3) { c = (c & 7) << 18 | (p[0] & 0x3fu) << 12 | (p[1] & 0x3fu) << 6 | (p[2] & 0x3fu); p += 3; }
        else if (c >= 0xe0 && e - p >= 2) { c = (c & 0xf) << 12 | (p[0] & 0x3fu) << 6 | (p[1] & 0x3fu); p += 2; }
        else if (c >= 0xc0 && e - p >= 1) { c = (c & 0x1f) << 6 | (p[0] & 0x3fu); p += 1; }
        lp_utf16_put(b, c);
    }
}
// XML character data -> UTF-8 in place: line ends normalized (\r\n, \r -> \n, as an XML parser reads them), entities
static size_t lp_unescape(char *s, size_t n) {
    char *o = s; size_t i = 0;
    while (i < n) {
        if (s[i] == '\r') { *o++ = '\n'; i += (i + 1 < n && s[i + 1] == '\n') ? 2 : 1; continue; }
        if (s[i] == '&') {
            const char *semi = memchr(s + i, ';', n - i > 12 ? 12 : n - i);
            if (semi) {
                size_t L = (size_t)(semi - (s + i)); uint32_t c = 0; int ok = 1;
                if (L == 3 && !strncmp(s + i, "&lt", 3)) c = '<';
                else if (L == 3 && !strncmp(s + i, "&gt", 3)) c = '>';
                else if (L == 4 && !strncmp(s + i, "&amp", 4)) c = '&';
                else if (L == 5 && !strncmp(s + i, "&quot", 5)) c = '"';
                else if (L == 5 && !strncmp(s + i, "&apos", 5)) c = '\'';
                else if (L > 2 && s[i + 1] == '#') c = (uint32_t)strtoul(s + i + (s[i + 2] == 'x' ? 3 : 2), NULL, s[i + 2] == 'x' ? 16 : 10);
                else ok = 0;
                if (ok) {
                    if (c < 0x80) *o++ = (char)c;
                    else if (c < 0x800) { *o++ = (char)(0xc0 | c >> 6); *o++ = (char)(0x80 | (c & 0x3f)); }
                    else if (c < 0x10000) { *o++ = (char)(0xe0 | c >> 12); *o++ = (char)(0x80 | (c >> 6 & 0x3f)); *o++ = (char)(0x80 | (c & 0x3f)); }
                    else { *o++ = (char)(0xf0 | c >> 18); *o++ = (char)(0x80 | (c >> 12 & 0x3f)); *o++ = (char)(0x80 | (c >> 6 & 0x3f)); *o++ = (char)(0x80 | (c & 0x3f)); }
                    i += L + 1; continue;
                }
            }
        }
        *o++ = s[i++];
    }
    return (size_t)(o - s);
}

/* ---- a small XML reader: elements with attributes and text (comments, declarations, CDATA skipped / kept) ---- */
typedef struct LpX { char *tag; char *attr[8][2]; int na; char *text; size_t ntext; struct LpX **kid; int nk, ck; } LpX;
static const char *lp_xattr(const LpX *x, const char *k) { for (int i = 0; i < x->na; i++) if (!strcmp(x->attr[i][0], k)) return x->attr[i][1]; return NULL; }
static void lp_xfree(LpX *x) {
    if (!x) return;
    for (int i = 0; i < x->nk; i++) lp_xfree(x->kid[i]);
    free(x->kid); free(x->text); free(x->tag);
    for (int i = 0; i < x->na; i++) { free(x->attr[i][0]); free(x->attr[i][1]); }
    free(x);
}
static LpX *lp_xparse(const char *s) {
    LpX *stack[64]; int sp = 0; LpX *root = NULL;
    const char *p = s;
    while (*p) {
        if (*p != '<') {                                         /* character data of the open element */
            const char *q = strchr(p, '<'); size_t n = q ? (size_t)(q - p) : strlen(p);
            if (sp) { LpX *x = stack[sp - 1]; x->text = realloc(x->text, x->ntext + n + 1); memcpy(x->text + x->ntext, p, n); x->ntext += n; x->text[x->ntext] = 0; }
            p += n; continue;
        }
        if (!strncmp(p, "<!--", 4)) { const char *q = strstr(p, "-->"); p = q ? q + 3 : p + strlen(p); continue; }
        if (!strncmp(p, "<![CDATA[", 9)) {
            const char *q = strstr(p, "]]>"); size_t n = q ? (size_t)(q - p - 9) : 0;
            if (sp && q) { LpX *x = stack[sp - 1]; x->text = realloc(x->text, x->ntext + n + 1); memcpy(x->text + x->ntext, p + 9, n); x->ntext += n; x->text[x->ntext] = 0; }
            p = q ? q + 3 : p + strlen(p); continue;
        }
        if (p[1] == '?' || p[1] == '!') { const char *q = strchr(p, '>'); p = q ? q + 1 : p + strlen(p); continue; }
        if (p[1] == '/') { const char *q = strchr(p, '>'); if (sp) sp--; p = q ? q + 1 : p + strlen(p); continue; }
        p++;
        const char *t = p; while (*p && !strchr(" \t\r\n/>", *p)) p++;
        LpX *x = calloc(1, sizeof *x); x->tag = lp_strndup(t, (size_t)(p - t));
        for (;;) {
            while (*p && strchr(" \t\r\n", *p)) p++;
            if (!*p || *p == '>' || *p == '/') break;
            const char *k = p; while (*p && !strchr(" \t\r\n=/>", *p)) p++;
            size_t kn = (size_t)(p - k);
            while (*p && strchr(" \t\r\n", *p)) p++;
            if (*p != '=') continue;
            p++; while (*p && strchr(" \t\r\n", *p)) p++;
            char qc = *p; if (qc != '"' && qc != '\'') break;
            const char *v = ++p; while (*p && *p != qc) p++;
            if (x->na < 8) { x->attr[x->na][0] = lp_strndup(k, kn); char *vv = lp_strndup(v, (size_t)(p - v)); vv[lp_unescape(vv, strlen(vv))] = 0; x->attr[x->na][1] = vv; x->na++; }
            if (*p) p++;
        }
        int selfclose = *p == '/';
        while (*p && *p != '>') p++;
        if (*p) p++;
        if (sp) { LpX *pa = stack[sp - 1]; if (pa->nk == pa->ck) { pa->ck = pa->ck ? pa->ck * 2 : 8; pa->kid = realloc(pa->kid, pa->ck * sizeof *pa->kid); } pa->kid[pa->nk++] = x; }
        else if (!root) root = x; else { lp_xfree(x); x = NULL; }
        if (x && !selfclose && sp < 64) stack[sp++] = x;
    }
    return root;
}

/* ---- MSBT (MsgStdBn): labels (LBL1, hashed into groups), attributes (ATR1), texts (TXT2, UTF-16) ---- */
typedef struct { char *name; uint32_t index; } LpLabel;
typedef struct { uint8_t *d; uint32_t n; } LpBlob;
typedef struct {
    uint8_t head[0x20]; uint32_t groups;
    int nsec; char magic[16][4]; LpBlob sec[16];   /* sections as read; LBL1 / ATR1 / TXT2 / TSY1 rebuilt on write */
    LpLabel *lab; int nlab, clab; LpBlob *txt; int ntxt, ctxt;
    uint32_t atr_size; uint8_t *atr; int has_tsy; uint8_t *tsy;
    uint32_t *hk; int32_t *hv; uint32_t hmask;     /* label name -> text index */
} LpMsbt;
static uint32_t lp_label_hash(const char *s) { uint32_t h = 0; for (; *s; s++) h = h * 0x492 + (uint8_t)*s; return h; }
static uint32_t lp_fnv(const char *s) { uint32_t h = 2166136261u; for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u; return h | 1; }
static void lp_msbt_index(LpMsbt *m) {
    uint32_t cap = 1024; while (cap < (uint32_t)m->nlab * 4 + 4096) cap <<= 1;
    free(m->hk); free(m->hv); m->hk = calloc(cap, 4); m->hv = calloc(cap, 4); m->hmask = cap - 1;
    for (int i = 0; i < m->nlab; i++) {
        uint32_t h = lp_fnv(m->lab[i].name), j = h & m->hmask;
        while (m->hk[j]) j = (j + 1) & m->hmask;
        m->hk[j] = h; m->hv[j] = i;
    }
}
static int lp_msbt_find(LpMsbt *m, const char *name) {
    uint32_t h = lp_fnv(name);
    for (uint32_t j = h & m->hmask; m->hk[j]; j = (j + 1) & m->hmask) if (m->hk[j] == h && !strcmp(m->lab[m->hv[j]].name, name)) return m->hv[j];
    return -1;
}
static void lp_msbt_free(LpMsbt *m) {
    for (int i = 0; i < m->nlab; i++) free(m->lab[i].name);
    for (int i = 0; i < m->ntxt; i++) free(m->txt[i].d);
    for (int i = 0; i < m->nsec; i++) free(m->sec[i].d);
    free(m->lab); free(m->txt); free(m->atr); free(m->tsy); free(m->hk); free(m->hv);
    memset(m, 0, sizeof *m);
}
static int lp_msbt_read(LpMsbt *m, const uint8_t *d, size_t n) {
    memset(m, 0, sizeof *m);
    if (n < 0x20 || memcmp(d, "MsgStdBn", 8) || d[8] != 0xff || d[9] != 0xfe) return 0;
    memcpy(m->head, d, 0x20);
    int nsec = lp_g16(d + 0x0e); size_t off = 0x20;
    const uint8_t *lbl = NULL, *txt = NULL; uint32_t nlbl = 0, ntx = 0;
    for (int i = 0; i < nsec && i < 16; i++) {
        if (off + 0x10 > n) return 0;
        uint32_t sz = lp_g32(d + off + 4);
        if (off + 0x10 + sz > n) return 0;
        memcpy(m->magic[i], d + off, 4);
        m->sec[i].d = malloc(sz ? sz : 1); memcpy(m->sec[i].d, d + off + 0x10, sz); m->sec[i].n = sz;
        if (!memcmp(d + off, "LBL1", 4)) { lbl = m->sec[i].d; nlbl = sz; }
        if (!memcmp(d + off, "TXT2", 4)) { txt = m->sec[i].d; ntx = sz; }
        off = (off + 0x10 + sz + 15) & ~(size_t)15;
        m->nsec = i + 1;
    }
    if (!lbl || !txt || nlbl < 4 || ntx < 4) return 0;
    uint32_t nt = lp_g32(txt);
    if (4 + 4ull * nt > ntx) return 0;
    m->ntxt = m->ctxt = (int)nt; m->txt = calloc(nt + 1, sizeof *m->txt);
    for (uint32_t i = 0; i < nt; i++) {
        uint32_t a = lp_g32(txt + 4 + 4 * i), b = i + 1 < nt ? lp_g32(txt + 4 + 4 * (i + 1)) : ntx;
        if (a > b || b > ntx) return 0;
        m->txt[i].n = b - a; m->txt[i].d = malloc(b - a ? b - a : 1); memcpy(m->txt[i].d, txt + a, b - a);
    }
    m->groups = lp_g32(lbl);
    if (4 + 8ull * m->groups > nlbl) return 0;
    for (uint32_t g = 0; g < m->groups; g++) {
        uint32_t cnt = lp_g32(lbl + 4 + 8 * g), p = lp_g32(lbl + 8 + 8 * g);
        for (uint32_t c = 0; c < cnt; c++) {
            if (p >= nlbl || p + 1 + lbl[p] + 4 > nlbl) return 0;
            if (m->nlab == m->clab) { m->clab = m->clab ? m->clab * 2 : 1024; m->lab = realloc(m->lab, m->clab * sizeof *m->lab); }
            m->lab[m->nlab].name = lp_strndup((const char *)lbl + p + 1, lbl[p]);
            m->lab[m->nlab].index = lp_g32(lbl + p + 1 + lbl[p]);
            if (m->lab[m->nlab].index >= nt) return 0;
            m->nlab++; p += 1 + lbl[p] + 4;
        }
    }
    for (int i = 0; i < m->nsec; i++) {
        if (!memcmp(m->magic[i], "ATR1", 4) && m->sec[i].n >= 8) {
            m->atr_size = lp_g32(m->sec[i].d + 4);
            if (m->atr_size && 8ull + (uint64_t)m->atr_size * nt <= m->sec[i].n) { m->atr = malloc((size_t)m->atr_size * nt + 1); memcpy(m->atr, m->sec[i].d + 8, (size_t)m->atr_size * nt); }
            else m->atr_size = 0;
        }
        if (!memcmp(m->magic[i], "TSY1", 4) && m->sec[i].n >= 4ull * nt) { m->has_tsy = 1; m->tsy = malloc(4ull * nt + 1); memcpy(m->tsy, m->sec[i].d, 4ull * nt); }
    }
    lp_msbt_index(m);
    return 1;
}
// a label's text: set (it exists) or added (a new text after the last)
static void lp_msbt_set(LpMsbt *m, const char *name, const uint8_t *t, uint32_t n) {
    int i = lp_msbt_find(m, name);
    if (i >= 0) { LpBlob *b = &m->txt[m->lab[i].index]; free(b->d); b->d = malloc(n ? n : 1); memcpy(b->d, t, n); b->n = n; return; }
    if (strlen(name) > 255) return;
    if (m->ntxt == m->ctxt) { m->ctxt = m->ctxt ? m->ctxt * 2 : 64; m->txt = realloc(m->txt, m->ctxt * sizeof *m->txt); }
    m->txt[m->ntxt].d = malloc(n ? n : 1); memcpy(m->txt[m->ntxt].d, t, n); m->txt[m->ntxt].n = n;
    if (m->atr_size) { m->atr = realloc(m->atr, (size_t)m->atr_size * (m->ntxt + 1)); memset(m->atr + (size_t)m->atr_size * m->ntxt, 0, m->atr_size); }
    if (m->has_tsy) { m->tsy = realloc(m->tsy, 4ull * (m->ntxt + 1)); memset(m->tsy + 4ull * m->ntxt, 0, 4); }
    if (m->nlab == m->clab) { m->clab = m->clab ? m->clab * 2 : 1024; m->lab = realloc(m->lab, m->clab * sizeof *m->lab); }
    m->lab[m->nlab].name = _strdup(name); m->lab[m->nlab].index = (uint32_t)m->ntxt;
    m->nlab++; m->ntxt++;
    if ((uint32_t)m->nlab * 2 > m->hmask) lp_msbt_index(m);
    else { uint32_t h = lp_fnv(name), j = h & m->hmask; while (m->hk[j]) j = (j + 1) & m->hmask; m->hk[j] = h; m->hv[j] = m->nlab - 1; }
}
static int lp_cmp_lab(const void *a, const void *b) { uint32_t x = ((const LpLabel *)a)->index, y = ((const LpLabel *)b)->index; return x < y ? -1 : x > y; }
static void lp_section(LpBuf *o, const char *magic, const uint8_t *d, uint32_t n) {
    lp_put(o, magic, 4); lp_put32(o, n); lp_put(o, NULL, 8); lp_put(o, d, n);
    while (o->n % 16) { uint8_t ab = 0xab; lp_put(o, &ab, 1); }
}
static void lp_msbt_write(LpMsbt *m, LpBuf *o) {
    uint32_t G = m->groups ? m->groups : 101;
    LpBuf lbl = { 0 }, body = { 0 }, txt = { 0 }, atr = { 0 };
    /* labels by group, in text order inside a group (as the game's files keep them) */
    LpLabel *s = malloc((m->nlab + 1) * sizeof *s); memcpy(s, m->lab, m->nlab * sizeof *s);
    qsort(s, m->nlab, sizeof *s, lp_cmp_lab);
    uint32_t *cnt = calloc(G, 4); for (int i = 0; i < m->nlab; i++) cnt[lp_label_hash(s[i].name) % G]++;
    lp_put32(&lbl, G);
    uint32_t off = 4 + 8 * G;
    for (uint32_t g = 0; g < G; g++) {
        size_t before = body.n;
        for (int i = 0; i < m->nlab; i++) {
            if (lp_label_hash(s[i].name) % G != g) continue;
            uint8_t L = (uint8_t)strlen(s[i].name); lp_put(&body, &L, 1); lp_put(&body, s[i].name, L); lp_put32(&body, s[i].index);
        }
        lp_put32(&lbl, cnt[g]); lp_put32(&lbl, off + (uint32_t)before);
    }
    lp_put(&lbl, body.p, body.n);
    lp_put32(&txt, (uint32_t)m->ntxt);
    uint32_t base = 4 + 4 * (uint32_t)m->ntxt, at = 0;
    for (int i = 0; i < m->ntxt; i++) { lp_put32(&txt, base + at); at += m->txt[i].n; }
    for (int i = 0; i < m->ntxt; i++) lp_put(&txt, m->txt[i].d, m->txt[i].n);
    lp_put32(&atr, (uint32_t)m->ntxt); lp_put32(&atr, m->atr_size); if (m->atr_size) lp_put(&atr, m->atr, (size_t)m->atr_size * m->ntxt);
    lp_put(o, m->head, 0x20);
    for (int i = 0; i < m->nsec; i++) {
        if (!memcmp(m->magic[i], "LBL1", 4)) lp_section(o, "LBL1", lbl.p, (uint32_t)lbl.n);
        else if (!memcmp(m->magic[i], "TXT2", 4)) lp_section(o, "TXT2", txt.p, (uint32_t)txt.n);
        else if (!memcmp(m->magic[i], "ATR1", 4)) lp_section(o, "ATR1", atr.p, (uint32_t)atr.n);
        else if (!memcmp(m->magic[i], "TSY1", 4) && m->has_tsy) lp_section(o, "TSY1", m->tsy, 4u * (uint32_t)m->ntxt);
        else lp_section(o, m->magic[i], m->sec[i].d, m->sec[i].n);
    }
    uint32_t total = (uint32_t)o->n; memcpy(o->p + 0x12, &total, 4);
    free(s); free(cnt); free(lbl.p); free(body.p); free(txt.p); free(atr.p);
}
static int lp_b64(const char *s, LpBuf *o) {
    uint32_t acc = 0; int bits = 0;
    for (; *s; s++) {
        int v; char c = *s;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62; else if (c == '/') v = 63; else if (c == '=' || strchr(" \t\r\n", c)) continue; else return 0;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; uint8_t b = (uint8_t)(acc >> bits); lp_put(o, &b, 1); }
    }
    return 1;
}
// one .xmsbt over the msbt: the number of labels set, -1 = not readable
static int lp_xmsbt_apply(LpMsbt *m, const char *path) {
    size_t n; uint8_t *raw = lp_read_file(path, &n); if (!raw) return -1;
    char *s = lp_utf8(raw, n); free(raw);
    LpX *x = lp_xparse(s); free(s);
    if (!x) return -1;
    int set = 0;
    for (int i = 0; i < x->nk; i++) {
        LpX *e = x->kid[i]; const char *lab = lp_xattr(e, "label");
        if (strcmp(e->tag, "entry") || !lab) continue;
        LpX *t = NULL; for (int k = 0; k < e->nk; k++) if (!strcmp(e->kid[k]->tag, "text")) { t = e->kid[k]; break; }
        if (!t) continue;
        LpBuf b = { 0 };
        const char *b64 = lp_xattr(e, "base64");
        if (b64 && !strcmp(b64, "true")) {
            if (!lp_b64(t->text ? t->text : "", &b)) { free(b.p); continue; }
            lp_put(&b, NULL, 2);
        } else {
            size_t L = t->text ? lp_unescape(t->text, t->ntext) : 0;
            lp_utf8_to_16(&b, t->text ? t->text : "", L); lp_put(&b, NULL, 2);
        }
        lp_msbt_set(m, lab, b.p, (uint32_t)b.n); free(b.p); set++;
    }
    lp_xfree(x);
    return set;
}

/* ---- param files (paracobn): 1 bool 2 sbyte 3 byte 4 short 5 ushort 6 int 7 uint 8 float 9 hash40 10 string 11 list 12 struct ---- */
typedef struct LpP { uint8_t t; union { int64_t i; float f; uint64_t h; } v; char *s; int n, cap; uint64_t *key; struct LpP **kid; } LpP;
static const uint8_t g_lp_psz[13] = { 0, 1, 1, 1, 2, 2, 4, 4, 4 };
static void lp_pfree(LpP *p) {
    if (!p) return;
    for (int i = 0; i < p->n; i++) lp_pfree(p->kid[i]);
    free(p->kid); free(p->key); free(p->s); free(p);
}
static LpP *lp_pnew(uint8_t t) { LpP *p = calloc(1, sizeof *p); p->t = t; return p; }
static void lp_padd(LpP *p, uint64_t key, LpP *k) {
    if (p->n == p->cap) { p->cap = p->cap ? p->cap * 2 : 4; p->kid = realloc(p->kid, p->cap * sizeof *p->kid); p->key = realloc(p->key, p->cap * sizeof *p->key); }
    p->key[p->n] = key; p->kid[p->n++] = k;
}
static LpP *lp_pclone(const LpP *p) {
    LpP *c = lp_pnew(p->t); c->v = p->v; c->s = p->s ? _strdup(p->s) : NULL;
    for (int i = 0; i < p->n; i++) lp_padd(c, p->key[i], lp_pclone(p->kid[i]));
    return c;
}
typedef struct { const uint8_t *d; size_t n; const uint64_t *hashes; uint32_t nh; size_t rbase, rsz; int depth; } LpPR;
static LpP *lp_pnode(LpPR *r, size_t off) {
    if (off >= r->n || ++r->depth > 64) return NULL;
    uint8_t t = r->d[off]; LpP *p = NULL;
    if (t >= 1 && t <= 8) {
        if (off + 1 + g_lp_psz[t] > r->n) goto out;
        p = lp_pnew(t); const uint8_t *q = r->d + off + 1;
        switch (t) {
        case 1: case 3: p->v.i = q[0]; break; case 2: p->v.i = (int8_t)q[0]; break;
        case 4: p->v.i = (int16_t)lp_g16(q); break; case 5: p->v.i = lp_g16(q); break;
        case 6: p->v.i = (int32_t)lp_g32(q); break; case 7: p->v.i = lp_g32(q); break;
        case 8: memcpy(&p->v.f, q, 4); break;
        }
    } else if (t == 9) {
        if (off + 5 > r->n) goto out;
        uint32_t hi = lp_g32(r->d + off + 1); if (hi >= r->nh) goto out;
        p = lp_pnew(9); p->v.h = r->hashes[hi];
    } else if (t == 10) {
        if (off + 5 > r->n) goto out;
        uint32_t ro = lp_g32(r->d + off + 1); if (ro >= r->rsz) goto out;
        const char *st = (const char *)r->d + r->rbase + ro; size_t mx = r->rsz - ro, L = strnlen(st, mx);
        p = lp_pnew(10); p->s = lp_strndup(st, L);
    } else if (t == 11) {
        if (off + 5 > r->n) goto out;
        uint32_t cnt = lp_g32(r->d + off + 1); if (off + 5 + 4ull * cnt > r->n) goto out;
        p = lp_pnew(11);
        for (uint32_t i = 0; i < cnt; i++) {
            LpP *k = lp_pnode(r, off + lp_g32(r->d + off + 5 + 4 * i));
            if (!k) { lp_pfree(p); p = NULL; goto out; }
            lp_padd(p, 0, k);
        }
    } else if (t == 12) {
        if (off + 9 > r->n) goto out;
        uint32_t cnt = lp_g32(r->d + off + 1), ro = lp_g32(r->d + off + 5);
        if ((uint64_t)ro + 8ull * cnt > r->rsz) goto out;
        p = lp_pnew(12);
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t *e = r->d + r->rbase + ro + 8 * i;
            int32_t hi = (int32_t)lp_g32(e); uint32_t po = lp_g32(e + 4);
            LpP *k = hi >= 0 && (uint32_t)hi < r->nh ? lp_pnode(r, off + po) : NULL;
            if (!k) { lp_pfree(p); p = NULL; goto out; }
            lp_padd(p, r->hashes[hi], k);
        }
    }
out:
    r->depth--;
    return p;
}
static LpP *lp_prc_read(const uint8_t *d, size_t n) {
    if (n < 0x10 || memcmp(d, "paracobn", 8)) return NULL;
    uint32_t hsz = lp_g32(d + 8), rsz = lp_g32(d + 12);
    if (0x10ull + hsz + rsz > n) return NULL;
    uint64_t *h = malloc(hsz + 8); for (uint32_t i = 0; i < hsz / 8; i++) h[i] = lp_g64(d + 0x10 + 8 * i);
    LpPR r = { d, n, h, hsz / 8, 0x10 + hsz, rsz, 0 };
    LpP *p = lp_pnode(&r, 0x10 + hsz + rsz);
    free(h);
    return p;
}
// writer: members by hash, hashes numbered as first met, strings and struct member tables shared (as the game's files)
typedef struct { uint32_t fp, off, len, kind; } LpRef;
typedef struct { LpBuf out, ref; uint64_t *h; uint32_t nh, ch; uint64_t *hk; uint32_t *hv; uint32_t hmask;
                 LpRef *rk; uint32_t rmask, nr; } LpPW;
static void lp_pw_grow(LpPW *w);
static uint32_t lp_pw_hash(LpPW *w, uint64_t h) {
    lp_pw_grow(w);
    for (uint32_t j = (uint32_t)((h * 0x9e3779b97f4a7c15ull) >> 40) & w->hmask;; j = (j + 1) & w->hmask) {
        if (w->hk[j] == h + 1) return w->hv[j];
        if (!w->hk[j]) {
            if (w->nh == w->ch) { w->ch *= 2; w->h = realloc(w->h, w->ch * 8); }
            w->hk[j] = h + 1; w->hv[j] = w->nh; w->h[w->nh] = h; return w->nh++;
        }
    }
}
static void lp_pw_grow(LpPW *w) {
    if (w->nh * 2 > w->hmask) {
        uint32_t cap = (w->hmask + 1) * 2; free(w->hk); free(w->hv); w->hk = calloc(cap, 8); w->hv = calloc(cap, 4); w->hmask = cap - 1;
        for (uint32_t i = 0; i < w->nh; i++) {
            uint64_t h = w->h[i]; uint32_t j = (uint32_t)((h * 0x9e3779b97f4a7c15ull) >> 40) & w->hmask;
            while (w->hk[j]) j = (j + 1) & w->hmask;
            w->hk[j] = h + 1; w->hv[j] = i;
        }
    }
    if (w->nr * 2 > w->rmask) {
        uint32_t cap = (w->rmask + 1) * 2, om = w->rmask; LpRef *old = w->rk;
        w->rk = calloc(cap, sizeof *w->rk); w->rmask = cap - 1;
        for (uint32_t i = 0; i <= om; i++) {
            if (!old[i].fp) continue;
            uint32_t j = old[i].fp & w->rmask; while (w->rk[j].fp) j = (j + 1) & w->rmask;
            w->rk[j] = old[i];
        }
        free(old);
    }
}
static uint32_t lp_pw_ref(LpPW *w, const uint8_t *d, uint32_t n, uint32_t kind) {      /* shared: the same string / member table once in the ref table */
    lp_pw_grow(w);
    uint32_t fp = 2166136261u ^ kind; for (uint32_t i = 0; i < n; i++) fp = (fp ^ d[i]) * 16777619u; fp |= 1;
    for (uint32_t j = fp & w->rmask;; j = (j + 1) & w->rmask) {
        if (!w->rk[j].fp) { w->rk[j].fp = fp; w->rk[j].off = (uint32_t)w->ref.n; w->rk[j].len = n; w->rk[j].kind = kind; lp_put(&w->ref, d, n); w->nr++; return w->rk[j].off; }
        if (w->rk[j].fp == fp && w->rk[j].len == n && w->rk[j].kind == kind && !memcmp(w->ref.p + w->rk[j].off, d, n)) return w->rk[j].off;
    }
}
static int lp_cmp_member(const void *a, const void *b) {       /* by hash; members of one hash keep their order (a stable sort) */
    const uint64_t *p = *(const uint64_t *const *)a, *q = *(const uint64_t *const *)b;
    return *p != *q ? (*p < *q ? -1 : 1) : (p < q ? -1 : p > q);
}
static void lp_pw_node(LpPW *w, const LpP *p) {
    uint8_t t = p->t; size_t at = w->out.n;
    if (t >= 1 && t <= 8) {
        lp_put(&w->out, &t, 1);
        uint8_t b = (uint8_t)p->v.i; uint16_t s = (uint16_t)p->v.i; uint32_t u = (uint32_t)p->v.i;
        if (g_lp_psz[t] == 1) lp_put(&w->out, &b, 1); else if (g_lp_psz[t] == 2) lp_put(&w->out, &s, 2);
        else if (t == 8) lp_put(&w->out, &p->v.f, 4); else lp_put(&w->out, &u, 4);
    } else if (t == 9) { lp_put(&w->out, &t, 1); lp_put32(&w->out, lp_pw_hash(w, p->v.h)); }
    else if (t == 10) { lp_put(&w->out, &t, 1); lp_put32(&w->out, lp_pw_ref(w, (const uint8_t *)p->s, (uint32_t)strlen(p->s) + 1, 1)); }
    else if (t == 11) {
        lp_put(&w->out, &t, 1); lp_put32(&w->out, (uint32_t)p->n); size_t offs = w->out.n; lp_put(&w->out, NULL, 4ull * p->n);
        for (int i = 0; i < p->n; i++) { uint32_t o = (uint32_t)(w->out.n - at); memcpy(w->out.p + offs + 4 * i, &o, 4); lp_pw_node(w, p->kid[i]); }
    } else if (t == 12) {
        lp_put(&w->out, &t, 1); lp_put32(&w->out, (uint32_t)p->n); lp_put32(&w->out, 0);
        const uint64_t **ord = malloc((p->n + 1) * sizeof *ord);
        for (int i = 0; i < p->n; i++) ord[i] = &p->key[i];
        qsort(ord, p->n, sizeof *ord, lp_cmp_member);
        uint8_t *blk = malloc(8ull * p->n + 1);
        for (int i = 0; i < p->n; i++) {
            int k = (int)(ord[i] - p->key);
            uint32_t hi = lp_pw_hash(w, p->key[k]), o = (uint32_t)(w->out.n - at);
            memcpy(blk + 8 * i, &hi, 4); memcpy(blk + 8 * i + 4, &o, 4);
            lp_pw_node(w, p->kid[k]);
        }
        uint32_t r = lp_pw_ref(w, blk, 8u * (uint32_t)p->n, 2);
        memcpy(w->out.p + at + 5, &r, 4);
        free(blk); free(ord);
    }
}
static void lp_prc_write(const LpP *root, LpBuf *o) {
    LpPW w; memset(&w, 0, sizeof w);
    w.ch = 1024; w.h = malloc(w.ch * 8);
    uint32_t cap = 1 << 16; w.hk = calloc(cap, 8); w.hv = calloc(cap, 4); w.hmask = cap - 1;
    w.rmask = (1 << 18) - 1; w.rk = calloc(w.rmask + 1, sizeof *w.rk);
    lp_pw_hash(&w, 0);
    lp_pw_node(&w, root);
    lp_put(o, "paracobn", 8); lp_put32(o, w.nh * 8); lp_put32(o, (uint32_t)w.ref.n);
    lp_put(o, w.h, w.nh * 8ull); lp_put(o, w.ref.p, w.ref.n); lp_put(o, w.out.p, w.out.n);
    free(w.out.p); free(w.ref.p); free(w.h); free(w.hk); free(w.hv); free(w.rk);
}
// .prcxml: <struct> / <list> / <bool|sbyte|byte|short|ushort|int|uint|float|hash40|string hash="name or 0x..">value</...>
static uint64_t lp_hash_of(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return strtoull(s, NULL, 16);
    char t[512]; size_t i = 0; for (; s[i] && i < sizeof t - 1; i++) t[i] = (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]);
    t[i] = 0; return lf_hash40(t);
}
static LpP *lp_from_xml(LpX *x) {
    static const char *const names[] = { NULL, "bool", "sbyte", "byte", "short", "ushort", "int", "uint", "float", "hash40", "string", "list", "struct" };
    int t = 0; for (int i = 1; i <= 12; i++) if (!strcmp(x->tag, names[i])) { t = i; break; }
    if (!t) return NULL;
    LpP *p = lp_pnew((uint8_t)t);
    char *v = x->text ? x->text : (char *)""; size_t L = x->text ? lp_unescape(x->text, x->ntext) : 0; if (x->text) x->text[L] = 0;
    if (t != 10) { while (*v && strchr(" \t\r\n", *v)) v++; char *e = v + strlen(v); while (e > v && strchr(" \t\r\n", e[-1])) *--e = 0; }
    if (t == 1) p->v.i = !strcmp(v, "true") || !strcmp(v, "True") || !strcmp(v, "1");
    else if (t >= 2 && t <= 7) p->v.i = strtoll(v, NULL, 0);
    else if (t == 8) p->v.f = strtof(v, NULL);
    else if (t == 9) p->v.h = lp_hash_of(v);
    else if (t == 10) p->s = _strdup(v);
    else for (int i = 0; i < x->nk; i++) {
        LpP *k = lp_from_xml(x->kid[i]); if (!k) continue;
        const char *h = lp_xattr(x->kid[i], "hash");
        lp_padd(p, t == 12 && h ? lp_hash_of(h) : 0, k);
    }
    return p;
}
static LpP *lp_prc_patch_read(const char *path) {
    size_t n; uint8_t *raw = lp_read_file(path, &n); if (!raw) return NULL;
    LpP *p = NULL;
    if (n >= 8 && !memcmp(raw, "paracobn", 8)) p = lp_prc_read(raw, n);
    else { char *s = lp_utf8(raw, n); LpX *x = lp_xparse(s); free(s); if (x) { p = lp_from_xml(x); lp_xfree(x); } }
    free(raw);
    return p && p->t == 12 ? p : (lp_pfree(p), NULL);
}
// the prcx crate's apply_patch: 0 = done, -1 = a value of another type (the source may be half patched: the caller works on a copy)
static int lp_papply(const LpP *pt, LpP *src) {
    if (pt->t != src->t) return -1;
    if (pt->t == 12) {
        for (int i = 0; i < pt->n; i++) {
            int nth = 0; for (int j = 0; j < i; j++) nth += pt->key[j] == pt->key[i];
            int found = -1; for (int j = 0; j < src->n; j++) if (src->key[j] == pt->key[i] && !nth--) { found = j; break; }
            if (found >= 0) { if (lp_papply(pt->kid[i], src->kid[found])) return -1; }
            else lp_padd(src, pt->key[i], lp_pclone(pt->kid[i]));
        }
    } else if (pt->t == 11) {
        static uint64_t dummy; if (!dummy) dummy = lf_hash40("dummy");
        for (int i = 0; i < pt->n; i++) {
            if (pt->kid[i]->t == 9 && pt->kid[i]->v.h == dummy) continue;
            if (i < src->n) { if (lp_papply(pt->kid[i], src->kid[i])) return -1; }
            else lp_padd(src, 0, lp_pclone(pt->kid[i]));
        }
    } else { src->v = pt->v; free(src->s); src->s = pt->s ? _strdup(pt->s) : NULL; }
    return 0;
}

/* ---- the patched file: the base bytes (the game's decompressed file, or the folder's whole file) and the patches ---- */
// patches [first, first + n) of one target, the current language's; out = the result. 0 = nothing applied.
static int lp_build(const uint8_t *base, size_t bn, int first, int n, const char *region, const char *what, LpBuf *out) {
    int kind = g_lp[first].kind, applied = 0, failed = 0; long entries = 0;
    if (kind == LP_XMSBT) {
        LpMsbt m;
        if (!lp_msbt_read(&m, base, bn)) { lf_logf("[loose] PATCH %s: the base is not a readable msbt (%zu bytes): patches NOT applied", what, bn); lp_msbt_free(&m); return 0; }
        for (int i = first; i < first + n; i++) {
            if (g_lp[i].region[0] && strcmp(g_lp[i].region, region)) continue;
            int r = lp_xmsbt_apply(&m, g_lp[i].disk);
            if (r < 0) { failed++; lf_logf("[loose] PATCH %s: %s is not readable: left out", what, g_lp[i].disk); continue; }
            applied++; entries += r;
        }
        if (applied) lp_msbt_write(&m, out);
        lf_logf("[loose] PATCH %s (%s): %d xmsbt patches applied (%ld labels; %d left out), %d labels now, %zu -> %zu bytes", what, region, applied, entries, failed, m.nlab, bn, out->n);
        lp_msbt_free(&m);
    } else {
        LpP *root = lp_prc_read(base, bn);
        if (!root) { lf_logf("[loose] PATCH %s: the base is not a readable param file (%zu bytes): patches NOT applied", what, bn); return 0; }
        for (int i = first; i < first + n; i++) {
            if (g_lp[i].region[0] && strcmp(g_lp[i].region, region)) continue;
            LpP *pt = lp_prc_patch_read(g_lp[i].disk);
            if (!pt) { failed++; lf_logf("[loose] PATCH %s: %s is not a readable param patch: left out", what, g_lp[i].disk); continue; }
            LpP *trial = lp_pclone(root);
            if (lp_papply(pt, trial)) { failed++; lp_pfree(trial); lf_logf("[loose] PATCH %s: %s gives a value of another type than the file's: left out", what, g_lp[i].disk); }
            else { lp_pfree(root); root = trial; applied++; }
            lp_pfree(pt);
        }
        if (applied) lp_prc_write(root, out);
        lf_logf("[loose] PATCH %s: %d param patches applied (%d left out), %zu -> %zu bytes", what, applied, failed, bn, out->n);
        lp_pfree(root);
    }
    return applied;
}
