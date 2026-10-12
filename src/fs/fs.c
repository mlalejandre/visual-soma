#include "image.h"
#include "mutex.h"
#include <stdint.h>
#include "console.h"
#include "fs.h"
#include "mem.h"
#include "virtio_blk.h"
#include "sysinfo.h"
#include "pmm.h"
#include "vmm.h"
#include "thread.h"
#include "rtc.h"
#include "net.h"
#include "io.h"
#include "arch.h"
#include "idt.h"
#include "virtio_net.h"
#include "disk_layout.h"

#define FS_SUPER_LBA         SOMA_LBA_FS_SUPER
#define FS_DATA_LBA          SOMA_LBA_FS_DATA
#define FS_SECTORS_PER_FILE  33
#define FS_DATA_SECTORS      32
#define FS_CHECKPOINT_LBA    SOMA_LBA_CHECKPOINT
#define FS_TOTAL_SECTORS     (1 + FS_MAX_FILES * FS_SECTORS_PER_FILE)

struct fs_superblock {
    char     magic[8];       /* "SOMAFS02" */
    uint32_t version;
    uint32_t num_files;
    uint32_t max_files;
    uint32_t max_file_size;
    uint8_t  pad[488];
} __attribute__((aligned(64)));

struct fs_disk_header {
    char     name[FS_NAME_MAX]; /* 48 B */
    uint32_t size;              /* 4 B */
    uint8_t  used;              /* 1 B */
    uint8_t  pad[11];           /* 11 B -> 64 B */
    uint8_t  sec_pad[448];      /* Total 512 B (Sector 0 del inodo) */
} __attribute__((aligned(64)));

static struct vfs_file files[FS_MAX_FILES] __attribute__((aligned(8)));
static struct kmutex vfs_mutex __attribute__((aligned(16)));
static int fs_initialized = 0;

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static int vfs_names_equal(const char *stored, const char *query)
{
    if (!stored || !query) return 0;
    if (str_eq(stored, query)) return 1;
    if (stored[0] == '/' && str_eq(stored + 1, query)) return 1;
    if (query[0] == '/' && str_eq(stored, query + 1)) return 1;
    return 0;
}

static const char *fs_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

static uint32_t str_len(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static void str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    if (!out || max == 0 || !pos || *pos >= max - 1) return;
    while (*s && *pos + 1 < max) out[(*pos)++] = *s++;
    out[*pos] = '\0';
}

static void fb_put_dec(char *out, uint32_t max, uint32_t *pos, uint64_t v)
{
    char tmp[24]; int n = 0;
    if (v == 0) { if (*pos + 1 < max) { out[(*pos)++] = '0'; out[*pos] = '\0'; } return; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && *pos + 1 < max) out[(*pos)++] = tmp[--n];
    out[*pos] = '\0';
}

static int is_ephemeral_path(const char *path)
{
    return (fs_strstr(path, "/sys/") != 0 || fs_strstr(path, "/tmp/") != 0);
}

static int is_proc_path(const char *path)
{
    return (path[0] == '/' && path[1] == 'p' && path[2] == 'r' && path[3] == 'o' && path[4] == 'c' && (path[5] == '/' || path[5] == '\0'));
}

static int is_dev_path(const char *path)
{
    return (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && (path[4] == '/' || path[4] == '\0'));
}

int vfs_is_hidden(const char *name)
{
    if (!name) return 0;
    const char *p = name;
    while (*p) {
        if (*p == '/' && *(p + 1) == '.') return 1;
        p++;
    }
    if (name[0] == '.') return 1;

    if (fs_strstr(name, "/agent/state") != 0) return 1;
    if (fs_strstr(name, "_stm.txt") != 0) return 1;
    if (fs_strstr(name, "/sys/") != 0) return 1;
    if (fs_strstr(name, "/tmp/") != 0) return 1;
    if (fs_strstr(name, "manifest") != 0) return 1;

    return 0;
}

static int agent_path_denied(const char *name)
{
    static const char *const deny_exact[] = {
        "/etc/init.sh", "/etc/soma_manifest.txt", "/etc/history.txt", 0
    };
    struct tcb *t = thread_current();
    if (!t || !t->agent_mode || !name) return 0;

    if (str_eq(name, "/agent/state") || 
        str_eq(name, "/agent/blackboard.txt") ||
        fs_strstr(name, "_stm.txt") != 0) {
        return 0;
    }

    int deny = (fs_strstr(name, "/agent/") == name);
    for (int i = 0; !deny && deny_exact[i]; ++i) {
        if (str_eq(name, deny_exact[i])) deny = 1;
    }
    if (deny) {
        kprint("\n[SEGURIDAD AGENTE] Escritura denegada en '");
        kprint(name);
        kprint("' (ruta de sistema protegida).\n");
    }
    return deny;
}

static uint64_t rng_state = 0x88888888ULL;
static uint64_t xorshift64(void)
{
    if (rng_state == 0) rng_state = arch_cycle_counter();
    uint64_t x = rng_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return rng_state = x;
}

