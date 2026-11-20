/* Wrapper module for Aqara mha_ir: start it as `mha_ir -r /data/esphome-ir/libirplug.so`.
 * DT_NEEDED pulls in the stock /lib/libha_ir.so. mha_ir's dlsym("ha_ir_open") hits ours first;
 * it points libha_ir's GOT slots for protocol_ac_product_ircode (encode, IR out) and
 * protocol_ac_sync (decode, IR in from the physical remote) at our versions, then chains to the
 * stock ha_ir_open. (uClibc 0.9.33's MIPS ld.so binds a library's own symbols to itself, so plain
 * symbol interposition does not work; the GOT is in the RW segment, no text patch.)
 * r_id == IRPLUG_MAGIC hands the work to the ESPHome-built irgen, which owns the model choice
 * (its YAML config); every other r_id goes to the original code.
 * Built freestanding against uClibc 0.9.33 symbols, so prototypes are declared here. */
#define IRPLUG_MAGIC 1189998819 /* 0118 999 881 9(99 119 725 3), cut to fit libha_ir's int r_id */
#define IRGEN "ESPHOME_PREFDIR=/tmp /data/esphome-ir/irgen"
/* /lib/libha_ir.so md5 2b73769db0b7569344ea5ca58fd925cc */
#define OFF_SYNC 0x20a18        /* protocol_ac_sync: exported, used to find the load base */
#define OFF_OPEN 0x662c         /* ha_ir_open */
#define OFF_GOT_PRODUCT 0x3c9a8 /* global GOT slot of protocol_ac_product_ircode */
#define OFF_GOT_SYNC 0x3c9ac    /* global GOT slot of protocol_ac_sync */
#define MAX_TIMINGS 998 /* mha_ir's ircode_num_valid wants even and < 1000; our own buffer below */

typedef struct FILE FILE;
FILE *popen(const char *cmd, const char *mode);
int pclose(FILE *f);
int fgetc(FILE *f);
int snprintf(char *buf, unsigned long n, const char *fmt, ...);
void protocol_ac_sync(void);

typedef int (*product_fn)(unsigned, unsigned, int, const unsigned char *, unsigned char *);
typedef int (*sync_fn)(unsigned, unsigned, unsigned, const unsigned short *, unsigned char *);
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

int ha_ir_open(void *a1, int a2) {
  char *base = (char *)protocol_ac_sync - OFF_SYNC;
  product_fn *ps = (product_fn *)(base + OFF_GOT_PRODUCT);
  sync_fn *ss = (sync_fn *)(base + OFF_GOT_SYNC);
  if (*ps != product) {
    orig_product = *ps;
    *ps = product;
  }
  if (*ss != rx_sync) {
    orig_sync = *ss;
    *ss = rx_sync;
  }
  return ((int (*)(void *, int))(base + OFF_OPEN))(a1, a2);
}
