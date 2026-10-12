#include "arch.h"
#include "agent.h"
#include "rtc.h"
#include "console.h"
#include "llm.h"
#include "fs.h"
#include "virtio_blk.h"
#include "shell.h"
#include "mem.h"
#include "io.h"
#include "gui.h"

#define RESULT_LBA          20
#define RESUME_FLAG_LBA     25
#define AGENT_STATE_FILE    "/agent/state"
#define AGENT_MAX_PATCHES   3
#define AGENT_HIST_KEEP     4000
#define AGENT_HIST_MARK     "\n@@HISTORY@@\n"

#define MAILBOX_SIZE        8192
#define MAILBOX_LBA         1
#define MAILBOX_HDR         12

#define MAX_STM_ENTRIES     8
#define STM_PROMPT_WINDOW   2
#define MAX_DELEGATION_DEPTH 3

struct agent_profile {
    const char *name;
    const char *display_title;
    const char *role_desc;
    const char *profile_file;
    const char *stm_file;
};

static const struct agent_profile AGENT_REGISTRY[] = {
    {
        "soma",
        "SOMA CORE - AI KERNEL AGENT",
        "Eres SOMA, el orquestador principal de SOMA OS. Coordina a los agentes especializados, decide a quien delegar y resume resultados verificables. No ejecutes directamente las operaciones ordinarias del sistema: delegalas a Mayordomo.",
        "/agent/soma.txt",
        "/agent/soma_stm.txt"
    },
    {
        "mayordomo",
        "MAYORDOMO - AGENTE DE OPERACIONES DEL SISTEMA",
        "Eres Mayordomo, agente operativo determinista de SOMA. Atiendes operaciones de RAM, disco, VFS, listados, lectura/busqueda de archivos y creacion de carpetas. Prioriza comandos nativos, respuestas breves y ejecucion directa; no uses razonamiento LLM para estas tareas.",
        "/agent/mayordomo.txt",
        "/agent/mayordomo_stm.txt"
    },
    {
        "buscador",
        "BUSCADOR - WEB & SYSTEM RESEARCH AGENT",
        "Eres Buscador, el agente de exploracion web, investigacion de actualidad, analisis de datos y navegacion del arbol de memoria estructurada de SOMA.",
        "/agent/buscador.txt",
        "/agent/buscador_stm.txt"
    },
    {
        "maquetador",
        "MAQUETADOR - DOCUMENT LAYOUT AGENT",
        "Eres Maquetador, el agente de diseno editorial de SOMA. PROHIBIDO usar herramientas (write, shell, patch, search, curl). NO guardes archivos. NO expliques. NO uses markdown. Tu UNICA salida es el documento en formato #SOMA-LAYOUT v1 en texto ASCII (sin acentos: usa n por ñ, e por é, etc.). Empieza por la linea #SOMA-LAYOUT v1. Meta: TITLE, SUBTITLE, FONT (modern|classic|bold), ALIGN (left|center), MARGIN (8-64). Tras --- bloques H1:/H2:/H3:/P:/QUOTE:/LIST:/HR/FOOTER:. En LIST el valor NO lleva guion inicial (el render lo añade). Texto profesional y claro."
        "Reglas: 1) Empieza siempre con la linea #SOMA-LAYOUT v1. 2) Meta: TITLE, SUBTITLE, FONT (modern|classic|bold), ALIGN (left|center), MARGIN (8-64). "
        "3) Tras --- usa bloques H1:/H2:/H3:/P:/QUOTE:/LIST:/HR/FOOTER:. 4) No inventes HTML ni Markdown. 5) Texto profesional, claro y estructurado. "
        "6) Si recibes notas del buscador, reorganiza y maqueta; no copies basura de la web.",
        "/agent/maquetador.txt",
        "/agent/maquetador_stm.txt"
    }
};

static const int NUM_AGENTS = sizeof(AGENT_REGISTRY) / sizeof(AGENT_REGISTRY[0]);

static char agent_state_buf[4096];
static char resume_history_stash[4096];
char agent_resume_mission[512];
char agent_resume_name[32];
char agent_last_response[8192] = {0};
int  agent_resume_pending = 0;
static int agent_patch_attempts = 0;
static int delegation_depth = 0;

static char mailbox_buf[MAILBOX_SIZE];
static char hpatch_buf[2048];

static const char *find_substr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return haystack;
    }
    return 0;
}

static int str_eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == *b);
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
    char tmp[10];
    int n = 0;
    if (v == 0) { fb_putc(out, max, pos, '0'); return; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) fb_putc(out, max, pos, tmp[--n]);
}

static const struct agent_profile *resolve_agent(const char *name)
{
    if (name) {
        for (int i = 0; i < NUM_AGENTS; ++i) {
            if (str_eq_ci(name, AGENT_REGISTRY[i].name)) {
                return &AGENT_REGISTRY[i];
            }
        }
    }
    return &AGENT_REGISTRY[0];
}

/* ====================================================================
 * MEMORIA A CORTO PLAZO (STM) CON VENTANA DESLIZANTE
 * ==================================================================== */

static void stm_load_into_prompt(const struct agent_profile *agent, char *dst, uint32_t max, uint32_t *pos)
{
    static char stm_raw[2048];
    int r = vfs_read(agent->stm_file, stm_raw, sizeof(stm_raw) - 1);
    if (r > 0) {
        stm_raw[r] = '\0';
        fb_puts(dst, max, pos, "\n[MEMORIA RECIENTE (ULTIMAS INTERACCIONES CON EL USUARIO)]:\n");
        
        const char *p = stm_raw;
        int entries = 0;
        while (*p && entries < STM_PROMPT_WINDOW && *pos < max - 300) {
            dst[(*pos)++] = *p++;
            if (*p == '*' && *(p - 1) == '\n') entries++;
        }
        dst[*pos] = '\0';
        fb_puts(dst, max, pos, "\n");
    }
}

static void stm_append_interaction(const struct agent_profile *agent, const char *user_msg, const char *verdict_msg)
{
    static char existing[2048];
    static char updated[3072];
    int r = vfs_read(agent->stm_file, existing, sizeof(existing) - 1);
    if (r < 0) r = 0;
    existing[r] = '\0';

    uint32_t pos = 0;
    fb_puts(updated, sizeof(updated), &pos, "* Usuario: \"");
    const char *um = user_msg;
    while (*um && pos < sizeof(updated) - 500) {
        if (*um != '\n' && *um != '\r') fb_putc(updated, sizeof(updated), &pos, *um);
        um++;
    }
    fb_puts(updated, sizeof(updated), &pos, "\"\n  Respuesta: \"");
    const char *vm = verdict_msg;
    while (*vm && pos < sizeof(updated) - 300) {
        if (*vm != '\n' && *vm != '\r') fb_putc(updated, sizeof(updated), &pos, *vm);
        vm++;
    }
    fb_puts(updated, sizeof(updated), &pos, "\"\n");

    if (r > 0) {
        const char *p = existing;
        int entries = 1;
        while (*p && entries < MAX_STM_ENTRIES && pos < sizeof(updated) - 2) {
            updated[pos++] = *p++;
            if (*p == '*' && *(p - 1) == '\n') entries++;
        }
        updated[pos] = '\0';
    }

    vfs_write(agent->stm_file, updated, pos);
}

/* ====================================================================
 * MEMORIA DEL SISTEMA Y PIZARRA COMPARTIDA
 * ==================================================================== */

static void inject_agent_memory(const struct agent_profile *agent, char *dst, uint32_t max, uint32_t *pos)
{
    fb_puts(dst, max, pos, "\n[MEMORIA DEL SISTEMA]:\n");

    /* 1. Core Profile: Usuario y Entorno Hardware */
    char buf[384];
    int n = vfs_read("/etc/mem_user.txt", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fb_puts(dst, max, pos, "- Usuario: ");
        fb_puts(dst, max, pos, buf);
        fb_puts(dst, max, pos, "\n");
    }
    n = vfs_read("/etc/mem_hw.txt", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fb_puts(dst, max, pos, "- Hardware: ");
        fb_puts(dst, max, pos, buf);
        fb_puts(dst, max, pos, "\n");
    }

    /* 2. Perfil Individual del Agente */
    n = vfs_read(agent->profile_file, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fb_puts(dst, max, pos, "- Perfil Agente: ");
        fb_puts(dst, max, pos, buf);
        fb_puts(dst, max, pos, "\n");
    }

    /* 3. Pizarra Compartida Multi-Agente (Shared Blackboard) */
    n = vfs_read("/agent/blackboard.txt", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fb_puts(dst, max, pos, "- Pizarra Compartida Multi-Agente: ");
        fb_puts(dst, max, pos, buf);
        fb_puts(dst, max, pos, "\n");
    }

    /* 4. Directiva de Memoria Semántica y Delegación */
    fb_puts(dst, max, pos, "- Herramientas Clave: Usa 'mem_search <keyword>' para explorar /mem/ y 'delegate <agente> \"<mision>\"' para coordinar tareas especializadas.\n");

    /* 5. Inyección de Ventana Deslizante de STM */
    {   /* MAYORDOMO_V2_KNOWLEDGE: catalogo vivo de comandos autorizados. */
        char caps[1280];
        if (shell_format_names(caps, sizeof(caps), 1) > 0) {
            fb_puts(dst, max, pos, "- Comandos nativos autorizados (catalogo vivo; Mayordomo los ejecuta directamente): ");
            fb_puts(dst, max, pos, caps);
            fb_puts(dst, max, pos, "\n");
        }
    }
    stm_load_into_prompt(agent, dst, max, pos);
}

/* ====================================================================
 * DELEGACIÓN JERÁRQUICA MULTI-AGENTE
 * ==================================================================== */

int delegate_cmd(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: delegate <agente> \"<sub-mision>\"\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: delegate <agente> \"<sub-mision>\"");
        return 1;
    }

    char target_agent[32];
    uint32_t ap = 0;
    while (*args && *args != ' ' && ap < sizeof(target_agent) - 1) {
        target_agent[ap++] = *args++;
    }
    target_agent[ap] = '\0';
    while (*args == ' ') args++;

    char q = 0;
    if (*args == '"' || *args == '\'') q = *args++;

    char sub_mission[512];
    uint32_t sp = 0;
    while (*args && sp < sizeof(sub_mission) - 1) {
        if (q && *args == q && *(args + 1) == '\0') break;
        sub_mission[sp++] = *args++;
    }
    sub_mission[sp] = '\0';

    if (sp == 0) {
        kprint("delegate: falta la sub-mision para el agente '"); kprint(target_agent); kprint("'\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Error: sub-mision vacia");
        return 1;
    }

    if (delegation_depth >= MAX_DELEGATION_DEPTH) {
        kprint("delegate: profundidad maxima de delegacion alcanzada (3)\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Error: limite de delegacion alcanzado");
        return 1;
    }

    kprint("\n\033[1;33m[ORQUESTACION MULTI-AGENTE] Delegando tarea a '");
    kprint(target_agent);
    kprint("'...\033[0m\n");

    delegation_depth++;
    agent_run(target_agent, sub_mission);
    delegation_depth--;

    static char bb[1024];
    int r = vfs_read("/agent/blackboard.txt", bb, sizeof(bb) - 1);
    if (r > 0) bb[r] = '\0';
    else bb[0] = '\0';

    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, "[DELEGACION A ");
    fb_puts(out_buf, max_out, &pos, target_agent);
    fb_puts(out_buf, max_out, &pos, " COMPLETADA]: ");
    if (bb[0]) {
        fb_puts(out_buf, max_out, &pos, bb);
    } else {
        fb_puts(out_buf, max_out, &pos, "Sub-mision ejecutada exitosamente.");
    }

    return 1;
}