static int read_proc_file(const char *name, char *buf_out, uint32_t max_len)
{
    uint32_t pos = 0;
    if (str_eq(name, "/proc/version")) {
        fb_puts(buf_out, max_len, &pos, "SOMA version 0.2-release (" MYOS_ARCH_NAME ") gcc-freestanding #1 Oct 2026\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/uptime")) {
        uint64_t ms = timer_get_uptime_ms();
        fb_put_dec(buf_out, max_len, &pos, ms / 1000ULL);
        fb_puts(buf_out, max_len, &pos, ".");
        uint32_t frac = (uint32_t)(ms % 1000ULL);
        if (frac < 100) fb_puts(buf_out, max_len, &pos, "0");
        if (frac < 10)  fb_puts(buf_out, max_len, &pos, "0");
        fb_put_dec(buf_out, max_len, &pos, frac);
        fb_puts(buf_out, max_len, &pos, " seconds\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/meminfo")) {
        size_t f_free = 0, f_used = 0, f_total = 0;
        pmm_get_stats(&f_free, &f_used, &f_total);
        size_t h_used = 0, h_free = 0;
        kheap_stats(&h_used, &h_free);
        fb_puts(buf_out, max_len, &pos, "MemTotal:       "); fb_put_dec(buf_out, max_len, &pos, (f_total * 4096ULL) / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\nMemFree:        "); fb_put_dec(buf_out, max_len, &pos, (f_free * 4096ULL) / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\nHeapFree:       "); fb_put_dec(buf_out, max_len, &pos, h_free / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\nHeapUsed:       "); fb_put_dec(buf_out, max_len, &pos, h_used / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/threads")) {
        return thread_format_table(buf_out, max_len);
    }
    if (str_eq(name, "/proc/net/dev")) {
        uint32_t tx_p = 0, rx_p = 0; uint64_t tx_b = 0, rx_b = 0;
        virtio_net_stats(&tx_p, &rx_p, &tx_b, &rx_b);
        fb_puts(buf_out, max_len, &pos, "Interface eth0: rx_bytes="); fb_put_dec(buf_out, max_len, &pos, rx_b);
        fb_puts(buf_out, max_len, &pos, " tx_bytes="); fb_put_dec(buf_out, max_len, &pos, tx_b);
        fb_puts(buf_out, max_len, &pos, "\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/net/arp")) {
        return arp_format_cache(buf_out, max_len);
    }
    return -1;
}

static int read_dev_file(const char *name, char *buf_out, uint32_t max_len)
{
    if (str_eq(name, "/dev/null")) { buf_out[0] = '\0'; return 0; }
    if (str_eq(name, "/dev/zero")) {
        uint32_t n = max_len > 1 ? max_len - 1 : 0;
        for (uint32_t i = 0; i < n; ++i) buf_out[i] = '\0';
        buf_out[n] = '\0'; return (int)n;
    }
    if (str_eq(name, "/dev/urandom")) {
        uint32_t n = 64; if (n >= max_len) n = max_len - 1;
        const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        for (uint32_t i = 0; i < n; ++i) buf_out[i] = charset[xorshift64() % (sizeof(charset) - 1)];
        buf_out[n] = '\0'; return (int)n;
    }
    if (str_eq(name, "/dev/rtc")) {
        struct rtc_time t;
        rtc_get_datetime(&t);
        uint32_t pos = 0;
        fb_puts(buf_out, max_len, &pos, "RTC: ");
        fb_put_dec(buf_out, max_len, &pos, t.year);
        fb_puts(buf_out, max_len, &pos, "-");
        if (t.month < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.month);
        fb_puts(buf_out, max_len, &pos, "-");
        if (t.day < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.day);
        fb_puts(buf_out, max_len, &pos, " ");
        if (t.hour < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.hour);
        fb_puts(buf_out, max_len, &pos, ":");
        if (t.min < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.min);
        fb_puts(buf_out, max_len, &pos, ":");
        if (t.sec < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.sec);
        fb_puts(buf_out, max_len, &pos, " UTC\n");
        return (int)pos;
    }
    return -1;
}

static int sync_file_to_disk(int i)
{
    uint8_t sec_buf[512] __attribute__((aligned(16)));
    struct fs_disk_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    memcpy(hdr.name, files[i].name, FS_NAME_MAX);
    hdr.size = files[i].size;
    hdr.used = files[i].used;

    uint64_t start_lba = FS_DATA_LBA + (uint64_t)i * FS_SECTORS_PER_FILE;

    /* 1. Escribir sector 0 del inodo (Cabecera) */
    memcpy(sec_buf, &hdr, sizeof(hdr));
    if (virtio_blk_write(start_lba, sec_buf) != 0) return -1;

    /* 2. Escribir los 32 sectores de datos (16 KiB) */
    if (files[i].used && files[i].data) {
        const uint8_t *raw = (const uint8_t *)files[i].data;
        for (int s = 0; s < FS_DATA_SECTORS; ++s) {
            memset(sec_buf, 0, sizeof(sec_buf));
            uint32_t off = (uint32_t)s * 512;
            if (off < files[i].size) {
                uint32_t take = files[i].size - off;
                if (take > 512) take = 512;
                memcpy(sec_buf, raw + off, take);
            }
            if (virtio_blk_write(start_lba + 1 + s, sec_buf) != 0) return -1;
        }
    }

    files[i].dirty = 0;
    return 0;
}

static int load_file_from_disk(int i)
{
    uint8_t sec_buf[512] __attribute__((aligned(16)));
    struct fs_disk_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    uint64_t start_lba = FS_DATA_LBA + (uint64_t)i * FS_SECTORS_PER_FILE;

    /* 1. Leer cabecera en sector 0 */
    if (virtio_blk_read(start_lba, sec_buf) != 0) return -1;
    memcpy(&hdr, sec_buf, sizeof(hdr));

    if (hdr.used > 1 || hdr.size >= FS_DATA_MAX) return -1;
    if (hdr.used) {
        int name_term = 0;
        for (uint32_t n = 0; n < FS_NAME_MAX; ++n) {
            if (hdr.name[n] == '\0') { name_term = 1; break; }
        }
        if (!name_term || hdr.name[0] == '\0') return -1;
    }

    memcpy(files[i].name, hdr.name, FS_NAME_MAX);
    files[i].size = hdr.size;
    files[i].used = hdr.used;
    files[i].dirty = 0;

    if (hdr.used) {
        if (!files[i].data) {
            files[i].data = (char *)kmalloc(FS_DATA_MAX);
            if (!files[i].data) return -1;
        }
        memset(files[i].data, 0, FS_DATA_MAX);

        uint8_t *raw = (uint8_t *)files[i].data;
        for (int s = 0; s < FS_DATA_SECTORS; ++s) {
            if (virtio_blk_read(start_lba + 1 + s, sec_buf) != 0) return -1;
            uint32_t off = (uint32_t)s * 512;
            if (off < hdr.size) {
                uint32_t take = hdr.size - off;
                if (take > 512) take = 512;
                memcpy(raw + off, sec_buf, take);
            }
        }
        files[i].data[hdr.size] = '\0';
    } else {
        if (files[i].data) {
            kfree(files[i].data);
            files[i].data = 0;
        }
    }

    return 0;
}

