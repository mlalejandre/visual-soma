#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
exportarmaquetador.py - Exportador de estructura y código fuente del
subsistema Maquetador de documentos de SOMA (app layout + icono + GUI +
agente maquetador + fuentes/render) para análisis y refinamiento con LLMs.

Uso (desde la carpeta tools/ o desde la raíz del proyecto):
  python3 tools/exportarmaquetador.py
  python3 exportarmaquetador.py

Salida:
  maquetador_proyecto.txt  (en la raíz de SOMA)
"""

from pathlib import Path
import sys

def find_project_root() -> Path:
    current = Path(__file__).resolve().parent
    for candidate in [current.parent, current, current.parent.parent]:
        if (candidate / "Makefile").exists() and (candidate / "src").exists():
            return candidate
    return current.parent if current.name == "tools" else current

ROOT = find_project_root()

IGNORE_DIRS = {
    ".git", "build", "isodir", "__pycache__", "snapshots",
    ".vscode", ".idea", ".backup", "build_backup_src", ".parches_backup",
    ".patch-backups", "build_backup_src",
}

# Archivos que intervienen en funcionamiento y visualización del Maquetador.
# Orden: app → icono → GUI → agente → render/fuentes → FS (blackboard) → build.
RELEVANT_FILES = [
    # App Maquetador
    "src/apps/layout.h",
    "src/apps/layout.c",
    # Icono y registro de iconos del dock
    "src/icons/icon_layout.h",
    "src/icons/icons.h",
    # GUI: dock, APP_LAYOUT, live hook, teclado/ratón
    "src/ui/gui.h",
    "src/ui/gui.c",
    # Tipografía y framebuffer (render del “papel”)
    "src/ui/gui_font.h",
    "src/ui/gui_font.c",
    "src/drivers/fb.h",
    "src/drivers/fb.c",
    # Agente maquetador + API de agentes
    "src/agent/agent.h",
    "src/agent/agent.c",
    "src/agent/llm.h",
    "src/agent/llm.c",
    # Blackboard / FS (contexto de investigación para el maquetador)
    "src/fs/fs.h",
    "src/fs/fs.c",
    # Build (layout.o en OBJS_CORE)
    "Makefile",
    # Documentación de formato si existe
    "docs/JAVASCRIPT_RUNTIME.md",  # no es del maquetador; se omite si no aporta — se filtra abajo
]

# Solo incluir docs si son del maquetador; filtramos el de JS en runtime
RELEVANT_FILES = [f for f in RELEVANT_FILES if "JAVASCRIPT" not in f]

# Glifos de fuente: opcionales pero útiles para entender el render 8x8
OPTIONAL_FONT_FILES = [
    "src/fonts/font_classic.h",
    "src/fonts/font_modern.h",
]


def build_tree(dir_path: Path, prefix: str = "", max_depth: int = 4, depth: int = 0) -> list[str]:
    lines = []
    if depth > max_depth:
        return lines
    try:
        items = sorted(
            [p for p in dir_path.iterdir() if p.name not in IGNORE_DIRS and not p.name.startswith(".patch")],
            key=lambda p: (not p.is_dir(), p.name.lower()),
        )
    except PermissionError:
        return lines

    for i, p in enumerate(items):
        is_last = i == len(items) - 1
        connector = "└── " if is_last else "├── "
        lines.append(f"{prefix}{connector}{p.name}")
        if p.is_dir():
            sub_prefix = f"{prefix}{'    ' if is_last else '│   '}"
            lines.extend(build_tree(p, sub_prefix, max_depth, depth + 1))
    return lines


def append_file(lines: list[str], rel: str) -> bool:
    p = ROOT / rel
    if p.exists() and p.is_file():
        try:
            content = p.read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            lines.append(f"# AVISO: no se pudo leer {rel}: {e}\n")
            return False
        size_b = len(content.encode("utf-8"))
        lines.append("#" * 80)
        lines.append(f"# ARCHIVO: {rel}")
        lines.append(f"# TAMAÑO: {size_b} bytes")
        lines.append("#" * 80)
        lines.append("")
        lines.append(content)
        lines.append("")
        return True
    lines.append(f"# AVISO: Archivo no encontrado -> {rel}\n")
    return False


def main() -> int:
    lines: list[str] = []
    lines.append("=" * 80)
    lines.append("ESTRUCTURA Y CÓDIGO DEL SUBSISTEMA MAQUETADOR DE DOCUMENTOS SOMA")
    lines.append("=" * 80)
    lines.append("")
    lines.append(f"Proyecto: {ROOT}")
    lines.append("Módulo: App Maquetador (layout) + icono + dock GUI + agente 'maquetador'")
    lines.append("Formato de documento: #SOMA-LAYOUT v1")
    lines.append("")
    lines.append("Componentes clave:")
    lines.append("  - src/apps/layout.c|.h     → app, parser, render del papel, Generar")
    lines.append("  - src/icons/icon_layout.h  → bitmap 16x16 del dock")
    lines.append("  - src/icons/icons.h        → ICON_LAYOUT en el enum")
    lines.append("  - src/ui/gui.c|.h          → APP_LAYOUT, dock, hooks, teclado/ratón")
    lines.append("  - src/agent/agent.c        → registro y prompt del subagente maquetador")
    lines.append("  - src/ui/gui_font.c + fb   → dibujo de texto y framebuffer")
    lines.append("")
    lines.append("=" * 80)
    lines.append("ESTRUCTURA DEL PROYECTO")
    lines.append("=" * 80)
    lines.append(f"{ROOT.name}/")
    lines.extend(build_tree(ROOT))
    lines.append("")
    lines.append("=" * 80)
    lines.append("CONTENIDO DE LOS ARCHIVOS DEL MAQUETADOR")
    lines.append("=" * 80)
    lines.append("")

    included = 0
    missing = 0
    for rel in RELEVANT_FILES:
        if append_file(lines, rel):
            included += 1
        else:
            missing += 1

    lines.append("=" * 80)
    lines.append("FUENTES 8x8 (OPCIONAL — glifos usados por el render)")
    lines.append("=" * 80)
    lines.append("")
    for rel in OPTIONAL_FONT_FILES:
        if append_file(lines, rel):
            included += 1

    lines.append("=" * 80)
    lines.append("NOTAS PARA REFINAMIENTO CON LLM")
    lines.append("=" * 80)
    lines.append("""
