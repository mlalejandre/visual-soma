#!/usr/bin/env python3
"""
tools/estructuranotepad.py
Genera 'estructuranotepad.txt' con la estructura del proyecto y el subsistema de Bloc de Notas / Editor.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructuranotepad.txt"
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
            marker = "  <-- [SUBSISTEMA BLOC DE NOTAS / EDITOR]" if any(k in entry.name.lower() for k in ["notepad", "nano"]) else ""
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
    print(f"[ESTRUCTURA] Analizando subsistema de Bloc de Notas / Editor en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE BLOC DE NOTAS / EDITOR")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DEL BLOC DE NOTAS Y EDICIÓN EN TIEMPO REAL")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                        MODOS DE ENTRADA Y DISPARO                      │
│   - Usuario interactivo: Clic en icono DOCK 'TXT NOTAS' o en FM        │
│   - Edición manual: Teclado con auto-wrap, saltos de línea y guardado  │
│   - Live Typewriter Hook: Disparo cuando SOMA genera un archivo .txt   │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│            GANCHO EN VIVO DE KERNEL (vfs_live_hook en src/fs.c)        │
│   - vfs_write() intercepta la creación de documentos de texto          │
│   - Conmuta la ventana a APP_NOTEPAD en caliente (gui_live_file_hook)  │
│   - notepad_live_begin() -> Inicia animación progresiva carácter a car.│
│   - notepad_live_step()  -> Escribe lotes visibles a 20 ms             │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               MOTOR DEL BLOC DE NOTAS (src/notepad.c / .h)             │
│   - Buffer de edición elástico (NP_MAX_SIZE = 16 KiB)                  │
│   - Cálculo de cursor de inserción y bandera 'np_dirty'                │
│   - Auto-scroll dinámico ('np_follow') sincronizado con el cursor      │
│   - Botón GUARDAR en interfaz gráfica -> Persistencia con vfs_write()  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                COMPONENTE CLI ALTERNATIVO: src/nano.c                  │
│   - Editor interactivo en consola de texto 80x25                       │
│   - Control por atajos de teclado (^S Guardar, ^X Salir)               │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA DEL BLOC DE NOTAS (src/notepad.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("notepad.h")) + "\n")

    sections.append("=" * 80)
    sections.append("4. IMPLEMENTACIÓN COMPLETA DEL BLOC DE NOTAS (src/notepad.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("notepad.c")) + "\n")

    sections.append("=" * 80)
    sections.append("5. GANCHO DE ESCRITURA EN VIVO DE IA (src/gui.c / src/fs.c)")
    sections.append("=" * 80)
    gui_code = read_file_safe(resolve_src("gui.c"))
    sections.append("--- [Función: gui_live_file_hook en src/gui.c] ---")
    sections.append(extract_function(gui_code, "gui_live_file_hook") + "\n")

    fs_code = read_file_safe(resolve_src("fs.c"))
    sections.append("--- [Interceptores en vfs_write en src/fs.c] ---")
    vfs_w_match = re.search(r'int vfs_write\(const char \*name, const char \*data, uint32_t len\)\s*\{.*?\}', fs_code, re.DOTALL)
    sections.append(vfs_w_match.group(0) if vfs_w_match else "/* vfs_write */\n")

    sections.append("=" * 80)
    sections.append("6. EDITOR VISUAL PARA TERMINAL (src/nano.h y src/nano.c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/nano.h ---")
    sections.append(read_file_safe(resolve_src("nano.h")) + "\n")
    sections.append(f"--- [ARCHIVO]: src/nano.c ---")
    sections.append(read_file_safe(resolve_src("nano.c")) + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()