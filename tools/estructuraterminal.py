#!/usr/bin/env python3
"""
tools/estructuraterminal.py
Genera 'estructuraterminal.txt' con la estructura del proyecto y el subsistema de Terminal / Shell / Consola.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructuraterminal.txt"
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
            marker = "  <-- [SUBSISTEMA TERMINAL / SHELL]" if any(k in entry.name.lower() for k in ["shell", "console", "terminal"]) else ""
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
    print(f"[ESTRUCTURA] Analizando subsistema de Terminal / Shell en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE TERMINAL / SHELL")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DE LA TERMINAL DUAL (TEXTO/VGA Y GRÁFICA)")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                     ENTRADA HARDWARE DE TECLADO                        │
│   - IRQ1 Teclado PS/2 (idt.c vector 33) -> Buffer circular (console.c) │
│   - Puerto Serie COM1 (0x3F8) -> Control y depuración por host        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ kgetc() / kgetline()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│             READLINE BARE-METAL Y PARSEO (src/console.c)               │
│   - Historial en RAM persistido en /etc/history.txt                    │
│   - Autocompletado con TAB (shell_autocomplete en shell.c)             │
│   - Edición interactiva: flechas, Home, End, retroceso atómico UTF-8   │
│   - Intérprete de secuencias de escape ANSI (colores y formato)        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ dispatch_command()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│           DESPACHADOR Y MOTOR DE SHELL (src/shell.c / .h)              │
│   - Operadores lógicos de control de flujo: 'cmd1 && cmd2', '||'       │
│   - Tuberías IPC: 'cmd1 | cmd2' con buffer en KHeap                    │
│   - Redirección universal: 'cmd > archivo.txt'                         │
│   - Expansión de variables de entorno: '$VAR' (env, export, unset)     │
│   - Intérprete por lotes: 'source script.sh' / /etc/init.sh            │
│   - Tabla canónica declarativa 'struct command_entry cmd_table[]'      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│       EMULADOR GRÁFICO DE TERMINAL EN ESCRITORIO (src/gui.c)           │
│   - Ventana gráfica dedicada 'APP_TERMINAL_SHELL'                      │
│   - Búfer de pantalla deslizante 'term_lines[72][256]'                 │
│   - Renderizado con fuentes activas mediante font_draw_string_ansi()   │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA Y TABLA DE COMANDOS (src/shell.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("shell.h")) + "\n")

    sections.append("=" * 80)
    sections.append("4. MOTOR DE LA SHELL Y DESPACHADOR (src/shell.c - FRAGMENTOS CLAVE)")
    sections.append("=" * 80)
    sh_code = read_file_safe(resolve_src("shell.c"))
    sections.append("--- [Función: dispatch_command (Tuberías, Redirección y &&/||)] ---")
    sections.append(extract_function(sh_code, "dispatch_command") + "\n")
    sections.append("--- [Función: env_expand (Expansión de Variables $VAR)] ---")
    sections.append(extract_function(sh_code, "env_expand") + "\n")
    sections.append("--- [Función: shell_autocomplete (Completado con TAB)] ---")
    sections.append(extract_function(sh_code, "shell_autocomplete") + "\n")
    sections.append("--- [Tabla Declarativa de Comandos cmd_table] ---")
    tbl_match = re.search(r'static const struct command_entry cmd_table\[\]\s*=\s*\{.*?\n\};', sh_code, re.DOTALL)
    sections.append(tbl_match.group(0) if tbl_match else "/* cmd_table */\n")

    sections.append("=" * 80)
    sections.append("5. CONSOLA FÍSICA, ANSI Y READLINE (src/console.h / .c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/console.h ---")
    sections.append(read_file_safe(resolve_src("console.h")) + "\n")
    cs_code = read_file_safe(resolve_src("console.c"))
    sections.append("--- [Función: kgetline (Readline bare-metal interactivo)] ---")
    sections.append(extract_function(cs_code, "kgetline") + "\n")
    sections.append("--- [Función: keyboard_irq_handler (Manejador PS/2)] ---")
    sections.append(extract_function(cs_code, "keyboard_irq_handler") + "\n")

    sections.append("=" * 80)
    sections.append("6. EMULADOR GRÁFICO DE TERMINAL EN ESCRITORIO (src/gui.c)")
    sections.append("=" * 80)
    gui_code = read_file_safe(resolve_src("gui.c"))
    sections.append("--- [Función: render_real_terminal] ---")
    sections.append(extract_function(gui_code, "render_real_terminal") + "\n")
    sections.append("--- [Función: term_init_banner] ---")
    sections.append(extract_function(gui_code, "term_init_banner") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()