static int vfs_sync_unlocked(void)
{
    struct fs_superblock sb;
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, "SOMAFS02", 8);
    sb.version = 2;
    sb.max_files = FS_MAX_FILES;
    sb.max_file_size = FS_DATA_MAX;

    uint32_t used_count = 0;
    int any_dirty = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            if (files[i].size >= FS_DATA_MAX) return -1;
            used_count++;
        }
        if (files[i].dirty) {
            any_dirty = 1;
            if (sync_file_to_disk(i) != 0) return -1;
        }
    }
    sb.num_files = used_count;
    if (any_dirty) {
        if (virtio_blk_write(FS_SUPER_LBA, &sb) != 0) return -1;
    }
    return 0;
}

static int vfs_create_internal(const char *name, const char *initial_data, uint32_t len)
{
    if (!name || name[0] == '\0') return -1;
    if (len > FS_DATA_MAX - 1) return -2;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && str_eq(files[i].name, name)) {
            if (!files[i].data) {
                files[i].data = (char *)kmalloc(FS_DATA_MAX);
                if (!files[i].data) return -1;
            }
            if (initial_data && len > 0) memcpy(files[i].data, initial_data, len);
            files[i].data[len] = '\0';
            files[i].size = len;
            files[i].dirty = !is_ephemeral_path(name);
            return 0;
        }
    }
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used) {
            if (!files[i].data) {
                files[i].data = (char *)kmalloc(FS_DATA_MAX);
                if (!files[i].data) return -1;
            }
            files[i].used = 1;
            files[i].dirty = !is_ephemeral_path(name);
            str_copy(files[i].name, name, FS_NAME_MAX);
            files[i].size = 0;
            if (initial_data && len > 0) {
                memcpy(files[i].data, initial_data, len);
                files[i].data[len] = '\0';
                files[i].size = len;
            } else {
                files[i].data[0] = '\0';
            }
            return 0;
        }
    }
    return -1;
}

static void vfs_format_unlocked(void)
{
    static uint8_t zero_buf[512] __attribute__((aligned(16)));
    memset(zero_buf, 0, sizeof(zero_buf));

    uint32_t fs_total_sectors = 1 + FS_MAX_FILES * FS_SECTORS_PER_FILE;
    for (uint32_t s = 0; s < fs_total_sectors; ++s) {
        virtio_blk_write(FS_SUPER_LBA + s, zero_buf);
    }
    virtio_blk_write(FS_CHECKPOINT_LBA - 1, zero_buf);

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].dirty = 0;
        files[i].name[0] = '\0';
        if (files[i].data) {
            kfree(files[i].data);
            files[i].data = 0;
        }
    }

    /* 1. Archivos Base de Sistema */
    vfs_create_internal("/etc/hostname", "soma-node1\n", 11);
    vfs_create_internal("/etc/os-release", "NAME=SOMA\nVERSION=0.2-release\nARCH=" MYOS_ARCH_NAME "\nSTORAGE=virtio-blk-v2 (64 inodos / 16 KiB)\nDESCRIPTION=Sistema Operativo Multi-Agente\n", 137);
    vfs_create_internal("/sys/status.txt", "KERNEL: SOMA " MYOS_ARCH_NAME " | VIRTIO: OK | NETWORK: 10.0.2.15 | STORAGE: PERSISTENT V2\n", 79);

    const char *init_code = "#!/bin/soma\n# SOMA Init Script\nexport MOTD=\"SOMA Bare-Metal Core Activo\"\necho \"$MOTD ($HOST en $ARCH)\"\n";
    vfs_create_internal("/etc/init.sh", init_code, str_len(init_code));
    vfs_create_internal("/etc/soma_manifest.txt", "Identity: SOMA AI Kernel (Nail-35b)\nRole: Multi-Agent System Core Intelligence\nStatus: Fully Operational (VFS V2 Active)\n", 123);
    vfs_create_internal("/etc/mem_user.txt", "Usuario: Marcos | Rol: Administrador y Arquitecto Principal del Sistema SOMA | Preferencias: Respuestas tecnicas concisas, directas y veraces.\n", 143);
    vfs_create_internal("/etc/mem_hw.txt", "CPU: 64-bit Ring 0 | RAM: 512 MiB | IP: 10.0.2.15 | Almacenamiento: virtio-blk (SOMAFS02: 64 inodos / 16 KiB)\n", 110);

    /* 2. Directorio de Usuario */
    vfs_create_internal("/mis_archivos/bienvenida.txt", "Carpeta personal del usuario Marcos.\nEspacio destinado a documentos, proyectos y notas personales.\n", 101);

    /* 3. Espacio de Trabajo para Agentes (/workspace/) */
    
    /* Recursos visuales y multimedia para el navegador web */
    static uint8_t demo_bmp[10240];
    int demo_bmp_sz = image_create_demo_bmp(demo_bmp, sizeof(demo_bmp), 80, 40);
    if (demo_bmp_sz > 0) {
        vfs_create_internal("/workspace/logo_soma.bmp", (const char *)demo_bmp, (uint32_t)demo_bmp_sz);
    }

    const char *demo_html = "<!DOCTYPE html><html><body>"
                                "<h1>SOMA OS - Red Multimedia Activa</h1>"
                                "<p>Demostracion de renderizado HTML con soporte de imagenes locales y de RED.</p>"
                                "<hr>"
                                "<p>1. Imagen BMP cargada desde disco (RamFS local):</p>"
                                "<img src=\"/workspace/logo_soma.bmp\" width=\"120\" height=\"60\">"
                                "<p>2. Imagen BMP descargada nativamente por HTTP (TCP/IP):</p>"
                                "<img src=\"http://127.0.0.1/workspace/logo_soma.bmp\" width=\"120\" height=\"60\">"
                                "<p>El navegador ha asignado un buffer dinamico, forjado la peticion HTTP GET, recibido la respuesta binaria y decodificado la imagen al vuelo.</p>"
                                "<hr>"
                                "<a href=\"http://10.0.2.15/\">Ver Dashboard (Telemetria)</a>"
                                "</body></html>";
    vfs_create_internal("/workspace/demo_web.html", demo_html, (uint32_t)str_len(demo_html));

    vfs_create_internal("/workspace/README.txt", "Espacio de trabajo preferente para la creacion de archivos y tareas de agentes.\n", 80);
    vfs_create_internal("/workspace/soma.txt", "[Workspace SOMA] Tareas, diagnosticos y scripts activos.\n", 57);
    vfs_create_internal("/workspace/buscador.txt", "[Workspace Buscador] Sintesis de investigacion y recopilacion web.\n", 67);

    /* 4. Coordinación y Memoria de Agentes */
    vfs_create_internal("/agent/soma.txt", "Agente SOMA Core: Especialista en gestion del kernel bare-metal, administracion de procesos, diagnósticos y seguridad.\n", 119);
    vfs_create_internal("/agent/buscador.txt", "Agente Buscador: Especialista en exploracion web, sintesis de informacion, noticias y busquedas en el arbol de memoria.\n", 121);
    vfs_create_internal("/agent/blackboard.txt", "[Pizarra Compartida Multi-Agente]\nEstado: Inicializado.\n", 57);

    /* 5. Árbol de Conocimiento Semántico (/mem/) */
    vfs_create_internal("/mem/indice.txt", "/mem/ (Arbol de Memoria Estructurada)\n+-- ciencia/\n|   +-- ciencia_fisica.txt\n|   +-- ciencia_computacion.txt\n+-- historia/\n|   +-- historia_antigua.txt\n|   +-- historia_moderna.txt\n+-- conversaciones/\n    +-- conversaciones.txt\n", 226);
    vfs_create_internal("/mem/ciencia_fisica.txt", "Fisica: Principios de mecanica clasica, termodinamica, relatividad y fisica cuantica.\n", 86);
    vfs_create_internal("/mem/ciencia_computacion.txt", "Computacion: Arquitectura x86_64, paginacion de 4 niveles, VirtIO, pilas de red bare-metal y sistemas multi-agente.\n", 118);
    vfs_create_internal("/mem/historia_antigua.txt", "Historia Antigua: Civilizaciones de Mesopotamia, Egipto, Grecia clasica y el Imperio Romano.\n", 94);
    vfs_create_internal("/mem/historia_moderna.txt", "Historia Moderna: Renacimiento, Ilustracion, Revolucion Industrial, Era Digital e Inteligencia Artificial.\n", 108);
    vfs_create_internal("/mem/conversaciones.txt", "Registro historico de temas clave tratados entre el usuario y los agentes del sistema.\n", 87);
    vfs_create_internal("/src/kernel.c", "/* SOMA Kernel Entry */\nvoid kernel_main(void) {\n  virtio_blk_init();\n  vfs_init();\n  shell_run();\n}\n", 94);

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) files[i].dirty = 1;
    }
    vfs_sync_unlocked();
}