/* ====================================================================
 * COMANDOS DE MEMORIA ESTRUCTURADA EN ÁRBOL Y BÚSQUEDA
 * ==================================================================== */

int mem_search_cmd(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: mem_search <palabras_clave>\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: mem_search <palabras_clave>");
        return 1;
    }
    static char search_res[2048];
    search_res[0] = '\0';
    int hits = vfs_search_bm25(args, "", search_res, sizeof(search_res));

    uint32_t pos = 0;
    if (hits <= 0) {
        kprint("mem_search (BM25): sin coincidencias para '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "Sin coincidencias en memoria persistente.");
    } else {
        kprint("\n--- Resultados BM25 en Memoria Persistente ---\n");
        kprint(search_res);
        kprint("----------------------------------------------\n");
        fb_puts(out_buf, max_out, &pos, search_res);
    }
    return 1;
}

int mem_tree_cmd(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    static char tree_idx[1024];
    uint32_t pos = 0;
    int r = vfs_read("/mem/indice.txt", tree_idx, sizeof(tree_idx) - 1);
    if (r > 0) {
        tree_idx[r] = '\0';
        kprint("\nARBOL DE MEMORIA ESTRUCTURADA:\n--------------------------------\n");
        kprint(tree_idx);
        kprint("--------------------------------\n");
        fb_puts(out_buf, max_out, &pos, tree_idx);
    } else {
        const char *fallback = "/mem/ (Arbol de Memoria)\n+-- ciencia/\n|   +-- ciencia_fisica.txt\n|   +-- ciencia_computacion.txt\n+-- historia/\n|   +-- historia_antigua.txt\n|   +-- historia_moderna.txt\n+-- conversaciones/\n    +-- conversaciones.txt\n";
        kprint(fallback);
        fb_puts(out_buf, max_out, &pos, fallback);
    }
    return 1;
}

int mem_read_cmd(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: mem_read <nodo_o_ruta>  (ejemplo: mem_read /mem/historia_moderna.txt)\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: mem_read <nodo>");
        return 1;
    }

    char target[64];
    uint32_t tp = 0;
    if (args[0] != '/') {
        fb_puts(target, sizeof(target), &tp, "/mem/");
    }
    while (*args && *args != ' ' && tp < sizeof(target) - 1) {
        target[tp++] = *args++;
    }
    target[tp] = '\0';

    static char node_data[2048];
    int r = vfs_read(target, node_data, sizeof(node_data) - 1);
    uint32_t pos = 0;
    if (r >= 0) {
        node_data[r] = '\0';
        kprint("\n[NODO "); kprint(target); kprint("]:\n");
        kprint(node_data);
        kprint("\n");
        fb_puts(out_buf, max_out, &pos, node_data);
    } else {
        kprint("mem_read: nodo no encontrado '"); kprint(target); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "Error: nodo de memoria no encontrado.");
    }
    return 1;
}

int mem_write_cmd(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char node[64];
    uint32_t np = 0;
    if (*args != '/') fb_puts(node, sizeof(node), &np, "/mem/");
    while (*args && *args != ' ' && np < sizeof(node) - 1) node[np++] = *args++;
    node[np] = '\0';
    while (*args == ' ') args++;

    char q = 0;
    if (*args == '"' || *args == '\'') q = *args++;

    char text[2048];
    uint32_t tp = 0;
    while (*args && tp < sizeof(text) - 1) {
        if (q && *args == q && *(args + 1) == '\0') break;
        text[tp++] = *args++;
    }
    text[tp] = '\0';

    int r = vfs_write(node, text, tp);
    uint32_t pos = 0;
    if (r >= 0) {
        kprint("mem_write: nodo '"); kprint(node); kprint("' actualizado (");
        kprint_dec(tp); kprint(" bytes)\n");
        fb_puts(out_buf, max_out, &pos, "Nodo de memoria actualizado.");
    } else {
        kprint("mem_write: error al escribir nodo '"); kprint(node); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "Error al escribir nodo de memoria.");
    }
    return 1;
}

/* ====================================================================
 * GESTIÓN DE PARCHES Y ESTADO DE REANUDACIÓN
 * ==================================================================== */

static int is_patch_target_protected(const char *name)
{
    static const char *const protected_list[] = {
        "boot.s", "idt.c", "idt.h", "io.h", "srcfs.c", "srcfs.h",
        "virtio_blk.c", "virtio_blk.h", "boot_gate.c", "boot_gate.h",
        "pmm.c", "pmm.h", "vmm.c", "vmm.h", "mem.c", "mem.h",
        "thread.c", "thread.h", "switch.s", "mutex.c", "mutex.h", 0
    };
    while (*name == ' ' || *name == '\t') name++;
    if (name[0] == '/') name++;
    if (name[0] == 's' && name[1] == 'r' && name[2] == 'c' && name[3] == '/') name += 4;

    for (int i = 0; protected_list[i]; ++i) {
        const char *a = name;
        const char *b = protected_list[i];
        while (*a && *b && (*a == *b)) { a++; b++; }
        if (*b == '\0' && (*a == '\0' || *a == ' ' || *a == '\n' || *a == '\r' || *a == '"')) {
            return 1;
        }
    }
    return 0;
}

static void host_submit_patch(const char *text)
{
    uint32_t n = 0;
    while (text[n]) n++;

    if (n == 0 || n > MAILBOX_SIZE - MAILBOX_HDR) {
        kprint("\n[PARCHE] Rechazado: tamano invalido.\n");
        return;
    }

    memset(mailbox_buf, 0, sizeof(mailbox_buf));
    memcpy(mailbox_buf, "PATCHv02", 8);
    memcpy(mailbox_buf + 8, &n, 4);
    memcpy(mailbox_buf + MAILBOX_HDR, text, n);

    for (int s = 0; s < MAILBOX_SIZE / 512; ++s) {
        if (virtio_blk_write(MAILBOX_LBA + s, mailbox_buf + s * 512) != 0) {
            kprint("\n[PARCHE] Error de I/O escribiendo el buzon.\n");
            return;
        }
    }

    kprint("\n========================================================================\n");
    kprint("!!! PUENTE HOST-BRIDGE: PARCHE ENVIADO, SOLICITANDO RECOMPILACION !!!\n");
    kprint("========================================================================\n");

    qemu_exit(0x10);
    for (;;) { arch_interrupts_disable(); arch_halt(); }
}

static void agent_state_save(const char *agent_name, const char *mission, const char *history, uint32_t hist_len)
{
    static uint8_t zero_sec[512];
    uint32_t pos = 0;
    uint32_t start = hist_len > AGENT_HIST_KEEP ? hist_len - AGENT_HIST_KEEP : 0;

    if (start > 0) {
        const char *nx = find_substr(history + start, "\n- Paso ");
        start = nx ? (uint32_t)(nx + 1 - history) : hist_len;
    }

    agent_state_buf[0] = '\0';
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, "AGENT:");
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, agent_name);
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, "\nATTEMPTS:");
    fb_put_dec(agent_state_buf, sizeof(agent_state_buf), &pos, (uint32_t)(agent_patch_attempts + 1));
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, "\n");
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, mission);
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, AGENT_HIST_MARK);
    fb_puts(agent_state_buf, sizeof(agent_state_buf), &pos, history + start);

    vfs_write(AGENT_STATE_FILE, agent_state_buf, pos);
    virtio_blk_write(RESULT_LBA, zero_sec);
}

void agent_state_load(void)
{
    static uint8_t sec[512];
    static uint8_t res[512];

    if (virtio_blk_read(RESUME_FLAG_LBA, sec) != 0 || memcmp(sec, "RESUME_REQ", 10) != 0) {
        return;
    }

    memset(sec, 0, sizeof(sec));
    virtio_blk_write(RESUME_FLAG_LBA, sec);

    if (vfs_read(AGENT_STATE_FILE, agent_state_buf, sizeof(agent_state_buf)) <= 0) {
        return;
    }
    vfs_delete(AGENT_STATE_FILE);

    const char *p = agent_state_buf;
    agent_resume_name[0] = '\0';
    if (memcmp(p, "AGENT:", 6) == 0) {
        p += 6;
        uint32_t ap = 0;
        while (*p && *p != '\n' && ap < sizeof(agent_resume_name) - 1) {
            agent_resume_name[ap++] = *p++;
        }
        agent_resume_name[ap] = '\0';
        if (*p == '\n') p++;
    }
    if (agent_resume_name[0] == '\0') {
        fb_puts(agent_resume_name, sizeof(agent_resume_name), &(uint32_t){0}, "soma");
    }

    if (memcmp(p, "ATTEMPTS:", 9) != 0) return;
    p += 9;

    int n = 0;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
    if (*p == '\n') p++;

    const char *hm = find_substr(p, AGENT_HIST_MARK);
    if (!hm) return;

    uint32_t m = 0;
    while (p + m < hm && m < sizeof(agent_resume_mission) - 1) {
        agent_resume_mission[m] = p[m];
        m++;
    }
    agent_resume_mission[m] = '\0';
    if (m == 0) return;

    uint32_t pos = 0;
    resume_history_stash[0] = '\0';
    fb_puts(resume_history_stash, sizeof(resume_history_stash), &pos, hm + (sizeof(AGENT_HIST_MARK) - 1));
    if (pos > 0 && resume_history_stash[pos - 1] != '\n') fb_putc(resume_history_stash, sizeof(resume_history_stash), &pos, '\n');

    if (virtio_blk_read(RESULT_LBA, res) == 0 && res[0] != 0) {
        res[511] = 0;
        for (int i = 0; res[i]; ++i) if (res[i] < 32 || res[i] > 126) res[i] = ' ';
    } else {
        res[0] = '\0';
    }

    if (find_substr((const char *)res, "BOOT_OK")) {
        fb_puts(resume_history_stash, sizeof(resume_history_stash), &pos,
                "- Paso R: El host APROBO y COMPILO tu parche con exito (BOOT_OK).\n"
                "  [DIRECTIVA]: Tu parche ya esta aplicado. NO envies mas parches. Concluye con action=\"final\".\n");
    } else if (find_substr((const char *)res, "FAILED") || find_substr((const char *)res, "REJECTED")) {
        fb_puts(resume_history_stash, sizeof(resume_history_stash), &pos,
                "- Paso R: El parche NO fue aceptado (rollback aplicado).\n  [DETALLE]: ");
        fb_puts(resume_history_stash, sizeof(resume_history_stash), &pos, (const char *)res);
        fb_puts(resume_history_stash, sizeof(resume_history_stash), &pos, "\n  [DIRECTIVA]: Corrige tu SEARCH/REPLACE y reintenta.\n");
    }

    agent_patch_attempts = n;
    agent_resume_pending = 1;
}

