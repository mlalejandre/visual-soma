#include "image.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "httpd.h"
#include "shell.h"
#include "console.h"
#include "fs.h"
#include "mem.h"
#include "thread.h"
#include "mutex.h"
#include "pmm.h"
#include "vmm.h"
#include "sysinfo.h"
#include "pci.h"
#include "net.h"
#include "ip.h"
#include "dns.h"
#include "http.h"
#include "virtio_blk.h"
#include "srcfs.h"
#include "boot_gate.h"
#include "rtc.h"
#include "nano.h"
#include "agent.h"
#include "llm.h"
#include "io.h"
#include "arch.h"
#include "idt.h"
#include "disk_layout.h"
#include "ramstat.h"

#ifndef SECTOR_USER_MIN
#define SECTOR_USER_MIN 3600
#endif

/* ====================================================================
 * UTILIDADES DE CADENA Y COMPARACIÓN
 * ==================================================================== */

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static char to_lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static const char *find_substr_ci(const char *haystack, const char *needle, int ignore_case)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n) {
            char ch = ignore_case ? to_lower_char(*h) : *h;
            char cn = ignore_case ? to_lower_char(*n) : *n;
            if (ch != cn) break;
            h++; n++;
        }
        if (!*n) return haystack;
    }
    return 0;
}

/* ====================================================================
 * SUBSISTEMA DE VARIABLES DE ENTORNO (ENV BARE-METAL)
 * ==================================================================== */

#define MAX_ENV_VARS 32
#define ENV_KEY_LEN  32
#define ENV_VAL_LEN  96

struct env_var {
    char key[ENV_KEY_LEN];
    char val[ENV_VAL_LEN];
    int  used;
    int  pad;
} __attribute__((aligned(8)));

static struct env_var env_table[MAX_ENV_VARS];
static int env_initialized = 0;

static const char *pipe_stdin_data = 0;
static uint32_t    pipe_stdin_len  = 0;

static void env_init_defaults(void)
{
    if (env_initialized) return;
    for (int i = 0; i < MAX_ENV_VARS; ++i) env_table[i].used = 0;

    int idx = 0;
    #define SET_DEF(k, v) do { \
        uint32_t p=0; while((k)[p] && p < ENV_KEY_LEN-1){ env_table[idx].key[p]=(k)[p]; p++; } env_table[idx].key[p]='\0'; \
        p=0; while((v)[p] && p < ENV_VAL_LEN-1){ env_table[idx].val[p]=(v)[p]; p++; } env_table[idx].val[p]='\0'; \
        env_table[idx].used = 1; idx++; \
    } while(0)

    SET_DEF("USER", "marcos");
    SET_DEF("HOST", "soma-node1");
    SET_DEF("ARCH", MYOS_ARCH_NAME);
    SET_DEF("OS", "SOMA-0.2");
    SET_DEF("SHELL", "/bin/soma");
    SET_DEF("PWD", "/");
    #undef SET_DEF

    env_initialized = 1;
}

static const char *env_get(const char *key)
{
    if (!env_initialized) env_init_defaults();
    if (!key || key[0] == '\0') return "";

    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used && str_eq(env_table[i].key, key)) {
            return env_table[i].val;
        }
    }
    return "";
}

static int env_set(const char *key, const char *val)
{
    if (!env_initialized) env_init_defaults();
    if (!key || key[0] == '\0') return -1;

    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used && str_eq(env_table[i].key, key)) {
            uint32_t p = 0;
            while (val && val[p] && p < ENV_VAL_LEN - 1) { env_table[i].val[p] = val[p]; p++; }
            env_table[i].val[p] = '\0';
            return 0;
        }
    }
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (!env_table[i].used) {
            uint32_t p = 0;
            while (key[p] && p < ENV_KEY_LEN - 1) { env_table[i].key[p] = key[p]; p++; }
            env_table[i].key[p] = '\0';
            p = 0;
            while (val && val[p] && p < ENV_VAL_LEN - 1) { env_table[i].val[p] = val[p]; p++; }
            env_table[i].val[p] = '\0';
            env_table[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static int env_unset(const char *key)
{
    if (!env_initialized) env_init_defaults();
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used && str_eq(env_table[i].key, key)) {
            env_table[i].used = 0;
            return 0;
        }
    }
    return -1;
}

static void env_expand(const char *src, char *dst, uint32_t max_dst)
{
    if (!env_initialized) env_init_defaults();
    if (!src || !dst || max_dst == 0) return;

    uint32_t dp = 0;
    while (*src && dp < max_dst - 1) {
        if (*src == '$' && *(src + 1) != '\0' && *(src + 1) != ' ' && *(src + 1) != '"' && *(src + 1) != '\'') {
            src++;
            char k_buf[ENV_KEY_LEN];
            uint32_t kp = 0;
            while ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') || (*src >= '0' && *src <= '9') || *src == '_') {
                if (kp < sizeof(k_buf) - 1) k_buf[kp++] = *src;
                src++;
            }
            k_buf[kp] = '\0';
            const char *val = env_get(k_buf);
            while (*val && dp < max_dst - 1) dst[dp++] = *val++;
        } else {
            dst[dp++] = *src++;
        }
    }
    dst[dp] = '\0';
}

/* ====================================================================
 * FORMATEO DE BUFFER Y HARDWARE
 * ==================================================================== */

struct bg_job {
    char cmd[256];
    uint32_t tid;
    char log_file[64];
    uint8_t agent_origin;
};

/* AGENT_POLICY_V1: frontera de privilegios del Agente ReAct. */
/* PARCHE 046: el modo Agente es POR HILO (tcb.agent_mode). Con un flag global,
 * un job del agente (spawn) que dormia heredaba privilegios completos cuando
 * otro hilo restauraba el flag a 0. */
static int agent_mode_fallback = 0;
static int *agent_mode_ptr(void)
{
    struct tcb *t = thread_current();
    return t ? &t->agent_mode : &agent_mode_fallback;
}
#define agent_dispatch_mode (*agent_mode_ptr())

int dispatch_command(const char *cmd_line, char *out_buf, uint32_t max_out);

int dispatch_agent_command(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    int previous = agent_dispatch_mode;
    agent_dispatch_mode = 1;

    int result = dispatch_command(cmd_line, out_buf, max_out);

    agent_dispatch_mode = previous;
    return result;
}

static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    if (!out || max == 0 || !pos || *pos >= max - 1) return;
    while (*s && *pos + 1 < max) {
        out[(*pos)++] = *s++;
    }
    out[*pos] = '\0';
}

static void fb_putc(char *out, uint32_t max, uint32_t *pos, char c)
{
    if (!out || max == 0) return;
    if (*pos + 1 < max) {
        out[(*pos)++] = c;
        out[*pos] = '\0';
    }
}

static void fb_put_dec(char *out, uint32_t max, uint32_t *pos, uint64_t v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) { fb_putc(out, max, pos, '0'); return; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) fb_putc(out, max, pos, tmp[--n]);
}

static void fb_put_hex16(char *out, uint32_t max, uint32_t *pos, uint16_t v)
{
    const char hex[] = "0123456789ABCDEF";
    fb_putc(out, max, pos, hex[(v >> 12) & 0xF]);
    fb_putc(out, max, pos, hex[(v >> 8) & 0xF]);
    fb_putc(out, max, pos, hex[(v >> 4) & 0xF]);
    fb_putc(out, max, pos, hex[v & 0xF]);
}

static void fb_put_hex32(char *out, uint32_t max, uint32_t *pos, uint32_t v)
{
    fb_put_hex16(out, max, pos, (uint16_t)(v >> 16));
    fb_put_hex16(out, max, pos, (uint16_t)v);
}

static void parse_ip(const char *s, uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        uint32_t val = 0;
        while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
        ip[i] = (uint8_t)val;
        if (*s == '.') s++;
    }
}

static uint64_t parse_num(const char **str)
{
    uint64_t val = 0;
    while (**str == ' ') (*str)++;
    while (**str >= '0' && **str <= '9') { val = val * 10 + (**str - '0'); (*str)++; }
    return val;
}

static void system_reboot(void)
{
    kprint("\n[REBOOT] Sincronizando discos...\n");
    vfs_sync();
#ifdef __x86_64__
    for (int i = 0; i < 10000; ++i) if ((inb(0x64) & 0x02) == 0) break;
    outb(0x64, 0xFE);
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idtr = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(null_idtr));
#endif
    for (;;) { arch_interrupts_disable(); arch_halt(); }
}

static void system_poweroff(void)
{
    kprint("\n[POWEROFF] Sincronizando discos...\n");
    vfs_sync();
#ifdef __x86_64__
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outb(0xF4, 0x00);
#endif
    for (;;) { arch_interrupts_disable(); arch_halt(); }
}

static const char *pci_device_name(uint16_t vendor, uint16_t device)
{
    if (vendor == 0x8086 && device == 0x1237) return "Intel 440FX Host Bridge";
    if (vendor == 0x8086 && device == 0x7000) return "Intel PIIX3 ISA Bridge";
    if (vendor == 0x8086 && device == 0x7010) return "Intel PIIX3 IDE";
    if (vendor == 0x8086 && device == 0x7113) return "Intel PIIX4 ACPI";
    if (vendor == 0x1234 && device == 0x1111) return "QEMU Standard VGA";
    if (vendor == 0x1AF4 && (device == 0x1000 || device == 0x1041)) return "VirtIO-NET Adapter";
    if (vendor == 0x1AF4 && (device == 0x1001 || device == 0x1042)) return "VirtIO-BLK Storage";
    return "Dispositivo PCI generico";
}

void pci_scan(void)
{
    kprint("\nPCI SCAN\n--------\n");
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFF) continue;
                uint16_t dev = pci_device_id((uint8_t)bus, slot, func);
                kprint("PCI "); kprint_hex16(bus); kputc(':'); kprint_dec(slot); kputc('.'); kprint_dec(func);
                kprint(" vendor=0x"); kprint_hex16(vendor); kprint(" device=0x"); kprint_hex16(dev);
                if (vendor == 0x1AF4 && (dev == 0x1000 || dev == 0x1041)) kprint("  <-- VIRTIO-NET");
                if (vendor == 0x1AF4 && (dev == 0x1001 || dev == 0x1042)) kprint("  <-- VIRTIO-BLK");
                kprint("\n");
            }
        }
    }
}

static int pci_format_scan(char *out, uint32_t max)
{
    uint32_t pos = 0;
    fb_puts(out, max, &pos, "Dispositivos PCI detectados:\n");
    uint32_t count = 0;

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFF) continue;
                uint16_t dev = pci_device_id((uint8_t)bus, slot, func);
                count++;

                fb_puts(out, max, &pos, "  PCI ");
                fb_put_hex16(out, max, &pos, bus);
                fb_putc(out, max, &pos, ':');
                fb_put_dec(out, max, &pos, slot);
                fb_putc(out, max, &pos, '.');
                fb_put_dec(out, max, &pos, func);
                fb_puts(out, max, &pos, " [0x");
                fb_put_hex16(out, max, &pos, vendor);
                fb_puts(out, max, &pos, ":0x");
                fb_put_hex16(out, max, &pos, dev);
                fb_puts(out, max, &pos, "] ");
                fb_puts(out, max, &pos, pci_device_name(vendor, dev));

                if (vendor == 0x1AF4) {
                    uint32_t bar0 = pci_bar0((uint8_t)bus, slot, func);
                    fb_puts(out, max, &pos, " (BAR0=0x");
                    fb_put_hex32(out, max, &pos, bar0 & 0xFFFFFFFC);
                    fb_putc(out, max, &pos, ')');
                }
                fb_putc(out, max, &pos, '\n');
            }
        }
    }
    return (int)pos;
}

static void do_hexdump(const char *filename, uint32_t max_bytes, char *out_buf, uint32_t max_out)
{
    static char hbuf[4096];
    uint32_t pos = 0;
    int r = vfs_read(filename, hbuf, sizeof(hbuf));
    if (r < 0) {
        kprint("hexdump: archivo no encontrado: '"); kprint(filename); kprint("'\n");
        if (out_buf) fb_puts(out_buf, max_out, &pos, "hexdump: archivo no encontrado");
        return;
    }
    uint32_t total = (uint32_t)r;
    uint32_t limit = (max_bytes > 0 && max_bytes < total) ? max_bytes : total;
    if (limit > 128) limit = 128;

    kprint("\nHEXDUMP de '"); kprint(filename); kprint("' ("); kprint_dec(total); kprint(" bytes):\n");
    const char hexchars[] = "0123456789ABCDEF";
    for (uint32_t off = 0; off < limit; off += 16) {
        for (int sh = 28; sh >= 0; sh -= 4) kputc(hexchars[(off >> sh) & 0x0F]);
        kprint(": ");
        for (uint32_t i = 0; i < 16; ++i) {
            if (off + i < limit) {
                uint8_t b = (uint8_t)hbuf[off + i];
                kputc(hexchars[(b >> 4) & 0x0F]);
                kputc(hexchars[b & 0x0F]);
                kputc(' ');
            } else {
                kprint("   ");
            }
            if (i == 7) kputc(' ');
        }
        kprint(" |");
        for (uint32_t i = 0; i < 16 && off + i < limit; ++i) {
            uint8_t c = (uint8_t)hbuf[off + i];
            kputc((c >= 32 && c < 127) ? (char)c : '.');
        }
        kprint("|\n");
    }
    if (out_buf) fb_puts(out_buf, max_out, &pos, "[HEXDUMP OK]");
}

void show_somafetch(char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000);
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;

    size_t f_free = 0, f_used = 0, f_total = 0;
    pmm_get_stats(&f_free, &f_used, &f_total);
    uint32_t ram_tot = (uint32_t)((f_total * 4096) / (1024 * 1024));
    uint32_t ram_usd = (uint32_t)((f_used * 4096) / (1024 * 1024));
    uint32_t ram_pct = f_total ? (uint32_t)((f_used * 100) / f_total) : 0;

    size_t h_used = 0, h_free = 0;
    kheap_stats(&h_used, &h_free);

    kprint("\n");
    kprint("\033[1;36m  ____  ____  __  __    _      \033[1;33mOS:     \033[1;37mSOMA v0.2 \033[0;36m(Multi-Agente)\033[0m\n");
    kprint("\033[1;36m / ___|/ __ \\|  \\/  |  / \\     \033[1;33mKernel: \033[1;37m" MYOS_ARCH_NAME " Bare-Metal \033[1;32m(HAL v1)\033[0m\n");
    kprint("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37m");
    if (h > 0) { kprint_dec(h); kprint("h "); }
    if (m > 0 || h > 0) { kprint_dec(m); kprint("m "); }
    kprint_dec(s); kprint("s \033[0;32m(PIT 1 kHz)\033[0m\n");
    kprint("\033[1;36m  ___)| |__| | |  | |/ ___ \\   \033[1;33mCPU:    \033[1;37mRing 0 \033[1;35m(CR0:WP, NXE)\033[0m\n");
    kprint("\033[1;36m |____/\\____/|_|  |_/_/   \\_\\  \033[1;33mRAM:    \033[1;37m");
    kprint_dec(ram_usd); kprint("/"); kprint_dec(ram_tot); kprint(" MiB \033[1;32m("); kprint_dec(ram_pct); kprint("% PMM)\033[0m\n");
    kprint("                               \033[1;33mHeap:   \033[1;37m");
    kprint_dec((uint32_t)(h_free / 1024)); kprint(" KiB libres\033[0m\n");
    kprint("                               \033[1;33mDisco:  \033[1;37mRamFS (16 MiB virtio-blk)\033[0m\n");
    kprint("                               \033[1;33mRed:    \033[1;37m10.0.2.15 (VirtIO-NET NAT)\033[0m\n");
    kprint("                               \033[1;33mAgente: \033[1;37mnail-35b (ReAct LAN)\033[0m\n\n");

    fb_puts(out_buf, max_out, &pos, "SOMA v0.2 Operational");
}

static void spawn_job_worker(void *arg)
{
    struct bg_job *job = (struct bg_job *)arg;
    if (!job) return;

    char *out_buf = (char *)kmalloc(2048);
    if (out_buf) {
        out_buf[0] = '\0';
        if (job->agent_origin) {
            dispatch_agent_command(job->cmd, out_buf, 2048);
        } else {
            dispatch_command(job->cmd, out_buf, 2048);
        }
        if (out_buf[0] != '\0') {
            uint32_t len = 0; while (out_buf[len]) len++;
            vfs_write(job->log_file, out_buf, len);
        }
        kfree(out_buf);
    }

    kprint("\n[JOB TID "); kprint_dec(job->tid); kprint(" FINALIZADO -> "); kprint(job->log_file); kprint("]\nsoma> ");
    kfree(job);
}

/* ====================================================================
 * MANEJADORES DE COMANDO DECLARATIVOS (cmd_handler_t)
 * ==================================================================== */

static int cmd_help(const char *args, char *out_buf, uint32_t max_out);

static int cmd_somafetch(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    show_somafetch(out_buf, max_out);
    return 1;
}

