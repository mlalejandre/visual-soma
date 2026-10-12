#!/usr/bin/env python3
"""
tools/estructurafs.py
Genera 'estructurafs.txt' con la arquitectura y el código del subsistema
de Almacenamiento, RamFS SOMAFS02, LBA en VirtIO-BLK y Motor Semántico BM25.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructurafs.txt"
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
            if entry.name == "fs" or any(k in entry.name.lower() for k in ["fs.", "disk_layout", "srcfs"]):
                marker = "  <-- [SUBSISTEMA ALMACENAMIENTO / FS]"
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
    print(f"[ESTRUCTURA] Analizando subsistema de Almacenamiento y VFS en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - SUBSISTEMA DE ALMACENAMIENTO, SOMAFS02 Y MOTOR BM25")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DE ALMACENAMIENTO PERSISTENTE (src/fs/)")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                MAPA FÍSICO DE DISCO (src/fs/disk_layout.h)             │
│   - LBA 1..16:     Buzón de parches host-bridge (Singularity Loop)     │
│   - LBA 20..23:    Resultado de compilación devuelto a la IA           │
│   - LBA 24..25:    Flags canarios (boot_gate y resume_flag)            │
│   - LBA 1024:      Superbloque SOMAFS02 ("SOMAFS02", 64 inodos)        │
│   - LBA 1025..3137:64 inodos x 33 sectores (1 cabecera + 16 KiB datos) │
│   - LBA 3500:      Scratch efímero de pruebas canarias                 │
│   - LBA 4096..6210:Zona de snapshot persistente (checkpoint/rollback)  │
│   - LBA 8192..16383:Índice y datos de fuentes del kernel (SRCFS)       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   VFS V2 Y CACHE EN RAM (src/fs/fs.c)                  │
│   - 64 Inodos dinámicos en KHeap (0x20000000)                          │
│   - Capacidad: hasta 16.384 bytes por archivo                          │
│   - Dirty-Tracking: solo los inodos modificados escriben en disco      │
│   - Pseudo-filesystems: /proc/ (uptime, meminfo...) y /dev/ (urandom..)|
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│           RECUPERACIÓN DE INFORMACIÓN BM25 BARE-METAL (src/fs/fs.c)    │
│   - Normalizador diacrítico UTF-8 (á->a, ñ->n, etc.)                   │
│   - Filtrado de stop-words en español e inglés                         │
│   - Ranking BM25 / TF-IDF: densidad de términos y coincidencias título │
│   - Extracción de snippets contextuales para prompts del agente        │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. LAYOUT FÍSICO DE DISCO (src/fs/disk_layout.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "fs" / "disk_layout.h") + "\n")

    sections.append("=" * 80)
    sections.append("4. CABECERA VFS SOMAFS02 (src/fs/fs.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "fs" / "fs.h") + "\n")

    sections.append("=" * 80)
    sections.append("5. IMPLEMENTACIÓN DEL VFS Y MOTOR BM25 (src/fs/fs.c - CLAVES)")
    sections.append("=" * 80)
    fs_code = read_file_safe(SRC / "fs" / "fs.c")
    sections.append("--- [Estructura del Superbloque y Cabecera de Inodo] ---")
    sb_match = re.search(r'struct fs_superblock\s*\{.*?\}\s*;.*?struct fs_disk_header\s*\{.*?\}\s*;', fs_code, re.DOTALL)
    sections.append(sb_match.group(0) if sb_match else "/* struct fs_superblock */")
    sections.append("\n--- [Función: vfs_write_core (Escritura y Dirty Track)] ---")
    sections.append(extract_function(fs_code, "vfs_write_core") + "\n")
    sections.append("--- [Función: vfs_read_unlocked (Lectura de archivos y pseudo-FS)] ---")
    sections.append(extract_function(fs_code, "vfs_read_unlocked") + "\n")
    sections.append("--- [Función: sync_file_to_disk (Serialización LBA VirtIO-BLK)] ---")
    sections.append(extract_function(fs_code, "sync_file_to_disk") + "\n")
    sections.append("--- [Función: load_file_from_disk (Carga de inodo desde disco)] ---")
    sections.append(extract_function(fs_code, "load_file_from_disk") + "\n")
    sections.append("--- [Función: vfs_search_bm25 (Motor de búsqueda semántica en C)] ---")
    sections.append(extract_function(fs_code, "vfs_search_bm25") + "\n")

    sections.append("=" * 80)
    sections.append("6. INSPECTOR DE FUENTES SRCFS (src/fs/srcfs.h / .c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/fs/srcfs.h ---")
    sections.append(read_file_safe(SRC / "fs" / "srcfs.h") + "\n")
    srcfs_code = read_file_safe(SRC / "fs" / "srcfs.c")
    sections.append("--- [Función: find_entry (Resolución de fuentes en subcarpetas)] ---")
    sections.append(extract_function(srcfs_code, "find_entry") + "\n")
    sections.append("--- [Función: srcfs_cat (Lectura paginada de código)] ---")
    sections.append(extract_function(srcfs_code, "srcfs_cat") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()