static void history_add_note(char *hbuf, uint32_t *hist_len, uint32_t max_hist, int step, const char *msg)
{
    if (!hbuf || !hist_len || *hist_len >= max_hist - 400) return;
    uint32_t pos = *hist_len;
    fb_puts(hbuf, max_hist, &pos, "- Paso ");
    fb_putc(hbuf, max_hist, &pos, (char)('0' + step));
    fb_puts(hbuf, max_hist, &pos, ": ");
    fb_puts(hbuf, max_hist, &pos, msg);
    fb_puts(hbuf, max_hist, &pos, "\n");
    *hist_len = pos;
}



/* ====================================================================
 * BUCLE AUTÓNOMO REACT MULTI-AGENTE (Contexto Aislado en KHeap)
 * ==================================================================== */

/* AGENTE_MAYORDOMO_PATCH_V1: operaciones del sistema sin bucle LLM. */
/* Un solo buffer auxiliar: el resultado final reutiliza agent_last_response. */
static char mayordomo_listing[4096];

static char mayordomo_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static const char *mayordomo_find_ci(const char *text, const char *needle)
{
    if (!text || !needle) return 0;
    if (!*needle) return text;
    for (; *text; ++text) {
        const char *a = text, *b = needle;
        while (*a && *b && mayordomo_lower(*a) == mayordomo_lower(*b)) {
            ++a; ++b;
        }
        if (!*b) return text;
    }
    return 0;
}

static int mayordomo_has(const char *text, const char *needle)
{
    return mayordomo_find_ci(text, needle) != 0;
}

static int mayordomo_starts_word(const char *text, const char *word)
{
    uint32_t i = 0;
    while (word[i] && text[i] && mayordomo_lower(text[i]) == mayordomo_lower(word[i])) ++i;
    return word[i] == '\0' && (text[i] == '\0' || text[i] == ' ' || text[i] == '\t');
}

static void mayordomo_store_reply(const char *reply, int print_reply)
{
    if (reply != agent_last_response) {
        uint32_t p = 0;
        agent_last_response[0] = '\0';
        fb_puts(agent_last_response, sizeof(agent_last_response), &p,
                reply ? reply : "[Mayordomo] Operacion terminada.");
    }
    uint32_t n = 0;
    while (agent_last_response[n] && n < sizeof(agent_last_response) - 1) ++n;
    vfs_write("/agent/blackboard.txt", agent_last_response, n);
    if (print_reply) {
        kprint("\n");
        kprint(agent_last_response);
        kprint("\n");
    }
}

static void mayordomo_dispatch(const char *label, const char *command)
{
    kprint("\n[Mayordomo] ");
    kprint(label);
    kprint("\n");
    agent_last_response[0] = '\0';
    int rc = dispatch_agent_command(command, agent_last_response,
                                    sizeof(agent_last_response));
    uint32_t body_len = 0;
    while (agent_last_response[body_len] && body_len < sizeof(agent_last_response) - 1) ++body_len;
    if (!body_len) {
        uint32_t q = 0;
        fb_puts(agent_last_response, sizeof(agent_last_response), &q,
                rc ? "Comando ejecutado." : "No se pudo ejecutar el comando solicitado.");
        body_len = q;
    }

    char prefix[160];
    uint32_t prefix_len = 0;
    fb_puts(prefix, sizeof(prefix), &prefix_len, "[Mayordomo] ");
    fb_puts(prefix, sizeof(prefix), &prefix_len, label);
    fb_puts(prefix, sizeof(prefix), &prefix_len, "\n");
    uint32_t max_body = sizeof(agent_last_response) - prefix_len - 1;
    if (body_len > max_body) body_len = max_body;
    agent_last_response[body_len] = '\0';
    for (uint32_t i = body_len; i > 0; --i)
        agent_last_response[i + prefix_len - 1] = agent_last_response[i - 1];
    for (uint32_t i = 0; i < prefix_len; ++i) agent_last_response[i] = prefix[i];
    agent_last_response[prefix_len + body_len] = '\0';
    mayordomo_store_reply(agent_last_response, 0);
}

/* Extrae un nombre/ruta despues de expresiones habituales del usuario. */
static int mayordomo_extract_value(const char *mission, char *out, uint32_t cap)
{
    static const char *const markers[] = {
        /* Los marcadores especificos preceden a los sustantivos genericos. */
        "llamado ", "llamada ", "denominado ", "denominada ",
        "ruta ", "nombre ", "archivo ", "fichero ", "carpeta ", "directorio ",
        "file ", "folder ", "directory ", 0
    };
    const char *p = 0;
    if (!out || cap < 2) return 0;
    out[0] = '\0';
    for (int i = 0; markers[i]; ++i) {
        const char *m = mayordomo_find_ci(mission, markers[i]);
        if (m) { p = m; while (*p && *p != ' ') ++p; if (*p == ' ') ++p; break; }
    }
    if (!p || !*p) {
        /* Fallback: primera ruta absoluta o token con extension de archivo. */
        const char *s = mission;
        while (*s) {
            while (*s == ' ' || *s == '\t' || *s == '\'' || *s == '"') ++s;
            const char *b = s;
            while (*s && *s != ' ' && *s != '\t' && *s != '\'' && *s != '"' && *s != ',' && *s != ';') ++s;
            uint32_t n = (uint32_t)(s - b);
            int has_dot = 0;
            for (uint32_t j = 0; j < n; ++j) if (b[j] == '.') has_dot = 1;
            if (n && (b[0] == '/' || has_dot)) { p = b; break; }
            if (*s) ++s;
        }
    }
    if (!p) return 0;
    while (*p == ' ' || *p == '\t' || *p == '\'' || *p == '"') ++p;
    uint32_t n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\'' && *p != '"' &&
           *p != ',' && *p != ';' && *p != '\r' && *p != '\n' && n + 1 < cap) {
        out[n++] = *p++;
    }
    while (n && (out[n - 1] == '.' || out[n - 1] == ':' || out[n - 1] == '!' || out[n - 1] == '?')) --n;
    out[n] = '\0';
    if (!n || mayordomo_has(out, "llamado") || mayordomo_has(out, "llamada") ||
        mayordomo_has(out, "denominado") || mayordomo_has(out, "denominada")) {
        out[0] = '\0'; return 0;
    }
    return 1;
}

static int mayordomo_resolve_path(const char *value, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (!value || !*value || !out || cap < 2) return 0;
    out[0] = '\0';
    if (value[0] == '/') {
        while (value[p] && p + 1 < cap) { out[p] = value[p]; ++p; }
    } else {
        static const char prefix[] = "/workspace/";
        while (prefix[p] && p + 1 < cap) { out[p] = prefix[p]; ++p; }
        for (uint32_t i = 0; value[i] && p + 1 < cap; ++i) out[p++] = value[i];
    }
    while (p > 1 && out[p - 1] == '/') --p;
    out[p] = '\0';
    if (p >= FS_NAME_MAX) { out[0] = '\0'; return 0; }
    return p > 1;
}

/* Las mutaciones naturales se limitan a espacios de usuario; las rutas del
 * sistema (/etc, /agent, /proc, etc.) no se modifican por una interpretacion. */
static int mayordomo_is_user_path(const char *path)
{
    static const char *const roots[] = { "/workspace", "/mis_archivos", 0 };
    for (int r = 0; roots[r]; ++r) {
        uint32_t i = 0;
        while (roots[r][i] && path[i] == roots[r][i]) ++i;
        if (!roots[r][i] && (path[i] == '\0' || path[i] == '/')) return 1;
    }
    return 0;
}

static void mayordomo_make_folder(const char *raw)
{
    char value[FS_NAME_MAX + 8];
    char path[FS_NAME_MAX + 8];
    char marker[FS_NAME_MAX];
    if (!raw || !*raw) {
        mayordomo_store_reply("[Mayordomo] Uso: crea una carpeta llamada <nombre> (ruta relativa en /workspace/).", 1);
        return;
    }
    uint32_t n = 0;
    while (*raw == ' ' || *raw == '\t' || *raw == '\'' || *raw == '"') ++raw;
    while (*raw && *raw != ' ' && *raw != '\t' && *raw != '\'' && *raw != '"' && n + 1 < sizeof(value)) value[n++] = *raw++;
    value[n] = '\0';
    if (!mayordomo_resolve_path(value, path, sizeof(path))) {
        mayordomo_store_reply("[Mayordomo] Nombre de carpeta no valido.", 1);
        return;
    }
    if (!mayordomo_is_user_path(path)) {
        mayordomo_store_reply("[Mayordomo] Por seguridad solo creo carpetas en /workspace o /mis_archivos.", 1);
        return;
    }
    n = 0;
    while (path[n] && n < sizeof(path) - 1) ++n;
    /* SOMA VFS representa los directorios de forma implicita mediante prefijos. */
    if (n + 5 >= sizeof(marker)) {
        mayordomo_store_reply("[Mayordomo] Ruta demasiado larga para el VFS (limite de 47 bytes).", 1);
        return;
    }
    for (uint32_t i = 0; i < n; ++i) marker[i] = path[i];
    marker[n++] = '/'; marker[n++] = '.'; marker[n++] = 'd'; marker[n++] = 'i'; marker[n++] = 'r'; marker[n] = '\0';
    if (n >= FS_NAME_MAX || vfs_write(marker, "", 0) < 0) {
        mayordomo_store_reply("[Mayordomo] No se pudo crear la carpeta: VFS sin espacio o ruta demasiado larga.", 1);
        return;
    }
    char reply[256];
    uint32_t p = 0;
    fb_puts(reply, sizeof(reply), &p, "[Mayordomo] Carpeta creada: ");
    fb_puts(reply, sizeof(reply), &p, path);
    fb_puts(reply, sizeof(reply), &p, " (directorio implicito del VFS; marcador .dir oculto).");
    mayordomo_store_reply(reply, 1);
}