static int cmd_creador(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kprint("SOMA (Sistema Operativo Multi-Agente) - Creado por Marcos\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "SOMA - Creado por Marcos");
    return 1;
}

/* PARCHE_FEEDBACK_V1: status con datos al agente */
static int cmd_status(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kprint("ARCH: "); kprint(MYOS_ARCH_NAME);
    kprint(" (HAL v1) | IP: "); kprint_ip(net_ip);
    kprint(" | GW: "); kprint_ip(net_gateway);
    kprint(" | MAC: "); kprint_mac(net_mac);
    kprint("\n");
    uint32_t p = 0;
    fb_puts(out_buf, max_out, &p, "ARCH: ");
    fb_puts(out_buf, max_out, &p, MYOS_ARCH_NAME);
    fb_puts(out_buf, max_out, &p, " | IP: 10.0.2.15 | Disco: 16 MiB virtio-blk | STORAGE: PERSISTENT");
    return 1;
}

/* PARCHE_FEEDBACK_V1: uptime real al agente */
static int cmd_uptime(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t sec = (uint32_t)(ms / 1000);
    kprint("Uptime: "); kprint_dec(sec); kprint("s\n");
    uint32_t p = 0;
    fb_puts(out_buf, max_out, &p, "Uptime: ");
    fb_put_dec(out_buf, max_out, &p, sec);
    fb_puts(out_buf, max_out, &p, " segundos");
    return 1;
}

/* ramstat v2: comando 'ram' - consumo real de RAM con desglose y delta */
static void ram_put_mib(char *out, uint32_t max, uint32_t *p, const char *label, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);
    fb_puts(out, max, p, label);
    fb_put_dec(out, max, p, mib);
    fb_puts(out, max, p, ".");
    if (frac < 10) fb_puts(out, max, p, "0");
    fb_put_dec(out, max, p, frac);
    fb_puts(out, max, p, " MiB\n");
}

static void ram_put_delta(char *out, uint32_t max, uint32_t *p, const char *label, uint64_t now, uint64_t base)
{
    fb_puts(out, max, p, label);
    if (now >= base) { fb_puts(out, max, p, "+"); fb_put_dec(out, max, p, (uint32_t)((now - base) >> 10)); }
    else             { fb_puts(out, max, p, "-"); fb_put_dec(out, max, p, (uint32_t)((base - now) >> 10)); }
    fb_puts(out, max, p, " KiB\n");
}

static int ram_arg_is(const char *a, const char *word)
{
    if (!a) return 0;
    while (*a == ' ') a++;
    while (*word) {
        if (*a != *word) return 0;
        a++; word++;
    }
    return (*a == 0 || *a == ' ');
}

/* PARCHE_TERMINAL_V2: 'ram' unifica a 'free' (alias) con un informe completo. */
static void shell_emit(const char *text, char *out_buf, uint32_t max_out)
{
    kprint(text);
    if (out_buf && max_out) {
        uint32_t p = 0;
        out_buf[0] = '\0';
        fb_puts(out_buf, max_out, &p, text);
    }
}

static void ram_put_mib_raw(char *out, uint32_t max, uint32_t *p, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);
    fb_put_dec(out, max, p, mib);
    fb_putc(out, max, p, '.');
    if (frac < 10) fb_putc(out, max, p, '0');
    fb_put_dec(out, max, p, frac);
    fb_puts(out, max, p, " MiB");
}

static void ram_put_pct10(char *out, uint32_t max, uint32_t *p, uint32_t pct10)
{
    fb_put_dec(out, max, p, pct10 / 10);
    fb_putc(out, max, p, '.');
    fb_put_dec(out, max, p, pct10 % 10);
    fb_puts(out, max, p, " %");
}

static void ram_put_bar(char *out, uint32_t max, uint32_t *p, uint32_t pct10)
{
    const uint32_t width = 40;
    uint32_t fill = (pct10 * width + 500) / 1000;
    if (fill > width) fill = width;
    fb_putc(out, max, p, '[');
    for (uint32_t i = 0; i < width; ++i) fb_putc(out, max, p, i < fill ? '#' : '.');
    fb_putc(out, max, p, ']');
}

static int cmd_ram(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[1792];
    uint32_t p = 0;
    struct ramstat_info ri, b;
    int brief = 0, verbose = 0;

    if (!args) args = "";
    while (*args == ' ') args++;

    if (ram_arg_is(args, "base")) {
        ramstat_set_baseline();
        shell_emit("RAM: linea base guardada. Abre una aplicacion y ejecuta 'ram -v' para ver el delta.\n", out_buf, max_out);
        return 1;
    }
    if (ram_arg_is(args, "reset")) {
        ramstat_clear_baseline();
        shell_emit("RAM: linea base borrada.\n", out_buf, max_out);
        return 1;
    }
    if (ram_arg_is(args, "-s") || ram_arg_is(args, "short") || ram_arg_is(args, "resumen")) {
        brief = 1;
    } else if (ram_arg_is(args, "-v") || ram_arg_is(args, "full") || ram_arg_is(args, "detalle")) {
        verbose = 1;
    } else if (*args) {
        shell_emit("Uso: ram [-s | -v | base | reset]   (alias: free)\n"
                   "  -s resumen en una linea | -v desglose completo y delta\n"
                   "  base / reset: fija o borra la linea base para medir el coste de una app\n",
                   out_buf, max_out);
        return 1;
    }

    ramstat_get(&ri);
    uint32_t free10 = ri.total ? (uint32_t)((ri.free_b * 1000ULL) / ri.total) : 0;
    rep[0] = '\0';

    if (brief) {
        fb_puts(rep, sizeof(rep), &p, "RAM: ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
        fb_puts(rep, sizeof(rep), &p, " / ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
        fb_puts(rep, sizeof(rep), &p, " (");
        ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
        fb_puts(rep, sizeof(rep), &p, ") | libre ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
        fb_puts(rep, sizeof(rep), &p, " | heap libre ");
        fb_put_dec(rep, sizeof(rep), &p, ri.heap_free >> 10);
        fb_puts(rep, sizeof(rep), &p, " KiB\n");
        shell_emit(rep, out_buf, max_out);
        return 1;
    }

    fb_puts(rep, sizeof(rep), &p, "=== MEMORIA RAM ===\n");
    fb_puts(rep, sizeof(rep), &p, "Total : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
    fb_puts(rep, sizeof(rep), &p, "\nUsada : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
    fb_puts(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
    fb_puts(rep, sizeof(rep), &p, ")\nLibre : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
    fb_puts(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, free10);
    fb_puts(rep, sizeof(rep), &p, ")\n");
    ram_put_bar(rep, sizeof(rep), &p, ri.pct10);
    fb_puts(rep, sizeof(rep), &p, "\nHeap del kernel: ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_used);
    fb_puts(rep, sizeof(rep), &p, " en uso | ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_free);
    fb_puts(rep, sizeof(rep), &p, " libre | ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_mapped);
    fb_puts(rep, sizeof(rep), &p, " mapeado\n");
    if (ri.pct10 >= 900) fb_puts(rep, sizeof(rep), &p, "AVISO: memoria casi agotada.\n");

    if (verbose) {
        fb_puts(rep, sizeof(rep), &p, "--- Desglose de la memoria comprometida ---\n");
        ram_put_mib(rep, sizeof(rep), &p, "Zona baja reservada : ", ri.lower);
        ram_put_mib(rep, sizeof(rep), &p, "  Imagen del kernel  : ", ri.lower_image);
        ram_put_mib(rep, sizeof(rep), &p, "  Reserva sin medir  : ", ri.lower_rest);
        ram_put_mib(rep, sizeof(rep), &p, "Heap mapeado        : ", ri.heap_mapped);
        ram_put_mib(rep, sizeof(rep), &p, "  En uso (kmalloc)   : ", ri.heap_used);
        ram_put_mib(rep, sizeof(rep), &p, "  Libre              : ", ri.heap_free);
        ram_put_mib(rep, sizeof(rep), &p, "Pilas/tablas/otros  : ", ri.other);
        if (ramstat_get_baseline(&b)) {
            fb_puts(rep, sizeof(rep), &p, "--- Delta desde 'ram base' ---\n");
            ram_put_delta(rep, sizeof(rep), &p, "Huella            : ", ri.footprint, b.footprint);
            ram_put_delta(rep, sizeof(rep), &p, "Heap en uso       : ", ri.heap_used, b.heap_used);
            ram_put_delta(rep, sizeof(rep), &p, "Pilas/tablas/otros: ", ri.other, b.other);
        } else {
            fb_puts(rep, sizeof(rep), &p, "(usa 'ram base' para medir el coste de una aplicacion)\n");
        }
    } else {
        fb_puts(rep, sizeof(rep), &p, "(ram -v: desglose | ram -s: resumen | ram base: medir una app)\n");
    }
    shell_emit(rep, out_buf, max_out);
    return 1;
}


/* PARCHE_HORA_LS_V1: date en CEST (UTC+2) */
static int cmd_date(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    struct rtc_time t; rtc_get_datetime(&t);

    /* Convertir UTC → CEST (UTC+2). Ajuste simple de hora/día. */
    uint32_t y = t.year, mo = t.month, d = t.day;
    uint32_t h = t.hour + 2, mi = t.min, s = t.sec;
    if (h >= 24) {
        h -= 24;
        d += 1;
        uint32_t dim = 31;
        if (mo == 4 || mo == 6 || mo == 9 || mo == 11) dim = 30;
        else if (mo == 2) dim = 28;
        if (d > dim) { d = 1; mo += 1; }
        if (mo > 12) { mo = 1; y += 1; }
    }

    char date_str[80]; uint32_t dp = 0;
    fb_puts(date_str, sizeof(date_str), &dp, "CEST: ");
    fb_put_dec(date_str, sizeof(date_str), &dp, y); fb_putc(date_str, sizeof(date_str), &dp, '-');
    if (mo < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, mo); fb_putc(date_str, sizeof(date_str), &dp, '-');
    if (d < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, d); fb_putc(date_str, sizeof(date_str), &dp, ' ');
    if (h < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, h); fb_putc(date_str, sizeof(date_str), &dp, ':');
    if (mi < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, mi); fb_putc(date_str, sizeof(date_str), &dp, ':');
    if (s < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, s);
    fb_puts(date_str, sizeof(date_str), &dp, " CEST");

    kprint(date_str); kprint("\n");
    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, date_str);
    return 1;
}

static int cmd_clear(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    console_clear();
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Pantalla borrada.");
    return 1;
}

static int cmd_test_suite(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    boot_gate_run_test_suite(out_buf, max_out);
    return 1;
}

static int cmd_mem(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    sysinfo_print_mem();
    sysinfo_format_mem(out_buf, max_out);
    return 1;
}

/* PARCHE_FEEDBACK_V1: df reporta capacidad real al agente */
static int cmd_df(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0;
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);

    kprint("\nALMACENAMIENTO (df):\n");
    kprint("  Disco: 16 MiB virtio-blk (SOMAFS02 persistente en KHeap)\n");
    kprint("  RamFS: "); kprint_dec(files_u); kprint("/64 inodos | ");
    kprint_dec(bytes_u / 1024); kprint(" KiB usados (max 16 KiB/archivo) | dirty=");
    kprint_dec(dirty_c); kprint("\n\n");

    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, "Disco: 16 MiB virtio-blk | RamFS V2: ");
    fb_put_dec(out_buf, max_out, &pos, files_u);
    fb_puts(out_buf, max_out, &pos, "/64 inodos | ");
    fb_put_dec(out_buf, max_out, &pos, bytes_u / 1024);
    fb_puts(out_buf, max_out, &pos, " KiB usados (16 KiB/archivo) | dirty=");
    fb_put_dec(out_buf, max_out, &pos, dirty_c);
    return 1;
}

static int cmd_stats(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    sysinfo_print_stats();
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "stats ejecutado");
    return 1;
}

static int cmd_heap(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kheap_dump_stats();
    kheap_test_self();
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Heap inspeccionado");
    return 1;
}

static int cmd_pmm(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    pmm_dump_stats();
    pmm_test_self();
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "PMM: Verificado.");
    return 1;
}

static int cmd_vmm(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vmm_test_self();
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "VMM: Verificado.");
    return 1;
}

static int cmd_pci(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    pci_scan();
    pci_format_scan(out_buf, max_out);
    return 1;
}

static int cmd_arp(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    arp_format_cache(out_buf, max_out);
    kprint(out_buf);
    return 1;
}

static int cmd_ps(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_dump();
    thread_format_table(out_buf, max_out);
    return 1;
}

static int cmd_threads(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_dump();
    thread_format_table(out_buf, max_out);
    return 1;
}

static int cmd_spawn(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: spawn <comando>  (alias: bg <comando>)\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: spawn <comando>");
        return 1;
    }

    struct bg_job *job = (struct bg_job *)kmalloc(sizeof(struct bg_job));
    if (!job) {
        kprint("Error: Sin memoria para spawn.\n");
        return 1;
    }
    memset(job, 0, sizeof(*job));

    /* AGENT_POLICY_V1: capturar el origen antes de lanzar el hilo. */
    job->agent_origin = (uint8_t)(agent_dispatch_mode != 0);

    uint32_t ci = 0;
    while (args[ci] && ci < sizeof(job->cmd) - 1) {
        job->cmd[ci] = args[ci];
        ci++;
    }
    job->cmd[ci] = '\0';

    char th_name[32];
    th_name[0] = 'b'; th_name[1] = 'g'; th_name[2] = '_';
    uint32_t ni = 3;
    for (uint32_t i = 0; args[i] && args[i] != ' ' && ni < sizeof(th_name) - 1; ++i) {
        th_name[ni++] = args[i];
    }
    th_name[ni] = '\0';

    struct tcb *nt = thread_create(th_name, spawn_job_worker, job);
    if (!nt) {
        kprint("Error: Fallo al crear hilo para spawn.\n");
        kfree(job);
        return 1;
    }

    job->tid = nt->tid;

    /* Formatear /tmp/job_<TID>.log limpiamente */
    uint32_t li = 0;
    const char *pfx = "/tmp/job_";
    while (*pfx) job->log_file[li++] = *pfx++;
    char tbuf[10]; int tn = 0; uint32_t tid_tmp = job->tid;
    if (tid_tmp == 0) tbuf[tn++] = '0';
    else { while (tid_tmp) { tbuf[tn++] = (char)('0' + (tid_tmp % 10)); tid_tmp /= 10; } }
    while (tn > 0) job->log_file[li++] = tbuf[--tn];
    const char *sfx = ".log";
    while (*sfx) job->log_file[li++] = *sfx++;
    job->log_file[li] = '\0';

    kprint("[SPAWN] Tarea '"); kprint(job->cmd);
    kprint("' lanzada en segundo plano [TID "); kprint_dec(job->tid);
    kprint(", Hilo '"); kprint(th_name);
    kprint("'] -> Log: "); kprint(job->log_file); kprint("\n");

    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, "[SPAWN OK] TID=");
    fb_put_dec(out_buf, max_out, &pos, job->tid);
    fb_puts(out_buf, max_out, &pos, " Log=");
    fb_puts(out_buf, max_out, &pos, job->log_file);
    return 1;
}

static int cmd_kill(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t tid = 0; while (*args >= '0' && *args <= '9') tid = tid * 10 + (*args++ - '0');
    thread_kill(tid);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Kill ejecutado");
    return 1;
}

static int cmd_sleep(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t ms = 0; while (*args >= '0' && *args <= '9') ms = ms * 10 + (*args++ - '0');
    if (ms == 0) ms = 1000;
    thread_sleep(ms);
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "sleep completado");
    return 1;
}

static int cmd_time(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: time <comando>\n");
        return 1;
    }
    uint64_t t0 = timer_get_uptime_ms(); uint64_t c0 = rdtsc();
    dispatch_command(args, out_buf, max_out);
    uint64_t c1 = rdtsc(); uint64_t t1 = timer_get_uptime_ms();
    kprint("\n[TIME]: "); kprint_dec((uint32_t)(t1 - t0)); kprint(" ms | "); kprint_dec((uint32_t)(c1 - c0)); kprint(" ciclos TSC\n");
    return 1;
}

static int cmd_reboot(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    system_reboot();
    return 1;
}

static int cmd_poweroff(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    system_poweroff();
    return 1;
}

static int cmd_halt(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    vfs_sync();
    kprint("Sistema detenido (HALT).\n");
    for (;;) { arch_interrupts_disable(); arch_halt(); }
    return 1;
}

static int cmd_ls(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int show_all = 0;
    char filter[64]; filter[0] = '\0';
    uint32_t fp = 0;

    while (*args) {
        while (*args == ' ') args++;
        if (*args == '-') {
            while (*args && *args != ' ') {
                if (*args == 'a' || *args == 'A') show_all = 1;
                args++;
            }
        } else if (*args) {
            while (*args && *args != ' ' && fp < sizeof(filter) - 1) {
                filter[fp++] = *args++;
            }
            filter[fp] = '\0';
        }
    }

    vfs_list_ext(show_all, filter[0] ? filter : 0);
    vfs_format_list_ext(show_all, filter[0] ? filter : 0, out_buf, max_out);
    return 1;
}

static int cmd_cat(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        if (pipe_stdin_data != 0) {
            kprint(pipe_stdin_data);
            uint32_t p = 0; fb_puts(out_buf, max_out, &p, pipe_stdin_data);
            return 1;
        }
        kprint("Uso: cat <archivo1> [archivo2 ...]\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: cat <archivo>");
        return 1;
    }

    uint32_t out_pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    while (*args) {
        while (*args == ' ') args++;
        if (*args == '\0') break;

        char fn[64];
        uint32_t fi = 0;
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';

        char buf[2048];
        int r = vfs_read(fn, buf, sizeof(buf) - 1);
        if (r >= 0) {
            buf[r] = '\0';
            kprint(buf);
            fb_puts(out_buf, max_out, &out_pos, buf);
        } else {
            kprint("cat: no encontrado '"); kprint(fn); kprint("'\n");
            fb_puts(out_buf, max_out, &out_pos, "cat: no encontrado");
        }
    }
    return 1;
}

