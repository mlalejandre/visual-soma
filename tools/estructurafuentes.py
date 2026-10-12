#!/usr/bin/env python3
"""
tools/estructurafuentes.py
Genera 'estrucuturafuentes.txt' con la estructura del proyecto y el subsistema de fuentes.
"""

from pathlib import Path
import re
import sys


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    if (here / "src").is_dir():
        return here
    if (here.parent / "src").is_dir():
        return here.parent
    return here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estrucuturafuentes.txt"

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
            marker = "  <-- [SUBSISTEMA FUENTES]" if "font" in entry.name.lower() else ""
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
    print(f"[ESTRUCTURA] Analizando proyecto SOMA en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE FUENTES TIPOGRAFICAS")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. FLUJO DE DATOS Y ARQUITECTURA DE FUENTES")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                        ENTRADA DE CONTROL / SHELL                      │
│   soma> font                  -> Lista catálogo de fuentes instaladas  │
│   soma> font modern / font 1  -> Conmuta en caliente a fuente moderna  │
│   soma> font classic / font 0 -> Conmuta en caliente a fuente clásica  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ cmd_font() en src/shell.c
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               GESTOR CENTRAL DE FUENTES (src/gui_font.c / .h)           │
│   - current_font_id           -> Índice de la fuente activa            │
│   - font_set()                -> Conmuta fuente por ID                 │
│   - font_set_by_name()        -> Conmuta fuente por cadena             │
│   - font_get_glyph(c)         -> Resuelve matriz 8x8 según activa      │
│                                                                        │
│   Catálogo Extensible SOMA_FONTS[]:                                   │
│   ├─► [0] FONT_CLASSIC: src/fonts/font_classic.h (IBM/BIOS Retro 8x8) │
│   └─► [1] FONT_MODERN:  src/fonts/font_modern.h  (Sans Cyber 8x8, 0) │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ font_get_glyph()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     SUBSISTEMAS CONSUMIDORES                           │
│   1. Framebuffer 2D HD (src/fb.c):                                     │
│      - fb_draw_glyph8_nodmg() -> Renderiza en VRAM TrueColor           │
│   2. Restaurador VGA (src/fb.c):                                       │
│      - vga_hardware_restore_mode3() -> Copia a plano VGA 0xA0000       │
│   3. Aplicaciones de Escritorio (Desktop, Browser, FM, Notepad, Term)  │
│      - font_draw_string() / font_draw_string_ansi()                    │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. MATRICES DE GLIFOS (src/fonts/)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/fonts/font_classic.h ---")
    sections.append(read_file_safe(SRC / "fonts" / "font_classic.h") + "\n")
    sections.append(f"--- [ARCHIVO]: src/fonts/font_modern.h ---")
    sections.append(read_file_safe(SRC / "fonts" / "font_modern.h") + "\n")

    sections.append("=" * 80)
    sections.append("4. API Y MOTOR DEL GESTOR (src/gui_font.h / .c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/gui_font.h ---")
    sections.append(read_file_safe(resolve_src("gui_font.h")) + "\n")
    sections.append(f"--- [ARCHIVO]: src/gui_font.c ---")
    sections.append(read_file_safe(resolve_src("gui_font.c")) + "\n")

    sections.append("=" * 80)
    sections.append("5. RASTERIZADOR Y RESTAURADOR DE FUENTES (src/fb.c)")
    sections.append("=" * 80)
    fb_code = read_file_safe(resolve_src("fb.c"))
    sections.append(extract_function(fb_code, "fb_draw_glyph8_nodmg") + "\n")
    sections.append(extract_function(fb_code, "fb_draw_glyph8") + "\n")

    sections.append("=" * 80)
    sections.append("6. CONTROL INTERACTIVO EN SHELL (src/shell.c)")
    sections.append("=" * 80)
    sh_code = read_file_safe(resolve_src("shell.c"))
    sections.append(extract_function(sh_code, "cmd_font") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()