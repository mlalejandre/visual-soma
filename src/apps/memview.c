#include "memview.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "console.h"
#include "fs.h"
#include "mem.h"

#define MAX_MEM_ITEMS     24
#define MEM_PREVIEW_MAX   8192

enum mem_owner_id {
    OWNER_ALL = 0,
    OWNER_SOMA,
    OWNER_BUSCADOR,
    OWNER_COMUN
};

struct mem_tree_item {
    char        label[48];
    char        path[64];
    char        desc[64];
    uint8_t     owner;      /* OWNER_SOMA, OWNER_BUSCADOR, OWNER_COMUN */
    uint8_t     depth;      /* 0 = Grupo raiz, 1 = seccion, 2 = hoja */
    uint8_t     is_folder;  /* 1 si se expande/colapsa */
    uint8_t     expanded;   /* 1 si desplegado */
    int8_t      parent;     /* Indice del padre (-1 si raiz) */
    uint32_t    accent_col; /* Color distintivo */
};

static struct mem_tree_item items[MAX_MEM_ITEMS];
static int total_items = 0;

static int visible_map[MAX_MEM_ITEMS];
static int visible_count = 0;
static int selected_idx = 1;

static int active_filter = OWNER_ALL;
static char preview_buf[MEM_PREVIEW_MAX];
static int preview_len = 0;
static int text_scroll = 0;