static void vfs_ensure_demo_files(void)
{
    static const char demo_html[] =
        "<!DOCTYPE html><html><body>"
        "<h1 style=\"color: #38bdf8;\">SOMA OS - Motor Visual HTML & CSS</h1>"
        "<p style=\"color: #94a3b8;\">Renderizado grafico bare-metal acelerado en Ring 0</p>"
        "<hr><br>"
        "<p style=\"background-color: #1e293b; color: #f8fafc;\">1. Logotipo Oficial SOMA (BMP 24bpp TrueColor):</p>"
        "<img src=\"/workspace/logo_soma.bmp\" width=\"160\" height=\"80\">"
        "<br>"
        "<p style=\"background-color: #0f172a; color: #34d399;\">2. Microchip de Control (Grafico Bare-Metal):</p>"
        "<img src=\"/workspace/chip_soma.bmp\" width=\"128\" height=\"64\">"
        "<br><hr>"
        "<a href=\"http://10.0.2.15/\" style=\"background-color: #0284c7; color: #ffffff;\">[ VER TELEMETRIA DEL SISTEMA ]</a>"
        "</body></html>";

    /* AGENTE_MAYORDOMO_PATCH_V1: perfiles persistentes sin borrar la STM previa. */
    char mayordomo_profile_probe[2];
    if (vfs_read("/agent/mayordomo.txt", mayordomo_profile_probe, sizeof(mayordomo_profile_probe)) < 0) {
        static const char mayordomo_profile[] = "Mayordomo: operaciones nativas y respuestas directas para RAM, disco, VFS y archivos.\n";
        vfs_write("/agent/mayordomo.txt", mayordomo_profile, sizeof(mayordomo_profile) - 1);
    }
    if (vfs_read("/agent/mayordomo_stm.txt", mayordomo_profile_probe, sizeof(mayordomo_profile_probe)) < 0) {
        vfs_write("/agent/mayordomo_stm.txt", "", 0);
    }

    vfs_write("/workspace/demo_web.html", demo_html, sizeof(demo_html) - 1);

    static uint8_t bmp1[10240];
    int sz1 = image_create_demo_bmp(bmp1, sizeof(bmp1), 80, 40);
    if (sz1 > 0) {
        vfs_write("/workspace/logo_soma.bmp", (const char *)bmp1, (uint32_t)sz1);
    }

    static uint8_t bmp2[10240];
    int sz2 = image_create_chip_bmp(bmp2, sizeof(bmp2), 64, 32);
    if (sz2 > 0) {
        vfs_write("/workspace/chip_soma.bmp", (const char *)bmp2, (uint32_t)sz2);
    }
}

