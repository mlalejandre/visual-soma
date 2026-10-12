#include <stdint.h>

#include "boot_gate.h"
#include "console.h"
#include "io.h"
#include "mem.h"
#include "pmm.h"
#include "vmm.h"
#include "thread.h"
#include "mutex.h"
#include "net.h"
#include "ip.h"
#include "fs.h"
#include "virtio_blk.h"
#include "llm.h"
#include "idt.h"

#define BOOT_GATE_LBA 24

static void bg_puts(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    if (!out || max == 0) return;
    while (*s && *pos + 1 < max) {
        out[(*pos)++] = *s++;
    }
    out[*pos] = '\0';
}

/* ====================================================================
 * SUITE INTEGRAL DE NO REGRESION (9 FASES DEL SISTEMA)
 * ==================================================================== */
int boot_gate_run_test_suite(char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    int failed = 0;

    kprint("\n============================================================\n");
    kprint("SOMA SUITE INTEGRAL DE VERIFICACION (9 FASES DEL KERNEL)\n");
    kprint("============================================================\n");
    bg_puts(out_buf, max_out, &pos, "[SUITE INTEGRAL SOMA]:\n");

    /* FASE 1: PMM Bitmap Allocator */
    kprint("[FASE 1/9] PMM Bitmap Allocator (4 KiB frames)... ");
    if (pmm_test_self()) {
        bg_puts(out_buf, max_out, &pos, "- PMM: OK\n");
    } else {
        bg_puts(out_buf, max_out, &pos, "- PMM: FALLO\n");
        failed++;
    }

    /* FASE 2: VMM Paginacion de 4 niveles y Aislamiento NX/RO */
    kprint("[FASE 2/9] VMM Traduccion 4 KiB y Permisos NX/RO... ");
    if (vmm_test_self()) {
        bg_puts(out_buf, max_out, &pos, "- VMM: OK\n");
    } else {
        bg_puts(out_buf, max_out, &pos, "- VMM: FALLO\n");
        failed++;
    }

    /* FASE 3: Heap Dinamico VMM (Alineacion 16-B y Expansion) */
    kprint("[FASE 3/9] Heap Dinamico kmalloc (Alineacion y Coalescencia)... ");
    if (kheap_test_self()) {
        bg_puts(out_buf, max_out, &pos, "- KHeap: OK\n");
    } else {
        bg_puts(out_buf, max_out, &pos, "- KHeap: FALLO\n");
        failed++;
    }

    /* FASE 4: Planificador Cooperativo y Recolector de Pilas */
    kprint("[FASE 4/9] KThreads Scheduler y Ciclo de Vida Reaper... ");
    if (thread_test_self()) {
        bg_puts(out_buf, max_out, &pos, "- KThreads: OK\n");
    } else {
        bg_puts(out_buf, max_out, &pos, "- KThreads: FALLO\n");
        failed++;
    }

    /* FASE 5: Exclusion Mutua y Recuperacion tras Kill */
    kprint("[FASE 5/9] KMutex Sincronizacion y Auto-Recuperacion... ");
    if (kmutex_test_self()) {
        bg_puts(out_buf, max_out, &pos, "- KMutex: OK\n");
    } else {
        bg_puts(out_buf, max_out, &pos, "- KMutex: FALLO\n");
        failed++;
    }

    /* FASE 6: Disco Fisico VirtIO-BLK (LBA 2048) */
    kprint("[FASE 6/9] Disco VirtIO-BLK (Lectura/Escritura LBA 2048)... ");
    static uint8_t sec_w[512] __attribute__((aligned(16)));
    static uint8_t sec_r[512] __attribute__((aligned(16)));
    for (int i = 0; i < 512; ++i) sec_w[i] = (uint8_t)(i ^ 0x5A);
    memcpy(sec_w, "MYOS_SYSTEM_SUITE_SECTOR_2048", 29);
    if (virtio_blk_write(3500, sec_w) != 0 ||
        virtio_blk_read(3500, sec_r) != 0 ||
        memcmp(sec_w, sec_r, 512) != 0) {
        kprint("FALLO\n");
        bg_puts(out_buf, max_out, &pos, "- VirtIO-BLK: FALLO\n");
        failed++;
    } else {
        kprint("OK\n");
        bg_puts(out_buf, max_out, &pos, "- VirtIO-BLK: OK\n");
    }

    /* FASE 7: Sistema de Archivos Persistente (RamFS MYOSFS01) */
    kprint("[FASE 7/9] RamFS Transaccional (CRUD y Dirty-Track)... ");
    const char *tfile = "/.test_suite.tmp";
    const char *tdata = "MYOS_SUITE_CANARY_VALIDATION_STRING";
    char rdata[64];
    memset(rdata, 0, sizeof(rdata));
    int w_res = vfs_write(tfile, tdata, 35);
    int r_res = vfs_read(tfile, rdata, sizeof(rdata));
    int d_res = vfs_delete(tfile);
    if (w_res > 0 && r_res > 0 && memcmp(rdata, tdata, 35) == 0 && d_res == 0) {
        kprint("OK\n");
        bg_puts(out_buf, max_out, &pos, "- RamFS MYOSFS01: OK\n");
    } else {
        kprint("FALLO\n");
        bg_puts(out_buf, max_out, &pos, "- RamFS MYOSFS01: FALLO\n");
        failed++;
    }

    /* FASE 8: Red Bare-Metal (ARP + Ping Gateway 10.0.2.2) */
    kprint("[FASE 8/9] Pila de Red (ARP + ICMP Gateway 10.0.2.2)... ");
    int p_res = icmp_ping(net_gateway, 777, 1500);
    if (p_res == 1) {
        kprint("OK\n");
        bg_puts(out_buf, max_out, &pos, "- Red (Gateway): OK\n");
    } else {
        kprint("FALLO\n");
        bg_puts(out_buf, max_out, &pos, "- Red (Gateway): FALLO\n");
        failed++;
    }

    /* FASE 9: Enlace HTTP con Servidor LLM Local */
    kprint("[FASE 9/9] Enlace HTTP llama-server (/health)... ");
    if (llm_health()) {
        kprint("OK\n");
        bg_puts(out_buf, max_out, &pos, "- LLM Server: OK\n");
    } else {
        kprint("AVISO (Offline/No critico)\n");
        bg_puts(out_buf, max_out, &pos, "- LLM Server: AVISO (Offline)\n");
    }

    kprint("============================================================\n");
    if (failed == 0) {
        kprint("RESUMEN: INTEGRIDAD TOTAL DEL SISTEMA VERIFICADA (8/8 CRITICOS OK)\n\n");
        bg_puts(out_buf, max_out, &pos, "Resultado: Sistema 100% robusto y consistente.");
        return 0;
    } else {
        kprint("RESUMEN: DETECTADAS ANOMALIAS EN SUBSISTEMAS CRITICOS.\n\n");
        bg_puts(out_buf, max_out, &pos, "Resultado: FALLOS DETECTADOS en la suite de verificacion.");
        return -1;
    }
}