static int cmd_write(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[64]; uint32_t fi = 0;
    while (*args && *args != ' ' && fi < sizeof(fn) - 1) fn[fi++] = *args++;
    fn[fi] = '\0';
    while (*args == ' ') args++;

    if (agent_dispatch_mode && (str_eq(fn, "/etc/init.sh") || str_eq(fn, "/etc/soma_manifest.txt"))) {
        kprint("write: operacion denegada por seguridad (Agente no puede modificar scripts de arranque).\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Error: Operacion denegada por politica de seguridad.");
        return 1;
    }

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }

    char *payload = (char *)kmalloc(FS_DATA_MAX);
    if (!payload) {
        kprint("write: sin memoria para buffer de escritura.\n");
        return 1;
    }

    uint32_t pi = 0;
    while (*args && pi < FS_DATA_MAX - 1) {
        if (q && *args == q && *(args + 1) == '\0') {
            break;
        }
        if (*args == '\\' && *(args + 1) == 'n') {
            payload[pi++] = '\n';
            args += 2;
            continue;
        }
        if (*args == '\\' && *(args + 1) == 't') {
            payload[pi++] = '\t';
            args += 2;
            continue;
        }
        payload[pi++] = *args++;
    }
    payload[pi] = '\0';

    int wres = vfs_write(fn, payload, pi);
    kfree(payload);

    if (wres == -2) {
        kprint("write: error, archivo demasiado grande (max 16383 bytes).\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Error: El archivo excede la cuota maxima de 16383 bytes.");
        return 1;
    }
    if (wres < 0) {
        kprint("write: error al escribir '"); kprint(fn); kprint("' (acceso denegado o fallo de disco).\n");
        uint32_t epos = 0;
        fb_puts(out_buf, max_out, &epos, "Error: no se pudo escribir el archivo (denegado o fallo de I/O).");
        return 1;
    }
    kprint("Escrito en '"); kprint(fn); kprint("' (");
    kprint_dec(pi); kprint(" bytes)\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Archivo escrito.");
    return 1;
}

static int cmd_rm(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;
    if (*args == '\0') {
        kprint("Uso: rm <archivo>\n");
        fb_puts(out_buf, max_out, &pos, "Error: falta el nombre del archivo.");
        return 1;
    }
    int r = vfs_delete(args);
    if (r == 0) {
        kprint("Eliminado '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "Archivo eliminado.");
    } else {
        kprint("rm: no se pudo eliminar '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos,
                "Error: archivo no encontrado o eliminacion denegada.");
    }
    return 1;
}

static int cmd_cp(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char src[48], dst[48]; uint32_t si = 0, di = 0;
    while (*args && *args != ' ' && si < sizeof(src) - 1) src[si++] = *args++;
    src[si] = '\0';
    while (*args == ' ') args++;
    while (*args && *args != ' ' && di < sizeof(dst) - 1) dst[di++] = *args++;
    dst[di] = '\0';
    static char cp_buf[4096]; int r = vfs_read(src, cp_buf, sizeof(cp_buf));
    uint32_t pos = 0;
    if (r < 0) { kprint("cp: origen no encontrado\n"); fb_puts(out_buf, max_out, &pos, "cp: error"); }
    else if (vfs_write(dst, cp_buf, (uint32_t)r) >= 0) { kprint("cp: copiado OK\n"); fb_puts(out_buf, max_out, &pos, "cp: OK"); }
    return 1;
}

static int cmd_mv(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char src[48], dst[48]; uint32_t si = 0, di = 0;
    while (*args && *args != ' ' && si < sizeof(src) - 1) src[si++] = *args++;
    src[si] = '\0';
    while (*args == ' ') args++;
    while (*args && *args != ' ' && di < sizeof(dst) - 1) dst[di++] = *args++;
    dst[di] = '\0';
    static char mv_buf[4096]; int r = vfs_read(src, mv_buf, sizeof(mv_buf));
    uint32_t pos = 0;
    if (r < 0) { kprint("mv: origen no encontrado\n"); fb_puts(out_buf, max_out, &pos, "mv: error"); }
    else if (vfs_write(dst, mv_buf, (uint32_t)r) >= 0) { vfs_delete(src); kprint("mv: movido OK\n"); fb_puts(out_buf, max_out, &pos, "mv: OK"); }
    return 1;
}

static int cmd_touch(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char tmp[8]; uint32_t pos = 0;
    if (vfs_read(args, tmp, sizeof(tmp)) < 0) { vfs_write(args, "", 0); kprint("touch: creado\n"); fb_puts(out_buf, max_out, &pos, "touch: OK"); }
    else { kprint("touch: ya existe\n"); fb_puts(out_buf, max_out, &pos, "touch: existe"); }
    return 1;
}

static int cmd_tree(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int show_all = 0;
    if (args[0] == '-' && (args[1] == 'a' || args[1] == 'A')) {
        show_all = 1;
    }
    vfs_tree_ext(show_all, out_buf, max_out);
    return 1;
}

static int cmd_head(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[48]; uint32_t fi = 0;
    uint32_t lines = 10;
    const char *src_text = 0;
    static char hbuf[4096];

    /* Parsear opciones: -n <N> o -<N> */
    while (*args == '-') {
        args++;
        if (*args == 'n' || *args == 'N') {
            args++;
            while (*args == ' ') args++;
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        } else if (*args >= '0' && *args <= '9') {
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        while (*args == ' ') args++;
    }

    if (pipe_stdin_data != 0 && *args == '\0') {
        src_text = pipe_stdin_data;
    } else {
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';
        while (*args == ' ') args++;
        if (*args >= '0' && *args <= '9' && lines == 10) {
            lines = 0; while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        if (fn[0] == '\0') {
            if (pipe_stdin_data != 0) {
                src_text = pipe_stdin_data;
            } else {
                kprint("Uso: head [-n N] <archivo>\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: head [-n N] <archivo>");
                return 1;
            }
        } else {
            int r = vfs_read(fn, hbuf, sizeof(hbuf) - 1);
            if (r < 0) {
                kprint("head: no encontrado: '"); kprint(fn); kprint("'\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "head: no encontrado");
                return 1;
            }
            hbuf[r] = '\0';
            src_text = hbuf;
        }
    }

    if (lines == 0) lines = 10;
    uint32_t lc = 0, pos = 0;
    for (int i = 0; src_text[i] && lc < lines; ++i) {
        kputc(src_text[i]);
        if (pos < max_out - 1) out_buf[pos++] = src_text[i];
        if (src_text[i] == '\n') lc++;
    }
    out_buf[pos] = '\0';
    return 1;
}

static int cmd_tail(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[48]; uint32_t fi = 0;
    uint32_t lines = 10;
    const char *src_text = 0;
    static char tbuf[4096];
    uint32_t total_len = 0;

    /* Parsear opciones: -n <N> o -<N> */
    while (*args == '-') {
        args++;
        if (*args == 'n' || *args == 'N') {
            args++;
            while (*args == ' ') args++;
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        } else if (*args >= '0' && *args <= '9') {
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        while (*args == ' ') args++;
    }

    if (pipe_stdin_data != 0 && *args == '\0') {
        src_text = pipe_stdin_data;
        total_len = pipe_stdin_len;
    } else {
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';
        while (*args == ' ') args++;
        if (*args >= '0' && *args <= '9' && lines == 10) {
            lines = 0; while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        if (fn[0] == '\0') {
            if (pipe_stdin_data != 0) {
                src_text = pipe_stdin_data;
                total_len = pipe_stdin_len;
            } else {
                kprint("Uso: tail [-n N] <archivo>\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: tail [-n N] <archivo>");
                return 1;
            }
        } else {
            int r = vfs_read(fn, tbuf, sizeof(tbuf) - 1);
            if (r < 0) {
                kprint("tail: no encontrado: '"); kprint(fn); kprint("'\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "tail: no encontrado");
                return 1;
            }
            total_len = (uint32_t)r;
            tbuf[r] = '\0';
            src_text = tbuf;
        }
    }

    if (lines == 0) lines = 10;
    uint32_t tl = 0; for (uint32_t i = 0; i < total_len; ++i) if (src_text[i] == '\n') tl++;
    uint32_t skip = (tl > lines) ? (tl - lines) : 0, cl = 0, si = 0;
    while (si < total_len && cl < skip) if (src_text[si++] == '\n') cl++;
    uint32_t pos = 0;
    for (uint32_t i = si; i < total_len; ++i) {
        kputc(src_text[i]);
        if (pos < max_out - 1) out_buf[pos++] = src_text[i];
    }
    out_buf[pos] = '\0';
    return 1;
}

static int cmd_wc(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    static char wcbuf[4096];
    const char *src_text = 0;
    uint32_t text_len = 0;

    if (*args != '\0') {
        int r = vfs_read(args, wcbuf, sizeof(wcbuf));
        if (r < 0) {
            uint32_t p = 0; fb_puts(out_buf, max_out, &p, "wc: no encontrado");
            return 1;
        }
        text_len = (uint32_t)r;
        wcbuf[r] = '\0';
        src_text = wcbuf;
    } else if (pipe_stdin_data != 0) {
        src_text = pipe_stdin_data;
        text_len = pipe_stdin_len;
    } else {
        kprint("Uso: wc [archivo]  (o bien: <cmd> | wc)\n");
        return 1;
    }

    uint32_t l = 0, w = 0; int in_w = 0;
    for (uint32_t i = 0; i < text_len; ++i) {
        if (src_text[i] == '\n') l++;
        if (src_text[i] == ' ' || src_text[i] == '\t' || src_text[i] == '\n' || src_text[i] == '\r') in_w = 0;
        else if (!in_w) { in_w = 1; w++; }
    }
    kprint("  "); kprint_dec(l); kprint(" lineas  "); kprint_dec(w); kprint(" palabras  "); kprint_dec(text_len); kprint(" bytes\n");
    uint32_t pos = 0;
    fb_put_dec(out_buf, max_out, &pos, l); fb_puts(out_buf, max_out, &pos, " lineas, ");
    fb_put_dec(out_buf, max_out, &pos, w); fb_puts(out_buf, max_out, &pos, " palabras, ");
    fb_put_dec(out_buf, max_out, &pos, text_len); fb_puts(out_buf, max_out, &pos, " bytes");
    return 1;
}

/* grep V2: sin archivo → busca en TODO el RamFS.
 * Sintaxis: grep [-i] [-l] <patron> [archivo]
 *   -i  ignore case
 *   -l  solo listar nombres de archivo con match
 * Un solo archivo opcional (no multi-file estilo GNU). */
static int cmd_grep(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int ignore_case = 0;
    int list_only = 0;

    /* Flags: -i -l -r ( -r = global, se ignora la lista de archivos posterior ) */
    int force_global = 0;
    for (;;) {
        if (args[0] == '-' && args[1] == 'i' && (args[2] == ' ' || args[2] == '\0')) {
            ignore_case = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'l' && (args[2] == ' ' || args[2] == '\0')) {
            list_only = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'r' && (args[2] == ' ' || args[2] == '\0')) {
            force_global = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'i' && args[2] == 'l' &&
            (args[3] == ' ' || args[3] == '\0')) {
            ignore_case = 1; list_only = 1; args += 3;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'l' && args[2] == 'i' &&
            (args[3] == ' ' || args[3] == '\0')) {
            ignore_case = 1; list_only = 1; args += 3;
            while (*args == ' ') args++;
            continue;
        }
        break;
    }

    char pat[64]; uint32_t pi = 0; char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pi < sizeof(pat) - 1) {
        if (q && *args == q) { args++; break; }
        if (!q && *args == ' ') break;
        pat[pi++] = *args++;
    }
    pat[pi] = '\0';
    while (*args == ' ') args++;

    if (pat[0] == '\0') {
        kprint("Uso: grep [-i] [-l] <patron> [archivo]\n");
        kprint("  Sin archivo: busca en todo el RamFS.\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: grep [-i] [-l] <patron> [archivo]");
        return 1;
    }

    /* Extraer SOLO el primer path (ignorar multi-file GNU) */
    char fname[FS_NAME_MAX];
    uint32_t fi = 0;
    if (*args != '\0') {
        if (*args == '\'' || *args == '"') {
            char qq = *args++;
            while (*args && *args != qq && fi < sizeof(fname) - 1) fname[fi++] = *args++;
            if (*args == qq) args++;
        } else {
            while (*args && *args != ' ' && fi < sizeof(fname) - 1) fname[fi++] = *args++;
        }
        fname[fi] = '\0';
    } else {
        fname[0] = '\0';
    }

    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    /* -r o sin archivo → búsqueda global (ignora paths extra estilo GNU) */
    if (force_global) fname[0] = '\0';

    /* ---- Sin archivo y sin pipe → búsqueda global en RamFS ---- */
    if (fname[0] == '\0' && pipe_stdin_data == 0) {
        static char g_all[2048];
        int hits = vfs_grep_all(pat, ignore_case, list_only, g_all, sizeof(g_all));
        kprint(g_all);
        fb_puts(out_buf, max_out, &pos, g_all);
        if (hits > 0) {
            kprint("("); kprint_dec((uint32_t)hits); kprint(" archivo(s) con coincidencias)\n");
        }
        return 1;
    }

    /* ---- Un archivo o pipe (comportamiento clásico) ---- */
    const char *src_text = 0;
    static char gbuf[4096];

    if (fname[0] != '\0') {
        int r = vfs_read(fname, gbuf, sizeof(gbuf) - 1);
        if (r < 0) {
            kprint("grep: archivo no encontrado: '"); kprint(fname); kprint("'\n");
            fb_puts(out_buf, max_out, &pos, "grep: archivo no encontrado");
            return 1;
        }
        gbuf[r] = '\0';
        src_text = gbuf;
    } else {
        src_text = pipe_stdin_data;
    }

    uint32_t line_num = 1;
    int matches = 0;
    const char *p = src_text;

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        uint32_t line_len = (uint32_t)(p - line_start);
        char line_tmp[256];
        uint32_t copy_len = line_len < sizeof(line_tmp) - 1 ? line_len : sizeof(line_tmp) - 1;
        for (uint32_t k = 0; k < copy_len; ++k) line_tmp[k] = line_start[k];
        line_tmp[copy_len] = '\0';

        if (find_substr_ci(line_tmp, pat, ignore_case)) {
            matches++;
            if (list_only) {
                if (fname[0]) {
                    kprint(fname); kprint("\n");
                    fb_puts(out_buf, max_out, &pos, fname);
                    fb_puts(out_buf, max_out, &pos, "\n");
                }
                break;
            }
            kprint_dec(line_num); kprint(": "); kprint(line_tmp); kprint("\n");
            fb_put_dec(out_buf, max_out, &pos, line_num);
            fb_puts(out_buf, max_out, &pos, ": ");
            fb_puts(out_buf, max_out, &pos, line_tmp);
            fb_puts(out_buf, max_out, &pos, "\n");
        }

        line_num++;
        if (*p == '\n') p++;
    }

    if (matches == 0) {
        kprint("grep: sin coincidencias.\n");
        fb_puts(out_buf, max_out, &pos, "grep: sin coincidencias.");
    }
    return 1;
}

static int cmd_echo(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char clean[512];
    uint32_t ci = 0;

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }

    while (*args && ci < sizeof(clean) - 1) {
        if (q && *args == q) {
            args++;
            while (*args == ' ') args++;
            if (*args == '"' || *args == '\'') q = *args++;
            else q = 0;
            continue;
        }
        clean[ci++] = *args++;
    }
    clean[ci] = '\0';

    kprint(clean); kprint("\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, clean); fb_putc(out_buf, max_out, &pos, '\n');
    return 1;
}

static int cmd_nano(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    run_nano(args);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "nano cerrado.");
    return 1;
}

static int cmd_hexdump(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    do_hexdump(args, 128, out_buf, max_out);
    return 1;
}

static int cmd_checkpoint(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (vfs_checkpoint_save() == 0) {
        kprint("[CHECKPOINT OK]\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Checkpoint OK");
    }
    return 1;
}

static int cmd_rollback(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (vfs_checkpoint_restore() == 0) {
        kprint("[ROLLBACK OK]\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Rollback OK");
    }
    return 1;
}

static int cmd_fs_sync(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vfs_sync();
    kprint("RamFS sincronizado.\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Sync OK");
    return 1;
}

static int cmd_fs_format(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vfs_format();
    kprint("RamFS formateado a fabrica.\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Format OK");
    return 1;
}

static int cmd_sector_read(const char *args, char *out_buf, uint32_t max_out)
{
    uint64_t sec = parse_num(&args);
    char sbuf[512];
    if (virtio_blk_read(sec, sbuf) == 0) {
        kprint("\n[LBA "); kprint_dec((uint32_t)sec); kprint("]: ");
        for (int i=0; i<512 && sbuf[i]; i++) if (sbuf[i]>=32 && sbuf[i]<127) kputc(sbuf[i]);
        kprint("\n");
    }
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "sector leido");
    return 1;
}

static int cmd_sector_write(const char *args, char *out_buf, uint32_t max_out)
{
    uint64_t sec = parse_num(&args);
    if (sec < 3600 || (sec >= 4095 && sec <= 4400) || (sec >= 8192 && sec < 16384)) {
        kprint("Error: sector protegido.\n");
        return 1;
    }
    while (*args == ' ') args++;
    char sbuf[512]; memset(sbuf, 0, 512); int bi = 0;
    while (*args && bi < 511) sbuf[bi++] = *args++;
    virtio_blk_write(sec, sbuf);
    kprint("Sector escrito.\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "sector escrito");
    return 1;
}

static int cmd_ping(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint8_t target[4];
    if (*args) parse_ip(args, target);
    else { target[0] = net_gateway[0]; target[1] = net_gateway[1]; target[2] = net_gateway[2]; target[3] = net_gateway[3]; }
    int r = icmp_ping(target, 1, 2000);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, (r == 1) ? "Ping exitoso" : "Ping fallido");
    return 1;
}

static int cmd_dns(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint8_t ip[4];
    dns_resolve(args, ip, 3000);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "DNS consultado");
    return 1;
}


/* PARCHE_SEARCH_V5: busqueda web multifuente conectada a 10.0.2.2:8095 */
static void search_url_encode(const char *in, char *out, uint32_t max)
{
    uint32_t o = 0;
    static const char *hex = "0123456789ABCDEF";
    while (*in && o + 4 < max) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

static int cmd_search(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: search <consulta>\n");
        uint32_t p = 0;
        fb_puts(out_buf, max_out, &p, "Uso: search <consulta>");
        return 1;
    }

    /* Limpiar comillas iniciales/finales si el agente o usuario las puso */
    char clean_query[256];
    uint32_t qp = 0;
    char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && qp < sizeof(clean_query) - 1) {
        if (q && *args == q && *(args + 1) == '\0') break;
        clean_query[qp++] = *args++;
    }
    clean_query[qp] = '\0';
    while (qp > 0 && (clean_query[qp - 1] == ' ' || clean_query[qp - 1] == '\'' || clean_query[qp - 1] == '"')) {
        clean_query[--qp] = '\0';
    }

    char enc[256];
    search_url_encode(clean_query, enc, sizeof(enc));

    char path[300];
    uint32_t pp = 0;
    const char *pre = "/search?q=";
    while (*pre && pp + 1 < sizeof(path)) path[pp++] = *pre++;
    for (uint32_t i = 0; enc[i] && pp + 1 < sizeof(path); ++i) path[pp++] = enc[i];
    path[pp] = '\0';

    /* Host Mac en QEMU NAT = net_gateway (10.0.2.2) */
    uint8_t tip[4];
    tip[0] = net_gateway[0];
    tip[1] = net_gateway[1];
    tip[2] = net_gateway[2];
    tip[3] = net_gateway[3];
    const char *host = "10.0.2.2";
    uint16_t proxy_port = 8095;

    static char http_buf[8192];
    struct http_response resp;
    int code = http_get_host(tip, proxy_port, host, path, http_buf, sizeof(http_buf), &resp, 45000);

    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    if (code < 0) {
        kprint("search: error HTTP/red (proxy 10.0.2.2:8095 no responde)\n");
        fb_puts(out_buf, max_out, &pos,
                "search: error de red o timeout (inicia 'python3 search_proxy.py' en el host)");
        return 1;
    }
    if (code < 200 || code >= 400) {
        kprint("search: HTTP error "); kprint_dec((uint32_t)code); kprint("\n");
        fb_puts(out_buf, max_out, &pos, "search: error devuelto por el proxy");
        return 1;
    }

    const char *body = (resp.body && resp.body[0]) ? resp.body : http_buf;
    kprint("\n--- Resultados Web ["); kprint_dec((uint32_t)code); kprint("] ---\n");
    kprint(body);
    kprint("\n------------------------------------\n");
    if (!body[0]) {
        fb_puts(out_buf, max_out, &pos, "search: respuesta vacia");
        return 1;
    }
    fb_puts(out_buf, max_out, &pos, body);
    return 1;
}

static int shell_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int cmd_curl(const char *args, char *out_buf, uint32_t max_out)
{
    /* PARCHE_RED_SEGURO_V1: URL completa delegada al proxy; el kernel no necesita TLS. */
    while (*args == ' ') args++;
    const char *url_arg = args;
    char url_quote = 0;
    if (*url_arg == '\'' || *url_arg == '"') {
        url_quote = *url_arg++;
    }
    if (shell_starts_with(url_arg, "http://") || shell_starts_with(url_arg, "https://")) {
        char target_url[321];
        uint32_t url_len = 0;
        while (url_arg[url_len] && url_len < sizeof(target_url) - 1 &&
               (url_quote ? url_arg[url_len] != url_quote : url_arg[url_len] != ' ')) {
            target_url[url_len] = url_arg[url_len];
            url_len++;
        }
        if (url_arg[url_len] &&
            (url_quote ? url_arg[url_len] != url_quote : url_arg[url_len] != ' ')) {
            kprint("curl: URL demasiado larga (maximo 320 caracteres).\n");
            return 1;
        }
        target_url[url_len] = '\0';
        if (url_len == 0) {
            kprint("curl: URL vacia.\n");
            return 1;
        }

        char encoded_url[1000];
        search_url_encode(target_url, encoded_url, sizeof(encoded_url));
        char proxy_path[1024];
        uint32_t path_pos = 0;
        const char *prefix = "/fetch?url=";
        while (*prefix && path_pos + 1 < sizeof(proxy_path)) {
            proxy_path[path_pos++] = *prefix++;
        }
        for (uint32_t i = 0; encoded_url[i] && path_pos + 1 < sizeof(proxy_path); ++i) {
            proxy_path[path_pos++] = encoded_url[i];
        }
        proxy_path[path_pos] = '\0';

        static char proxy_buf[4096];
        struct http_response proxy_resp;
        int proxy_code = http_get_host(net_gateway, 8095, "10.0.2.2", proxy_path,
                                       proxy_buf, sizeof(proxy_buf), &proxy_resp, 40000);
        uint32_t out_pos = 0;
        if (out_buf && max_out) out_buf[0] = '\0';
        if (proxy_code < 0) {
            kprint("curl: el proxy HTTPS 10.0.2.2:8095 no responde.\n");
            fb_puts(out_buf, max_out, &out_pos, "curl: error de red o timeout al consultar el proxy");
            return 1;
        }
        const char *display = (proxy_resp.body && proxy_resp.body[0]) ? proxy_resp.body : proxy_buf;
        kprint("\n--- RESPUESTA HTTP VIA PROXY [");
        kprint_dec((uint32_t)proxy_code);
        kprint("] ---\n");
        kprint(display);
        kprint("\n");
        fb_puts(out_buf, max_out, &out_pos, display);
        return 1;
    }
    char target[64]; uint32_t ti = 0; while (*args && *args != ' ' && ti < sizeof(target) - 1) target[ti++] = *args++; target[ti] = '\0';
    while (*args == ' ') args++;
    uint16_t port = 80; if (*args >= '0' && *args <= '9') { port = 0; while (*args >= '0' && *args <= '9') port = port * 10 + (*args++ - '0'); }
    while (*args == ' ') args++;
    const char *path = (*args) ? args : "/";
    uint8_t tip[4]; const char *hhdr = 0; int is_ip = 1, dots = 0;
    for (int i = 0; target[i]; ++i) { if (target[i] == '.') dots++; else if (target[i] < '0' || target[i] > '9') is_ip = 0; }
    if (dots != 3) is_ip = 0;
    if (is_ip) parse_ip(target, tip);
    else { if (!dns_resolve(target, tip, 3000)) { kprint("DNS error\n"); return 1; } hhdr = target; }
    static char http_buf[4096]; struct http_response resp;
    int code = http_get_host(tip, port, hhdr, path, http_buf, sizeof(http_buf), &resp, 40000);
    if (code >= 0) {
        const char *disp = (resp.body && resp.body[0]) ? resp.body : http_buf;
        kprint("\n--- RESPUESTA HTTP ["); kprint_dec((uint32_t)code); kprint("] ---\n"); kprint(disp); kprint("\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, disp);
    }
    return 1;
}

static int cmd_soma(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    agent_run("soma", args);
    return 1;
}

static int cmd_buscador(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    agent_run("buscador", args);
    return 1;
}

static int cmd_mayordomo(const char *args, char *out_buf, uint32_t max_out)
{
    if (!args || !*args) {
        kprint("Uso: mayordomo <tarea del sistema>\n");
        if (out_buf && max_out) {
            const char *msg = "Uso: mayordomo <tarea del sistema>";
            uint32_t p = 0; while (msg[p] && p + 1 < max_out) { out_buf[p] = msg[p]; ++p; }
            out_buf[p] = '\0';
        }
        return 1;
    }
    agent_run("mayordomo", args);
    if (out_buf && max_out) {
        uint32_t p = 0;
        while (agent_last_response[p] && p + 1 < max_out) { out_buf[p] = agent_last_response[p]; ++p; }
        out_buf[p] = '\0';
    }
    return 1;
}

static int cmd_agent(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;
    char target_agent[32];
    uint32_t ap = 0;
    while (*args && *args != ' ' && ap < sizeof(target_agent) - 1) {
        target_agent[ap++] = *args++;
    }
    target_agent[ap] = '\0';
    while (*args == ' ') args++;

    if (str_eq(target_agent, "buscador")) {
        agent_run("buscador", args);
    } else if (str_eq(target_agent, "soma")) {
        agent_run("soma", args);
    } else if (str_eq(target_agent, "mayordomo")) {
        agent_run("mayordomo", args);
    } else {
        /* Si no coincide con un nombre de agente, invocar a soma con la frase completa */
        agent_run("soma", target_agent[0] ? target_agent : args);
    }
    return 1;
}

static int cmd_health(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    int ok = llm_health();
    kprint(ok ? "Servidor LLM: OK (HTTP 200 OK)\n" : "Servidor LLM: ERROR\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, ok ? "Servidor LLM: OK" : "Servidor LLM: ERROR");
    return 1;
}

static int cmd_llm(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: llm <mensaje>\n");
        return 1;
    }
    char *reply = (char *)kmalloc(16384);
    if (!reply) {
        kprint("llm: sin memoria.\n");
        return 1;
    }
    kprint("Consultando a nail-35b...\n");
    if (llm_chat(args, reply, 16384, 35000)) {
        kprint("\n[nail-35b]: "); kprint(reply); kprint("\n\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, reply);
    } else {
        kprint("Error al consultar el LLM.\n");
    }
    kfree(reply);
    return 1;
}

static int cmd_llm_diag(const char *args, char *out_buf, uint32_t max_out)
{
    static char reply[4096];
    kprint("Solicitando diagnostico al LLM...\n");
    if (llm_diagnose(args, reply, sizeof(reply), 35000)) {
        kprint("\n[DIAGNOSTICO IA]:\n"); kprint(reply); kprint("\n\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, reply);
    } else {
        kprint("Error al solicitar diagnostico al LLM.\n");
    }
    return 1;
}

static int cmd_src_ls(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    srcfs_ls(out_buf, max_out);
    kprint(out_buf); kputc('\n');
    return 1;
}

static int cmd_src_cat(const char *args, char *out_buf, uint32_t max_out)
{
    char full[128]; uint32_t p = 0;
    fb_puts(full, sizeof(full), &p, "src_cat ");
    fb_puts(full, sizeof(full), &p, args);
    src_tool(full, out_buf, max_out);
    return 1;
}

static int cmd_src_grep(const char *args, char *out_buf, uint32_t max_out)
{
    char full[128]; uint32_t p = 0;
    fb_puts(full, sizeof(full), &p, "src_grep ");
    fb_puts(full, sizeof(full), &p, args);
    src_tool(full, out_buf, max_out);
    return 1;
}

static int cmd_hpatch(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    hpatch_command(args);
    return 1;
}

static int cmd_panic(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;
    if (args[0] == 'd' && args[1] == 'i' && args[2] == 'v') {
        kprint("Provocando division por cero (#DE)...\n");
        volatile int z = 0; volatile int x = 42 / z; (void)x;
    } else if (args[0] == 'u' && args[1] == 'd') {
        kprint("Ejecutando instruccion invalida (#UD)...\n");
#ifdef __x86_64__
        __asm__ volatile ("ud2");
#else
        __builtin_trap();
#endif
    } else {
        kprint("Provocando fallo de pagina (#PF) en 0xDEADBEEF00...\n");
        volatile uint64_t *bad = (volatile uint64_t *)0xDEADBEEF00ULL; *bad = 0xCAFEBABE;
    }
    return 1;
}

static int cmd_env(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (!env_initialized) env_init_defaults();
    uint32_t pos = 0;
    kprint("\nVARIABLES DE ENTORNO ACTIVAS:\n------------------------------\n");
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used) {
            kprint("  "); kprint(env_table[i].key); kputc('='); kprint(env_table[i].val); kprint("\n");
            fb_puts(out_buf, max_out, &pos, env_table[i].key);
            fb_putc(out_buf, max_out, &pos, '=');
            fb_puts(out_buf, max_out, &pos, env_table[i].val);
            fb_putc(out_buf, max_out, &pos, '\n');
        }
    }
    kprint("------------------------------\n");
    return 1;
}

static int cmd_export(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        return cmd_env(args, out_buf, max_out);
    }
    char k[ENV_KEY_LEN], v[ENV_VAL_LEN];
    uint32_t kp = 0, vp = 0;
    while (*args && *args != '=' && *args != ' ' && kp < sizeof(k) - 1) k[kp++] = *args++;
    k[kp] = '\0';
    if (*args == '=' || *args == ' ') args++;
    while (*args == ' ') args++;

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }
    while (*args && vp < sizeof(v) - 1) {
        if (q && *args == q) { args++; break; }
        v[vp++] = *args++;
    }
    v[vp] = '\0';

    if (kp > 0) {
        env_set(k, v);
        kprint("export: "); kprint(k); kputc('='); kprint(v); kprint("\n");
        uint32_t p = 0;
        fb_puts(out_buf, max_out, &p, k); fb_putc(out_buf, max_out, &p, '='); fb_puts(out_buf, max_out, &p, v);
        return 1;
    }
    kprint("Uso: export VAR=valor\n");
    return 1;
}