void vfs_init(void)
{
    if (fs_initialized) return;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].dirty = 0;
        files[i].name[0] = '\0';
        files[i].data = 0;
    }

    fs_initialized = 1;
    kmutex_init(&vfs_mutex, "vfs_lock");

    struct fs_superblock sb;
    if (virtio_blk_read(FS_SUPER_LBA, &sb) == 0 &&
        memcmp(sb.magic, "SOMAFS02", 8) == 0 &&
        sb.version == 2 &&
        sb.max_files == FS_MAX_FILES &&
        sb.num_files <= FS_MAX_FILES) {
        kprint("VFS: Sistema de archivos persistente detectado (SOMAFS02 - 64 inodos / 16 KiB)\n");
        int loaded = 0;
        for (int i = 0; i < FS_MAX_FILES; ++i) {
            int r = load_file_from_disk(i);
            if (r == 0 && files[i].used) loaded++;
        }
        kprint("VFS: "); kprint_dec((uint32_t)loaded); kprint(" archivos cargados desde virtio-blk (KHeap dinamico).\n");
        kprint("VFS: Directorios '/workspace/' y '/mis_archivos/' activos.\n");

        char chk_init[16];
        if (vfs_read("/etc/init.sh", chk_init, sizeof(chk_init)) <= 0) {
            const char *init_code = "#!/bin/soma\n# SOMA Init Script\nexport MOTD=\"SOMA Bare-Metal Core Activo\"\necho \"$MOTD ($HOST en $ARCH)\"\n";
            vfs_write("/etc/init.sh", init_code, str_len(init_code));
        }
        vfs_ensure_demo_files();
        return;
    }

    kprint("VFS: Formateando sistema de archivos de alta capacidad SOMAFS02 (64 inodos / 16 KiB)...\n");
    vfs_format();
    vfs_ensure_demo_files();
}

static int vfs_create_unlocked(const char *name, const char *initial_data, uint32_t len)
{
    if (!fs_initialized) vfs_init();
    int r = vfs_create_internal(name, initial_data, len);
    if (r == 0 && !is_ephemeral_path(name) && vfs_sync_unlocked() != 0) {
        return -1;
    }
    return r;
}

/* SOMA_PARCHE_LIVE_V1: hook opcional (lo registra el GUI). */
void (*vfs_live_hook)(const char *name, const char *data, uint32_t len) = 0;

static int vfs_write_core(const char *name, const char *data, uint32_t len);

int vfs_write(const char *name, const char *data, uint32_t len)
{
    int r = vfs_write_core(name, data, len);
    if (r >= 0 && vfs_live_hook) vfs_live_hook(name, data, len);
    return r;
}

static int vfs_write_core(const char *name, const char *data, uint32_t len)
{
    if (!fs_initialized) vfs_init();
    if (!name) return -1;
    if (is_proc_path(name)) return -1;
    if (agent_path_denied(name)) return -1;
    if (str_eq(name, "/dev/null") || str_eq(name, "/dev/zero")) return (int)len;

    kmutex_lock(&vfs_mutex);
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            if (len > FS_DATA_MAX - 1) { kmutex_unlock(&vfs_mutex); return -2; }
            if (!files[i].data) {
                files[i].data = (char *)kmalloc(FS_DATA_MAX);
                if (!files[i].data) { kmutex_unlock(&vfs_mutex); return -1; }
            }
            if (data && len > 0) memcpy(files[i].data, data, len);
            files[i].data[len] = '\0';
            files[i].size = len;
            if (!is_ephemeral_path(name)) {
                files[i].dirty = 1;
                if (vfs_sync_unlocked() != 0) { kmutex_unlock(&vfs_mutex); return -1; }
            }
            kmutex_unlock(&vfs_mutex);
            return (int)len;
        }
    }
    int r = vfs_create_unlocked(name, data, len);
    kmutex_unlock(&vfs_mutex);
    return r == 0 ? (int)len : -1;
}

static int vfs_read_unlocked(const char *name, char *buf_out, uint32_t max_len)
{
    if (!fs_initialized) vfs_init();
    if (!name || !buf_out || max_len == 0) return -1;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            if (!files[i].data) return -1;
            uint32_t to_copy = files[i].size;
            if (to_copy >= max_len) to_copy = max_len - 1;
            memcpy(buf_out, files[i].data, to_copy);
            buf_out[to_copy] = '\0';
            return (int)to_copy;
        }
    }
    return -1;
}

int vfs_read(const char *name, char *buf_out, uint32_t max_len)
{
    if (!fs_initialized) vfs_init();
    if (!name || !buf_out || max_len == 0) return -1;

    if (is_proc_path(name)) return read_proc_file(name, buf_out, max_len);
    if (is_dev_path(name))  return read_dev_file(name, buf_out, max_len);

    kmutex_lock(&vfs_mutex);
    int r = vfs_read_unlocked(name, buf_out, max_len);
    kmutex_unlock(&vfs_mutex);
    return r;
}

int vfs_delete(const char *name)
{
    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();
    if (is_proc_path(name) || is_dev_path(name)) { kmutex_unlock(&vfs_mutex); return -1; }
    if (agent_path_denied(name)) { kmutex_unlock(&vfs_mutex); return -1; }

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            files[i].used = 0;
            files[i].size = 0;
            files[i].name[0] = '\0';
            files[i].dirty = 1;
            if (files[i].data) {
                kfree(files[i].data);
                files[i].data = 0;
            }
            int r = vfs_sync_unlocked();
            kmutex_unlock(&vfs_mutex);
            return r != 0 ? -1 : 0;
        }
    }
    kmutex_unlock(&vfs_mutex);
    return -1;
}

void vfs_list(void)
{
    vfs_list_ext(0, 0);
}