static void str_copy_mv(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void add_item(const char *label, const char *path, const char *desc,
                     uint8_t owner, uint8_t depth, uint8_t is_folder,
                     uint8_t expanded, int8_t parent, uint32_t col)
{
    if (total_items >= MAX_MEM_ITEMS) return;
    struct mem_tree_item *it = &items[total_items++];
    str_copy_mv(it->label, label, sizeof(it->label));
    str_copy_mv(it->path, path, sizeof(it->path));
    str_copy_mv(it->desc, desc, sizeof(it->desc));
    it->owner = owner;
    it->depth = depth;
    it->is_folder = is_folder;
    it->expanded = expanded;
    it->parent = parent;
    it->accent_col = col;
}

static void rebuild_visible_list(void)
{
    visible_count = 0;
    for (int i = 0; i < total_items; ++i) {
        if (active_filter != OWNER_ALL && items[i].owner != active_filter) {
            continue;
        }
        int p = items[i].parent;
        int show = 1;
        while (p >= 0) {
            if (!items[p].expanded) { show = 0; break; }
            p = items[p].parent;
        }
        if (show) {
            visible_map[visible_count++] = i;
        }
    }
}

static void memview_load_content(void)
{
    text_scroll = 0;
    if (selected_idx < 0 || selected_idx >= total_items || items[selected_idx].path[0] == '\0') {
        preview_buf[0] = '\0';
        preview_len = 0;
        return;
    }

    int r = vfs_read(items[selected_idx].path, preview_buf, sizeof(preview_buf) - 1);
    if (r >= 0) {
        preview_len = r;
        preview_buf[preview_len] = '\0';
    } else {
        preview_buf[0] = '\0';
        preview_len = 0;
    }
}

void memview_init(void)
{
    total_items = 0;

    /* Rama 1: AGENTE SOMA */
    add_item("🤖 SOMA (Core Kernel)", "", "Inteligencia central y control del SO", OWNER_SOMA, 0, 1, 1, -1, 0x0038BDF8);
    add_item("Perfil Agente", "/agent/soma.txt", "Directivas, rol y seguridad del kernel", OWNER_SOMA, 1, 0, 0, 0, 0x0038BDF8);
    add_item("Memoria Reciente STM", "/agent/soma_stm.txt", "Ventana deslizante de ultimas conversaciones", OWNER_SOMA, 1, 0, 0, 0, 0x0038BDF8);
    add_item("Notas Workspace", "/workspace/soma.txt", "Informes y planes persistidos", OWNER_SOMA, 1, 0, 0, 0, 0x0038BDF8);
    add_item("Estado ReAct / Host", "/agent/state", "Transiciones y auto-modificaciones", OWNER_SOMA, 1, 0, 0, 0, 0x0038BDF8);

    /* Rama 2: AGENTE BUSCADOR */
    add_item("🔍 BUSCADOR (Research)", "", "Agente de navegacion web e investigacion", OWNER_BUSCADOR, 0, 1, 1, -1, 0x0010B981);
    add_item("Perfil Investigacion", "/agent/buscador.txt", "Especialidad en analisis y sintesis web", OWNER_BUSCADOR, 1, 0, 0, 5, 0x0010B981);
    add_item("Memoria Reciente STM", "/agent/buscador_stm.txt", "Consultas y respuestas recientes", OWNER_BUSCADOR, 1, 0, 0, 5, 0x0010B981);
    add_item("Notas Workspace", "/workspace/buscador.txt", "Resumenes y fuentes recopiladas", OWNER_BUSCADOR, 1, 0, 0, 5, 0x0010B981);

    /* Rama 3: MEMORIA COMÚN / COMPARTIDA */
    add_item("🌐 Memoria Comun / Sistema", "", "Conocimiento global y sincronizacion", OWNER_COMUN, 0, 1, 1, -1, 0x00F59E0B);
    add_item("Pizarra IPC (Blackboard)", "/agent/blackboard.txt", "Consenso y acuerdos compartidos", OWNER_COMUN, 1, 0, 0, 9, 0x00F59E0B);
    add_item("Perfil Usuario", "/etc/mem_user.txt", "Preferencias del administrador Marcos", OWNER_COMUN, 1, 0, 0, 9, 0x00F59E0B);
    add_item("Entorno Hardware", "/etc/mem_hw.txt", "Telemetria de CPU, RAM y bus VirtIO", OWNER_COMUN, 1, 0, 0, 9, 0x00F59E0B);

    /* Sub-rama: Árbol Semántico (/mem/) */
    add_item("Arbol Semantico (/mem)", "/mem/indice.txt", "Grafo de conceptos BM25 en memoria", OWNER_COMUN, 1, 1, 1, 9, 0x00A855F7);
    add_item("Indice de Materias", "/mem/indice.txt", "Estructura general de temas", OWNER_COMUN, 2, 0, 0, 13, 0x00A855F7);
    add_item("Ciencia: Computacion", "/mem/ciencia_computacion.txt", "Arquitectura x86_64 y Ring 0", OWNER_COMUN, 2, 0, 0, 13, 0x00A855F7);
    add_item("Ciencia: Fisica", "/mem/ciencia_fisica.txt", "Mecanica, termodinamica y cuantica", OWNER_COMUN, 2, 0, 0, 13, 0x00A855F7);
    add_item("Historia: Antigua", "/mem/historia_antigua.txt", "Civilizaciones clasicas y filosofia", OWNER_COMUN, 2, 0, 0, 13, 0x00A855F7);
    add_item("Historia: Moderna", "/mem/historia_moderna.txt", "Revolucion industrial y era digital", OWNER_COMUN, 2, 0, 0, 13, 0x00A855F7);
    add_item("Conversaciones Clave", "/mem/conversaciones.txt", "Registro de conclusiones con el usuario", OWNER_COMUN, 2, 0, 0, 13, 0x00A855F7);

    active_filter = OWNER_ALL;
    selected_idx = 1; /* Seleccionar /agent/soma.txt */
    rebuild_visible_list();
    memview_load_content();
}

const char *memview_get_selected_path(void)
{
    if (selected_idx >= 0 && selected_idx < total_items) {
        return items[selected_idx].path;
    }
    return "/agent/blackboard.txt";
}

void memview_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    (void)show_cursor;

    /* Sombra y contenedor de ventana */
    fb_fill_rect(win_x + 8, win_y + 8, win_w, win_h, 0x00050810);
    fb_fill_rect(win_x, win_y, win_w, win_h, 0x000F172A);
    fb_draw_rect(win_x, win_y, win_w, win_h, 0x00A855F7);

    /* Barra de título */
    fb_fill_rect(win_x, win_y, win_w, 36, 0x001E293B);
    fb_draw_rect(win_x, win_y, win_w, 36, 0x00334155);

    /* Botón cerrar [X] */
    uint32_t btn_close_x = win_x + 12;
    uint32_t btn_close_y = win_y + 10;
    fb_fill_rect(btn_close_x, btn_close_y, 16, 16, 0x00EF4444);
    fb_draw_rect(btn_close_x, btn_close_y, 16, 16, 0x00FFFFFF);

    font_draw_string(win_x + 76, win_y + 14,
                     "SOMA Agent Memory Tree :: Visor Jerarquico de Memoria Cognitiva",
                     0x00F8FAFC, 1);

    /* Barra de filtros / pestañas (Toolbar) */
    uint32_t tool_y = win_y + 42;
    uint32_t tool_h = 36;
    fb_fill_rect(win_x + 14, tool_y, win_w - 28, tool_h, 0x0004070E);
    fb_draw_rect(win_x + 14, tool_y, win_w - 28, tool_h, 0x001E293B);

    font_draw_string(win_x + 24, tool_y + 14, "Filtro:", 0x0094A3B8, 1);

    const char *tab_names[] = { "Todos", "SOMA (Core)", "Buscador", "Comun / Red" };
    uint32_t tab_x = win_x + 78;
    for (int t = 0; t < 4; ++t) {
        int is_act = (active_filter == t);
        uint32_t bg = is_act ? 0x007C3AED : 0x001E293B;
        uint32_t border = is_act ? 0x00C084FC : 0x00334155;
        uint32_t tw = 86;
        fb_fill_rounded_rect(tab_x, tool_y + 5, tw, 26, 4, bg);
        fb_draw_rounded_rect(tab_x, tool_y + 5, tw, 26, 4, border);
        font_draw_string(tab_x + 8, tool_y + 13, tab_names[t], is_act ? 0x00FFFFFF : 0x0094A3B8, 1);
        tab_x += tw + 8;
    }

    /* Botón actualizar en Toolbar */
    uint32_t b_ref_x = win_x + win_w - 120;
    fb_fill_rounded_rect(b_ref_x, tool_y + 5, 96, 26, 4, 0x001E293B);
    fb_draw_rounded_rect(b_ref_x, tool_y + 5, 96, 26, 4, 0x0010B981);
    font_draw_string(b_ref_x + 12, tool_y + 13, "Actualizar", 0x0010B981, 1);

    /* División de dos paneles */
    uint32_t body_y = win_y + 86;
    uint32_t body_h = win_h - 132;
    uint32_t tree_w = 290;

    /* PANEL IZQUIERDO: ÁRBOL JERÁRQUICO */
    uint32_t tree_x = win_x + 14;
    fb_fill_rect(tree_x, body_y, tree_w, body_h, 0x00080D15);
    fb_draw_rect(tree_x, body_y, tree_w, body_h, 0x001E293B);

    fb_fill_rect(tree_x + 2, body_y + 2, tree_w - 4, 26, 0x00161F30);
    font_draw_string(tree_x + 12, body_y + 10, "ESTRUCTURA DE MEMORIA", 0x00C084FC, 1);

    fb_set_clip((int32_t)tree_x, (int32_t)(body_y + 30), (int32_t)tree_w, (int32_t)(body_h - 32));

    uint32_t row_y = body_y + 34;
    for (int k = 0; k < visible_count; ++k) {
        int idx = visible_map[k];
        struct mem_tree_item *it = &items[idx];

        int is_sel = (idx == selected_idx);
        if (is_sel) {
            fb_fill_rounded_rect(tree_x + 4, row_y - 2, tree_w - 8, 20, 3, 0x002E1065);
            fb_draw_rounded_rect(tree_x + 4, row_y - 2, tree_w - 8, 20, 3, 0x00A855F7);
        }

        uint32_t indent = tree_x + 10 + (uint32_t)(it->depth * 14);

        if (it->is_folder) {
            const char *f_mark = it->expanded ? "[-]" : "[+]";
            font_draw_string(indent, row_y + 2, f_mark, it->accent_col, 1);
            font_draw_string(indent + 24, row_y + 2, it->label, is_sel ? 0x00FFFFFF : 0x00E2E8F0, 1);
        } else {
            font_draw_string(indent, row_y + 2, "*", it->accent_col, 1);
            font_draw_string(indent + 12, row_y + 2, it->label, is_sel ? 0x00F8FAFC : 0x0094A3B8, 1);
        }

        row_y += 22;
    }
    fb_reset_clip();

    /* PANEL DERECHO: INSPECTOR DE CONTENIDO */
    uint32_t insp_x = tree_x + tree_w + 10;
    uint32_t insp_w = win_w - tree_w - 38;

    fb_fill_rect(insp_x, body_y, insp_w, body_h, 0x00020617);
    fb_draw_rect(insp_x, body_y, insp_w, body_h, 0x001F2937);

    /* Encabezado del nodo seleccionado */
    fb_fill_rect(insp_x + 2, body_y + 2, insp_w - 4, 34, 0x000F172A);
    fb_draw_rect(insp_x + 2, body_y + 2, insp_w - 4, 34, 0x001E293B);

    if (selected_idx >= 0 && selected_idx < total_items) {
        struct mem_tree_item *cur = &items[selected_idx];

        /* Badge del propietario */
        const char *owner_tag = (cur->owner == OWNER_SOMA) ? "[ SOMA ]" :
                                (cur->owner == OWNER_BUSCADOR) ? "[ BUSCADOR ]" : "[ COMUN ]";
        font_draw_string(insp_x + 12, body_y + 12, owner_tag, cur->accent_col, 1);

        /* Ruta del archivo */
        const char *p_disp = cur->path[0] ? cur->path : "(Rama Organizativa)";
        font_draw_string(insp_x + 116, body_y + 12, p_disp, 0x00F8FAFC, 1);

        /* Botón de acción: [ABRIR EN NOTEPAD] */
        if (cur->path[0]) {
            uint32_t b_np_x = insp_x + insp_w - 146;
            fb_fill_rounded_rect(b_np_x, body_y + 6, 136, 24, 4, 0x000284C7);
            fb_draw_rounded_rect(b_np_x, body_y + 6, 136, 24, 4, 0x0038BDF8);
            font_draw_string(b_np_x + 8, body_y + 12, "ABRIR EN NOTEPAD", 0x00FFFFFF, 1);
        }
    }

    /* Área de texto con visor y numeración de línea */
    uint32_t text_y = body_y + 44;
    uint32_t text_h = body_h - 48;

    fb_set_clip((int32_t)insp_x, (int32_t)text_y, (int32_t)insp_w, (int32_t)text_h);

    if (preview_len > 0) {
        const char *p = preview_buf;
        int line_no = 1;
        uint32_t line_y = text_y + 6 - (uint32_t)text_scroll;

        while (*p) {
            char num_str[8];
            num_str[0] = (char)('0' + (line_no / 10) % 10);
            num_str[1] = (char)('0' + (line_no % 10));
            num_str[2] = ' '; num_str[3] = '|'; num_str[4] = ' '; num_str[5] = '\0';

            char line_text[128];
            uint32_t lp = 0;
            while (*p && *p != '\n' && lp < sizeof(line_text) - 1) {
                if (*p != '\r') line_text[lp++] = *p;
                p++;
            }
            line_text[lp] = '\0';
            if (*p == '\n') p++;

            if (line_y + 14 >= text_y && line_y < text_y + text_h) {
                font_draw_string(insp_x + 12, line_y, num_str, 0x00475569, 1);
                font_draw_string(insp_x + 56, line_y, line_text, 0x00E2E8F0, 1);
            }

            line_y += 16;
            line_no++;
        }
    } else {
        const char *msg = (selected_idx >= 0 && items[selected_idx].path[0])
            ? "(Nodo de memoria vacio o pendiente de inicializar)"
            : "(Selecciona una hoja del arbol para inspeccionar su contenido)";
        font_draw_string(insp_x + 24, text_y + 24, msg, 0x0064748B, 1);
    }
    fb_reset_clip();

    /* BARRA INFERIOR DE ESTADO */
    uint32_t st_y = win_y + win_h - 36;
    fb_fill_rect(win_x, st_y, win_w, 36, 0x000B0F19);
    fb_draw_rect(win_x, st_y, win_w, 36, 0x001E293B);

    if (selected_idx >= 0 && selected_idx < total_items) {
        font_draw_string(win_x + 16, st_y + 14, items[selected_idx].desc, 0x00C084FC, 1);

        char sz_info[48];
        uint32_t sip = 0;
        const char *lbl = "Tamano: ";
        while (*lbl) sz_info[sip++] = *lbl++;
        uint32_t v = (uint32_t)preview_len;
        char tb[10]; int tn = 0;
        if (v == 0) sz_info[sip++] = '0';
        else { while (v) { tb[tn++] = '0' + (v % 10); v /= 10; } while (tn) sz_info[sip++] = tb[--tn]; }
        const char *lbl2 = " Bytes";
        while (*lbl2) sz_info[sip++] = *lbl2++;
        sz_info[sip] = '\0';
        font_draw_string(win_x + win_w - 220, st_y + 14, sz_info, 0x0094A3B8, 1);
    }
}