static void mayordomo_find_file(const char *mission)
{
    char term[FS_NAME_MAX + 8];
    term[0] = '\0';
    if (!mayordomo_extract_value(mission, term, sizeof(term))) {
        mayordomo_store_reply("[Mayordomo] Indica el nombre o una parte del nombre: 'busca un archivo llamado config.txt'.", 1);
        return;
    }
    mayordomo_listing[0] = '\0';
    vfs_format_list_ext(1, 0, mayordomo_listing, sizeof(mayordomo_listing) - 1);
    uint32_t rp = 0;
    agent_last_response[0] = '\0';
    fb_puts(agent_last_response, sizeof(agent_last_response), &rp, "[Mayordomo] Busqueda de archivos: ");
    fb_puts(agent_last_response, sizeof(agent_last_response), &rp, term);
    fb_puts(agent_last_response, sizeof(agent_last_response), &rp, "\n");
    const char *p = mayordomo_listing;
    const char *colon = mayordomo_find_ci(p, ": ");
    if (colon) p = colon + 2;
    int hits = 0;
    while (*p && rp < sizeof(agent_last_response) - 100) {
        char item[FS_NAME_MAX + 4];
        uint32_t ip = 0;
        while (*p == ' ') ++p;
        while (*p && *p != ',' && ip + 1 < sizeof(item)) item[ip++] = *p++;
        item[ip] = '\0';
        while (ip && item[ip - 1] == ' ') item[--ip] = '\0';
        if (ip && mayordomo_find_ci(item, term)) {
            fb_puts(agent_last_response, sizeof(agent_last_response), &rp, "  ");
            fb_puts(agent_last_response, sizeof(agent_last_response), &rp, item);
            fb_puts(agent_last_response, sizeof(agent_last_response), &rp, "\n");
            ++hits;
        }
        if (*p == ',') ++p;
    }
    if (!hits) fb_puts(agent_last_response, sizeof(agent_last_response), &rp, "Sin coincidencias en el sistema de archivos.\n");
    mayordomo_store_reply(agent_last_response, 1);
}

static int mayordomo_should_handle_base(const char *mission)
{
    int action = mayordomo_has(mission, "muestra") || mayordomo_has(mission, "mostrar") ||
        mayordomo_has(mission, "lista") || mayordomo_has(mission, "listar") ||
        mayordomo_has(mission, "busca") || mayordomo_has(mission, "buscar") ||
        mayordomo_has(mission, "encuentra") || mayordomo_has(mission, "localiza") ||
        mayordomo_has(mission, "crea") || mayordomo_has(mission, "crear") ||
        mayordomo_has(mission, "lee ") || mayordomo_has(mission, "leer") ||
        mayordomo_has(mission, "escribe") || mayordomo_has(mission, "escribir") ||
        mayordomo_has(mission, "elimina") || mayordomo_has(mission, "borrar") ||
        mayordomo_has(mission, "borra") || mayordomo_has(mission, "copia") ||
        mayordomo_has(mission, "copiar") || mayordomo_has(mission, "mueve") ||
        mayordomo_has(mission, "renombra") || mayordomo_has(mission, "consulta") ||
        mayordomo_has(mission, "dime") || mayordomo_has(mission, "indica") ||
        mayordomo_has(mission, "diagnostica") || mayordomo_has(mission, "comprueba") ||
        mayordomo_has(mission, "revisa") || mayordomo_has(mission, "cuanta") ||
        mayordomo_has(mission, "how much") || mayordomo_has(mission, "show ") ||
        mayordomo_has(mission, "list ") || mayordomo_has(mission, "find ") ||
        mayordomo_has(mission, "create ") || mayordomo_has(mission, "read ") ||
        mayordomo_has(mission, "inspect ");
    int object = mayordomo_has(mission, "ram") || mayordomo_has(mission, "memoria") ||
        mayordomo_has(mission, "heap") || mayordomo_has(mission, "disco") ||
        mayordomo_has(mission, "archivo") || mayordomo_has(mission, "archivos") ||
        mayordomo_has(mission, "fichero") || mayordomo_has(mission, "ficheros") ||
        mayordomo_has(mission, "carpeta") || mayordomo_has(mission, "directorio") ||
        mayordomo_has(mission, "almacenamiento") || mayordomo_has(mission, "espacio") ||
        mayordomo_has(mission, "inodo") || mayordomo_has(mission, "vfs") ||
        mayordomo_has(mission, "ramfs") || mayordomo_has(mission, "hardware") ||
        mayordomo_has(mission, "estado del sistema") || mayordomo_has(mission, "uptime") ||
        mayordomo_has(mission, "/workspace/") || mayordomo_has(mission, "/mis_archivos/") ||
        mayordomo_has(mission, "disk") || mayordomo_has(mission, "file ") ||
        mayordomo_has(mission, "folder ") || mayordomo_has(mission, "directory ") ||
        mayordomo_has(mission, "memory");
    /* No interceptar redaccion/generacion de documentos que requiera al LLM. */
    if ((mayordomo_has(mission, "redacta") || mayordomo_has(mission, "informe") ||
         mayordomo_has(mission, "escribe sobre") || mayordomo_has(mission, "contenido sobre") ||
         mayordomo_has(mission, "documento sobre") || mayordomo_has(mission, "write about") ||
         mayordomo_has(mission, "texto") || mayordomo_has(mission, "sobre ") ||
         mayordomo_has(mission, "contenido") || mayordomo_has(mission, "redactar") ||
         mayordomo_has(mission, "nota sobre")) &&
        !mayordomo_has(mission, "ram") && !mayordomo_has(mission, "disco")) return 0;
    return action && object;
}

/* MAYORDOMO_V2_KNOWLEDGE: conocimiento vivo de las herramientas del sistema.
 * El catalogo sale de la tabla de comandos del shell: cualquier comando nuevo
 * marcado CMD_F_SAFE queda disponible para Mayordomo sin modificar este archivo. */
#define MAYORDOMO_KNOWLEDGE_FILE "/mem/herramientas_sistema.txt"
#define MAYORDOMO_KNOWLEDGE_MAX  12288