void vfs_list_ext(int show_all, const char *filter_dir)
{
    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();

    kprint("\nARCHIVOS EN RamFS (");
    if (filter_dir && *filter_dir) {
        kprint(filter_dir);
    } else {
        kprint("/");
    }
    if (show_all) kprint(" - Modo Todo/Sistema");
    kprint("):\n--------------------------------------------------\n");

    int count = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            const char *fn = files[i].name;

            if (filter_dir && *filter_dir && !str_eq(filter_dir, "/")) {
                if (fs_strstr(fn, filter_dir) != fn) continue;
            }

            int hidden = vfs_is_hidden(fn);
            if (hidden && !show_all) continue;

            kprint("  ");
            kprint(fn);
            if (hidden) kprint(" [OCULTO]");
            uint32_t nl = str_len(fn) + (hidden ? 9 : 0);
            for (uint32_t s = nl; s < 36; ++s) kputc(' ');
            kprint_dec(files[i].size); kprint(" bytes\n");
            count++;
        }
    }

    if (show_all) {
        kprint("\nPSEUDO-FILESYSTEM (Memoria Virtual):\n");
        kprint("  /proc/version, /proc/uptime, /proc/meminfo, /proc/threads\n");
        kprint("  /dev/null, /dev/zero, /dev/urandom, /dev/rtc\n");
    }
    kprint("--------------------------------------------------\n");
    kprint("Total: "); kprint_dec((uint32_t)count); kprint(" archivos visibles (Capacidad: 64 inodos / 16 KiB por archivo).\n\n");
    kmutex_unlock(&vfs_mutex);
}

int vfs_format_list(char *out_buf, uint32_t max)
{
    return vfs_format_list_ext(0, 0, out_buf, max);
}

int vfs_format_list_ext(int show_all, const char *filter_dir, char *out_buf, uint32_t max)
{
    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();
    uint32_t pos = 0;
    const char *hdr = "Archivos: ";
    while (*hdr && pos < max - 1) out_buf[pos++] = *hdr++;

    int count = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            const char *n = files[i].name;
            if (filter_dir && *filter_dir && !str_eq(filter_dir, "/")) {
                if (fs_strstr(n, filter_dir) != n) continue;
            }
            if (vfs_is_hidden(n) && !show_all) continue;

            if (count > 0 && pos < max - 2) { out_buf[pos++] = ','; out_buf[pos++] = ' '; }
            while (*n && pos < max - 1) out_buf[pos++] = *n++;
            count++;
        }
    }
    out_buf[pos] = '\0';
    kmutex_unlock(&vfs_mutex);
    return (int)pos;
}

void vfs_get_stats(uint32_t *files_used, uint32_t *bytes_used, uint32_t *dirty_count)
{
    kmutex_lock(&vfs_mutex);
    uint32_t f = 0, b = 0, d = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            f++;
            b += files[i].size;
            if (files[i].dirty) d++;
        }
    }
    if (files_used) *files_used = f;
    if (bytes_used) *bytes_used = b;
    if (dirty_count) *dirty_count = d;
    kmutex_unlock(&vfs_mutex);
}

/* ====================================================================
 * MOTOR DE BÚSQUEDA SEMÁNTICA Y PONDERADA BM25 / TF-IDF BARE-METAL
 * ==================================================================== */

static char normalize_char_utf8(const char **p)
{
    unsigned char c = (unsigned char)**p;
    if (c == 0) return 0;
    (*p)++;

    if (c == 0xC3 && **p) {
        unsigned char c2 = (unsigned char)**p;
        (*p)++;
        switch (c2) {
            case 0xA1: case 0x81: return 'a'; /* á, Á */
            case 0xA9: case 0x89: return 'e'; /* é, É */
            case 0xAD: case 0x8D: return 'i'; /* í, Í */
            case 0xB3: case 0x93: return 'o'; /* ó, Ó */
            case 0xBA: case 0x9A: return 'u'; /* ú, Ú */
            case 0xBC: case 0x9C: return 'u'; /* ü, Ü */
            case 0xB1: case 0x91: return 'n'; /* ñ, Ñ */
            default: return '?';
        }
    }

    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return (char)c;
    return ' ';
}

