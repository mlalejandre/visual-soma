#!/usr/bin/env python3
"""
tools/estructuranavegador.py
Genera 'estructuranavegador.txt' con la estructura del proyecto y el subsistema del navegador web.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructuranavegador.txt"
EXCLUDED_DIRS = {".git", "build", ".idea", ".vscode", "__pycache__", ".parche_backup", "snapshots"}


def build_tree(root_dir: Path) -> str:
    lines = [f"{root_dir.name}/"]

    def walk(directory: Path, prefix: str = ""):
        entries = sorted(
            [e for e in directory.iterdir() if e.name not in EXCLUDED_DIRS],
            key=lambda x: (not x.is_dir(), x.name.lower())
        )
        total = len(entries)
        for i, entry in enumerate(entries):
            is_last = (i == total - 1)
            connector = "└── " if is_last else "├── "
            marker = "  <-- [SUBSISTEMA NAVEGADOR WEB]" if any(k in entry.name.lower() for k in ["browser", "http", "proxy"]) else ""
            lines.append(f"{prefix}{connector}{entry.name}{marker}")
            if entry.is_dir():
                walk(entry, prefix + ("    " if is_last else "│   "))

    walk(root_dir)
    return "\n".join(lines)


def resolve_src(sub_or_name: str) -> Path:
    p = SRC / sub_or_name
    if p.exists():
        return p
    target_name = Path(sub_or_name).name
    matches = list(SRC.rglob(target_name))
    return matches[0] if matches else p

def read_file_safe(path: Path) -> str:
    if not path.exists():
        return f"[ARCHIVO NO ENCONTRADO: {path}]"
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="latin-1", errors="replace")


def extract_function(code: str, func_name: str) -> str:
    pattern = rf'(?:static\s+|inline\s+|const\s+|int\s+|void\s+|\w+\s+)*\b{func_name}\s*\([^)]*\)\s*\{{'
    match = re.search(pattern, code)
    if not match:
        return f"/* Función '{func_name}' no encontrada */"
    start_idx = match.start()
    brace_start = match.end() - 1
    depth = 0
    end_idx = len(code)
    for i in range(brace_start, len(code)):
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
            if depth == 0:
                end_idx = i + 1
                break
    return code[start_idx:end_idx]


def generate_report():
    print(f"[ESTRUCTURA] Analizando subsistema del Navegador Web en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DEL NAVEGADOR WEB")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DEL MOTOR WEB BARE-METAL")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                        ORIGEN DE PETICIÓN WEB                          │
│   - Barra de URL interactiva en ventana gráfica                        │
│   - Clic del ratón en enlace <a href="..."> o imagen linkeada          │
│   - Disparo automático de IA (cuando el agente genera un archivo .html)│
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ browser_load_url(url)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│             HILO KTHREAD DEDICADO ('web_worker' en thread.c)           │
│   - Archivos locales (file:/// o /workspace/*.html):                   │
│       -> vfs_read() instantáneo desde SOMAFS02 en KHeap                │
│   - Peticiones remotas HTTP/HTTPS:                                     │
│       -> Gateway SLIRP 10.0.2.2:8095 (search_proxy.py)                 │
│       -> DNS dinámico (10.0.2.3) y socket TCP bare-metal (tcp.c)       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ parse_html_to_display_list()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│             MOTOR DE RENDERIZADO Y DISPLAY LIST (src/browser.c)        │
│   - Árbol de elementos gráficos: texto, <hr>, imágenes TrueColor       │
│   - Decodificación y cache de imágenes BMP 24/32 bpp (image.c)         │
│   - Soporte de estilos CSS básicos (color, background-color)           │
│   - Enlaces interactivos con cálculo de coordenadas relativas de clic  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ browser_render_window()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                 CAPA GRÁFICA Y FRAMEBUFFER (src/fb.c)                  │
│   - fb_set_clip() / fb_reset_clip() -> Viewport con recorte hardware   │
│   - font_draw_string() -> Renderizado con la fuente activa             │
│   - fb_present() / fb_present_full() -> Volcado atómico en VRAM HD     │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA DEL NAVEGADOR (src/browser.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("browser.h")) + "\n")

    sections.append("=" * 80)
    sections.append("4. NÚCLEO DEL MOTOR WEB (src/browser.c - FRAGMENTOS CLAVE)")
    sections.append("=" * 80)
    b_code = read_file_safe(resolve_src("browser.c"))
    sections.append("--- [Estructuras del Display List y Enlaces] ---")
    elem_match = re.search(r'struct web_element\s*\{.*?\}\s*;.*?struct web_link\s*\{.*?\}\s*;', b_code, re.DOTALL)
    sections.append(elem_match.group(0) if elem_match else "/* struct web_element */")
    sections.append("\n--- [Función: parse_html_to_display_list] ---")
    sections.append(extract_function(b_code, "parse_html_to_display_list") + "\n")
    sections.append("--- [Función: browser_worker_task (Descarga Asíncrona KThread)] ---")
    sections.append(extract_function(b_code, "browser_worker_task") + "\n")
    sections.append("--- [Función: browser_render_window (Dibujado en Framebuffer)] ---")
    sections.append(extract_function(b_code, "browser_render_window") + "\n")
    sections.append("--- [Función: browser_handle_click (Interacción y Enlaces)] ---")
    sections.append(extract_function(b_code, "browser_handle_click") + "\n")

    sections.append("=" * 80)
    sections.append("5. INTEGRACIÓN EN EL ENTORNO DE ESCRITORIO (src/gui.c)")
    sections.append("=" * 80)
    gui_code = read_file_safe(resolve_src("gui.c"))
    sections.append("--- [Función: gui_open_browser (Apertura y Conmutación en Caliente)] ---")
    sections.append(extract_function(gui_code, "gui_open_browser") + "\n")

    sections.append("=" * 80)
    sections.append("6. PROXY MULTIMEDIA Y SANITIZADOR WEB (tools/search_proxy.py)")
    sections.append("=" * 80)
    proxy_code = read_file_safe(ROOT / "tools" / "search_proxy.py")
    sections.append("--- [Función: clean_html_for_soma] ---")
    sections.append(extract_function(proxy_code, "clean_html_for_soma") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()