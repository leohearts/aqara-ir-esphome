/* Wrapper module for Aqara mha_ir: start it as `mha_ir -r /data/esphome-ir/libirplug.so`.
 * DT_NEEDED pulls in the stock /lib/libha_ir.so. mha_ir's dlsym("ha_ir_open") hits ours first;
 * it points libha_ir's GOT slots for protocol_ac_product_ircode (encode, IR out) and
 * protocol_ac_sync (decode, IR in from the physical remote) at our versions, then chains to the
 * stock ha_ir_open. (uClibc 0.9.33's MIPS ld.so binds a library's own symbols to itself, so plain
 * symbol interposition does not work; the GOT is in the RW segment, no text patch.)
 * The addresses are looked up at run time in libha_ir's dynamic symbol table, so other builds work
 * as long as they export these functions; if a lookup fails, nothing is patched (stock behaviour).
 * What cannot be checked here is the layout of the state structs below (known good: libha_ir.so
 * md5 2b73769db0b7569344ea5ca58fd925cc, firmware 4.5.20_0033.0652).
 * The result is written to /tmp/irplug.status.
 * r_id == IRPLUG_MAGIC hands the work to the ESPHome-built irgen, which owns the model choice
 * (its YAML config); every other r_id goes to the original code.
 * Built freestanding against uClibc 0.9.33 symbols, so prototypes are declared here. */
#define IRPLUG_MAGIC 1189998819 /* 0118 999 881 9(99 119 725 3), cut to fit libha_ir's int r_id */
#define IRGEN "ESPHOME_PREFDIR=/tmp /data/esphome-ir/irgen"
#define MAX_TIMINGS 998 /* mha_ir's ircode_num_valid wants even and < 1000; our own buffer below */

typedef struct FILE FILE;
FILE *popen(const char *cmd, const char *mode);
int pclose(FILE *f);
int fgetc(FILE *f);
int snprintf(char *buf, unsigned long n, const char *fmt, ...);
int open(const char *path, int flags, ...);
long write(int fd, const void *buf, unsigned long n);
int close(int fd);
void protocol_ac_sync(void); /* any libha_ir export: an address inside its first segment */

typedef int (*product_fn)(unsigned, unsigned, int, const unsigned char *, unsigned char *);
typedef int (*sync_fn)(unsigned, unsigned, unsigned, const unsigned short *, unsigned char *);
typedef int (*open_fn)(void *, int);
static product_fn orig_product;
static sync_fn orig_sync;
static unsigned short timings[MAX_TIMINGS];

/* Runs cmd, stores the decimal numbers it prints in out[] (at most max); returns how many it printed.
 * pclose() never returns 0 inside mha_ir (likely SIGCHLD ignored), so callers judge by the output. */
static unsigned run_numbers(const char *cmd, unsigned short *out, unsigned max) {
  FILE *p = popen(cmd, "r");
  if (!p) return 0;
  unsigned n = 0, v = 0;
  int c, digits = 0;
  while ((c = fgetc(p)) >= 0) {
    if (c >= '0' && c <= '9') {
      v = v * 10 + (c - '0');
      digits = 1;
    } else if (digits) {
      if (n < max) out[n] = v > 65535 ? 65535 : v;
      n++;
      v = digits = 0;
    }
  }
  pclose(p);
  return n;
}

/* state: +0 power (1 = off), +4 mode, +9 temp, +12 fan, +16 swing (libha_ir ac_status layout).
 * out: u16 count at +0, u16 *timings at +4. */
static int product(unsigned brand, unsigned rid, int a3, const unsigned char *state, unsigned char *out) {
  if (rid != IRPLUG_MAGIC) return orig_product(brand, rid, a3, state, out);
  char cmd[160];
  snprintf(cmd, sizeof cmd, "AQARA_AC=%d,%d,%d,%d,%d " IRGEN, *(const int *)state, *(const int *)(state + 4),
           state[9], *(const int *)(state + 12), *(const int *)(state + 16));
  unsigned n = run_numbers(cmd, timings, MAX_TIMINGS);
  int ok = n > 0 && n <= MAX_TIMINGS && !(n & 1);
  *(unsigned short **)(out + 4) = timings;
  *(unsigned short *)out = ok ? n : 0;
  return ok;
}

/* rx: n received timings (us, mark first). st: libha_ir's 24-byte "formate" status, preset to
 * "unknown" by ac_status_formate_init: +0 P, +4 M, +8 temp valid, +9 T, +10 fan valid, +12 S, +16 D.
 * Returns 1 when decoded; mha_ir then reports the state to the apps. */