static int is_bm25_stopword(const char *w, uint32_t len)
{
    static const char *const stops[] = {
        "el", "la", "los", "las", "un", "una", "unos", "unas", "de", "del",
        "en", "con", "por", "para", "que", "se", "su", "sus", "es", "son",
        "fue", "era", "ha", "han", "este", "esta", "estos", "estas", "como",
        "cuando", "donde", "quien", "cual", "cuales", "sobre", "entre", "sin",
        "the", "of", "and", "in", "to", "for", "is", "on", "that", "by", "with", 0
    };

    if (len < 2) return 1;
    for (int i = 0; stops[i]; ++i) {
        uint32_t slen = str_len(stops[i]);
        if (slen == len) {
            int match = 1;
            for (uint32_t k = 0; k < len; ++k) {
                if (w[k] != stops[i][k]) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

#define MAX_BM25_TERMS 8
#define MAX_TERM_LEN   32

struct bm25_query {
    char terms[MAX_BM25_TERMS][MAX_TERM_LEN];
    uint32_t term_lens[MAX_BM25_TERMS];
    uint32_t term_count;
};

static void parse_bm25_query(const char *query, struct bm25_query *q)
{
    q->term_count = 0;
    const char *p = query;

    while (*p && q->term_count < MAX_BM25_TERMS) {
        while (*p && (unsigned char)*p <= 32) p++;
        if (!*p) break;

        char term[MAX_TERM_LEN];
        uint32_t tlen = 0;

        while (*p && tlen < MAX_TERM_LEN - 1) {
            const char *prev = p;
            char nc = normalize_char_utf8(&p);
            if (nc == ' ' || nc == 0) {
                if (nc == 0) p = prev + 1;
                break;
            }
            term[tlen++] = nc;
        }
        term[tlen] = '\0';

        if (tlen >= 2 && !is_bm25_stopword(term, tlen)) {
            memcpy(q->terms[q->term_count], term, tlen + 1);
            q->term_lens[q->term_count] = tlen;
            q->term_count++;
        }
    }
}

struct bm25_match {
    int file_idx;
    uint32_t score;
    uint32_t best_snippet_offset;
};

int vfs_search_bm25(const char *query, const char *path_prefix, char *out, uint32_t max)
{
    if (!query || !*query || !out || max == 0) return 0;
    out[0] = '\0';

    struct bm25_query q;
    parse_bm25_query(query, &q);
    if (q.term_count == 0) {
        uint32_t p = 0;
        fb_puts(out, max, &p, "bm25: consulta demasiado generica o sin terminos significativos.\n");
        return 0;
    }

    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();

    struct bm25_match matches[FS_MAX_FILES];
    uint32_t match_count = 0;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used || !files[i].data) continue;
        const char *fn = files[i].name;

        if (path_prefix && *path_prefix) {
            if (fs_strstr(fn, path_prefix) != fn) continue;
        }

        if (vfs_is_hidden(fn) && (!path_prefix || !*path_prefix)) continue;

        uint32_t score = 0;
        uint32_t unique_hits = 0;
        uint32_t best_off = 0;
        uint32_t max_para_hits = 0;

        const char *data = files[i].data;
        uint32_t dlen = files[i].size;
        if (dlen == 0) continue;

        for (uint32_t t = 0; t < q.term_count; ++t) {
            const char *term = q.terms[t];
            const char *fp = fn;
            char fn_norm[64]; uint32_t fnp = 0;
            while (*fp && fnp < sizeof(fn_norm) - 1) {
                fn_norm[fnp++] = normalize_char_utf8(&fp);
            }
            fn_norm[fnp] = '\0';

            if (fs_strstr(fn_norm, term) != 0) {
                score += 30;
                unique_hits++;
            }
        }

        uint32_t off = 0;
        while (off < dlen) {
            uint32_t para_start = off;
            while (off < dlen && data[off] != '\n') off++;
            uint32_t para_len = off - para_start;
            if (off < dlen && data[off] == '\n') off++;

            char para_norm[512];
            uint32_t pnp = 0;
            const char *pp = data + para_start;
            const char *pend = pp + (para_len < 400 ? para_len : 400);

            while (pp < pend && pnp < sizeof(para_norm) - 1) {
                para_norm[pnp++] = normalize_char_utf8(&pp);
            }
            para_norm[pnp] = '\0';

            uint32_t para_hits = 0;
            for (uint32_t t = 0; t < q.term_count; ++t) {
                const char *term = q.terms[t];
                const char *pos = para_norm;
                int term_hit_in_para = 0;

                while ((pos = fs_strstr(pos, term)) != 0) {
                    para_hits++;
                    term_hit_in_para++;
                    pos += q.term_lens[t];
                    score += 8;
                }
                if (term_hit_in_para) unique_hits++;
            }

            if (para_hits > max_para_hits) {
                max_para_hits = para_hits;
                best_off = para_start;
            }
        }

        score += (unique_hits * 25);
        if (fs_strstr(fn, "/workspace/") == fn) score += 10;
        if (fs_strstr(fn, "/mem/") == fn) score += 15;

        if (score > 15) {
            matches[match_count].file_idx = i;
            matches[match_count].score = score;
            matches[match_count].best_snippet_offset = best_off;
            match_count++;
        }
    }

    kmutex_unlock(&vfs_mutex);

    if (match_count == 0) {
        uint32_t p = 0;
        fb_puts(out, max, &p, "bm25: sin coincidencias relevantes en memoria.\n");
        return 0;
    }

    for (uint32_t a = 0; a < match_count; ++a) {
        for (uint32_t b = a + 1; b < match_count; ++b) {
            if (matches[b].score > matches[a].score) {
                struct bm25_match tmp = matches[a];
                matches[a] = matches[b];
                matches[b] = tmp;
            }
        }
    }

    uint32_t pos = 0;
    uint32_t top = match_count < 3 ? match_count : 3;

    for (uint32_t r = 0; r < top; ++r) {
        int fi = matches[r].file_idx;
        const char *fn = files[fi].name;
        const char *data = files[fi].data;
        uint32_t size = files[fi].size;
        uint32_t snip_off = matches[r].best_snippet_offset;

        fb_puts(out, max, &pos, "[");
        fb_put_dec(out, max, &pos, r + 1);
        fb_puts(out, max, &pos, "] ");
        fb_puts(out, max, &pos, fn);
        fb_puts(out, max, &pos, " (Score: ");
        fb_put_dec(out, max, &pos, matches[r].score);
        fb_puts(out, max, &pos, ")\n    ");

        uint32_t snip_len = 0;
        while (snip_off + snip_len < size && snip_len < 220) {
            char c = data[snip_off + snip_len];
            if (c == '\r') { snip_len++; continue; }
            if (c == '\n') {
                if (snip_len > 80) break;
                c = ' ';
            }
            if (pos + 1 < max) out[pos++] = c;
            snip_len++;
        }
        if (snip_off + snip_len < size) {
            fb_puts(out, max, &pos, "...");
        }
        fb_puts(out, max, &pos, "\n\n");
    }

    return (int)top;
}

int vfs_grep_all(const char *pat, int ignore_case, int list_only,
                 char *out, uint32_t max)
{
    if (!pat || !*pat) return -1;
    if (max && out) out[0] = '\0';

    uint32_t pos = 0;
    int files_hit = 0;

    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used || !files[i].data) continue;

        const char *data = files[i].data;
        uint32_t size = files[i].size;
        uint32_t line_no = 1;
        uint32_t off = 0;
        int file_matched = 0;

        while (off < size) {
            uint32_t line_start = off;
            while (off < size && data[off] != '\n') off++;
            uint32_t line_len = off - line_start;
            if (off < size && data[off] == '\n') off++;

            char line[256];
            uint32_t cl = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
            for (uint32_t k = 0; k < cl; ++k) line[k] = data[line_start + k];
            line[cl] = '\0';

            const char *h = line;
            const char *n = pat;
            int match = 0;
            while (*h) {
                const char *hh = h;
                const char *nn = n;
                while (*hh && *nn) {
                    char ch = ignore_case && (*hh >= 'A' && *hh <= 'Z') ? (char)(*hh + 32) : *hh;
                    char cn = ignore_case && (*nn >= 'A' && *nn <= 'Z') ? (char)(*nn + 32) : *nn;
                    if (ch != cn) break;
                    hh++; nn++;
                }
                if (!*nn) { match = 1; break; }
                h++;
            }

            if (match) {
                if (!file_matched) {
                    files_hit++;
                    file_matched = 1;
                }
                if (list_only) break;

                if (out && max > 8) {
                    const char *nm = files[i].name;
                    while (*nm && pos + 1 < max) out[pos++] = *nm++;
                    if (pos + 1 < max) out[pos++] = ':';
                    char nb[12]; int nn = 0; uint32_t v = line_no;
                    if (v == 0) nb[nn++] = '0';
                    else { while (v) { nb[nn++] = (char)('0' + v % 10); v /= 10; } }
                    while (nn && pos + 1 < max) out[pos++] = nb[--nn];
                    if (pos + 1 < max) out[pos++] = ':';
                    if (pos + 1 < max) out[pos++] = ' ';
                    for (uint32_t k = 0; k < cl && pos + 1 < max; ++k) out[pos++] = line[k];
                    if (pos + 1 < max) out[pos++] = '\n';
                    out[pos] = '\0';
                }
            }
            line_no++;
        }

        if (list_only && file_matched && out && max > 2) {
            const char *nm = files[i].name;
            while (*nm && pos + 1 < max) out[pos++] = *nm++;
            if (pos + 1 < max) out[pos++] = '\n';
            out[pos] = '\0';
        }
    }

    kmutex_unlock(&vfs_mutex);

    if (files_hit == 0 && out && max > 24) {
        const char *msg = "grep: sin coincidencias en RamFS.\n";
        pos = 0;
        while (*msg && pos + 1 < max) out[pos++] = *msg++;
        out[pos] = '\0';
    }
    return files_hit;
}