static int cmd_unset(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: unset <VAR>\n");
        return 1;
    }
    env_unset(args);
    kprint("unset: "); kprint(args); kprint("\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "unset OK");
    return 1;
}

#define SOURCE_MAX_DEPTH 3

static int cmd_source(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: source <archivo.sh>  (alias: sh <archivo.sh>)\n");
        return 1;
    }

    /* PARCHE 046: profundidad por hilo + buffer propio (antes: static compartido). */
    struct tcb *self = thread_current();
    static int source_depth_fallback = 0;
    int *sdepth = self ? &self->source_depth : &source_depth_fallback;
    uint32_t pos = 0;

    if (*sdepth >= SOURCE_MAX_DEPTH) {
        kprint("source: anidamiento maximo alcanzado (3).\n");
        fb_puts(out_buf, max_out, &pos, "Error: source anidado en exceso");
        return 0;
    }

    char *script_buf = (char *)kmalloc(4096);
    if (!script_buf) {
        kprint("source: sin memoria.\n");
        fb_puts(out_buf, max_out, &pos, "Error: sin memoria");
        return 0;
    }

    int r = vfs_read(args, script_buf, 4095);
    if (r < 0) {
        kprint("source: archivo no encontrado: '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "source: archivo no encontrado");
        kfree(script_buf);
        return 1;
    }
    script_buf[r] = '\0';

    (*sdepth)++;
    const char *p = script_buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        char line[256];
        uint32_t lp = 0;
        while (*p && *p != '\n' && lp < sizeof(line) - 1) {
            if (*p != '\r') line[lp++] = *p;
            p++;
        }
        line[lp] = '\0';
        if (*p == '\n') p++;

        while (lp > 0 && (line[lp - 1] == ' ' || line[lp - 1] == '\t')) {
            line[--lp] = '\0';
        }

        if (lp > 0) {
            char sub_out[1024];
            sub_out[0] = '\0';
            dispatch_command(line, sub_out, sizeof(sub_out));
        }
    }
    (*sdepth)--;
    kfree(script_buf);

    fb_puts(out_buf, max_out, &pos, "source: ejecucion completada");
    return 1;
}

/* ====================================================================
 * TABLA DECLARATIVA CANÓNICA DE COMANDOS DE SOMA
 * ==================================================================== */

static int cmd_history(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    return console_history_dump(out_buf, max_out);
}

/* PARCHE 046: banco de pruebas de la politica del Agente (solo usuario). */
static int cmd_agent_sim(const char *args, char *out_buf, uint32_t max_out)
{
    char cmd[256];
    uint32_t n = 0;
    char q = 0;

    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: agent_sim <comando>   (usa comillas si contiene ; | > &&)\n");
        return 1;
    }
    if (*args == '"' || *args == '\'') q = *args++;
    while (*args && n < sizeof(cmd) - 1) {
        if (q && *args == q && args[1] == '\0') break;
        cmd[n++] = *args++;
    }
    cmd[n] = '\0';

    int r = dispatch_agent_command(cmd, out_buf, max_out);
    kprint("[agent_sim] rc="); kprint_dec(r ? 1 : 0); kprint("\n");
    return 1;
}


static int cmd_paint(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t p = 0;
    kprint("Iniciando SOMA Paint...\n");
    gui_open_paint(args);
    fb_puts(out_buf, max_out, &p, "SOMA Paint abierto.");
    return 1;
}

static int cmd_memview(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t p = 0;
    kprint("Iniciando Visor de Memoria Multi-Agente...\n");
    gui_open_memview();
    fb_puts(out_buf, max_out, &p, "Visor de memoria multi-agente abierto.");
    return 1;
}