static int rx_sync(unsigned brand, unsigned rid, unsigned n, const unsigned short *rx, unsigned char *st) {
  if (rid != IRPLUG_MAGIC) return orig_sync(brand, rid, n, rx, st);
  static char cmd[8 * 1024];
  unsigned len = snprintf(cmd, sizeof cmd, "AQARA_RX=");
  for (unsigned i = 0; i < n && len < sizeof cmd - 128; i++)
    len += snprintf(cmd + len, sizeof cmd - len, i ? ",%u" : "%u", rx[i]);
  /* keep the last raw frame for debugging remotes */
  snprintf(cmd + len, sizeof cmd - len, "; echo $AQARA_RX > /tmp/irplug.rx; AQARA_RX=$AQARA_RX " IRGEN);
  unsigned short v[5];
  if (run_numbers(cmd, v, 5) != 5) return 0;
  *(int *)st = v[0];
  *(int *)(st + 4) = v[1];
  st[8] = 1;
  st[9] = v[2];
  st[10] = 1;
  *(int *)(st + 12) = v[3];
  *(int *)(st + 16) = v[4];
  return 1;
}

/* libha_ir's dynamic section, read from its mapped image. */
static struct {
  char *base;
  const char *strtab;
  const unsigned *symtab; /* Elf32_Sym: name, value, size, info/other/shndx */
  unsigned *got;
  unsigned symtabno, local_gotno, gotsym;
} lib;

static int streq(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}

static int load_lib(void) {
  /* The first PT_LOAD maps the file from offset 0 and contains the export: walk back to the ELF header. */
  char *p = (char *)((unsigned)protocol_ac_sync & ~0xfffu);
  for (int i = 0; i < 4096 && !(p[0] == 0x7f && p[1] == 'E' && p[2] == 'L' && p[3] == 'F'); i++) p -= 0x1000;
  if (p[0] != 0x7f || p[1] != 'E') return 0;
  lib.base = p;
  const unsigned *ph = (const unsigned *)(p + *(unsigned *)(p + 0x1c));
  const unsigned *dyn = 0;
  for (unsigned i = 0, n = *(unsigned short *)(p + 0x2c); i < n; i++, ph += 8)
    if (ph[0] == 2 /* PT_DYNAMIC */) dyn = (const unsigned *)(p + ph[2]);
  if (!dyn) return 0;
  /* d_ptr values may or may not be relocated by ld.so */
#define PTR(v) ((v) < (unsigned)p ? p + (v) : (char *)(v))
  for (; dyn[0]; dyn += 2) switch (dyn[0]) {
      case 3: lib.got = (unsigned *)PTR(dyn[1]); break;                   /* DT_PLTGOT */
      case 5: lib.strtab = PTR(dyn[1]); break;                             /* DT_STRTAB */
      case 6: lib.symtab = (const unsigned *)PTR(dyn[1]); break;           /* DT_SYMTAB */
      case 0x7000000a: lib.local_gotno = dyn[1]; break;                    /* DT_MIPS_LOCAL_GOTNO */
      case 0x70000011: lib.symtabno = dyn[1]; break;                       /* DT_MIPS_SYMTABNO */
      case 0x70000013: lib.gotsym = dyn[1]; break;                         /* DT_MIPS_GOTSYM */
    }
  return lib.got && lib.strtab && lib.symtab && lib.symtabno;
}

/* Address of a defined export, and its global GOT slot (null when it has none). */
static char *lookup(const char *name, unsigned **slot) {
  for (unsigned i = 1; i < lib.symtabno; i++) {
    const unsigned *s = lib.symtab + 4 * i;
    if (s[1] == 0 || !streq(lib.strtab + s[0], name)) continue;
    char *addr = lib.base + s[1];
    if (slot) *slot = i >= lib.gotsym ? lib.got + lib.local_gotno + (i - lib.gotsym) : 0;
    return addr;
  }
  return 0;
}

static void status(const char *text) {
  int fd = open("/tmp/irplug.status", 1 | 0x100 | 0x200, 0644); /* MIPS O_WRONLY|O_CREAT|O_TRUNC */
  if (fd < 0) return;
  unsigned n = 0;
  while (text[n]) n++;
  write(fd, text, n);
  close(fd);
}

int ha_ir_open(void *a1, int a2) {
  static open_fn stock_open;
  unsigned *ps = 0, *ss = 0;
  char *pa = 0, *sa = 0;
  if (!stock_open) {
    if (!load_lib() || !(stock_open = (open_fn)lookup("ha_ir_open", 0))) {
      status("libha_ir.so: dynamic section or ha_ir_open not found\n");
      return -1;
    }
    pa = lookup("protocol_ac_product_ircode", &ps);
    sa = lookup("protocol_ac_sync", &ss);
  }
  char msg[200];
  /* patch only slots that hold exactly the function they should: anything else means we misread */
  if (ps && ss && *ps == (unsigned)pa && *ss == (unsigned)sa) {
    orig_product = (product_fn)pa;
    orig_sync = (sync_fn)sa;
    *ps = (unsigned)product;
    *ss = (unsigned)rx_sync;
    snprintf(msg, sizeof msg, "patched: base %p, product %p (got +0x%x), sync %p (got +0x%x), open %p\n",
             lib.base, pa, (unsigned)((char *)ps - lib.base), sa, (unsigned)((char *)ss - lib.base), stock_open);
    status(msg);
  } else if (pa || sa || ps) {
    snprintf(msg, sizeof msg, "not patched, stock behaviour: product %p slot %p, sync %p slot %p\n", pa, ps, sa, ss);
    status(msg);
  }
  return stock_open(a1, a2);
}