int memview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    /* Botón cerrar [X] */
    if (point_in_rect(mx, my, win_x + 8, win_y + 6, 26, 26)) {
        return 1;
    }

    uint32_t tool_y = win_y + 42;

    /* Pestañas de filtrado */
    uint32_t tab_x = win_x + 78;
    for (int t = 0; t < 4; ++t) {
        if (point_in_rect(mx, my, tab_x, tool_y + 5, 86, 26)) {
            active_filter = t;
            rebuild_visible_list();
            return 0;
        }
        tab_x += 86 + 8;
    }

    /* Botón refrescar */
    uint32_t b_ref_x = win_x + win_w - 120;
    if (point_in_rect(mx, my, b_ref_x, tool_y + 5, 96, 26)) {
        memview_load_content();
        return 0;
    }

    /* Filas del árbol en panel izquierdo */
    uint32_t body_y = win_y + 86;
    uint32_t body_h = win_h - 132;
    uint32_t tree_w = 290;
    uint32_t tree_x = win_x + 14;

    if (point_in_rect(mx, my, tree_x, body_y + 30, tree_w, body_h - 32)) {
        int clicked_row = (my - (int32_t)(body_y + 34)) / 22;
        if (clicked_row >= 0 && clicked_row < visible_count) {
            int idx = visible_map[clicked_row];
            if (items[idx].is_folder) {
                items[idx].expanded = !items[idx].expanded;
                rebuild_visible_list();
            }
            selected_idx = idx;
            memview_load_content();
            return 0;
        }
    }

    /* Botón [ABRIR EN NOTEPAD] en panel derecho */
    uint32_t insp_x = tree_x + tree_w + 10;
    uint32_t insp_w = win_w - tree_w - 38;
    uint32_t b_np_x = insp_x + insp_w - 146;

    if (selected_idx >= 0 && items[selected_idx].path[0]) {
        if (point_in_rect(mx, my, b_np_x, body_y + 6, 136, 24)) {
            return 4; /* Código 4: Abrir archivo en Notepad */
        }
    }

    /* Scroll en panel de contenido al hacer clic superior/inferior */
    uint32_t text_y = body_y + 44;
    uint32_t text_h = body_h - 48;
    if (point_in_rect(mx, my, insp_x, text_y, insp_w, text_h)) {
        if (my < (int32_t)(text_y + 30) && text_scroll > 0) {
            text_scroll -= 32;
            if (text_scroll < 0) text_scroll = 0;
        } else if (my > (int32_t)(text_y + text_h - 30)) {
            text_scroll += 32;
        }
    }

    return 0;
}

void memview_handle_key(char c)
{
    if (c == 16) { /* Flecha arriba: seleccionar fila anterior */
        int cur_row = -1;
        for (int k = 0; k < visible_count; ++k) {
            if (visible_map[k] == selected_idx) { cur_row = k; break; }
        }
        if (cur_row > 0) {
            selected_idx = visible_map[cur_row - 1];
            memview_load_content();
        }
    } else if (c == 14) { /* Flecha abajo: seleccionar siguiente fila */
        int cur_row = -1;
        for (int k = 0; k < visible_count; ++k) {
            if (visible_map[k] == selected_idx) { cur_row = k; break; }
        }
        if (cur_row >= 0 && cur_row < visible_count - 1) {
            selected_idx = visible_map[cur_row + 1];
            memview_load_content();
        }
    } else if (c == ' ' || c == 6) { /* Scroll hacia abajo en texto */
        text_scroll += 32;
    } else if (c == 2) { /* Scroll hacia arriba en texto */
        if (text_scroll > 32) text_scroll -= 32;
        else text_scroll = 0;
    }
}
