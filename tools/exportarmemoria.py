#!/usr/bin/env python3
"""
exportarmemoria.py - Exportador de estructura y código fuente del subsistema
de memoria multi-agente de SOMA para análisis y refinamiento con LLMs.
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
    ".vscode", ".idea", ".backup", "build_backup_src", ".parches_backup"
}

RELEVANT_FILES = [
    "src/apps/memview.h",
    "src/apps/memview.c",
    "src/icons/icon_memory.h",
    "src/icons/icons.h",
    "src/ui/gui.h",
    "src/ui/gui.c",
    "src/agent/agent.h",
    "src/agent/agent.c",
    "src/fs/fs.h",
    "src/fs/fs.c",
    "src/fs/disk_layout.h",
    "Makefile"
]

def build_tree(dir_path: Path, prefix: str = "") -> list[str]:
    lines = []
    try:
        items = sorted(
            [p for p in dir_path.iterdir() if p.name not in IGNORE_DIRS],
            key=lambda p: (not p.is_dir(), p.name.lower())
        )
    except PermissionError:
        return lines

    for i, p in enumerate(items):
        is_last = (i == len(items) - 1)
        connector = "└── " if is_last else "├── "
        lines.append(f"{prefix}{connector}{p.name}")
        if p.is_dir():
            sub_prefix = f"{prefix}{'    ' if is_last else '│   '}"
            lines.extend(build_tree(p, sub_prefix))
    return lines

def main():
    lines = []
    lines.append("=" * 80)
    lines.append("ESTRUCTURA Y CÓDIGO DEL SUBSISTEMA DE MEMORIA DE AGENTES SOMA")
    lines.append("=" * 80)
    lines.append("")
    lines.append(f"Proyecto: {ROOT}")
    lines.append("Módulo: Visor de Memoria Multi-Agente (memview) y Arquitectura Cognitiva")
    lines.append("")
    lines.append("=" * 80)
    lines.append("ESTRUCTURA DEL PROYECTO")
    lines.append("=" * 80)
    lines.append(f"{ROOT.name}/")
    lines.extend(build_tree(ROOT))
    lines.append("")
    lines.append("=" * 80)
    lines.append("CONTENIDO DE LOS ARCHIVOS RELATIVOS A LA MEMORIA Y VISUALIZADOR")
    lines.append("=" * 80)
    lines.append("")

    for rel in RELEVANT_FILES:
        p = ROOT / rel
        if p.exists() and p.is_file():
            content = p.read_text(encoding="utf-8", errors="replace")
            size_b = len(content.encode("utf-8"))
            lines.append("#" * 80)
            lines.append(f"# ARCHIVO: {rel}")
            lines.append(f"# TAMAÑO: {size_b} bytes")
            lines.append("#" * 80)
            lines.append("")
            lines.append(content)
            lines.append("")
        else:
            lines.append(f"# AVISO: Archivo no encontrado -> {rel}\n")

    full_output = "\n".join(lines)

    out_file = ROOT / "memoria_proyecto.txt"
    out_file.write_text(full_output, encoding="utf-8")

    if not sys.stdout.isatty():
        sys.stdout.write(full_output)
    else:
        print("\n[OK] Volcado completado con éxito.")
        print(f"  - Raíz del proyecto detectada: {ROOT}")
        print(f"  - Archivo generado: {out_file} ({len(full_output.encode('utf-8'))} bytes)")
        print(f"  - Archivos incluidos: {len(RELEVANT_FILES)}")
        print("  - Puedes enviarlo directamente al LLM o copiarlo al portapapeles:")
        print(f"      cat {out_file.name} | pbcopy   (en macOS)")
        print(f"      cat {out_file.name} | xclip    (en Linux)\n")

if __name__ == "__main__":
    main()
