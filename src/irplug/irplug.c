/* Wrapper module for Aqara mha_ir: start it as `mha_ir -r /data/esphome-ir/libirplug.so`.
 * DT_NEEDED pulls in the stock /lib/libha_ir.so. mha_ir's dlsym("ha_ir_open") hits ours first;
 * it points libha_ir's GOT slot for protocol_ac_product_ircode at our version, then chains to
 * the stock ha_ir_open. (uClibc 0.9.33's MIPS ld.so binds a library's own symbols to itself,
 * so plain symbol interposition does not work; the GOT is in the RW segment, no text patch.)
 * r_id == IRPLUG_MAGIC hands the AC state to the ESPHome-built irgen, which owns the
 * model choice (its YAML config); every other r_id goes to the original code.
 * Built freestanding against uClibc 0.9.33 symbols, so prototypes are declared here. */
#define IRPLUG_MAGIC 1189998819 /* 0118 999 881 9(99 119 725 3), cut to fit libha_ir's int r_id */
#define IRGEN "/data/esphome-ir/irgen"
/* /lib/libha_ir.so md5 2b73769db0b7569344ea5ca58fd925cc */
#define OFF_SYNC 0x20a18     /* protocol_ac_sync: exported, used to find the load base */
#define OFF_OPEN 0x662c      /* ha_ir_open */
#define OFF_GOT_PRODUCT 0x3c9a8 /* global GOT slot of protocol_ac_product_ircode */
#define MAX_TIMINGS 510 /* libha_ir buffer is 512 u16; ircode_num_valid wants even and < 1000 */

typedef struct FILE FILE;
FILE *popen(const char *cmd, const char *mode);
int pclose(FILE *f);
int fgetc(FILE *f);
int snprintf(char *buf, unsigned long n, const char *fmt, ...);
void protocol_ac_sync(void);

typedef int (*product_fn)(unsigned, unsigned, int, const unsigned char *, unsigned char *);
static product_fn orig_product;
static unsigned short timings[512];

/* state: +0 power (1 = off), +4 mode, +9 temp, +12 fan, +16 swing (libha_ir ac_status layout).
 * out: u16 count at +0, u16 *timings at +4. */
static int product(unsigned brand, unsigned rid, int a3, const unsigned char *state, unsigned char *out) {
  if (rid != IRPLUG_MAGIC) return orig_product(brand, rid, a3, state, out);
  char cmd[160];
  snprintf(cmd, sizeof cmd, "AQARA_AC=%d,%d,%d,%d,%d ESPHOME_PREFDIR=/tmp " IRGEN, *(const int *)state,
           *(const int *)(state + 4), state[9], *(const int *)(state + 12), *(const int *)(state + 16));
  FILE *p = popen(cmd, "r");
  if (!p) return 0;
  unsigned n = 0, total = 0, v = 0;
  int c, digits = 0;
  while ((c = fgetc(p)) >= 0) {
    if (c >= '0' && c <= '9') {
      v = v * 10 + (c - '0');
      digits = 1;
    } else if (digits) {
      if (n < MAX_TIMINGS) timings[n++] = v > 65535 ? 65535 : v;
      total++;
      v = digits = 0;
    }
  }
  pclose(p); /* never returns 0 inside mha_ir (likely SIGCHLD ignored), so judge by the output */
  int ok = n > 0 && n == total && !(n & 1);
  *(unsigned short **)(out + 4) = timings;
  *(unsigned short *)out = ok ? n : 0;
  return ok;
}

int ha_ir_open(void *a1, int a2) {
  char *base = (char *)protocol_ac_sync - OFF_SYNC;
  product_fn *slot = (product_fn *)(base + OFF_GOT_PRODUCT);
  if (*slot != product) {
    orig_product = *slot;
    *slot = product;
  }
  return ((int (*)(void *, int))(base + OFF_OPEN))(a1, a2);
}