Formato #SOMA-LAYOUT v1 (salida esperada del subagente maquetador):

  #SOMA-LAYOUT v1
  TITLE: ...
  SUBTITLE: ...
  FONT: modern|classic|bold|...
  ALIGN: left|center
  MARGIN: 8-64
  ---
  H1: ...
  H2: ...
  H3: ...
  P: párrafo
  LIST: ítem (sin guion inicial; el render añade "- ")
  QUOTE: cita
  HR
  FOOTER: pie

Flujo actual:
  1. Usuario escribe tema en la app MAQUETADOR y pulsa Generar / Enter
  2. layout.c → agent_run("maquetador", misión [+ blackboard])
  3. Se extrae #SOMA-LAYOUT de agent_last_response (o de /workspace/*layout*.txt)
  4. layout_set_source → ly_parse → render en el lienzo (papel)

Problemas conocidos a mejorar:
  - Llamada LLM síncrona bloquea ratón/GUI ~30s
  - Fuentes 8x8 sin glifos latinos (transliteración UTF-8 → ASCII)
  - El agente a veces usa write y el live-hook debe derivar a APP_LAYOUT
""")

    full_output = "\n".join(lines)
    out_file = ROOT / "maquetador_proyecto.txt"
    out_file.write_text(full_output, encoding="utf-8")
    size_out = len(full_output.encode("utf-8"))

    if not sys.stdout.isatty():
        sys.stdout.write(full_output)
    else:
        print("\n[OK] Volcado del Maquetador completado.")
        print(f"  - Raíz del proyecto: {ROOT}")
        print(f"  - Archivo generado:  {out_file} ({size_out} bytes)")
        print(f"  - Archivos incluidos: {included}  |  no encontrados: {missing}")
        print("  - Enviar al LLM o copiar:")
        print(f"      cat {out_file.name} | pbcopy   (macOS)")
        print(f"      cat {out_file.name} | xclip -selection clipboard  (Linux)\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