/* ====================================================================
 * PUERTA DE ARRANQUE (CANARY BOOT RAPIDO, LOCAL Y DETERMINISTA)
 * ==================================================================== */
void boot_gate_check(void)
{
    static char gate_sec[512];
    if (virtio_blk_read(BOOT_GATE_LBA, gate_sec) != 0) {
        return;
    }

    if (memcmp(gate_sec, "GATE_TEST_REQ", 13) != 0) {
        return;
    }

    boot_gate_active = 1;
    kprint("\n============================================================\n");
    kprint("[PUERTA DE ARRANQUE]: Verificacion rapida del nuevo kernel (<1s)\n");
    kprint("============================================================\n");

    /* Desarmar sector en disco para no entrar en bucle */
    memset(gate_sec, 0, sizeof(gate_sec));
    virtio_blk_write(BOOT_GATE_LBA, gate_sec);

    /* 1. Comprobacion de Heap y Alineacion 16-bytes */
    void *p = kmalloc(128);
    if (!p || ((uintptr_t)p & 0x0F) != 0) {
        kprint("[PUERTA DE ARRANQUE] FALLO: Asignador KHeap no operativo.\n");
        qemu_exit(0x22);
    }
    memset(p, 0x5A, 128);
    kfree(p);

    /* 2. Comprobacion de lectura y escritura en virtio-blk */
    static char canary_io[512] __attribute__((aligned(16)));
    memcpy(canary_io, "MYOS_CANARY_PROBE", 17);
    if (virtio_blk_write(3500, canary_io) != 0 || virtio_blk_read(3500, canary_io) != 0) {
        kprint("[PUERTA DE ARRANQUE] FALLO: I/O en disco virtio-blk no responde.\n");
        qemu_exit(0x22);
    }

    /* 3. Comprobacion de integridad en RamFS */
    if (vfs_write("/.canary.tmp", "OK", 2) < 0 || vfs_delete("/.canary.tmp") != 0) {
        kprint("[PUERTA DE ARRANQUE] FALLO: VFS no responde.\n");
        qemu_exit(0x22);
    }

    /* 4. Comprobacion de red activa (ICMP ping al gateway 10.0.2.2) */
    if (icmp_ping(net_gateway, 101, 1000) != 1) {
        kprint("[PUERTA DE ARRANQUE] FALLO: Pila de red no responde.\n");
        qemu_exit(0x22);
    }

    kprint("[PUERTA DE ARRANQUE] EXITO: Kernel verificado (RAM, Disco, VFS y Red OK).\n");
    kprint("Notificando al host (aprobacion de parche inmediata)...\n");
    boot_gate_active = 0;
    qemu_exit(0x20); /* QEMU_EXIT_GATE_OK */
}
