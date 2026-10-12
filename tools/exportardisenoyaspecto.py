#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
exportardisenoyaspecto.py - Exportador de estructura y código fuente de SOMA
relacionado con el diseño, el aspecto visual, el comportamiento de la interfaz
y el kernel.

Uso:
  python3 tools/exportardisenoyaspecto.py

Salida:
  diseno_y_aspecto.txt
"""

import os
import sys
from pathlib import Path

def find_project_root() -> Path:
    current = Path(__file__).resolve().parent
    for candidate in [current.parent, current, current.parent.parent]:
        if (candidate / "Makefile").exists() and (candidate / "src").exists():
            return candidate
    return current.parent if current.name == "tools" else current

ROOT = find_project_root()

IGNORE_DIRS = {
    ".git", "build", "isodir", "__pycache__", "snapshots",
    ".vscode", ".idea", ".backup", "build_backup_src", ".parches_backup"
}

# Archivos base de la interfaz, aplicaciones visuales y núcleo
RELEVANT_FILES = [
    "src/core/kernel.c",
    "src/ui/gui.h",
    "src/ui/gui.c",
    "src/ui/gui_font.h",
    "src/ui/gui_font.c",
    "src/ui/console.h",
    "src/ui/console.c",
    "src/ui/nano.h",
    "src/ui/nano.c",
    "src/drivers/fb.h",
    "src/drivers/fb.c",
    "src/drivers/mouse.h",
    "src/drivers/mouse.c",
    "src/apps/browser.h",
    "src/apps/browser.c",
    "src/apps/fm.h",
    "src/apps/fm.c",
    "src/apps/image.h",
    "src/apps/image.c",
    "src/apps/imgview.h",
    "src/apps/imgview.c",
    "src/apps/layout.h",
    "src/apps/layout.c",
    "src/apps/memview.h",
    "src/apps/memview.c",
    "src/apps/notepad.h",
    "src/apps/notepad.c",
    "Makefile"
]

# Recolectar iconos, cursores y fuentes automáticamente para evitar omitir nuevos recursos
for asset_dir in ["src/icons", "src/cursors", "src/fonts"]:
    d = ROOT / asset_dir
    if d.exists() and d.is_dir():
        for f in sorted(d.glob("*.h")):
            # Añadir si no tiene extensión tipo backup de un parche
            if not f.name.endswith(".bak") and ".bak-" not in f.name:
                rel_path = f.relative_to(ROOT).as_posix()
                if rel_path not in RELEVANT_FILES:
                    RELEVANT_FILES.append(rel_path)

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

def main():
    lines = []
    lines.append("=" * 80)
    lines.append("ESTRUCTURA Y CÓDIGO: DISEÑO, ASPECTO VISUAL E INTERFAZ SOMA")
    lines.append("=" * 80)
    lines.append("")
    lines.append(f"Proyecto: {ROOT}")
    lines.append("Módulo: Aspecto visual, comportamiento de interfaz, Apps y Kernel")
    lines.append("")
    lines.append("=" * 80)
    lines.append("ESTRUCTURA DEL PROYECTO")
    lines.append("=" * 80)
    lines.append(f"{ROOT.name}/")
    lines.extend(build_tree(ROOT))
    lines.append("")
    lines.append("=" * 80)
    lines.append("CONTENIDO DE LOS ARCHIVOS RELACIONADOS")
    lines.append("=" * 80)
    lines.append("")

    included = 0
    missing = 0

    for rel in RELEVANT_FILES:
        p = ROOT / rel
        if p.exists() and p.is_file():
            try:
                content = p.read_text(encoding="utf-8", errors="replace")
            except OSError as e:
                lines.append(f"# AVISO: no se pudo leer {rel}: {e}\n")
                continue
            
            size_b = len(content.encode("utf-8"))
            lines.append("#" * 80)
            lines.append(f"# ARCHIVO: {rel}")
            lines.append(f"# TAMAÑO: {size_b} bytes")
            lines.append("#" * 80)
            lines.append("")
            lines.append(content)
            lines.append("")
            included += 1
        else:
            lines.append(f"# AVISO: Archivo no encontrado -> {rel}\n")
            missing += 1

    full_output = "\n".join(lines)
    out_file = ROOT / "diseno_y_aspecto.txt"
    out_file.write_text(full_output, encoding="utf-8")
    
    if not sys.stdout.isatty():
        sys.stdout.write(full_output)
    else:
        print("\n[OK] Volcado completado con éxito.")
        print(f"  - Raíz del proyecto: {ROOT}")
        print(f"  - Archivo generado: {out_file} ({len(full_output.encode('utf-8'))} bytes)")
        print(f"  - Archivos incluidos: {included} | no encontrados: {missing}")
        print("  - Copiar al portapapeles:")
        print(f"      cat {out_file.name} | pbcopy   (macOS)")
        print(f"      cat {out_file.name} | xclip -selection clipboard  (Linux)\n")

if __name__ == "__main__":
    main()