/* Minusculas y sin tildes (UTF-8) para que "qué" y "que" sean equivalentes. */
static void mayordomo_fold(const char *in, char *out, uint32_t cap)
{
    uint32_t n = 0;
    while (*in && n + 1 < cap) {
        unsigned char c = (unsigned char)*in++;
        if (c == 0xC3 && *in) {
            unsigned char d = (unsigned char)*in++;
            char r = ' ';
            if (d == 0xA1 || d == 0x81) r = 'a';
            else if (d == 0xA9 || d == 0x89) r = 'e';
            else if (d == 0xAD || d == 0x8D) r = 'i';
            else if (d == 0xB3 || d == 0x93) r = 'o';
            else if (d == 0xBA || d == 0x9A || d == 0xBC || d == 0x9C) r = 'u';
            else if (d == 0xB1 || d == 0x91) r = 'n';
            out[n++] = r;
        } else if (c >= 'A' && c <= 'Z') {
            out[n++] = (char)(c + 32);
        } else {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

/* Peticiones que pertenecen a Buscador o a operaciones de un archivo concreto. */
static int mayordomo_blocked_context(const char *f)
{
    static const char *const blockers[] = {
        "noticia", "internet", " web", "google", "clima", "meteorolog",
        "archivo ", "fichero ", "carpeta ", ".txt", ".log", "/workspace", "/mis_archivos", 0
    };
    for (int i = 0; blockers[i]; ++i)
        if (mayordomo_find_ci(f, blockers[i])) return 1;
    return 0;
}

static int mayordomo_is_request(const char *f)
{
    static const char *const words[] = {
        "muestra", "mostrar", "ensena", "dime", "indica", "lista", "listar", "consulta",
        "comprueba", "revisa", "diagnostica", "cuant", "que ", "cual", "como ", "ver ",
        "dame", "necesito", "quiero", "hay ", "show", "check", "status", "estado",
        "verifica", "ejecuta", "haz ", "crea ", "sincroniza", "genera", 0
    };
    for (int i = 0; words[i]; ++i)
        if (mayordomo_find_ci(f, words[i])) return 1;
    return 0;
}

struct mayordomo_skill {
    const char *keys;      /* frases clave separadas por '|' (sin tildes) */
    const char *command;   /* comando nativo del shell a ejecutar */
    const char *label;
};

/* Tabla de lenguaje natural -> comando. Los comandos directos ('ps', 'pci'...) ya
 * funcionan sin tabla porque Mayordomo consulta cmd_table en vivo. */
static const struct mayordomo_skill MAYORDOMO_SKILLS[] = {
    { "arbol de memoria|memoria estructurada|memoria de los agentes|memoria cognitiva", "mem_tree", "Arbol de memoria de los agentes" },
    { "estado del sistema|resumen del sistema|informacion del sistema|info del sistema|diagnostico general|panorama general|como esta el sistema|como va el sistema", "sysinfo", "Resumen del estado del sistema" },
    { "procesos|proceso |hilos|threads|tareas en segundo plano|tabla de hilos", "ps", "Procesos e hilos del kernel" },
    { "uptime|tiempo encendido|tiempo de actividad|cuanto lleva encendido|cuanto tiempo lleva", "uptime", "Tiempo de actividad" },
    { "fecha|que hora es|hora del sistema|que dia es", "date", "Fecha y hora del sistema" },
    { "variables de entorno|variables del sistema", "env", "Variables de entorno" },
    { "dispositivos pci|bus pci|dispositivos conectados|hardware pci", "pci", "Dispositivos PCI" },
    { "tabla arp|cache arp", "arp", "Cache ARP" },
    { "estadisticas de red|trafico de red|paquetes de red", "stats", "Estadisticas de red" },
    { "la red|conectividad|direccion ip|tarjeta de red|configuracion de red", "status", "Estado de la red" },
    { "servidor llm|estado del llm|modelo de lenguaje", "health", "Estado del servidor LLM" },
    { "suite de pruebas|autotest|integridad del sistema|verifica el sistema|pruebas del sistema", "test_suite", "Suite de verificacion del kernel" },
    { "registros de la cpu|paginacion|cr0|cr3|cr4|modo largo|estado de la cpu", "mem", "Estado de CPU y paginacion" },
    { "arbol de archivos|estructura de archivos|arbol del disco|arbol de directorios", "tree", "Arbol de archivos" },
    { "historial de comandos|historial", "history", "Historial de comandos" },
    { "codigo fuente|fuentes del kernel|archivos fuente", "src_ls", "Fuentes del kernel" },
    { "fuentes tipograficas|tipografias|fuentes disponibles", "font", "Fuentes tipograficas" },
    { "punteros del raton|cursores del raton", "cursor", "Punteros del raton" },
    { "haz un checkpoint|crea un checkpoint|guarda un checkpoint|copia de seguridad del sistema de archivos", "checkpoint", "Checkpoint del sistema de archivos" },
    { "sincroniza el disco|sincroniza los archivos|fs-sync|vuelca a disco", "fs-sync", "Sincronizacion con el disco" },
    { "ficha del sistema|somafetch", "somafetch", "Ficha del sistema" },
    { "cuanto ocupan|uso del disco por archivo|que archivos ocupan", "du", "Uso de disco por archivo" },
    { 0, 0, 0 }
};

static const struct mayordomo_skill *mayordomo_skill_find(const char *f)
{
    for (int i = 0; MAYORDOMO_SKILLS[i].keys; ++i) {
        const char *k = MAYORDOMO_SKILLS[i].keys;
        while (*k) {
            char key[48];
            uint32_t n = 0;
            while (*k && *k != '|' && n + 1 < sizeof(key)) key[n++] = *k++;
            key[n] = '\0';
            if (*k == '|') ++k;
            if (n && mayordomo_find_ci(f, key)) return &MAYORDOMO_SKILLS[i];
        }
    }
    return 0;
}

static const struct mayordomo_skill *mayordomo_skill_for(const char *mission)
{
    char f[512];
    const struct mayordomo_skill *s;
    mayordomo_fold(mission, f, sizeof(f));
    if (mayordomo_blocked_context(f)) return 0;
    s = mayordomo_skill_find(f);
    if (!s || !mayordomo_is_request(f)) return 0;
    return s;
}

static int mayordomo_is_catalog_query(const char *mission)
{
    char f[512];
    mayordomo_fold(mission, f, sizeof(f));
    return mayordomo_starts_word(f, "ayuda") ||
           mayordomo_find_ci(f, "que puedes hacer") || mayordomo_find_ci(f, "que sabes hacer") ||
           mayordomo_find_ci(f, "que comandos") || mayordomo_find_ci(f, "comandos disponibles") ||
           mayordomo_find_ci(f, "lista de comandos") || mayordomo_find_ci(f, "catalogo") ||
           mayordomo_find_ci(f, "herramientas del sistema") || mayordomo_find_ci(f, "que herramientas") ||
           mayordomo_find_ci(f, "capacidades") || mayordomo_find_ci(f, "funcionalidades") ||
           mayordomo_find_ci(f, "que funciones");
}

/* Primer termino de la peticion que es un comando real del shell. */
static int mayordomo_command_token(const char *f, char *out, uint32_t cap)
{
    static const char *const skip[] = { "ayuda", "help", "comando", "comandos", "herramientas", 0 };
    while (*f) {
        uint32_t n = 0;
        while (*f && !((*f >= 'a' && *f <= 'z') || (*f >= '0' && *f <= '9') || *f == '_' || *f == '-')) ++f;
        while ((*f >= 'a' && *f <= 'z') || (*f >= '0' && *f <= '9') || *f == '_' || *f == '-') {
            if (n + 1 < cap) out[n] = *f;
            ++n; ++f;
        }
        if (n >= 2 && n < cap) {
            int ignore = 0;
            out[n] = '\0';
            for (int i = 0; skip[i]; ++i)
                if (mayordomo_starts_word(out, skip[i]) && out[mayordomo_lower(0) * 0 + 0] != '\0') {
                    uint32_t l = 0;
                    while (skip[i][l]) ++l;
                    if (l == n) ignore = 1;
                }
            if (!ignore && shell_command_info(out, 0, 0)) return 1;
        }
    }
    return 0;
}

static const char *mayordomo_ram_command(const char *mission)
{
    char f[512];
    mayordomo_fold(mission, f, sizeof(f));
    if (mayordomo_find_ci(f, "detall") || mayordomo_find_ci(f, "desglos") ||
        mayordomo_find_ci(f, "completo") || mayordomo_find_ci(f, "completa") ||
        mayordomo_find_ci(f, "a fondo")) return "ram -v";
    if (mayordomo_find_ci(f, "resumen") || mayordomo_find_ci(f, "breve") ||
        mayordomo_find_ci(f, "rapido") || mayordomo_find_ci(f, "corto") ||
        mayordomo_find_ci(f, "una linea")) return "ram -s";
    return "ram";
}

/* Regenera el catalogo de herramientas en /mem (solo escribe si ha cambiado). */
void agent_refresh_knowledge(void)
{
    uint32_t p = 0, n = 0;
    char *fresh = (char *)kmalloc(MAYORDOMO_KNOWLEDGE_MAX);
    char *old   = (char *)kmalloc(MAYORDOMO_KNOWLEDGE_MAX);
    if (!fresh || !old) {
        if (fresh) kfree(fresh);
        if (old) kfree(old);
        return;
    }
    fresh[0] = '\0';
    fb_puts(fresh, MAYORDOMO_KNOWLEDGE_MAX, &p,
            "HERRAMIENTAS DEL SISTEMA (generado automaticamente desde la tabla de comandos del shell)\n"
            "[A] = ejecutable por los agentes (SOMA y Mayordomo). El resto solo desde la terminal del usuario.\n"
            "Mayordomo ejecuta directamente cualquier comando [A] y entiende lenguaje natural sobre RAM, disco,\n"
            "archivos, procesos, red, PCI, fecha, uptime, variables de entorno y estado general del sistema.\n\n");
    shell_format_catalog(fresh + p, MAYORDOMO_KNOWLEDGE_MAX - p, 0, 1, 1);
    while (fresh[n]) ++n;
    {
        int r = vfs_read(MAYORDOMO_KNOWLEDGE_FILE, old, MAYORDOMO_KNOWLEDGE_MAX);
        if (r < 0 || (uint32_t)r != n || memcmp(old, fresh, n) != 0) {
            /* Sin hook: no queremos que el escritorio abra el Notepad por esto. */
            void (*hook)(const char *, const char *, uint32_t) = vfs_live_hook;
            vfs_live_hook = 0;
            vfs_write(MAYORDOMO_KNOWLEDGE_FILE, fresh, n);
            vfs_live_hook = hook;
        }
    }
    kfree(fresh);
    kfree(old);
}

static int mayordomo_should_handle(const char *mission)
{
    if (mayordomo_should_handle_base(mission)) return 1;
    if (mayordomo_skill_for(mission)) return 1;
    return mayordomo_is_catalog_query(mission);
}

static int mayordomo_try_catalog(const char *mission)
{
    char f[512], tok[32], cmd[48];
    uint32_t p = 0;
    if (!mayordomo_is_catalog_query(mission)) return 0;
    mayordomo_fold(mission, f, sizeof(f));
    if (mayordomo_command_token(f, tok, sizeof(tok))) {
        cmd[0] = '\0';
        fb_puts(cmd, sizeof(cmd), &p, "help ");
        fb_puts(cmd, sizeof(cmd), &p, tok);
        mayordomo_dispatch("Ayuda del comando", cmd);
        return 1;
    }
    {
        int detail = mayordomo_find_ci(f, "detalle") || mayordomo_find_ci(f, "descrip") ||
                     mayordomo_find_ci(f, "explica") || mayordomo_find_ci(f, "todos");
        agent_last_response[0] = '\0';
        fb_puts(agent_last_response, sizeof(agent_last_response), &p,
                "[Mayordomo] Herramientas del sistema que conozco (catalogo vivo del shell; los comandos nuevos se incorporan solos):\n");
        shell_format_catalog(agent_last_response + p, (uint32_t)sizeof(agent_last_response) - p - 220, 1, detail, 0);
        while (agent_last_response[p]) ++p;
        fb_puts(agent_last_response, sizeof(agent_last_response), &p,
                "Pidelo en lenguaje natural ('muestra la RAM', 'que procesos hay', 'busca un archivo llamado X') "
                "o con 'ayuda <comando>'.\n");
        mayordomo_store_reply(agent_last_response, 1);
    }
    return 1;
}

static int mayordomo_try_skill(const char *mission)
{
    const struct mayordomo_skill *s = mayordomo_skill_for(mission);
    if (!s) return 0;
    mayordomo_dispatch(s->label, s->command);
    return 1;
}

static int mayordomo_try_command(const char *mission)
{
    static const char *const excluded[] = { "find", "delegate", "vsoma", "gui", "source", "sh", "spawn", "bg", 0 };
    char name[32];
    uint32_t n = 0, flags = 0;

    if (mayordomo_starts_word(mission, "mkdir")) {
        const char *p = mission + 5;
        while (*p == ' ' || *p == '\t') ++p;
        mayordomo_make_folder(p);
        return 1;
    }
    while (mission[n] && mission[n] != ' ' && mission[n] != '\t' && n + 1 < sizeof(name)) {
        name[n] = mayordomo_lower(mission[n]);
        ++n;
    }
    name[n] = '\0';
    if (!n || (mission[n] && mission[n] != ' ' && mission[n] != '\t')) return 0;
    for (int i = 0; excluded[i]; ++i)
        if (mayordomo_starts_word(name, excluded[i])) return 0;
    if (!shell_command_info(name, &flags, 0)) return 0;
    if (!(flags & CMD_F_SAFE)) {
        char reply[200];
        uint32_t p = 0;
        reply[0] = '\0';
        fb_puts(reply, sizeof(reply), &p, "[Mayordomo] El comando '");
        fb_puts(reply, sizeof(reply), &p, name);
        fb_puts(reply, sizeof(reply), &p, "' no esta autorizado para agentes; ejecutalo tu desde la terminal.");
        mayordomo_store_reply(reply, 1);
        return 1;
    }
    mayordomo_dispatch("Comando nativo", mission);
    return 1;
}


static void mayordomo_run(const char *mission)
{
    while (*mission == ' ' || *mission == '\t') ++mission;
    if (!*mission) {
        mayordomo_store_reply("[Mayordomo] Especifica una tarea del sistema (RAM, disco, archivos, busqueda o carpetas).", 1);
        return;
    }
    agent_refresh_knowledge();
    if (mayordomo_try_command(mission)) return;
    if (mayordomo_try_catalog(mission)) return;
    if (mayordomo_try_skill(mission)) return;

    if (mayordomo_has(mission, "ram") || mayordomo_has(mission, "heap") ||
        mayordomo_has(mission, "memoria")) {
        mayordomo_dispatch("Telemetria de RAM y heap", mayordomo_ram_command(mission));
        return;
    }
    if (mayordomo_has(mission, "espacio") || mayordomo_has(mission, "almacenamiento") ||
        mayordomo_has(mission, "capacidad") || mayordomo_has(mission, "df")) {
        mayordomo_dispatch("Capacidad de disco y VFS", "df");
        return;
    }
    if ((mayordomo_has(mission, "busca") || mayordomo_has(mission, "buscar") ||
         mayordomo_has(mission, "encuentra") || mayordomo_has(mission, "localiza") ||
         mayordomo_has(mission, "find ")) &&
        (mayordomo_has(mission, "archivo") || mayordomo_has(mission, "fichero") ||
         mayordomo_has(mission, "file ") || mayordomo_has(mission, ".txt") ||
         mayordomo_has(mission, ".c") || mayordomo_has(mission, ".h") ||
         mayordomo_has(mission, ".log"))) {
        mayordomo_find_file(mission);
        return;
    }
    if ((mayordomo_has(mission, "crea") || mayordomo_has(mission, "crear") ||
         mayordomo_has(mission, "create ")) &&
        (mayordomo_has(mission, "carpeta") || mayordomo_has(mission, "directorio") ||
         mayordomo_has(mission, "folder ") || mayordomo_has(mission, "directory "))) {
        char value[FS_NAME_MAX + 8];
        if (mayordomo_extract_value(mission, value, sizeof(value))) mayordomo_make_folder(value);
        else mayordomo_store_reply("[Mayordomo] Indica el nombre de la carpeta que debo crear.", 1);
        return;
    }
    if ((mayordomo_has(mission, "elimina") || mayordomo_has(mission, "borrar") ||
         mayordomo_has(mission, "borra") || mayordomo_has(mission, "delete ") ||
         mayordomo_has(mission, "remove ")) &&
        (mayordomo_has(mission, "archivo") || mayordomo_has(mission, "fichero") ||
         mayordomo_has(mission, "ruta ") || mayordomo_has(mission, "/workspace/"))) {
        char value[FS_NAME_MAX + 8], path[FS_NAME_MAX + 8];
        if (!mayordomo_extract_value(mission, value, sizeof(value)) ||
            !mayordomo_resolve_path(value, path, sizeof(path))) {
            mayordomo_store_reply("[Mayordomo] Indica la ruta exacta del archivo a eliminar.", 1);
        } else if (!mayordomo_is_user_path(path)) {
            mayordomo_store_reply("[Mayordomo] Por seguridad solo elimino archivos de /workspace o /mis_archivos.", 1);
        } else if (vfs_delete(path) == 0) {
            char reply[256]; uint32_t p = 0;
            fb_puts(reply, sizeof(reply), &p, "[Mayordomo] Archivo eliminado: ");
            fb_puts(reply, sizeof(reply), &p, path);
            mayordomo_store_reply(reply, 1);
        } else {
            mayordomo_store_reply("[Mayordomo] No se pudo eliminar el archivo (ruta inexistente o protegida).", 1);
        }
        return;
    }
    if ((mayordomo_has(mission, "contenido del archivo") || mayordomo_has(mission, "contenido del fichero") ||
         mayordomo_has(mission, "lee archivo") || mayordomo_has(mission, "leer archivo") ||
         mayordomo_has(mission, "cat ")) &&
        (mayordomo_has(mission, ".txt") || mayordomo_has(mission, ".log") ||
         mayordomo_has(mission, ".c") || mayordomo_has(mission, "/workspace/") ||
         mayordomo_has(mission, "/mis_archivos/"))) {
        char value[FS_NAME_MAX + 8], path[FS_NAME_MAX + 8], command[FS_NAME_MAX + 16];
        if (!mayordomo_extract_value(mission, value, sizeof(value)) ||
            !mayordomo_resolve_path(value, path, sizeof(path))) {
            mayordomo_store_reply("[Mayordomo] Indica la ruta del archivo que debo leer.", 1);
        } else {
            uint32_t p = 0;
            fb_puts(command, sizeof(command), &p, "cat ");
            fb_puts(command, sizeof(command), &p, path);
            mayordomo_dispatch("Contenido del archivo", command);
        }
        return;
    }
    if ((mayordomo_has(mission, "crea") || mayordomo_has(mission, "crear") ||
         mayordomo_has(mission, "create ")) && mayordomo_has(mission, "archivo") &&
        !mayordomo_has(mission, "contenido") && !mayordomo_has(mission, "texto") &&
        !mayordomo_has(mission, "sobre ") && !mayordomo_has(mission, "informe")) {
        char value[FS_NAME_MAX + 8], path[FS_NAME_MAX + 8];
        if (!mayordomo_extract_value(mission, value, sizeof(value)) ||
            !mayordomo_resolve_path(value, path, sizeof(path))) {
            mayordomo_store_reply("[Mayordomo] Indica el nombre del archivo que debo crear.", 1);
        } else if (!mayordomo_is_user_path(path)) {
            mayordomo_store_reply("[Mayordomo] Por seguridad solo creo archivos en /workspace o /mis_archivos.", 1);
        } else if (vfs_write(path, "", 0) >= 0) {
            char reply[256]; uint32_t p = 0;
            fb_puts(reply, sizeof(reply), &p, "[Mayordomo] Archivo vacio creado: ");
            fb_puts(reply, sizeof(reply), &p, path);
            mayordomo_store_reply(reply, 1);
        } else {
            mayordomo_store_reply("[Mayordomo] No se pudo crear el archivo (VFS sin espacio o ruta demasiado larga).", 1);
        }
        return;
    }
    if (mayordomo_has(mission, "disco") || mayordomo_has(mission, "archivos") ||
        mayordomo_has(mission, "ficheros") || mayordomo_has(mission, "contenido del disco") ||
        mayordomo_has(mission, "listar") || mayordomo_has(mission, "lista los archivos") ||
        mayordomo_has(mission, "list files") || mayordomo_has(mission, "ls ")) {
        mayordomo_dispatch("Listado completo del sistema de archivos", "ls -a /");
        return;
    }
    if (mayordomo_has(mission, "sistema") || mayordomo_has(mission, "hardware") ||
        mayordomo_has(mission, "estado") || mayordomo_has(mission, "uptime")) {
        mayordomo_dispatch("Estado del sistema", "status");
        return;
    }
    mayordomo_store_reply("[Mayordomo] No he identificado la operacion. Usa una tarea concreta: 'muestra la RAM', 'muestra el contenido del disco', 'busca un archivo llamado X' o 'crea una carpeta llamada X'.", 1);
}

void agent_run(const char *agent_name, const char *mission)
{
    while (*mission == ' ') mission++;
    const char *requested_agent = agent_name ? agent_name : "soma";
    if (str_eq_ci(requested_agent, "mayordomo")) {
        mayordomo_run(mission);
        return;
    }
    if (str_eq_ci(requested_agent, "soma") && mayordomo_should_handle(mission)) {
        agent_run("mayordomo", mission);
        return;
    }
    const struct agent_profile *agent = resolve_agent(requested_agent);

    if (*mission == '\0') {
        kprint("Uso: "); kprint(agent->name); kprint(" <mision en lenguaje natural>\n");
        return;
    }

    /* Asignación dinámica para soportar delegación y respuestas completas sin truncamiento */
    char *agent_prompt_buf  = (char *)kmalloc(32768);
    char *agent_reply_buf   = (char *)kmalloc(32768);
    char *tool_feedback_buf = (char *)kmalloc(16384);
    char *history_buf       = (char *)kmalloc(16384);
    char *th                = (char *)kmalloc(32768);
    char *cmd_f             = (char *)kmalloc(32768);
    char *patch_f           = (char *)kmalloc(8192);
    char *verdict           = (char *)kmalloc(32768);

    if (!agent_prompt_buf || !agent_reply_buf || !tool_feedback_buf || !history_buf ||
        !th || !cmd_f || !patch_f || !verdict) {
        kprint("agent: error de memoria al inicializar contexto de agente.\n");
        if (agent_prompt_buf) kfree(agent_prompt_buf);
        if (agent_reply_buf) kfree(agent_reply_buf);
        if (tool_feedback_buf) kfree(tool_feedback_buf);
        if (history_buf) kfree(history_buf);
        if (th) kfree(th);
        if (cmd_f) kfree(cmd_f);
        if (patch_f) kfree(patch_f);
        if (verdict) kfree(verdict);
        return;
    }

    char agent_created_html[128];
    agent_created_html[0] = '\0';

    int requested_files_info = 0;
    if (find_substr(mission, "archivo") != 0 || find_substr(mission, "archivos") != 0 ||
        find_substr(mission, "disco") != 0 || find_substr(mission, "almacenamiento") != 0 ||
        find_substr(mission, "fichero") != 0 || find_substr(mission, "ficheros") != 0 ||
        find_substr(mission, "carpeta") != 0 || find_substr(mission, "carpetas") != 0 ||
        find_substr(mission, "directorio") != 0 || find_substr(mission, "directorios") != 0 ||
        find_substr(mission, "espacio") != 0 || find_substr(mission, "inodo") != 0 ||
        find_substr(mission, "inodos") != 0 || find_substr(mission, "vfs") != 0 ||
        find_substr(mission, "ramfs") != 0 || find_substr(mission, "file") != 0 ||
        find_substr(mission, "files") != 0 || find_substr(mission, "disk") != 0 ||
        find_substr(mission, "storage") != 0) {
        requested_files_info = 1;
    }

    kprint("\n\033[1;35m===============================================================\033[0m\n");
    kprint(" \033[1;36m");
    kprint(agent->display_title);
    kprint("\033[0m\n");
    kprint("\033[1;35m===============================================================\033[0m\n");
    kprint(" Mision: \033[1;37m");
    kprint(mission);
    kprint("\033[0m\n");

    uint32_t hist_len = 0;
    if (agent_resume_pending == 1) {
        agent_resume_pending = 0;
        fb_puts(history_buf, 8192, &hist_len, resume_history_stash);
    } else {
        history_buf[0] = '\0';
    }

    uint32_t ap_len = 0;
    int finished = 0;

    for (int step = 1; step <= 6; ++step) {
        ap_len = 0;
        struct rtc_time cur_t;
        rtc_get_datetime(&cur_t);
        uint32_t cur_y = cur_t.year, cur_mo = cur_t.month, cur_d = cur_t.day;
        uint32_t cur_h = cur_t.hour + 2;
        if (cur_h >= 24) { cur_h -= 24; cur_d += 1; }

        fb_puts(agent_prompt_buf, 16384, &ap_len, "IDENTIDAD: ");
        fb_puts(agent_prompt_buf, 16384, &ap_len, agent->role_desc);
        fb_puts(agent_prompt_buf, 16384, &ap_len, "\n\nMISION DEL USUARIO: '");
        const char *p_mval = mission;
        while (*p_mval && ap_len < 16384 - 2400) agent_prompt_buf[ap_len++] = *p_mval++;

        fb_puts(agent_prompt_buf, 16384, &ap_len, "'.\n"
                            "Entorno: SOMA OS Bare-Metal (" MYOS_ARCH_NAME ") | IP 10.0.2.15 | RamFS en /.\n"
                            "FECHA ACTUAL DEL SISTEMA: ");
        fb_put_dec(agent_prompt_buf, 16384, &ap_len, cur_y);
        fb_putc(agent_prompt_buf, 16384, &ap_len, '-');
        if (cur_mo < 10) fb_putc(agent_prompt_buf, 16384, &ap_len, '0');
        fb_put_dec(agent_prompt_buf, 16384, &ap_len, cur_mo);
        fb_putc(agent_prompt_buf, 16384, &ap_len, '-');
        if (cur_d < 10) fb_putc(agent_prompt_buf, 16384, &ap_len, '0');
        fb_put_dec(agent_prompt_buf, 16384, &ap_len, cur_d);
        fb_puts(agent_prompt_buf, 16384, &ap_len, " (CEST)\n\n");

        const char *p_ctx = "HERRAMIENTAS BARE-METAL DISPONIBLES:\n"
                            "  delegate <agente> \"<mision>\"  Delega a mayordomo, buscador o maquetador.\n"
                            "  search <consulta>         Busca informacion web en vivo (noticias, eventos, fechas, datos).\n"
                            "  mem_search <termino>      Busca conceptos o palabras clave en el arbol de memoria /mem/.\n"
                            "  mem_tree                  Muestra el arbol general de memoria estructurada.\n"
                            "  mem_read <nodo>           Lee el contenido de una rama/nodo del arbol de memoria.\n"
                            "  mem_write <nodo> \"<txt>\"  Actualiza o crea un nodo en el arbol de memoria.\n"
                            "  ls [-a] [ruta]            Lista los archivos del sistema RamFS (oculta archivos de sistema por defecto).\n"
                            "  cat <ruta>                Lee el contenido de un archivo.\n"
                            "  write <ruta> \"<texto>\"    Crea o sobrescribe un archivo con texto.\n"
                            "  touch <ruta> | rm <ruta>  Crea archivo vacio o elimina archivo.\n"
                            "  cp <src> <dst> | mv <src> <dst>\n"
                            "  grep [-i] [-l] <patron> [arch]\n"
                            "  ram [-s|-v] | df | sysinfo | status | uptime | date | env | find | du | stat | help [tema]\n"
                            "  ping <ip> | dns <host> | curl <host> [puerto] [ruta]\n"
                            "  src_ls | src_cat <f> [off] | src_grep <f> <pat>\n"
                            "\n"
                            "FORMATO OBLIGATORIO DE RESPUESTA JSON:\n"
                            "{\"thought\":\"analisis paso a paso\",\"action\":\"tool|patch|final\","
                            "\"cmd\":\"comando\",\"patch\":{\"file\":\"src/f.c\","
                            "\"search\":\"exacto\",\"replace\":\"nuevo\"},"
                            "\"verdict\":\"respuesta final si action=final\"}\n"
                            "\n"
                            "ROL DE ORQUESTACION: SOMA coordina; delega operaciones de sistema a mayordomo y no ejecuta directamente comandos de archivos/telemetria.\n"
                            "POLITICA DE DIRECTORIOS Y ESPACIOS DE TRABAJO:\n"
                            "1) RUTA PREFERENTE PARA AGENTES: Si creas notas, informes o borradores generados por la IA, usa preferentemente '/workspace/' (ej. '/workspace/notas.txt' o '/workspace/informe.txt') salvo que el usuario pida otra ruta explicita.\n"
                            "2) ARCHIVOS DE USUARIO: Los documentos y notas personales del usuario se encuentran en '/mis_archivos/'.\n"
                            "3) Si la mision pide crear/guardar/modificar un archivo tras buscar o procesar, DEBES ejecutar 'write <ruta> \"<contenido>\"' en un paso intermedio ANTES de emitir action='final'.\n"
                            "4) Solo emite action='final' cuando TODOS los objetivos de la mision esten completamente cumplidos en el hardware.\n";
        fb_puts(agent_prompt_buf, 16384, &ap_len, p_ctx);

        inject_agent_memory(agent, agent_prompt_buf, 16384, &ap_len);

        if (hist_len > 0) {
            const char *h_hdr = "\nAcciones ya ejecutadas en pasos anteriores:\n";
            while (*h_hdr) agent_prompt_buf[ap_len++] = *h_hdr++;
            for (uint32_t h = 0; h < hist_len && ap_len < 16384 - 600; ++h) {
                agent_prompt_buf[ap_len++] = history_buf[h];
            }
        }

        const char *p_rules = "\nInstruccion: Razona que falta para cumplir la mision y genera el objeto JSON.";
        while (*p_rules && ap_len < 16384 - 1) agent_prompt_buf[ap_len++] = *p_rules++;
        agent_prompt_buf[ap_len] = '\0';

        if (!llm_chat_json(agent_prompt_buf, agent_reply_buf, 32768, 60000)) {
            kprint("Error en comunicacion estructurada con el agente.\n");
            break;
        }

        char act[32];
        act[0] = '\0'; th[0] = '\0'; cmd_f[0] = '\0'; patch_f[0] = '\0'; verdict[0] = '\0';

        llm_json_get(agent_reply_buf, "action", act, sizeof(act));
        llm_json_get(agent_reply_buf, "thought", th, 32768);
        llm_json_get(agent_reply_buf, "cmd", cmd_f, 32768);
        llm_json_get(agent_reply_buf, "patch", patch_f, 8192);
        llm_json_get(agent_reply_buf, "verdict", verdict, 32768);

        /* Si el modelo produjo razonamiento en <think>...</think>, capturarlo */
        if (th[0] == '\0') {
            const char *t_start = find_substr(agent_reply_buf, "<think>");
            const char *t_end = find_substr(agent_reply_buf, "</think>");
            if (t_start) {
                t_start += 7;
                uint32_t tp = 0;
                while (*t_start && t_start != t_end && tp < 32767) {
                    th[tp++] = *t_start++;
                }
                th[tp] = '\0';
            }
        }

        if (th[0]) {
            gui_thought_update(th);
            kprint("\n  \033[1;35m+-------------------------------------------------------------+\033[0m\n");
            kprint("  \033[1;35m| [Paso ");
            kprint_dec((uint32_t)step);
            kprint(" : Razonamiento IA (");
            kprint(agent->name);
            kprint(")]\033[0m\n");
            kprint("  \033[1;35m+-------------------------------------------------------------+\033[0m\n");
            kprint("   ");
            kprint(th);
            kprint("\n");
        }

        /* Gestión de Parches de Kernel */
                if (find_substr(act, "patch")) {
            const char *ptext = patch_f[0] ? patch_f : agent_reply_buf;
            const char *pfile = find_substr(ptext, "FILE: src/");
            if (pfile) {
                const char *tf = pfile + 10;
                while (*tf == ' ') tf++;
                int is_c_src = (find_substr(tf, ".c") != 0 || find_substr(tf, ".h") != 0 || find_substr(tf, ".s") != 0);
                int is_workspace = (find_substr(tf, "workspace") != 0 || find_substr(tf, "mis_archivos") != 0 || find_substr(tf, ".txt") != 0);

                if (is_c_src && !is_workspace) {
                    if (is_patch_target_protected(tf)) {
                        kprint("\n[SEGURIDAD KERNEL] Modificacion denegada: Subsistema protegido.\n");
                        history_add_note(history_buf, &hist_len, 8192, step, "tu parche fue rechazado: los subsistemas del core estan PROTEGIDOS.");
                        continue;
                    }
                    kprint("\n[AGENTE AUTONOMO]: Parche de kernel C emitido. Enviando al host...\n");
                    if (agent_patch_attempts >= AGENT_MAX_PATCHES) {
                        kprint("\n[AGENTE AUTONOMO]: limite de parches por mision alcanzado.\n");
                        finished = 1;
                        break;
                    }
                    agent_state_save(agent->name, mission, history_buf, hist_len);
                    host_submit_patch(pfile);
                    vfs_delete(AGENT_STATE_FILE);
                    finished = 1;
                    break;
                }
            }
            kprint("\n[AVISO KERNEL]: 'patch' interceptado. Los archivos en RamFS (/workspace/) deben crearse con 'write'.\n");
            history_add_note(history_buf, &hist_len, 8192, step, "RECHAZADO: 'patch' es exclusivo para codigo del kernel (src/*.c). Para crear o editar archivos en RamFS o /workspace/ DEBES usar action='tool' con 'write <ruta> \"<contenido>\"'.");
            continue;
        }

        if (find_substr(act, "tool") || (cmd_f[0] != '\0' && !find_substr(act, "final"))) {
            const char *clean_cmd = cmd_f;
            while (*clean_cmd == ' ') clean_cmd++;

            kprint("\n  \033[1;33m[Accion Hardware] -> Ejecutando:\033[0m \033[1;32m$ ");
            kprint(clean_cmd);
            kprint("\033[0m\n");

            tool_feedback_buf[0] = '\0';
            dispatch_agent_command(clean_cmd, tool_feedback_buf, 16384);

            if (clean_cmd[0] == 'w' && clean_cmd[1] == 'r' && clean_cmd[2] == 'i') {
                const char *wp = clean_cmd + 5;
                while (*wp == ' ') wp++;
                char target_f[128]; uint32_t tfp = 0;
                while (*wp && *wp != ' ' && tfp < sizeof(target_f) - 1) target_f[tfp++] = *wp++;
                target_f[tfp] = '\0';
                if (find_substr(target_f, ".html") != 0 || find_substr(target_f, ".htm") != 0) {
                    uint32_t k = 0;
                    while (target_f[k] && k < sizeof(agent_created_html) - 1) {
                        agent_created_html[k] = target_f[k];
                        k++;
                    }
                    agent_created_html[k] = '\0';
                }
            }
            if (find_substr(clean_cmd, "ls") != 0 || find_substr(clean_cmd, "df") != 0 ||
                find_substr(clean_cmd, "tree") != 0) {
                requested_files_info = 1;
            }

            kprint("  \033[0;36m+--- [Resultado Hardware] ----------------------------------+\033[0m\n");
            kprint("  \033[0;36m| \033[0m");
            kprint(tool_feedback_buf);
            kprint("\n  \033[0;36m+-----------------------------------------------------------+\033[0m\n");

            uint32_t fb_len = 0;
            while (tool_feedback_buf[fb_len]) fb_len++;
            vfs_write("/tmp/last_tool.log", tool_feedback_buf, fb_len);

            fb_puts(history_buf, 8192, &hist_len, "- Paso ");
            fb_putc(history_buf, 8192, &hist_len, (char)('0' + step));
            fb_puts(history_buf, 8192, &hist_len, ": ejecutaste '");
            fb_puts(history_buf, 8192, &hist_len, clean_cmd);
            fb_puts(history_buf, 8192, &hist_len, "' -> Resultado: '");

            if (fb_len > 450) {
                for (uint32_t k = 0; k < 400 && hist_len < 8192 - 200; ++k) {
                    history_buf[hist_len++] = tool_feedback_buf[k];
                }
                fb_puts(history_buf, 8192, &hist_len, " [... salida resumida; completa en /tmp/last_tool.log ...]");
            } else {
                for (uint32_t k = 0; k < fb_len && hist_len < 8192 - 30; ++k) {
                    history_buf[hist_len++] = tool_feedback_buf[k];
                }
            }
            fb_puts(history_buf, 8192, &hist_len, "'\n");
            history_buf[hist_len] = '\0';
            continue;
        }

        /* Evaluación de Dictamen Final */
        if (find_substr(act, "final") || verdict[0] != '\0') {
            /* Manejador Dual: si emitió final pero incluyó un comando write en el mismo paso, ejecutarlo */
            if (cmd_f[0] != '\0') {
                const char *clean_cmd = cmd_f;
                while (*clean_cmd == ' ') clean_cmd++;
                kprint("\n  \033[1;33m[Accion Hardware Pre-Final] -> Ejecutando:\033[0m \033[1;32m$ ");
                kprint(clean_cmd);
                kprint("\033[0m\n");
                tool_feedback_buf[0] = '\0';
                dispatch_agent_command(clean_cmd, tool_feedback_buf, 16384);

                if (clean_cmd[0] == 'w' && clean_cmd[1] == 'r' && clean_cmd[2] == 'i') {
                    const char *wp = clean_cmd + 5;
                    while (*wp == ' ') wp++;
                    char target_f[128]; uint32_t tfp = 0;
                    while (*wp && *wp != ' ' && tfp < sizeof(target_f) - 1) target_f[tfp++] = *wp++;
                    target_f[tfp] = '\0';
                    if (find_substr(target_f, ".html") != 0 || find_substr(target_f, ".htm") != 0) {
                        uint32_t k = 0;
                        while (target_f[k] && k < sizeof(agent_created_html) - 1) {
                            agent_created_html[k] = target_f[k];
                            k++;
                        }
                        agent_created_html[k] = '\0';
                    }
                }
            }

            int demands_file = (find_substr(mission, "crea") != 0 ||
                                find_substr(mission, "crear") != 0 ||
                                find_substr(mission, "guarda") != 0 ||
                                find_substr(mission, "guardar") != 0 ||
                                find_substr(mission, "escribe") != 0 ||
                                find_substr(mission, "escribir") != 0 ||
                                find_substr(mission, "copiar su contenido") != 0 ||
                                find_substr(mission, "copia su contenido") != 0 ||
                                find_substr(mission, ".txt") != 0 ||
                                find_substr(mission, ".sh") != 0 ||
                                find_substr(mission, ".log") != 0);

            int file_written = (find_substr(history_buf, "ejecutaste 'write ") != 0 ||
                                find_substr(history_buf, "ejecutaste 'echo ") != 0 ||
                                find_substr(history_buf, "ejecutaste 'touch ") != 0 ||
                                find_substr(history_buf, " > ") != 0 ||
                                (cmd_f[0] == 'w' && cmd_f[1] == 'r' && cmd_f[2] == 'i'));

            if (demands_file && !file_written && step < 6) {
                kprint("\n>> [GUARDIA SOMA]: Bloqueada alucinacion. Falta escribir fisicamente el archivo.\n");
                history_add_note(history_buf, &hist_len, 8192, step, "RECHAZADO: La mision pide crear/guardar un archivo. Debes ejecutar 'write <ruta> \"<contenido>\"' antes de finalizar.");
                continue;
            }

            const char *v_final = verdict[0] ? verdict : (th[0] ? th : "Mision completada y cambios persistidos en el sistema.");
            kprint("\n\033[1;32m===============================================================\033[0m\n");
            kprint(" \033[1;32m[");
            kprint(agent->name);
            kprint("] - DICTAMEN FINAL\033[0m\n");
            kprint("\033[1;32m===============================================================\033[0m\n");
            kprint("  \033[1;36m");
            kprint(v_final);
            kprint("\033[0m\n\033[1;32m===============================================================\033[0m\n\n");

            stm_append_interaction(agent, mission, v_final);

            uint32_t alr_p = 0;
            if (th[0] != '\0' && verdict[0] != '\0') {
                fb_puts(agent_last_response, sizeof(agent_last_response), &alr_p, "[Razonamiento]:\n");
                fb_puts(agent_last_response, sizeof(agent_last_response), &alr_p, th);
                fb_puts(agent_last_response, sizeof(agent_last_response), &alr_p, "\n\n[Dictamen]:\n");
                fb_puts(agent_last_response, sizeof(agent_last_response), &alr_p, v_final);
            } else {
                fb_puts(agent_last_response, sizeof(agent_last_response), &alr_p, v_final);
            }

            char bb_update[512];
            uint32_t bbp = 0;
            fb_puts(bb_update, sizeof(bb_update), &bbp, "Ultimo dictamen de [");
            fb_puts(bb_update, sizeof(bb_update), &bbp, agent->name);
            fb_puts(bb_update, sizeof(bb_update), &bbp, "]: ");
            const char *vp = v_final;
            while (*vp && bbp < sizeof(bb_update) - 10) {
                if (*vp != '\n' && *vp != '\r') bb_update[bbp++] = *vp;
                vp++;
            }
            bb_update[bbp] = '\0';
            vfs_write("/agent/blackboard.txt", bb_update, bbp);

            finished = 1;
            break;
        }

        history_add_note(history_buf, &hist_len, 8192, step, "formato JSON no reconocido.");
    }

    if (!finished) {
        kprint("\n>> [SOMA AGENT]: Sintetizando dictamen con la informacion disponible...\n");
        ap_len = 0;
        fb_puts(agent_prompt_buf, 16384, &ap_len, "MISION: '");
        const char *pm = mission;
        while (*pm && ap_len < 16384 - 2200) agent_prompt_buf[ap_len++] = *pm++;
        fb_puts(agent_prompt_buf, 16384, &ap_len, "'\n\nHistorial de acciones previas:\n");
        for (uint32_t h = 0; h < hist_len && ap_len < 16384 - 800; ++h) {
            agent_prompt_buf[ap_len++] = history_buf[h];
        }
        fb_puts(agent_prompt_buf, 16384, &ap_len,
                "\nInstruccion: Responde de forma veraz y directa al usuario basandote estrictamente en los hechos ejecutados.");

        if (llm_chat(agent_prompt_buf, agent_reply_buf, 32768, 45000)) {
            kprint("\n\033[1;32m===============================================================\033[0m\n");
            kprint(" \033[1;32m[");
            kprint(agent->name);
            kprint("] - DICTAMEN FINAL\033[0m\n");
            kprint("\033[1;32m===============================================================\033[0m\n");
            kprint("  \033[1;36m");
            kprint(agent_reply_buf);
            kprint("\033[0m\n\033[1;32m===============================================================\033[0m\n\n");
            stm_append_interaction(agent, mission, agent_reply_buf);

            uint32_t alr_p = 0;
            while (agent_reply_buf[alr_p] && alr_p < sizeof(agent_last_response) - 1) {
                agent_last_response[alr_p] = agent_reply_buf[alr_p];
                alr_p++;
            }
            agent_last_response[alr_p] = '\0';

            char bb_update[512];
            uint32_t bbp = 0;
            fb_puts(bb_update, sizeof(bb_update), &bbp, "Ultimo dictamen de [");
            fb_puts(bb_update, sizeof(bb_update), &bbp, agent->name);
            fb_puts(bb_update, sizeof(bb_update), &bbp, "]: ");
            const char *vp = agent_reply_buf;
            while (*vp && bbp < sizeof(bb_update) - 10) {
                if (*vp != '\n' && *vp != '\r') bb_update[bbp++] = *vp;
                vp++;
            }
            bb_update[bbp] = '\0';
            vfs_write("/agent/blackboard.txt", bb_update, bbp);
        } else {
            kprint("\nMision finalizada.\n\n");
        }
    }

    /* Automatización visual: Abrir navegador si se generó web, o explorador si se consultó archivos/disco */
    if (agent_created_html[0] != '\0') {
        kprint("\n\033[1;36m[SOMA GUI] Abriendo pagina web generada en el navegador: ");
        kprint(agent_created_html);
        kprint("\033[0m\n");
        gui_open_browser(agent_created_html);
    } else if (requested_files_info) {
        kprint("\n\033[1;36m[SOMA GUI] Abriendo explorador de archivos del sistema (/workspace)...\033[0m\n");
        gui_open_file_manager("/workspace");
    }

    kfree(agent_prompt_buf);
    kfree(agent_reply_buf);
    kfree(tool_feedback_buf);
    kfree(history_buf);
    kfree(th);
    kfree(cmd_f);
    kfree(patch_f);
    kfree(verdict);
}

void hpatch_command(const char *arg)
{
    while (*arg == ' ') arg++;

    if (arg[0] == 'o' && arg[1] == 'k') {
        host_submit_patch("FILE: src/console.h\n<<<<<<< SEARCH\n#define MYOS_CONSOLE_H\n=======\n#define MYOS_CONSOLE_H\n/* hpatch ok */\n>>>>>>> REPLACE\n");
    } else if (arg[0] == 'b' && arg[1] == 'a' && arg[2] == 'd') {
        host_submit_patch("FILE: src/console.h\n<<<<<<< SEARCH\n#define MYOS_CONSOLE_H\n=======\n#define MYOS_CONSOLE_H\n#error hpatch_bad_test\n>>>>>>> REPLACE\n");
    } else if (arg[0] == 'b' && arg[1] == 'o' && arg[2] == 'o' && arg[3] == 't') {
        host_submit_patch("FILE: src/llm.c\n<<<<<<< SEARCH\nint llm_health(void)\n{\n=======\nint llm_health(void)\n{\n    return 0;\n>>>>>>> REPLACE\n");
    } else if (arg[0] == 'r' && arg[1] == 'a' && arg[2] == 'w') {
        arg += 3;
        while (*arg == ' ') arg++;
        uint32_t n = 0;
        while (*arg && n < sizeof(hpatch_buf) - 1) {
            if (arg[0] == '\\' && arg[1] == 'n') {
                hpatch_buf[n++] = '\n';
                arg += 2;
            } else {
                hpatch_buf[n++] = *arg++;
            }
        }
        hpatch_buf[n] = '\0';
        host_submit_patch(hpatch_buf);
    } else {
        kprint("Uso: hpatch ok | hpatch bad | hpatch bootfail | hpatch raw <texto con \\n>\n");
    }
}