void vfs_tree(char *out_buf, uint32_t max)
{
    vfs_tree_ext(0, out_buf, max);
}

void vfs_tree_ext(int show_all, char *out_buf, uint32_t max)
{
    kmutex_lock(&vfs_mutex);
    uint32_t pos = 0;
    const char *hdr = show_all
        ? "/ (SOMAFS02 - 64 Inodos / 16 KiB - Arbol Completo)\n+-- dev/\n|   +-- null\n|   +-- zero\n|   +-- urandom\n|   +-- rtc\n+-- proc/\n|   +-- version\n|   +-- uptime\n|   +-- meminfo\n|   +-- cpuinfo\n|   +-- threads\n|   +-- net/\n|       +-- dev\n|       +-- arp\n"
        : "/ (SOMAFS02 - Archivos Visibles)\n";

    kprint(hdr);
    fb_puts(out_buf, max, &pos, hdr);

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used) continue;
        const char *fn = files[i].name;
        if (vfs_is_hidden(fn) && !show_all) continue;

        if (fn[0] == '/') fn++;
        kprint("+-- "); kprint(fn); kprint("\n");
        fb_puts(out_buf, max, &pos, "+-- ");
        fb_puts(out_buf, max, &pos, fn);
        fb_puts(out_buf, max, &pos, "\n");
    }
    out_buf[pos] = '\0';
    kmutex_unlock(&vfs_mutex);
}

int vfs_sync(void)
{
    kmutex_lock(&vfs_mutex);
    int r = vfs_sync_unlocked();
    kmutex_unlock(&vfs_mutex);
    return r;
}

void vfs_format(void)
{
    kmutex_lock(&vfs_mutex);
    vfs_format_unlocked();
    kmutex_unlock(&vfs_mutex);
}

int vfs_create(const char *name, const char *initial_data)
{
    kmutex_lock(&vfs_mutex);
    uint32_t len = str_len(initial_data);
    int r = vfs_create_unlocked(name, initial_data, len);
    kmutex_unlock(&vfs_mutex);
    return r;
}

int vfs_checkpoint_save(void)
{
    if (!fs_initialized) vfs_init();
    kmutex_lock(&vfs_mutex);
    vfs_sync_unlocked();

    static uint8_t c_buf[512] __attribute__((aligned(16)));
    memset(c_buf, 0, sizeof(c_buf));
    memcpy(c_buf, "MYOS_CHECKPOINT_V2", 18);
    if (virtio_blk_write(FS_CHECKPOINT_LBA - 1, c_buf) != 0) {
        kmutex_unlock(&vfs_mutex);
        return -1;
    }

    for (uint32_t s = 0; s < FS_TOTAL_SECTORS; ++s) {
        if (virtio_blk_read(FS_SUPER_LBA + s, c_buf) != 0 ||
            virtio_blk_write(FS_CHECKPOINT_LBA + s, c_buf) != 0) {
            kmutex_unlock(&vfs_mutex);
            return -1;
        }
    }

    kmutex_unlock(&vfs_mutex);
    return 0;
}

int vfs_checkpoint_restore(void)
{
    if (!fs_initialized) vfs_init();
    kmutex_lock(&vfs_mutex);

    static uint8_t c_buf[512] __attribute__((aligned(16)));
    if (virtio_blk_read(FS_CHECKPOINT_LBA - 1, c_buf) != 0 ||
        memcmp(c_buf, "MYOS_CHECKPOINT_V2", 18) != 0) {
        kmutex_unlock(&vfs_mutex);
        return -1;
    }

    for (uint32_t s = 0; s < FS_TOTAL_SECTORS; ++s) {
        if (virtio_blk_read(FS_CHECKPOINT_LBA + s, c_buf) != 0 ||
            virtio_blk_write(FS_SUPER_LBA + s, c_buf) != 0) {
            kmutex_unlock(&vfs_mutex);
            return -1;
        }
    }

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        load_file_from_disk(i);
    }

    kmutex_unlock(&vfs_mutex);
    return 0;
}
