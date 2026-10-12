#!/usr/bin/env python3
"""
tools/estructurainterfaz.py
Genera 'estructurainterfaz.txt' con la estructura del proyecto y el subsistema
completo de la Interfaz Gráfica (Desktop Manager, Framebuffer 2D, BGA, Ratón y GUI).
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructurainterfaz.txt"
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
            marker = "  <-- [SUBSISTEMA INTERFAZ GRAFICA]" if any(
                k in entry.name.lower() for k in ["gui", "fb", "mouse", "font"]
            ) else ""
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
    print(f"[ESTRUCTURA] Analizando subsistema de Interfaz Gráfica (GUI) en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE INTERFAZ GRÁFICA (GUI)")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DE LA INTERFAZ GRÁFICA Y GESTOR DE ESCRITORIO")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                       HARDWARE & CONTROLADORES                         │
│   - Bochs BGA / QEMU VGA PCI (0x1234:0x1111) -> LFB en 0xE0000000     │
│   - Controlador PS/2 Ratón (0x64 / 0x60)     -> Paquetes 3 bytes dx/dy │
│   - PIT 8254 Timer / RTC Reloj CMOS          -> Parpadeo cursor & hora │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               MOTOR DE FRAMEBUFFER 2D (src/fb.c / .h)                  │
│   - Doble Búfer: Backbuffer en KHeap + VRAM física mapeada por VMM     │
│   - Daño sucio (Dirty Rects): fb_damage() y fb_present() a 60 FPS      │
│   - Scissor Clipping: fb_set_clip() / fb_reset_clip()                  │
│   - Primitivas: Rectángulos redondeados, alfa blend (div 255), degradado│
│   - Superposición de cursor: fb_cursor_show() compuesto en VRAM        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│          GESTOR DE ESCRITORIO Y VENTANAS (src/gui.c / .h)              │
│   - Riel Superior del Agente IA (SOMA_AGENT_BAR_H = 248 px)            │
│       -> Historial con scroll (UP/DOWN), entrada global 'soma >'       │
│   - Dock Lateral Izquierdo (420 px): 6 Aplicaciones integradas         │
│       -> [AI] Agente, [>_] Terminal, [WEB] Browser, [DIR] Archivos,    │
│          [IMG] Imágenes, [TXT] Notas                                   │
│   - Ventana Principal Activa: Viewport acoplado y recortado            │
│   - Barra de Sistema Inferior: Reloj digital RTC, estado, foco TAB     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   SUITE DE APLICACIONES GRÁFICAS                       │
│   1. Navegador Web (src/browser.c) -> Display list HTML & imágenes BMP │
│   2. Explorador de Archivos (src/fm.c) -> Listado VFS y preview        │
│   3. Visor de Imágenes (src/imgview.c) -> Zoom 50%-400% y centrado     │
│   4. Bloc de Notas (src/notepad.c) -> Edición 16 KiB y Live Typewriter │
│   5. Terminal Emulada (src/gui.c) -> Shell interactivo a color ANSI    │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA DEL GESTOR DE ESCRITORIO (src/gui.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("gui.h")) + "\n")

    sections.append("=" * 80)
    sections.append("4. NÚCLEO DEL ESCRITORIO Y EVENTOS (src/gui.c - FRAGMENTOS CLAVE)")
    sections.append("=" * 80)
    gui_code = read_file_safe(resolve_src("gui.c"))
    sections.append("--- [Estructura de Ventana y Constantes del Riel de IA / Dock] ---")
    geom_match = re.search(r'#define\s+SOMA_AGENT_BAR_H.*?#define\s+SOMA_DOCK_ICON_H\s+\d+u;', gui_code, re.DOTALL)
    sections.append(geom_match.group(0) if geom_match else "/* Geometría de ventana */")
    sections.append("\n--- [Función: render_desktop (Dibujado integral del escritorio)] ---")
    sections.append(extract_function(gui_code, "render_desktop") + "\n")
    sections.append("--- [Función: render_agent_global_rail (Riel superior del Agente)] ---")
    sections.append(extract_function(gui_code, "render_agent_global_rail") + "\n")
    sections.append("--- [Función: ensure_window_geom] ---")
    sections.append(extract_function(gui_code, "ensure_window_geom") + "\n")
    sections.append("--- [Función: gui_open_browser (Apertura automática de navegador)] ---")
    sections.append(extract_function(gui_code, "gui_open_browser") + "\n")
    sections.append("--- [Función: gui_open_file_manager (Apertura automática de explorador)] ---")
    sections.append(extract_function(gui_code, "gui_open_file_manager") + "\n")
    sections.append("--- [Función: cmd_vsoma (Bucle interactivo principal a 60 FPS)] ---")
    sections.append(extract_function(gui_code, "cmd_vsoma") + "\n")

    sections.append("=" * 80)
    sections.append("5. CABECERA DEL FRAMEBUFFER (src/fb.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("fb.h")) + "\n")

    sections.append("=" * 80)
    sections.append("6. CONTROLADOR DE VRAM Y PRIMITIVAS 2D (src/fb.c - FRAGMENTOS CLAVE)")
    sections.append("=" * 80)
    fb_code = read_file_safe(resolve_src("fb.c"))
    sections.append("--- [Función: fb_set_mode (Configuración BGA Bochs / QEMU TrueColor)] ---")
    sections.append(extract_function(fb_code, "fb_set_mode") + "\n")
    sections.append("--- [Función: fb_present (Doble Búfer y volcado por daño sucio)] ---")
    sections.append(extract_function(fb_code, "fb_present") + "\n")
    sections.append("--- [Función: fb_damage (Registro de rectángulos sucios)] ---")
    sections.append(extract_function(fb_code, "fb_damage") + "\n")
    sections.append("--- [Función: fb_blend_color (Composición con Canal Alfa)] ---")
    sections.append(extract_function(fb_code, "fb_blend_color") + "\n")
    sections.append("--- [Función: fb_fill_rounded_rect (Geometría moderna redondeada)] ---")
    sections.append(extract_function(fb_code, "fb_fill_rounded_rect") + "\n")

    sections.append("=" * 80)
    sections.append("7. CONTROLADOR DEL RATÓN PS/2 (src/mouse.h y src/mouse.c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/mouse.h ---")
    sections.append(read_file_safe(resolve_src("mouse.h")) + "\n")
    mouse_code = read_file_safe(resolve_src("mouse.c"))
    sections.append("--- [Función: mouse_poll (Lectura de paquetes hardware PS/2)] ---")
    sections.append(extract_function(mouse_code, "mouse_poll") + "\n")
    sections.append("--- [Función: mouse_init (Inicialización del dispositivo auxiliar)] ---")
    sections.append(extract_function(mouse_code, "mouse_init") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()