static int cmd_view(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: view <archivo.bmp|ppm>\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: view <archivo.bmp>");
        return 1;
    }

    static uint8_t v_buf[24576];
    int r = vfs_read(args, (char *)v_buf, sizeof(v_buf) - 1);
    if (r <= 0) {
        kprint("view: archivo no encontrado: '"); kprint(args); kprint("'\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "view: archivo no encontrado");
        return 1;
    }

    struct image img;
    if (image_decode(v_buf, (size_t)r, &img) == 0) {
        kprint("\n[IMAGEN]: "); kprint(args);
        kprint(" | Formato: "); kprint(img.format == IMAGE_FMT_BMP ? "BMP TrueColor" : "PPM");
        kprint(" | Dimensiones: "); kprint_dec(img.width); kprint("x"); kprint_dec(img.height);
        kprint(" px | Pixeles: "); kprint_dec(img.width * img.height); kprint("\n");

        if (fb_is_active()) {
            image_render(&img, 100, 100, (int32_t)img.width, (int32_t)img.height);
            fb_present();
            kprint("[view]: Renderizada en pantalla grafica activa.\n");
        } else {
            kprint("[view]: Para verla en pantalla completa, abre el navegador con 'vsoma'.\n");
        }

        uint32_t p = 0;
        fb_puts(out_buf, max_out, &p, "Imagen valida: ");
        fb_put_dec(out_buf, max_out, &p, img.width); fb_puts(out_buf, max_out, &p, "x");
        fb_put_dec(out_buf, max_out, &p, img.height);
        image_free(&img);
    } else {
        kprint("view: formato de imagen no reconocido o archivo corrupto.\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "view: formato no soportado");
    }
    return 1;
}

static int cmd_font(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;

    /* SOMA_VECTOR_FONTS_PATCH: 'font size <40-300>' y 'font contrast <0-100>' */
    if (args[0] == 's' && args[1] == 'i' && args[2] == 'z' && args[3] == 'e' &&
        (args[4] == ' ' || args[4] == '\0')) {
        const char *q = args + 4;
        while (*q == ' ') q++;
        if (*q >= '0' && *q <= '9') {
            int v = 0;
            while (*q >= '0' && *q <= '9' && v < 100000) { v = v * 10 + (*q - '0'); q++; }
            font_vector_set_size(v);
        }
        kprint("\n\033[1;32m[FONT]: tamano vectorial = ");
        kprint_dec((uint32_t)font_vector_get_size());
        kprint("%\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Tamano vectorial: ");
        fb_put_dec(out_buf, max_out, &pos, font_vector_get_size());
        fb_puts(out_buf, max_out, &pos, "%");
        if (fb_is_active()) fb_present_full();
        return 1;
    }
    if (args[0] == 'c' && args[1] == 'o' && args[2] == 'n' && args[3] == 't' &&
        args[4] == 'r' && args[5] == 'a' && args[6] == 's' && args[7] == 't' &&
        (args[8] == ' ' || args[8] == '\0')) {
        const char *q = args + 8;
        while (*q == ' ') q++;
        if (*q >= '0' && *q <= '9') {
            int v = 0;
            while (*q >= '0' && *q <= '9' && v < 100000) { v = v * 10 + (*q - '0'); q++; }
            font_vector_set_contrast(v);
        }
        kprint("\n\033[1;32m[FONT]: contraste vectorial = ");
        kprint_dec((uint32_t)font_vector_get_contrast());
        kprint("\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Contraste vectorial: ");
        fb_put_dec(out_buf, max_out, &pos, font_vector_get_contrast());
        if (fb_is_active()) fb_present_full();
        return 1;
    }


    if (*args == '\0') {
        kprint("\nFUENTES TIPOGRAFICAS DISPONIBLES EN SOMA:\n");
        kprint("------------------------------------------\n");
        fb_puts(out_buf, max_out, &pos, "Fuentes disponibles:\n");
        int cur = font_get_current();
        for (int order = 0; order < font_get_count(); ++order) {
            int i = font_get_sorted_id(order);
            if (i < 0) continue;
            kprint("  ["); kprint_dec((uint32_t)i); kprint("] ");
            kprint(font_get_name(i));
            kprint(" - ");
            kprint(font_get_desc(i));
            if (i == cur) kprint("  \033[1;32m<-- ACTIVA\033[0m");
            kprint("\n");

            fb_puts(out_buf, max_out, &pos, "  [");
            fb_put_dec(out_buf, max_out, &pos, i);
            fb_puts(out_buf, max_out, &pos, "] ");
            fb_puts(out_buf, max_out, &pos, font_get_name(i));
            fb_puts(out_buf, max_out, &pos, " - ");
            fb_puts(out_buf, max_out, &pos, font_get_desc(i));
            if (i == cur) fb_puts(out_buf, max_out, &pos, " (ACTIVA)");
            fb_putc(out_buf, max_out, &pos, '\n');
        }
        kprint("------------------------------------------\n");
        kprint("Uso: font <nombre|id>  (ej. 'font modern' o 'font 0')\n\n");
        return 1;
    }

    int ok = 0;
    if (*args >= '0' && *args <= '9') {
        int fid = 0;
        const char *q = args;
        while (*q >= '0' && *q <= '9' && fid < 100000) { fid = fid * 10 + (*q - '0'); q++; }
        while (*q == ' ') q++;
        if (*q != '\0') fid = -1;
        ok = font_set(fid);
    } else {
        ok = font_set_by_name(args);
    }

    if (ok) {
        int cur = font_get_current();
        kprint("\n\033[1;32m[FONT]: Fuente del sistema cambiada a: '");
        kprint(font_get_name(cur));
        kprint("' (");
        kprint(font_get_desc(cur));
        kprint(")\033[0m\n\n");

        fb_puts(out_buf, max_out, &pos, "Fuente cambiada a: ");
        fb_puts(out_buf, max_out, &pos, font_get_name(cur));

        if (fb_is_active()) {
            fb_present_full();
        }
    } else {
        kprint("\n\033[1;31m[ERROR]: Fuente no reconocida '");
        kprint(args);
        kprint("'. Escribe 'font' para listar las disponibles.\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Error: fuente no reconocida");
    }
    return 1;
}

static int cmd_cursor(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;

    if (*args == '\0') {
        kprint("\nPUNTEROS DE RATON DISPONIBLES EN SOMA:\n");
        kprint("---------------------------------------\n");
        fb_puts(out_buf, max_out, &pos, "Punteros disponibles:\n");
        int cur = mouse_get_current_cursor();
        for (int i = 0; i < mouse_get_cursor_count(); ++i) {
            kprint("  ["); kprint_dec((uint32_t)i); kprint("] ");
            kprint(mouse_get_cursor_name(i));
            kprint(" - ");
            kprint(mouse_get_cursor_desc(i));
            if (i == cur) kprint("  \033[1;32m<-- ACTIVO\033[0m");
            kprint("\n");

            fb_puts(out_buf, max_out, &pos, "  [");
            fb_put_dec(out_buf, max_out, &pos, i);
            fb_puts(out_buf, max_out, &pos, "] ");
            fb_puts(out_buf, max_out, &pos, mouse_get_cursor_name(i));
            fb_puts(out_buf, max_out, &pos, " - ");
            fb_puts(out_buf, max_out, &pos, mouse_get_cursor_desc(i));
            if (i == cur) fb_puts(out_buf, max_out, &pos, " (ACTIVO)");
            fb_putc(out_buf, max_out, &pos, '\n');
        }
        kprint("---------------------------------------\n");
        kprint("Uso: cursor <nombre|id>  (ej. 'cursor crosshair' o 'cursor 2')\n\n");
        return 1;
    }

    int ok = 0;
    if (*args >= '0' && *args <= '9' && *(args + 1) == '\0') {
        ok = mouse_set_cursor(*args - '0');
    } else {
        ok = mouse_set_cursor_by_name(args);
    }

    if (ok) {
        int cur = mouse_get_current_cursor();
        kprint("\n\033[1;32m[CURSOR]: Puntero cambiado a: '");
        kprint(mouse_get_cursor_name(cur));
        kprint("' (");
        kprint(mouse_get_cursor_desc(cur));
        kprint(")\033[0m\n\n");

        fb_puts(out_buf, max_out, &pos, "Puntero cambiado a: ");
        fb_puts(out_buf, max_out, &pos, mouse_get_cursor_name(cur));

        if (fb_is_active()) {
            fb_present_full();
        }
    } else {
        kprint("\n\033[1;31m[ERROR]: Puntero no reconocido '");
        kprint(args);
        kprint("'. Escribe 'cursor' para ver la lista.\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Error: puntero no reconocido");
    }
    return 1;
}

/* ====================================================================
 * PARCHE_TERMINAL_V2: nuevas utilidades de terminal
 * ==================================================================== */

static uint32_t shell_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void shell_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void shell_put_dec_pad(char *out, uint32_t max, uint32_t *p, uint64_t v, uint32_t width)
{
    uint32_t n = 0;
    uint64_t t = v;
    if (t == 0) n = 1;
    else while (t) { n++; t /= 10; }
    for (uint32_t i = n; i < width; ++i) fb_putc(out, max, p, ' ');
    fb_put_dec(out, max, p, v);
}

/* Copia el primer argumento (admite comillas) y devuelve el resto de la linea. */
static const char *shell_next_arg(const char *s, char *dst, uint32_t cap)
{
    uint32_t n = 0;
    char q = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '"' || *s == '\'') q = *s++;
    while (*s && n + 1 < cap) {
        if (q ? (*s == q) : (*s == ' ' || *s == '\t')) break;
        dst[n++] = *s++;
    }
    if (q && *s == q) s++;
    dst[n] = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Recorre la lista "Archivos: a, b, c" que produce vfs_format_list_ext. */
static const char *shell_list_start(const char *list)
{
    const char *c = find_substr_ci(list, ": ", 0);
    return c ? c + 2 : list;
}

static const char *shell_list_next(const char *p, char *item, uint32_t cap)
{
    uint32_t n = 0;
    while (*p == ' ' || *p == ',') p++;
    while (*p && *p != ',' && n + 1 < cap) item[n++] = *p++;
    while (n && item[n - 1] == ' ') n--;
    item[n] = '\0';
    return p;
}

static void shell_out_line(char *out_buf, uint32_t max_out, uint32_t *pos, const char *line)
{
    kprint(line);
    kprint("\n");
    if (out_buf && max_out) {
        fb_puts(out_buf, max_out, pos, line);
        fb_putc(out_buf, max_out, pos, '\n');
    }
}

/* ---- identidad ---- */
static int cmd_whoami(const char *args, char *out_buf, uint32_t max_out)
{
    char t[ENV_VAL_LEN + 4];
    uint32_t p = 0;
    (void)args;
    fb_puts(t, sizeof(t), &p, env_get("USER"));
    fb_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

static int cmd_hostname(const char *args, char *out_buf, uint32_t max_out)
{
    char t[ENV_VAL_LEN + 4];
    uint32_t p = 0;
    (void)args;
    fb_puts(t, sizeof(t), &p, env_get("HOST"));
    fb_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

static int cmd_uname(const char *args, char *out_buf, uint32_t max_out)
{
    char t[192];
    uint32_t p = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    fb_puts(t, sizeof(t), &p, "SOMA");
    if (args[0] == '-' && args[1] == 'a') {
        fb_putc(t, sizeof(t), &p, ' ');
        fb_puts(t, sizeof(t), &p, env_get("HOST"));
        fb_puts(t, sizeof(t), &p, " 0.2 ");
        fb_puts(t, sizeof(t), &p, MYOS_ARCH_NAME);
        fb_puts(t, sizeof(t), &p, " bare-metal Ring0 multi-agente");
    }
    fb_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

/* ---- resumen global del sistema ---- */
static void sys_put_ip(char *o, uint32_t max, uint32_t *p, const uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        fb_put_dec(o, max, p, ip[i]);
        if (i < 3) fb_putc(o, max, p, '.');
    }
}

static int cmd_sysinfo(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[1536];
    uint32_t p = 0;
    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0, threads = 0;
    struct ramstat_info ri;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000), m = s / 60, h = m / 60;
    (void)args;
    s %= 60; m %= 60;

    ramstat_get(&ri);
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);
    {
        char *tt = (char *)kmalloc(2048);
        if (tt) {
            thread_format_table(tt, 2048);
            for (uint32_t i = 0; tt[i]; ++i) if (tt[i] == '\n') threads++;
            if (threads) threads--;
            kfree(tt);
        }
    }

    rep[0] = '\0';
    fb_puts(rep, sizeof(rep), &p, "=== ESTADO DEL SISTEMA ===\nSOMA 0.2 | arch ");
    fb_puts(rep, sizeof(rep), &p, MYOS_ARCH_NAME);
    fb_puts(rep, sizeof(rep), &p, " | host ");
    fb_puts(rep, sizeof(rep), &p, env_get("HOST"));
    fb_puts(rep, sizeof(rep), &p, " | usuario ");
    fb_puts(rep, sizeof(rep), &p, env_get("USER"));
    fb_puts(rep, sizeof(rep), &p, "\nUptime    : ");
    if (h) { fb_put_dec(rep, sizeof(rep), &p, h); fb_puts(rep, sizeof(rep), &p, "h "); }
    fb_put_dec(rep, sizeof(rep), &p, m); fb_puts(rep, sizeof(rep), &p, "m ");
    fb_put_dec(rep, sizeof(rep), &p, s); fb_puts(rep, sizeof(rep), &p, "s\nRAM       : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
    fb_puts(rep, sizeof(rep), &p, " / ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
    fb_puts(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
    fb_puts(rep, sizeof(rep), &p, ") | libre ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
    fb_puts(rep, sizeof(rep), &p, "\nAlmacen.  : ");
    fb_put_dec(rep, sizeof(rep), &p, files_u);
    fb_putc(rep, sizeof(rep), &p, '/');
    fb_put_dec(rep, sizeof(rep), &p, FS_MAX_FILES);
    fb_puts(rep, sizeof(rep), &p, " inodos | ");
    fb_put_dec(rep, sizeof(rep), &p, bytes_u / 1024);
    fb_puts(rep, sizeof(rep), &p, " KiB usados | pendientes de sync: ");
    fb_put_dec(rep, sizeof(rep), &p, dirty_c);
    fb_puts(rep, sizeof(rep), &p, "\nHilos     : ");
    fb_put_dec(rep, sizeof(rep), &p, threads);
    fb_puts(rep, sizeof(rep), &p, " activos\nRed       : IP ");
    sys_put_ip(rep, sizeof(rep), &p, net_ip);
    fb_puts(rep, sizeof(rep), &p, " | GW ");
    sys_put_ip(rep, sizeof(rep), &p, net_gateway);
    fb_puts(rep, sizeof(rep), &p, "\n(detalle: ram -v | df | ps | status | pci)\n");
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* ---- find / du / stat ---- */
static int cmd_find(const char *args, char *out_buf, uint32_t max_out)
{
    char term[48], root[48], tok[48], item[FS_NAME_MAX + 4];
    int show_all = 0;
    uint32_t hits = 0, p = 0;
    term[0] = root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!term[0]) shell_copy(term, tok, sizeof(term));
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    if (!term[0]) {
        shell_emit("Uso: find <texto> [ruta] [-a]   (-a incluye ocultos y de sistema)\n", out_buf, max_out);
        return 1;
    }
    char *list = (char *)kmalloc(4096);
    char *rep  = (char *)kmalloc(4096);
    if (!list || !rep) {
        if (list) kfree(list);
        if (rep) kfree(rep);
        kprint("find: sin memoria.\n");
        return 1;
    }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (item[0] && find_substr_ci(item, term, 1)) {
            fb_puts(rep, 4096, &p, item);
            fb_putc(rep, 4096, &p, '\n');
            hits++;
        }
    }
    if (!hits) fb_puts(rep, 4096, &p, "find: sin coincidencias.\n");
    else { fb_put_dec(rep, 4096, &p, hits); fb_puts(rep, 4096, &p, " coincidencia(s)\n"); }
    shell_emit(rep, out_buf, max_out);
    kfree(list);
    kfree(rep);
    return 1;
}

static int cmd_du(const char *args, char *out_buf, uint32_t max_out)
{
    char root[48], tok[48], item[FS_NAME_MAX + 4];
    int show_all = 0;
    uint32_t total = 0, count = 0, used = 0, bytes_u = 0, dirty = 0, p = 0;
    root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    char *list = (char *)kmalloc(4096);
    char *data = (char *)kmalloc(FS_DATA_MAX);
    char *rep  = (char *)kmalloc(6144);
    if (!list || !data || !rep) {
        if (list) kfree(list);
        if (data) kfree(data);
        if (rep) kfree(rep);
        kprint("du: sin memoria.\n");
        return 1;
    }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (!item[0]) continue;
        int r = vfs_read(item, data, FS_DATA_MAX);
        uint32_t sz = (r > 0) ? (uint32_t)r : 0;
        shell_put_dec_pad(rep, 6144, &p, sz, 7);
        fb_puts(rep, 6144, &p, "  ");
        fb_puts(rep, 6144, &p, item);
        fb_putc(rep, 6144, &p, '\n');
        total += sz;
        count++;
    }
    vfs_get_stats(&used, &bytes_u, &dirty);
    fb_puts(rep, 6144, &p, "Total: ");
    fb_put_dec(rep, 6144, &p, count);
    fb_puts(rep, 6144, &p, " archivos, ");
    fb_put_dec(rep, 6144, &p, total);
    fb_puts(rep, 6144, &p, " bytes (");
    fb_put_dec(rep, 6144, &p, total / 1024);
    fb_puts(rep, 6144, &p, " KiB) | inodos libres: ");
    fb_put_dec(rep, 6144, &p, (used <= FS_MAX_FILES) ? (FS_MAX_FILES - used) : 0);
    fb_putc(rep, 6144, &p, '\n');
    shell_emit(rep, out_buf, max_out);
    kfree(list);
    kfree(data);
    kfree(rep);
    return 1;
}

static int cmd_stat(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], rep[512];
    uint32_t p = 0, lines = 0;
    int binary = 0;
    if (!args) args = "";
    shell_next_arg(args, path, sizeof(path));
    if (!path[0]) {
        shell_emit("Uso: stat <archivo>\n", out_buf, max_out);
        return 1;
    }
    char *data = (char *)kmalloc(FS_DATA_MAX);
    if (!data) { kprint("stat: sin memoria.\n"); return 1; }
    int r = vfs_read(path, data, FS_DATA_MAX);
    if (r < 0) {
        fb_puts(rep, sizeof(rep), &p, "stat: no encontrado: ");
        fb_puts(rep, sizeof(rep), &p, path);
        fb_putc(rep, sizeof(rep), &p, '\n');
        shell_emit(rep, out_buf, max_out);
        kfree(data);
        return 1;
    }
    for (int i = 0; i < r; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\n') lines++;
        else if (c < 9 || (c > 13 && c < 32)) binary = 1;
    }
    if (r > 0 && data[r - 1] != '\n') lines++;
    rep[0] = '\0';
    fb_puts(rep, sizeof(rep), &p, "Archivo     : ");
    fb_puts(rep, sizeof(rep), &p, path);
    fb_puts(rep, sizeof(rep), &p, "\nTamano      : ");
    fb_put_dec(rep, sizeof(rep), &p, (uint64_t)r);
    fb_puts(rep, sizeof(rep), &p, " bytes\nTipo        : ");
    fb_puts(rep, sizeof(rep), &p, r == 0 ? "vacio" : (binary ? "binario" : "texto"));
    if (r > 0 && !binary) {
        fb_puts(rep, sizeof(rep), &p, "\nLineas      : ");
        fb_put_dec(rep, sizeof(rep), &p, lines);
    }
    fb_puts(rep, sizeof(rep), &p, "\nVisibilidad : ");
    fb_puts(rep, sizeof(rep), &p, vfs_is_hidden(path) ? "oculto" : "visible");
    fb_puts(rep, sizeof(rep), &p, "\nAlmacenaje  : ");
    if (find_substr_ci(path, "/proc/", 0) == path || find_substr_ci(path, "/dev/", 0) == path)
        fb_puts(rep, sizeof(rep), &p, "pseudo-archivo del kernel");
    else if (find_substr_ci(path, "/tmp/", 0) || find_substr_ci(path, "/sys/", 0))
        fb_puts(rep, sizeof(rep), &p, "volatil (solo RAM)");
    else
        fb_puts(rep, sizeof(rep), &p, "persistente (virtio-blk)");
    fb_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    kfree(data);
    return 1;
}

/* ---- directorios (el VFS los representa con un marcador oculto <dir>/.dir) ---- */
static int shell_dir_path(const char *args, char *path, uint32_t cap)
{
    char tok[FS_NAME_MAX + 8];
    uint32_t p = 0;
    shell_next_arg(args ? args : "", tok, sizeof(tok));
    if (!tok[0]) return 0;
    if (tok[0] != '/') {
        const char *pre = "/workspace/";
        while (*pre && p + 1 < cap) path[p++] = *pre++;
    }
    for (uint32_t i = 0; tok[i] && p + 1 < cap; ++i) path[p++] = tok[i];
    while (p > 1 && path[p - 1] == '/') p--;
    path[p] = '\0';
    return (int)p;
}

static int shell_user_space(const char *path)
{
    static const char *const roots[] = { "/workspace", "/mis_archivos", 0 };
    for (int r = 0; roots[r]; ++r) {
        uint32_t i = 0;
        while (roots[r][i] && path[i] == roots[r][i]) ++i;
        if (!roots[r][i] && (path[i] == '\0' || path[i] == '/')) return 1;
    }
    return 0;
}

static int shell_dir_marker(const char *path, int n, char *marker, uint32_t cap)
{
    if (n <= 1 || (uint32_t)n + 6 > cap || (uint32_t)n + 6 > FS_NAME_MAX) return 0;
    for (int i = 0; i < n; ++i) marker[i] = path[i];
    marker[n] = '/'; marker[n + 1] = '.'; marker[n + 2] = 'd';
    marker[n + 3] = 'i'; marker[n + 4] = 'r'; marker[n + 5] = '\0';
    return 1;
}

static int cmd_mkdir(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], marker[FS_NAME_MAX + 16], probe[2], rep[160];
    uint32_t p = 0;
    int n = shell_dir_path(args, path, sizeof(path));
    if (n <= 1) {
        shell_emit("Uso: mkdir <ruta>   (relativa a /workspace si no empieza por /)\n", out_buf, max_out);
        return 1;
    }
    if (agent_dispatch_mode && !shell_user_space(path)) {
        shell_emit("mkdir: los agentes solo pueden crear directorios en /workspace o /mis_archivos.\n", out_buf, max_out);
        return 1;
    }
    if (!shell_dir_marker(path, n, marker, sizeof(marker))) {
        shell_emit("mkdir: ruta demasiado larga para el VFS (maximo 42 caracteres).\n", out_buf, max_out);
        return 1;
    }
    rep[0] = '\0';
    if (vfs_read(marker, probe, sizeof(probe)) >= 0) {
        fb_puts(rep, sizeof(rep), &p, "mkdir: ya existe: ");
    } else if (vfs_write(marker, "", 0) < 0) {
        fb_puts(rep, sizeof(rep), &p, "mkdir: no se pudo crear (VFS lleno o ruta protegida): ");
    } else {
        fb_puts(rep, sizeof(rep), &p, "Directorio creado: ");
    }
    fb_puts(rep, sizeof(rep), &p, path);
    fb_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

static int cmd_rmdir(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], marker[FS_NAME_MAX + 16], prefix[FS_NAME_MAX + 16];
    char item[FS_NAME_MAX + 4], probe[2], rep[160];
    uint32_t p = 0;
    int n = shell_dir_path(args, path, sizeof(path));
    if (n <= 1) {
        shell_emit("Uso: rmdir <ruta>   (solo directorios vacios)\n", out_buf, max_out);
        return 1;
    }
    if (agent_dispatch_mode && !shell_user_space(path)) {
        shell_emit("rmdir: los agentes solo pueden borrar directorios en /workspace o /mis_archivos.\n", out_buf, max_out);
        return 1;
    }
    if (!shell_dir_marker(path, n, marker, sizeof(marker))) {
        shell_emit("rmdir: ruta no valida.\n", out_buf, max_out);
        return 1;
    }
    rep[0] = '\0';
    if (vfs_read(marker, probe, sizeof(probe)) < 0) {
        fb_puts(rep, sizeof(rep), &p, "rmdir: no existe: ");
        fb_puts(rep, sizeof(rep), &p, path);
        fb_putc(rep, sizeof(rep), &p, '\n');
        shell_emit(rep, out_buf, max_out);
        return 1;
    }
    shell_copy(prefix, marker, sizeof(prefix));
    prefix[n + 1] = '\0';                      /* "<ruta>/" */
    char *list = (char *)kmalloc(2048);
    if (!list) { kprint("rmdir: sin memoria.\n"); return 1; }
    vfs_format_list_ext(1, prefix, list, 2048);
    const char *q = shell_list_start(list);
    int nonempty = 0;
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (item[0] && !str_eq(item, marker)) { nonempty = 1; break; }
    }
    kfree(list);
    if (nonempty) {
        fb_puts(rep, sizeof(rep), &p, "rmdir: el directorio no esta vacio: ");
    } else if (vfs_delete(marker) == 0) {
        fb_puts(rep, sizeof(rep), &p, "Directorio eliminado: ");
    } else {
        fb_puts(rep, sizeof(rep), &p, "rmdir: no se pudo eliminar: ");
    }
    fb_puts(rep, sizeof(rep), &p, path);
    fb_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* ---- sort / uniq (archivo o tuberia) ---- */
#define SHELL_MAX_LINES 512

/* Devuelve una copia en heap del texto de entrada (archivo o stdin de tuberia). */
static char *shell_read_input(const char *fn, const char *who)
{
    char *buf;
    if (fn && fn[0]) {
        buf = (char *)kmalloc(FS_DATA_MAX);
        if (!buf) { kprint(who); kprint(": sin memoria.\n"); return 0; }
        if (vfs_read(fn, buf, FS_DATA_MAX) < 0) {
            kprint(who); kprint(": no encontrado: "); kprint(fn); kprint("\n");
            kfree(buf);
            return 0;
        }
        return buf;
    }
    if (pipe_stdin_data) {
        uint32_t n = shell_strlen(pipe_stdin_data);
        buf = (char *)kmalloc(n + 1);
        if (!buf) { kprint(who); kprint(": sin memoria.\n"); return 0; }
        memcpy(buf, pipe_stdin_data, n + 1);
        return buf;
    }
    return 0;
}

static uint32_t shell_split_lines(char *buf, char **lines, uint32_t max)
{
    uint32_t n = 0;
    char *p = buf;
    while (*p && n < max) {
        lines[n++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = '\0'; p++; }
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t l = shell_strlen(lines[i]);
        if (l && lines[i][l - 1] == '\r') lines[i][l - 1] = '\0';
    }
    return n;
}

static int32_t shell_leading_int(const char *s)
{
    int neg = 0;
    int32_t v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9' && v < 100000000) { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static int shell_line_cmp(const char *a, const char *b, int numeric)
{
    if (numeric) {
        int32_t x = shell_leading_int(a), y = shell_leading_int(b);
        if (x != y) return x < y ? -1 : 1;
    }
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int cmd_sort(const char *args, char *out_buf, uint32_t max_out)
{
    char fn[FS_NAME_MAX + 8];
    char *lines[SHELL_MAX_LINES];
    int rev = 0, num = 0;
    uint32_t pos = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    while (*args == '-') {
        args++;
        while (*args && *args != ' ') {
            if (*args == 'r') rev = 1;
            else if (*args == 'n') num = 1;
            args++;
        }
        while (*args == ' ') args++;
    }
    shell_next_arg(args, fn, sizeof(fn));
    char *buf = shell_read_input(fn, "sort");
    if (!buf) {
        if (!fn[0] && !pipe_stdin_data) shell_emit("Uso: sort [-r] [-n] [archivo]   o   <comando> | sort\n", out_buf, max_out);
        return 1;
    }
    uint32_t n = shell_split_lines(buf, lines, SHELL_MAX_LINES);
    for (uint32_t i = 1; i < n; ++i) {
        char *key = lines[i];
        uint32_t j = i;
        while (j > 0) {
            int c = shell_line_cmp(lines[j - 1], key, num);
            if (rev ? (c < 0) : (c > 0)) { lines[j] = lines[j - 1]; j--; }
            else break;
        }
        lines[j] = key;
    }
    if (out_buf && max_out) out_buf[0] = '\0';
    for (uint32_t i = 0; i < n; ++i) shell_out_line(out_buf, max_out, &pos, lines[i]);
    kfree(buf);
    return 1;
}

static int cmd_uniq(const char *args, char *out_buf, uint32_t max_out)
{
    char fn[FS_NAME_MAX + 8];
    char *lines[SHELL_MAX_LINES];
    char tmp[300];
    int count = 0;
    uint32_t pos = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || args[2] == '\0')) {
        count = 1;
        args += 2;
    }
    shell_next_arg(args, fn, sizeof(fn));
    char *buf = shell_read_input(fn, "uniq");
    if (!buf) {
        if (!fn[0] && !pipe_stdin_data) shell_emit("Uso: uniq [-c] [archivo]   o   <comando> | uniq\n", out_buf, max_out);
        return 1;
    }
    uint32_t n = shell_split_lines(buf, lines, SHELL_MAX_LINES);
    if (out_buf && max_out) out_buf[0] = '\0';
    for (uint32_t i = 0; i < n; ) {
        uint32_t j = i + 1, tp = 0;
        while (j < n && str_eq(lines[i], lines[j])) j++;
        tmp[0] = '\0';
        if (count) {
            shell_put_dec_pad(tmp, sizeof(tmp), &tp, j - i, 4);
            fb_putc(tmp, sizeof(tmp), &tp, ' ');
        }
        fb_puts(tmp, sizeof(tmp), &tp, lines[i]);
        shell_out_line(out_buf, max_out, &pos, tmp);
        i = j;
    }
    kfree(buf);
    return 1;
}

static const struct command_entry cmd_table[] = {
    /* Scripting y Automatización */
    { "history",       cmd_history,      CMD_F_SAFE,   "Muestra el historial persistente de comandos" },
    { "source",       cmd_source,       CMD_F_SAFE,   "Ejecuta un script de comandos por lotes (.sh)" },
    { "sh",           cmd_source,       CMD_F_ALIAS,  "Alias de source" },

    /* Sistema, Ayuda e Identidad */
    { "help",         cmd_help,         CMD_F_SAFE,   "Ayuda por categorias: help [categoria|comando|texto|all|agentes]" },
    { "somafetch",    cmd_somafetch,    CMD_F_SAFE,   "Ficha del sistema y telemetria en arte ASCII" },
    { "neofetch",     cmd_somafetch,    CMD_F_ALIAS,  "Alias de somafetch" },
    { "fetch",        cmd_somafetch,    CMD_F_ALIAS,  "Alias de somafetch" },
    { "creador",      cmd_creador,      CMD_F_SAFE,   "Muestra informacion de la autoría de SOMA" },
    { "status",       cmd_status,       CMD_F_SAFE,   "Muestra estado basico de red e IP" },
    { "uptime",       cmd_uptime,       CMD_F_SAFE,   "Muestra el tiempo de actividad del kernel" },
    { "ram",          cmd_ram,          CMD_F_SAFE,   "Memoria RAM: total/usada/libre, barra y heap (-s resumen, -v desglose; alias: free)" },
    { "date",         cmd_date,         CMD_F_SAFE,   "Fecha y hora real UTC desde el reloj CMOS RTC" },
    { "sysinfo",      cmd_sysinfo,      CMD_F_SAFE,   "Resumen del sistema: uptime, RAM, almacenamiento, hilos y red" },
    { "sys",          cmd_sysinfo,      CMD_F_ALIAS | CMD_F_SAFE, "Alias de sysinfo" },
    { "whoami",       cmd_whoami,       CMD_F_SAFE,   "Muestra el usuario actual" },
    { "hostname",     cmd_hostname,     CMD_F_SAFE,   "Muestra el nombre del equipo" },
    { "uname",        cmd_uname,        CMD_F_SAFE,   "Version y arquitectura de SOMA (uname -a: completo)" },
    { "clear",        cmd_clear,        CMD_F_SAFE,   "Limpia el buffer de pantalla VGA" },
    { "cls",          cmd_clear,        CMD_F_ALIAS,  "Alias de clear" },

    /* Variables de Entorno */
    { "env",          cmd_env,          CMD_F_SAFE,   "Muestra variables de entorno activas" },
    { "export",       cmd_export,       CMD_F_SAFE,   "Define o modifica una variable de entorno (VAR=val)" },
    { "unset",        cmd_unset,        CMD_F_SAFE,   "Elimina una variable de entorno activa" },

    /* Diagnóstico, Telemetría y Memoria */
    { "test_suite",   cmd_test_suite,   CMD_F_SAFE,   "Suite integral de auto-test y no-regresion (9 fases)" },
    { "test",         cmd_test_suite,   CMD_F_ALIAS,  "Alias de test_suite" },
    { "mem",          cmd_mem,          CMD_F_SAFE,   "Inspeccion fisica de CR0, CR3, CR4 y paginacion" },
    { "free",         cmd_ram,          CMD_F_ALIAS | CMD_F_SAFE, "Alias de ram" },
    { "df",           cmd_df,           CMD_F_SAFE,   "Capacidad de almacenamiento y estado de RamFS" },
    { "stats",        cmd_stats,        CMD_F_SAFE,   "Estadisticas de paquetes y bytes VirtIO-NET" },
    { "heap",         cmd_heap,         CMD_F_SAFE,   "Telemetria y auto-test del KHeap dinamico" },
    { "pmm",          cmd_pmm,          CMD_F_SAFE,   "Telemetria y auto-test del PMM bitmap allocator" },
    { "vmm",          cmd_vmm,          CMD_F_SAFE,   "Verificacion de tablas de paginas MMU 4 KiB" },
    { "pci",          cmd_pci,          CMD_F_SAFE,   "Escaneo completo del bus PCI y BAR0 I/O" },
    { "arp",          cmd_arp,          CMD_F_SAFE,   "Tabla de cache de resolucion dinamica ARP" },

    /* Multitarea, Hilos y Control de Energía */
    { "ps",           cmd_ps,           CMD_F_SAFE,   "Tabla de hilos de kernel, demonios y ticks CPU" },
    { "threads",      cmd_threads,      CMD_F_SAFE,   "Supervision de KThreads y stacks virtuales" },
    { "spawn",        cmd_spawn,        CMD_F_SAFE,   "Lanza un comando como hilo en segundo plano" },
    { "bg",           cmd_spawn,        CMD_F_ALIAS,  "Alias de spawn" },
    { "kill",         cmd_kill,         CMD_F_NORMAL, "Termina un hilo en segundo plano (TID > 3)" },
    { "sleep",        cmd_sleep,        CMD_F_SAFE,   "Suspende la CPU con HLT durante N milisegundos" },
    { "time",         cmd_time,         CMD_F_SAFE,   "Mide milisegundos y ciclos TSC de un comando" },
    { "reboot",       cmd_reboot,       CMD_F_NORMAL, "Reinicia fisicamente el equipo (8042 reset)" },
    { "poweroff",     cmd_poweroff,     CMD_F_NORMAL, "Apaga el sistema limpiamente via ACPI" },
    { "shutdown",     cmd_poweroff,     CMD_F_ALIAS,  "Alias de poweroff" },
    { "halt",         cmd_halt,         CMD_F_NORMAL, "Sincroniza discos y detiene la CPU (HLT)" },

    /* Sistema de Archivos y Coreutils */
    { "ls",           cmd_ls,           CMD_F_SAFE,   "Lista archivos persistentes del RamFS" },
    { "cat",          cmd_cat,          CMD_F_SAFE,   "Muestra el contenido de un archivo" },
    { "write",        cmd_write,        CMD_F_SAFE,   "Crea o sobrescribe un archivo en RamFS" },
    { "rm",           cmd_rm,           CMD_F_SAFE,   "Elimina un archivo del RamFS" },
    { "cp",           cmd_cp,           CMD_F_SAFE,   "Copia un archivo a una nueva ruta" },
    { "mv",           cmd_mv,           CMD_F_SAFE,   "Mueve o renombra un archivo en RamFS" },
    { "touch",        cmd_touch,        CMD_F_SAFE,   "Crea un archivo vacio si no existe" },
    { "tree",         cmd_tree,         CMD_F_SAFE,   "Muestra la jerarquia de archivos en arbol" },
    { "head",         cmd_head,         CMD_F_SAFE,   "Muestra las primeras N lineas de un archivo" },
    { "tail",         cmd_tail,         CMD_F_SAFE,   "Muestra las ultimas N lineas de un archivo" },
    { "wc",           cmd_wc,           CMD_F_SAFE,   "Cuenta lineas, palabras y bytes de un archivo" },
    { "grep",         cmd_grep,         CMD_F_SAFE,   "Busca patrones de texto dentro de un archivo" },
    { "echo",         cmd_echo,         CMD_F_SAFE,   "Imprime texto en consola o con redireccion" },
    { "nano",         cmd_nano,         CMD_F_NORMAL, "Editor de texto visual interactivo en 80x25" },
    { "edit",         cmd_nano,         CMD_F_ALIAS,  "Alias de nano" },
    { "hexdump",      cmd_hexdump,      CMD_F_SAFE,   "Volcado canonico hexadecimal y ASCII de archivo" },
    { "xxd",          cmd_hexdump,      CMD_F_ALIAS,  "Alias de hexdump" },
    { "find",         cmd_find,         CMD_F_SAFE,   "Busca archivos por nombre: find <texto> [ruta] [-a]" },
    { "mkdir",        cmd_mkdir,        CMD_F_SAFE,   "Crea un directorio (agentes: solo en /workspace y /mis_archivos)" },
    { "rmdir",        cmd_rmdir,        CMD_F_SAFE,   "Elimina un directorio vacio" },
    { "stat",         cmd_stat,         CMD_F_SAFE,   "Metadatos de un archivo: tamano, tipo, lineas y persistencia" },
    { "du",           cmd_du,           CMD_F_SAFE,   "Uso de disco por archivo y total (du [ruta] [-a])" },
    { "sort",         cmd_sort,         CMD_F_SAFE,   "Ordena lineas (-r inverso, -n numerico); admite tuberias" },
    { "uniq",         cmd_uniq,         CMD_F_SAFE,   "Quita lineas repetidas consecutivas (-c cuenta); admite tuberias" },

    /* Almacenamiento Persistente a Bajo Nivel */
    { "checkpoint",   cmd_checkpoint,   CMD_F_SAFE,   "Guarda snapshot persistente en virtio-blk (LBA 4096)" },
    { "fs-backup",    cmd_checkpoint,   CMD_F_ALIAS,  "Alias de checkpoint" },
    { "rollback",     cmd_rollback,     CMD_F_NORMAL, "Restaura RamFS desde el snapshot fisico" },
    { "fs-restore",   cmd_rollback,     CMD_F_ALIAS,  "Alias de rollback" },
    { "fs-sync",      cmd_fs_sync,      CMD_F_SAFE,   "Fuerza sincronizacion de inodos sucios a disco" },
    { "fs-format",    cmd_fs_format,    CMD_F_NORMAL, "Formatea RamFS restaurando imagen de fabrica" },
    { "sector_read",  cmd_sector_read,  CMD_F_SAFE,   "Lectura fisica directa de sector LBA de 512 B" },
    { "sector_r",     cmd_sector_read,  CMD_F_ALIAS,  "Alias de sector_read" },
    { "sector_write", cmd_sector_write, CMD_F_NORMAL, "Escritura fisica directa en sector LBA de usuario" },
    { "sector_w",     cmd_sector_write, CMD_F_ALIAS,  "Alias de sector_write" },

    /* Red TCP/IP y Web */
    { "httpd",        cmd_httpd,        CMD_F_SAFE,   "Estado del servidor web HTTP (puerto 80 / localhost:8090)" },
    { "ping",         cmd_ping,         CMD_F_SAFE,   "Envia peticiones ICMP Echo a una IP destino" },
    { "dns",          cmd_dns,          CMD_F_SAFE,   "Resolucion dinamica de nombres de dominio DNS" },
    { "curl",         cmd_curl,         CMD_F_SAFE,   "Cliente HTTP/1.1 con resolucion y cabeceras" },
    { "search",       cmd_search,       CMD_F_SAFE,   "Busqueda web DuckDuckGo Instant Answer" },
    { "ddg",          cmd_search,       CMD_F_ALIAS,  "Alias de search" },

    /* Inteligencia Artificial y Sistema Multi-Agente */
    { "soma",         cmd_soma,         CMD_F_NORMAL, "Invoca al Agente Central SOMA con memoria episódica" },
    { "buscador",     cmd_buscador,     CMD_F_NORMAL, "Invoca al Agente Buscador (Web & System Research)" },
    { "agent",        cmd_agent,        CMD_F_NORMAL, "Despacha misiones a agentes (soma, buscador, etc.)" },
    { "mayordomo",    cmd_mayordomo,     CMD_F_NORMAL, "Agente directo para operaciones del sistema" },
    { "delegate",     delegate_cmd,     CMD_F_SAFE,   "Delega una sub-mision especializada a otro agente" },
    { "myos",         cmd_soma,         CMD_F_ALIAS,  "Alias de soma" },

    /* Memoria Estructurada en Árbol */
    { "mem_tree",     mem_tree_cmd,     CMD_F_SAFE,   "Muestra la jerarquia de temas del arbol de memoria" },
    { "mem_read",     mem_read_cmd,     CMD_F_SAFE,   "Lee un nodo tematico del arbol de memoria" },
    { "mem_write",    mem_write_cmd,    CMD_F_SAFE,   "Escribe o actualiza un nodo en el arbol de memoria" },
    { "mem_search",   mem_search_cmd,   CMD_F_SAFE,   "Busca conceptos o palabras clave en el arbol de memoria" },
    { "bm25",         mem_search_cmd,   CMD_F_ALIAS,  "Alias de mem_search (motor BM25)" },
    { "health",       cmd_health,       CMD_F_SAFE,   "Verifica estado del servidor LLM local (/health)" },
    { "llm",          cmd_llm,          CMD_F_NORMAL, "Consulta libre en lenguaje natural a nail-35b" },
    { "llm-diag",     cmd_llm_diag,     CMD_F_SAFE,   "Diagnostico holistico del kernel mediante IA" },

    /* Inspección de Código Fuente del Kernel (SRCFS) */
    { "src_ls",       cmd_src_ls,       CMD_F_SAFE,   "Lista archivos fuente del kernel empaquetados" },
    { "src_cat",      cmd_src_cat,      CMD_F_SAFE,   "Muestra un fragmento de codigo fuente desde offset" },
    { "src_grep",     cmd_src_grep,     CMD_F_SAFE,   "Busca cadenas en los fuentes con linea y offset" },

    /* Entorno Grafico GUI */
        { "view",         cmd_view,         CMD_F_SAFE,   "Inspecciona y visualiza archivos de imagen (BMP/PPM)" },
{ "cursor",       cmd_cursor,       CMD_F_SAFE,   "Muestra o cambia el puntero activo del raton (arrow|crosshair|hand)" },
    { "font",         cmd_font,         CMD_F_SAFE,   "Muestra o cambia la fuente tipografica activa (classic|modern)" },
    { "paint",        cmd_paint,        CMD_F_SAFE,   "Editor de dibujo bitmap estilo Paint (lienzo, paleta y guardado BMP)" },
    { "memview",      cmd_memview,      CMD_F_SAFE,   "Visor visual interactivo del arbol de memoria de agentes (GUI)" },
    { "agentmem",     cmd_memview,      CMD_F_ALIAS,  "Alias de memview" },
    { "vsoma",        cmd_vsoma,        CMD_F_SAFE,   "Inicia la interfaz grafica HD (1280x720, 32 bpp, 16.7M colores)" },
    { "gui",          cmd_vsoma,        CMD_F_ALIAS,  "Alias de vsoma" },

    /* Auto-Modificación y Pruebas Críticas */
    { "hpatch",       cmd_hpatch,       CMD_F_NORMAL, "Prueba del buzon de parches y Singularity Loop" },
    { "panic",        cmd_panic,        CMD_F_NORMAL, "Dispara excepciones de CPU para verificar IDT/TSS" },
    { "agent_sim",    cmd_agent_sim,    CMD_F_NORMAL, "Ejecuta un comando bajo la politica del Agente (pruebas de seguridad)" },
};

static const int cmd_table_count = sizeof(cmd_table) / sizeof(cmd_table[0]);

/* ====================================================================
 * PARCHE_TERMINAL_V2: ayuda por categorias y catalogo vivo de comandos
 * Las categorias solo agrupan por nombre; todo comando nuevo de cmd_table que
 * no figure aqui aparece automaticamente en "Otros (sin categoria)".
 * ==================================================================== */
#define HELP_BUF 12288

struct help_category {
    const char *title;
    const char *names;
};

static const struct help_category help_categories[] = {
    { "Ayuda y sistema",      "help sysinfo somafetch creador status uptime date whoami hostname uname clear history env export unset" },
    { "Memoria y diagnostico","ram df mem heap pmm vmm pci arp stats test_suite health llm-diag" },
    { "Procesos",             "ps threads spawn kill sleep time" },
    { "Archivos y texto",     "ls tree cat head tail wc grep find sort uniq echo write touch mkdir rmdir cp mv rm stat du hexdump nano source" },
    { "Almacenamiento",       "checkpoint rollback fs-sync fs-format sector_read sector_write" },
    { "Red y web",            "httpd ping dns curl search" },
    { "IA y multiagente",     "soma buscador mayordomo agent delegate llm mem_tree mem_read mem_write mem_search memview" },
    { "Codigo fuente",        "src_ls src_cat src_grep" },
    { "Interfaz grafica",     "vsoma view cursor font paint" },
    { "Energia y pruebas",    "reboot poweroff halt hpatch panic agent_sim" },
};
#define HELP_NCAT ((int)(sizeof(help_categories) / sizeof(help_categories[0])))

struct help_usage {
    const char *name;
    const char *usage;
};

static const struct help_usage help_usages[] = {
    { "help",       "help [categoria | comando | texto | all | agentes]" },
    { "ram",        "ram [-s | -v | base | reset]   (-s resumen, -v desglose, base/reset: medir una app)" },
    { "uname",      "uname [-a]" },
    { "find",       "find <texto> [ruta] [-a]   (-a incluye ocultos y de sistema)" },
    { "du",         "du [ruta] [-a]" },
    { "stat",       "stat <archivo>" },
    { "mkdir",      "mkdir <ruta>   (relativa a /workspace si no empieza por /)" },
    { "rmdir",      "rmdir <ruta>   (solo directorios vacios)" },
    { "sort",       "sort [-r] [-n] [archivo]   o   <comando> | sort" },
    { "uniq",       "uniq [-c] [archivo]   o   <comando> | uniq" },
    { "ls",         "ls [-a] [ruta]" },
    { "grep",       "grep [-i] [-l] <patron> [archivo]   (sin archivo busca en todo el RamFS)" },
    { "head",       "head [-n N] <archivo>" },
    { "tail",       "tail [-n N] <archivo>" },
    { "cp",         "cp <origen> <destino>" },
    { "mv",         "mv <origen> <destino>" },
    { "write",      "write <ruta> \"<texto>\"   (\\n = salto de linea)" },
    { "curl",       "curl <url> | curl <host> [puerto] [ruta]" },
    { "search",     "search <consulta>" },
    { "spawn",      "spawn <comando>   (segundo plano; log en /tmp/job_<TID>.log)" },
    { "kill",       "kill <TID>" },
    { "delegate",   "delegate <agente> \"<mision>\"" },
    { "mem_search", "mem_search <palabras clave>" },
    { "mayordomo",  "mayordomo <tarea>   p. ej. 'muestra la RAM', 'que procesos hay', 'ayuda ram'" },
    { "font",       "font [nombre | id]" },
    { "cursor",     "cursor [nombre | id]" },
};

static int names_has(const char *list, const char *name)
{
    uint32_t nl = shell_strlen(name);
    if (!nl) return 0;
    while (*list) {
        const char *s;
        uint32_t l = 0;
        while (*list == ' ') list++;
        s = list;
        while (list[l] && list[l] != ' ') l++;
        if (l == nl) {
            uint32_t i = 0;
            while (i < l && s[i] == name[i]) i++;
            if (i == l) return 1;
        }
        list += l;
    }
    return 0;
}

static const struct command_entry *help_find(const char *name)
{
    if (!name || !*name) return 0;
    for (int i = 0; i < cmd_table_count; ++i)
        if (str_eq(cmd_table[i].name, name)) return &cmd_table[i];
    return 0;
}

static const struct command_entry *help_primary(const struct command_entry *e)
{
    if (!(e->flags & CMD_F_ALIAS)) return e;
    for (int i = 0; i < cmd_table_count; ++i)
        if (!(cmd_table[i].flags & CMD_F_ALIAS) && cmd_table[i].handler == e->handler)
            return &cmd_table[i];
    return e;
}

static int help_category_of(const char *name)
{
    for (int c = 0; c < HELP_NCAT; ++c)
        if (names_has(help_categories[c].names, name)) return c;
    return -1;
}

static int help_in_cat(const struct command_entry *e, int cat)
{
    if (cat >= 0) return names_has(help_categories[cat].names, e->name);
    return help_category_of(e->name) < 0;
}

static const char *help_usage_for(const char *name)
{
    for (uint32_t i = 0; i < sizeof(help_usages) / sizeof(help_usages[0]); ++i)
        if (str_eq(help_usages[i].name, name)) return help_usages[i].usage;
    return 0;
}

/* Coincidencia por prefijo de cualquier palabra del titulo ("red" -> "Red y web"). */
static int help_cat_match(const char *title, const char *q)
{
    uint32_t ql = shell_strlen(q);
    const char *w = title;
    if (ql < 2) return 0;
    while (*w) {
        uint32_t i = 0;
        while (i < ql && w[i] && to_lower_char(w[i]) == to_lower_char(q[i])) i++;
        if (i == ql) return 1;
        while (*w && *w != ' ') w++;
        while (*w == ' ') w++;
    }
    return 0;
}

static void help_put_wrapped(char *o, uint32_t max, uint32_t *p, const char *word, uint32_t *col)
{
    uint32_t wl = shell_strlen(word);
    if (*col > 4 && *col + 1 + wl > 76) {
        fb_puts(o, max, p, "\n    ");
        *col = 4;
    } else if (*col > 4) {
        fb_putc(o, max, p, ' ');
        (*col)++;
    }
    fb_puts(o, max, p, word);
    *col += wl;
}

/* only_cat: -2 = todas, -1 = sin categoria, >=0 = esa categoria. */
static void help_render(char *o, uint32_t max, uint32_t *p, int with_help,
                        int only_safe, int tag, int only_cat)
{
    for (int ci = 0; ci <= HELP_NCAT; ++ci) {
        int cat = (ci == HELP_NCAT) ? -1 : ci;
        int k = 0;
        uint32_t col = 0;
        if (only_cat != -2 && cat != only_cat) continue;
        for (int i = 0; i < cmd_table_count; ++i) {
            const struct command_entry *e = &cmd_table[i];
            if (e->flags & CMD_F_ALIAS) continue;
            if (only_safe && !(e->flags & CMD_F_SAFE)) continue;
            if (help_in_cat(e, cat)) k++;
        }
        if (!k) continue;
        fb_puts(o, max, p, "== ");
        fb_puts(o, max, p, cat >= 0 ? help_categories[cat].title : "Otros (sin categoria)");
        fb_puts(o, max, p, " (");
        fb_put_dec(o, max, p, (uint64_t)k);
        fb_puts(o, max, p, ") ==\n");
        for (int i = 0; i < cmd_table_count; ++i) {
            const struct command_entry *e = &cmd_table[i];
            if (e->flags & CMD_F_ALIAS) continue;
            if (only_safe && !(e->flags & CMD_F_SAFE)) continue;
            if (!help_in_cat(e, cat)) continue;
            if (with_help) {
                uint32_t nl = shell_strlen(e->name);
                fb_puts(o, max, p, "  ");
                fb_puts(o, max, p, e->name);
                for (uint32_t s = nl; s < 14; ++s) fb_putc(o, max, p, ' ');
                fb_putc(o, max, p, ' ');
                if (tag) fb_puts(o, max, p, (e->flags & CMD_F_SAFE) ? "[A] " : "    ");
                fb_puts(o, max, p, "- ");
                fb_puts(o, max, p, e->help);
                fb_putc(o, max, p, '\n');
            } else {
                if (col == 0) { fb_puts(o, max, p, "    "); col = 4; }
                help_put_wrapped(o, max, p, e->name, &col);
            }
        }
        if (!with_help && col) fb_putc(o, max, p, '\n');
    }
}

static void help_detail(char *o, uint32_t max, uint32_t *p, const struct command_entry *e)
{
    const struct command_entry *pr = help_primary(e);
    const char *usage = help_usage_for(pr->name);
    int cat = help_category_of(pr->name);
    int first = 1;

    fb_puts(o, max, p, pr->name);
    fb_puts(o, max, p, " - ");
    fb_puts(o, max, p, pr->help);
    fb_putc(o, max, p, '\n');
    if (e != pr) {
        fb_puts(o, max, p, "  ('");
        fb_puts(o, max, p, e->name);
        fb_puts(o, max, p, "' es un alias de '");
        fb_puts(o, max, p, pr->name);
        fb_puts(o, max, p, "')\n");
    }
    fb_puts(o, max, p, "  Categoria : ");
    fb_puts(o, max, p, cat >= 0 ? help_categories[cat].title : "Otros (sin categoria)");
    fb_putc(o, max, p, '\n');
    for (int i = 0; i < cmd_table_count; ++i) {
        if ((cmd_table[i].flags & CMD_F_ALIAS) && cmd_table[i].handler == pr->handler) {
            fb_puts(o, max, p, first ? "  Alias     : " : ", ");
            fb_puts(o, max, p, cmd_table[i].name);
            first = 0;
        }
    }
    if (!first) fb_putc(o, max, p, '\n');
    if (usage) {
        fb_puts(o, max, p, "  Uso       : ");
        fb_puts(o, max, p, usage);
        fb_putc(o, max, p, '\n');
    }
    fb_puts(o, max, p, "  Agentes   : ");
    fb_puts(o, max, p, (pr->flags & CMD_F_SAFE)
            ? "permitido (SOMA y Mayordomo pueden ejecutarlo)\n"
            : "solo usuario (bloqueado para agentes)\n");
}

/* ---- API publica para los agentes (Mayordomo / SOMA) ---- */
int shell_command_info(const char *name, uint32_t *flags_out, const char **help_out)
{
    const struct command_entry *e = help_find(name);
    if (!e) return 0;
    if (flags_out) *flags_out = e->flags;
    if (help_out) *help_out = e->help;
    return 1;
}

int shell_format_names(char *out, uint32_t max, int only_agent_safe)
{
    uint32_t p = 0;
    if (!out || !max) return 0;
    out[0] = '\0';
    for (int i = 0; i < cmd_table_count; ++i) {
        const struct command_entry *e = &cmd_table[i];
        if (e->flags & CMD_F_ALIAS) continue;
        if (only_agent_safe && !(e->flags & CMD_F_SAFE)) continue;
        if (p) fb_putc(out, max, &p, ' ');
        fb_puts(out, max, &p, e->name);
    }
    return (int)p;
}

int shell_format_catalog(char *out, uint32_t max, int only_agent_safe, int with_help, int tag_agent)
{
    uint32_t p = 0;
    if (!out || !max) return 0;
    out[0] = '\0';
    help_render(out, max, &p, with_help, only_agent_safe, tag_agent, -2);
    return (int)p;
}

static int cmd_help(const char *args, char *out_buf, uint32_t max_out)
{
    char q[48];
    uint32_t p = 0;
    int total = 0, aliases = 0;
    char *o = (char *)kmalloc(HELP_BUF);
    if (!o) { kprint("help: sin memoria.\n"); return 1; }
    o[0] = '\0';
    shell_next_arg(args ? args : "", q, sizeof(q));
    for (int i = 0; i < cmd_table_count; ++i) {
        if (cmd_table[i].flags & CMD_F_ALIAS) aliases++;
        else total++;
    }

    if (!q[0] || str_eq(q, "all") || str_eq(q, "-a") || str_eq(q, "todo") ||
        str_eq(q, "agentes")) {
        int all = q[0] != 0;
        int only_safe = str_eq(q, "agentes");
        fb_puts(o, HELP_BUF, &p, "Comandos disponibles en SOMA 0.2 (");
        fb_put_dec(o, HELP_BUF, &p, (uint64_t)total);
        fb_puts(o, HELP_BUF, &p, " comandos, ");
        fb_put_dec(o, HELP_BUF, &p, (uint64_t)aliases);
        fb_puts(o, HELP_BUF, &p, " alias)\n");
        if (!all) {
            fb_puts(o, HELP_BUF, &p, "Uso: help <categoria> | help <comando> | help <texto> | help all | help agentes\n");
        } else if (only_safe) {
            fb_puts(o, HELP_BUF, &p, "Comandos que SOMA y Mayordomo pueden ejecutar:\n");
        } else {
            fb_puts(o, HELP_BUF, &p, "[A] = utilizable por los agentes (SOMA / Mayordomo)\n");
        }
        help_render(o, HELP_BUF, &p, all, only_safe, all && !only_safe, -2);
    } else {
        const struct command_entry *e = help_find(q);
        int cat = -3;
        if (e) {
            help_detail(o, HELP_BUF, &p, e);
        } else {
            for (int c = 0; c < HELP_NCAT; ++c)
                if (help_cat_match(help_categories[c].title, q)) { cat = c; break; }
            if (cat >= 0) {
                help_render(o, HELP_BUF, &p, 1, 0, 1, cat);
            } else {
                uint32_t hits = 0;
                fb_puts(o, HELP_BUF, &p, "Resultados para '");
                fb_puts(o, HELP_BUF, &p, q);
                fb_puts(o, HELP_BUF, &p, "':\n");
                for (int i = 0; i < cmd_table_count; ++i) {
                    const struct command_entry *c = &cmd_table[i];
                    uint32_t nl;
                    if (c->flags & CMD_F_ALIAS) continue;
                    if (!find_substr_ci(c->name, q, 1) && !find_substr_ci(c->help, q, 1)) continue;
                    nl = shell_strlen(c->name);
                    fb_puts(o, HELP_BUF, &p, "  ");
                    fb_puts(o, HELP_BUF, &p, c->name);
                    for (uint32_t s = nl; s < 14; ++s) fb_putc(o, HELP_BUF, &p, ' ');
                    fb_puts(o, HELP_BUF, &p, " - ");
                    fb_puts(o, HELP_BUF, &p, c->help);
                    fb_putc(o, HELP_BUF, &p, '\n');
                    hits++;
                }
                if (!hits) fb_puts(o, HELP_BUF, &p, "  sin resultados. Escribe 'help' para ver las categorias.\n");
            }
        }
    }
    shell_emit(o, out_buf, max_out);
    kfree(o);
    return 1;
}


int shell_autocomplete(const char *prefix, uint32_t plen, const char *matches[], int max_matches)
{
    int count = 0;
    for (int i = 0; i < cmd_table_count; ++i) {
        const char *name = cmd_table[i].name;
        int match = 1;
        for (uint32_t j = 0; j < plen; ++j) {
            if (name[j] != prefix[j]) { match = 0; break; }
        }
        if (match && count < max_matches) {
            matches[count++] = name;
        }
    }
    return count;
}

/* PARCHE 046: limite de anidamiento (evita desbordar la pila de 32 KiB de los
 * hilos con 'a;b;c;...' o 'a&&b&&...'). El limite cuenta niveles de recursion. */
#define DISPATCH_MAX_DEPTH 8
static int dispatch_depth_fallback = 0;

static int dispatch_command_inner(const char *cmd_line, char *out_buf, uint32_t max_out);

int dispatch_command(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    struct tcb *self = thread_current();
    int *depth = self ? &self->dispatch_depth : &dispatch_depth_fallback;

    if (*depth >= DISPATCH_MAX_DEPTH) {
        console_muted = 0;
        kprint("\n[SEGURIDAD] Anidamiento de comandos excesivo (max 8): abortado.\n");
        if (out_buf && max_out) {
            uint32_t ep = 0;
            fb_puts(out_buf, max_out, &ep, "Error: anidamiento de comandos excesivo");
        }
        return 0;
    }

    (*depth)++;
    int r = dispatch_command_inner(cmd_line, out_buf, max_out);
    (*depth)--;
    return r;
}

static int dispatch_command_inner(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    static int redir_active = 0;
    int ret_val = 0;

    if (!cmd_line) return 0;
    while (*cmd_line == ' ') cmd_line++;
    if (*cmd_line == '\0') return 0;

    char *expanded_cmd = (char *)kmalloc(16384);
    char *redir_buf    = (char *)kmalloc(16384);
    char *pipe_buf     = (char *)kmalloc(16384);

    if (!expanded_cmd || !redir_buf || !pipe_buf) {
        if (expanded_cmd) kfree(expanded_cmd);
        if (redir_buf) kfree(redir_buf);
        if (pipe_buf) kfree(pipe_buf);
        kprint("shell: sin memoria en KHeap para despachador de comandos.\n");
        return 0;
    }

    /* 0. Interceptores Lógicos Condicionales ('&&' y '||') */
    int logic_and_idx = -1;
    int logic_or_idx = -1;
    char l_quote = 0;
    for (int i = 0; cmd_line[i] && cmd_line[i + 1]; ++i) {
        char c = cmd_line[i];
        if (c == '"' || c == '\'') {
            if (l_quote == 0) l_quote = c;
            else if (l_quote == c) l_quote = 0;
        } else if (!l_quote) {
            if (c == '&' && cmd_line[i + 1] == '&') {
                logic_and_idx = i;
                break;
            }
            if (c == '|' && cmd_line[i + 1] == '|') {
                logic_or_idx = i;
                break;
            }
        }
    }

    if (logic_and_idx >= 0) {
        char *left_cmd = (char *)kmalloc(4096);
        if (left_cmd) {
            int lp = 0;
            for (int i = 0; i < logic_and_idx && lp < 4095; ++i) left_cmd[lp++] = cmd_line[i];
            while (lp > 0 && left_cmd[lp - 1] == ' ') lp--;
            left_cmd[lp] = '\0';

            const char *right_cmd = cmd_line + logic_and_idx + 2;
            while (*right_cmd == ' ') right_cmd++;

            int ok = dispatch_command(left_cmd, out_buf, max_out);
            kfree(left_cmd);
            if (ok && *right_cmd != '\0') {
                ret_val = dispatch_command(right_cmd, out_buf, max_out);
            } else {
                ret_val = ok;
            }
        }
        goto cleanup;
    }

    if (logic_or_idx >= 0) {
        char *left_cmd = (char *)kmalloc(4096);
        if (left_cmd) {
            int lp = 0;
            for (int i = 0; i < logic_or_idx && lp < 4095; ++i) left_cmd[lp++] = cmd_line[i];
            while (lp > 0 && left_cmd[lp - 1] == ' ') lp--;
            left_cmd[lp] = '\0';

            const char *right_cmd = cmd_line + logic_or_idx + 2;
            while (*right_cmd == ' ') right_cmd++;

            int ok = dispatch_command(left_cmd, out_buf, max_out);
            kfree(left_cmd);
            if (!ok && *right_cmd != '\0') {
                ret_val = dispatch_command(right_cmd, out_buf, max_out);
            } else {
                ret_val = ok;
            }
        }
        goto cleanup;
    }

    /* 1. Interceptor de Comandos Encadenados (';') */
    int semi_idx = -1;
    char s_quote = 0;
    for (int i = 0; cmd_line[i]; ++i) {
        char c = cmd_line[i];
        if (c == '"' || c == '\'') {
            if (s_quote == 0) s_quote = c;
            else if (s_quote == c) s_quote = 0;
        } else if (!s_quote && c == ';') {
            semi_idx = i;
            break;
        }
    }
    if (semi_idx >= 0) {
        char *first_cmd = (char *)kmalloc(4096);
        if (first_cmd) {
            int f_p = 0;
            for (int i = 0; i < semi_idx && f_p < 4095; ++i) first_cmd[f_p++] = cmd_line[i];
            while (f_p > 0 && first_cmd[f_p - 1] == ' ') f_p--;
            first_cmd[f_p] = '\0';

            const char *next_cmd = cmd_line + semi_idx + 1;
            while (*next_cmd == ' ') next_cmd++;

            if (f_p > 0) {
                dispatch_command(first_cmd, out_buf, max_out);
            }
            kfree(first_cmd);
            if (*next_cmd != '\0') {
                ret_val = dispatch_command(next_cmd, out_buf, max_out);
            } else {
                ret_val = 1;
            }
        }
        goto cleanup;
    }

    /* 2. Expansión dinámica de variables de entorno ($VAR) en buffer de 16 KiB */
    env_expand(cmd_line, expanded_cmd, 16384);
    cmd_line = expanded_cmd;
    while (*cmd_line == ' ') cmd_line++;

    /* 3. Interceptor de Tuberías Bare-Metal ('|') */
    int pipe_idx = -1;
    char p_quote = 0;
    for (int i = 0; cmd_line[i]; ++i) {
        char c = cmd_line[i];
        if (c == '"' || c == '\'') {
            if (p_quote == 0) p_quote = c;
            else if (p_quote == c) p_quote = 0;
        } else if (!p_quote && c == '|') {
            pipe_idx = i;
            break;
        }
    }
    if (pipe_idx >= 0) {
        char *left_cmd = (char *)kmalloc(4096);
        char *right_cmd = (char *)kmalloc(4096);
        if (left_cmd && right_cmd) {
            int lp = 0, rp = 0;
            for (int i = 0; i < pipe_idx && lp < 4095; ++i) left_cmd[lp++] = cmd_line[i];
            while (lp > 0 && left_cmd[lp - 1] == ' ') lp--;
            left_cmd[lp] = '\0';

            const char *r_ptr = cmd_line + pipe_idx + 1;
            while (*r_ptr == ' ') r_ptr++;
            while (*r_ptr && rp < 4095) right_cmd[rp++] = *r_ptr++;
            right_cmd[rp] = '\0';

            if (lp > 0 && rp > 0) {
                pipe_buf[0] = '\0';
                console_muted = 1;
                int r_left = dispatch_command(left_cmd, pipe_buf, 16384);
                console_muted = 0;

                if (r_left) {
                    pipe_stdin_data = pipe_buf;
                    pipe_stdin_len = 0;
                    while (pipe_buf[pipe_stdin_len]) pipe_stdin_len++;

                    ret_val = dispatch_command(right_cmd, out_buf, max_out);
                    pipe_stdin_data = 0;
                    pipe_stdin_len = 0;
                }
            }
        }
        if (left_cmd) kfree(left_cmd);
        if (right_cmd) kfree(right_cmd);
        goto cleanup;
    }

    /* 4. Interceptor de Redirección Universal ('>') */
    if (!redir_active) {
        int gt_idx = -1;
        char active_quote = 0;
        for (int i = 0; cmd_line[i]; ++i) {
            char c = cmd_line[i];
            if (c == '"' || c == '\'') {
                if (active_quote == 0) active_quote = c;
                else if (active_quote == c) active_quote = 0;
            } else if (!active_quote && c == '>') {
                int prev_space = (i > 0 && cmd_line[i - 1] == ' ');
                int next_space = (cmd_line[i + 1] == ' ' || cmd_line[i + 1] == '/');
                if (prev_space && next_space) { gt_idx = i; break; }
            }
        }
        if (gt_idx >= 0) {
            char *sub_cmd = (char *)kmalloc(4096);
            char dst_file[64];
            int sp = 0, dp = 0;
            if (sub_cmd) {
                for (int i = 0; i < gt_idx && sp < 4095; ++i) sub_cmd[sp++] = cmd_line[i];
                while (sp > 0 && sub_cmd[sp - 1] == ' ') sp--;
                sub_cmd[sp] = '\0';
                const char *p = cmd_line + gt_idx + 1;
                while (*p == ' ') p++;
                while (*p && *p != ' ' && dp < 63) dst_file[dp++] = *p++;
                dst_file[dp] = '\0';

                if (sp > 0 && dp > 0) {
                    redir_active = 1; console_muted = 1; redir_buf[0] = '\0';
                    int res = dispatch_command(sub_cmd, redir_buf, 16384);
                    console_muted = 0; redir_active = 0;
                    if (res) {
                        uint32_t rlen = 0; while (redir_buf[rlen]) rlen++;
                        if (vfs_write(dst_file, redir_buf, rlen) < 0) {
                            kprint("Error: redireccion fallida hacia '"); kprint(dst_file);
                            kprint("' (acceso denegado o fallo de disco).\n");
                            uint32_t epos = 0;
                            fb_puts(out_buf, max_out, &epos, "Error: redireccion denegada o fallida");
                            ret_val = 1;
                        } else {
                            kprint("Salida redirigida con exito a '"); kprint(dst_file); kprint("' (");
                            kprint_dec(rlen); kprint(" bytes)\n");
                            uint32_t pos = 0;
                            fb_puts(out_buf, max_out, &pos, "Salida redirigida a ");
                            fb_puts(out_buf, max_out, &pos, dst_file);
                            ret_val = 1;
                        }
                    }
                }
                kfree(sub_cmd);
            }
            goto cleanup;
        }
    }

    /* 5. Extraer token del nombre del comando */
    char cmd_name[32];
    uint32_t ci = 0;
    const char *p = cmd_line;
    while (*p && *p != ' ' && ci < sizeof(cmd_name) - 1) {
        cmd_name[ci++] = *p++;
    }
    cmd_name[ci] = '\0';
    while (*p == ' ') p++;

    /* 6. Búsqueda canónica en la tabla declarativa */
    for (int i = 0; i < cmd_table_count; ++i) {
        if (str_eq(cmd_table[i].name, cmd_name)) {
            if (agent_dispatch_mode && !(cmd_table[i].flags & CMD_F_SAFE)) {
                kprint("\n[SEGURIDAD AGENTE] Comando bloqueado: '");
                kprint(cmd_table[i].name);
                kprint("' no esta autorizado para el Agente ReAct.\n");

                uint32_t denied_pos = 0;
                fb_puts(out_buf, max_out, &denied_pos,
                        "AGENT_DENIED: comando no autorizado");
                ret_val = 0;
                goto cleanup;
            }

            ret_val = cmd_table[i].handler(p, out_buf, max_out);
            goto cleanup;
        }
    }

    /* 7. Fallback para utilidades SRCFS */
    if (src_tool(cmd_line, out_buf, max_out)) {
        ret_val = 1;
        goto cleanup;
    }

cleanup:
    kfree(expanded_cmd);
    kfree(redir_buf);
    kfree(pipe_buf);
    return ret_val;
}

void shell_run(void)
{
    char cmd[512];

    for (;;) {
        if (agent_resume_pending == 1) {
            const char *pre = agent_resume_name[0] ? agent_resume_name : "soma";
            uint32_t ci = 0;
            while (*pre) cmd[ci++] = *pre++;
            cmd[ci++] = ' ';
            for (uint32_t k = 0; agent_resume_mission[k] && ci < sizeof(cmd) - 1; ++k) cmd[ci++] = agent_resume_mission[k];
            cmd[ci] = '\0';
            kprint("\033[1;36msoma\033[1;32m>\033[0m "); kprint(cmd); kprint(" [reanudacion automatica]\n");
        } else {
            kprint("\033[1;36msoma\033[1;32m>\033[0m ");
            kgetline(cmd, sizeof(cmd));
        }

        char *s0 = cmd; while (*s0 == ' ') s0++;
        if (*s0 == '\0') continue;

        static char shell_buf[2048];
        if (!dispatch_command(s0, shell_buf, sizeof(shell_buf))) {
            kprint("Comando desconocido: '"); kprint(s0); kprint("'. Escribe 'help'.\n");
        }
    }
}
