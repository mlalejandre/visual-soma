#!/usr/bin/env python3
"""
tools/estructurafm.py
Genera 'estructurafm.txt' con la arquitectura y el código del explorador
de archivos gráfico de SOMA (File Manager en src/apps/fm.*).
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructurafm.txt"
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
            marker = ""
            if entry.name == "fm.c" or entry.name == "fm.h":
                marker = "  <-- [EXPLORADOR DE ARCHIVOS / FM]"
            elif entry.name.startswith("estructura") and entry.name.endswith(".py"):
                marker = "  [SCRIPT DE ESTRUCTURA]"

            lines.append(f"{prefix}{connector}{entry.name}{marker}")
            if entry.is_dir():
                walk(entry, prefix + ("    " if is_last else "│   "))

    walk(root_dir)
    return "\n".join(lines)


def read_file_safe(path: Path) -> str:
    if not path.exists():
        matches = list(SRC.rglob(path.name))
        if matches:
            path = matches[0]
        else:
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
    print(f"[ESTRUCTURA] Analizando Explorador de Archivos (FM) en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - SUBSISTEMA DE EXPLORADOR DE ARCHIVOS (FILE MANAGER)")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DEL EXPLORADOR DE ARCHIVOS (src/apps/fm.*)")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                    ENTRADA Y NAVEGACIÓN DE LUGARES                     │
│   - Panel lateral de Lugares: /workspace, /mis_archivos, /etc, /proc, /│
│   - Botón de actualización [Actualizar] (vfs_format_list_ext)          │
│   - Apertura programática: gui_open_file_manager(folder)               │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ fm_refresh()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               IDENTIFICACIÓN DE TIPOS Y METADATOS EN RAM               │
│   - struct fm_item: nombre, tamaño en bytes, flags MIME                │
│       -> [IMG] .bmp (TrueColor)                                        │
│       -> [WEB] .html, .htm                                             │
│       -> [TXT] .txt, .sh, .c, .h                                       │
│       -> [DOC] Otros archivos                                          │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ fm_handle_click()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   DELEGACIÓN INTER-APLICACIÓN GRÁFICA                  │
│   - Clic en archivo de imagen -> Abre Visor de Imágenes (imgview)      │
│   - Clic en archivo web       -> Abre Navegador Web (browser)          │
│   - Clic en archivo de texto  -> Abre Bloc de Notas (notepad)          │
│   - Panel inferior con previsualización de texto en tiempo real        │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA DEL EXPLORADOR (src/apps/fm.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "apps" / "fm.h") + "\n")

    sections.append("=" * 80)
    sections.append("4. IMPLEMENTACIÓN COMPLETA (src/apps/fm.c)")
    sections.append("=" * 80)
    fm_code = read_file_safe(SRC / "apps" / "fm.c")
    sections.append(fm_code + "\n")

    sections.append("=" * 80)
    sections.append("5. INTEGRACIÓN EN EL ESCRITORIO (src/ui/gui.c)")
    sections.append("=" * 80)
    gui_code = read_file_safe(SRC / "ui" / "gui.c")
    sections.append("--- [Función: gui_open_file_manager] ---")
    sections.append(extract_function(gui_code, "gui_open_file_manager") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()