#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
exportarinterfaz.py — Exportador de estructura y código fuente del
subsistema de interfaz visual de SOMA (GUI minimalista tipo "Her",
framebuffer, tipografía, ratón, iconos, cursores y parche HER-MINIMAL).

Genera un .txt autocontenido para análisis / refinamiento con LLMs.

Uso (desde la carpeta tools/ o desde la raíz del proyecto):
  python3 tools/exportarinterfaz.py
  python3 exportarinterfaz.py

Salida:
  interfaz_proyecto.txt  (en la raíz de SOMA)
"""

from __future__ import annotations

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

# Archivos que intervienen en apariencia y funcionalidad de la interfaz.
# Orden: parche HER → GUI core → framebuffer → tipografía → ratón →
#        iconos → cursores → shell (entrada de teclado al GUI) → build.
RELEVANT_FILES = [
    # Parche de la UI minimalista "Her"
    "parche.py",
    "tools/parche.py",

    # Núcleo de la interfaz gráfica
    "src/ui/gui.h",
    "src/ui/gui.c",

    # Framebuffer y primitivas de dibujo (círculos, rects, present)
    "src/drivers/fb.h",
    "src/drivers/fb.c",

    # Tipografía 8x8 (render de texto fantasma / chrome de ventana)
    "src/ui/gui_font.h",
    "src/ui/gui_font.c",

    # Ratón y cursores (visibilidad tras tecla 'x', drag/resize)
    "src/drivers/mouse.h",
    "src/drivers/mouse.c",
    "src/cursors/cursor_arrow.h",
    "src/cursors/cursor_crosshair.h",
    "src/cursors/cursor_hand.h",

    # Iconos del dock
    "src/icons/icons.h",
    "src/icons/icon_agent.h",
    "src/icons/icon_terminal.h",
    "src/icons/icon_browser.h",
    "src/icons/icon_files.h",
    "src/icons/icon_image.h",
    "src/icons/icon_notes.h",
    "src/icons/icon_memory.h",
    "src/icons/icon_layout.h",

    # Consola / shell (puente teclado → GUI, cmd_vsoma)
    "src/ui/console.h",
    "src/ui/console.c",
    "src/agent/shell.h",
    "src/agent/shell.c",

    # Agente (log de respuestas que alimenta la zona inferior)
    "src/agent/agent.h",
    "src/agent/agent.c",

    # Build (gui.o, fb.o, mouse.o, gui_font.o en OBJS_CORE)
    "Makefile",
]

# Fuentes 8x8 opcionales (útiles para entender el render del texto)
OPTIONAL_FONT_FILES = [
    "src/fonts/font_classic.h",
    "src/fonts/font_modern.h",
]


def build_tree(dir_path: Path, prefix: str = "", max_depth: int = 4, depth: int = 0) -> list[str]:
    lines: list[str] = []
    if depth > max_depth:
        return lines
    try:
        items = sorted(
            [p for p in dir_path.iterdir()
             if p.name not in IGNORE_DIRS and not p.name.startswith(".patch")],
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
    return False


def main() -> int:
    lines: list[str] = []
    lines.append("=" * 80)
    lines.append("ESTRUCTURA Y CÓDIGO DEL SUBSISTEMA DE INTERFAZ VISUAL SOMA")
    lines.append("UI minimalista tipo película \"Her\" + framebuffer + tipografía + ratón")
    lines.append("=" * 80)
    lines.append("")
    lines.append(f"Proyecto: {ROOT}")
    lines.append("Módulo: Interfaz gráfica (gui) · HER-MINIMAL v2 · partículas · drag/resize")
    lines.append("")
    lines.append("Componentes clave:")
    lines.append("  - parche.py                 → transformador HER-MINIMAL (partículas, drag, resize)")
    lines.append("  - src/ui/gui.c|.h           → escritorio, dock, ventanas, bucle principal")
    lines.append("  - src/drivers/fb.c|.h       → framebuffer, círculos, present, colores")
    lines.append("  - src/ui/gui_font.c|.h      → tipografía 8x8 (texto fantasma / chrome)")
    lines.append("  - src/drivers/mouse.c|.h    → cursor y polling (visible tras tecla 'x')")
    lines.append("  - src/icons/*               → bitmaps del dock horizontal")
    lines.append("  - src/cursors/*             → cursores del sistema")
    lines.append("  - src/agent/shell.c + agent → entrada de comandos y log de respuestas")
    lines.append("")
    lines.append("Comportamiento HER-MINIMAL v2:")
    lines.append("  1. Pantalla negra absoluta.")
    lines.append("  2. Círculo central azul claro con destellos arcoíris + 48 partículas.")
    lines.append("  3. Texto usuario ENCIMA del círculo: invisible hasta teclear.")
    lines.append("  4. Respuesta agente DEBAJO del círculo: invisible hasta que haya log.")
    lines.append("  5. Tecla 'x' → muestra/oculta dock de iconos + ratón.")
    lines.append("  6. Clic en icono → abre ventana arrastrable (título) y redimensionable (bordes).")
    lines.append("  7. Botón X rojo de la barra de título cierra la ventana.")
    lines.append("")
    lines.append("=" * 80)
    lines.append("ESTRUCTURA DEL PROYECTO")
    lines.append("=" * 80)
    lines.append(f"{ROOT.name}/")
    lines.extend(build_tree(ROOT))
    lines.append("")
    lines.append("=" * 80)
    lines.append("CONTENIDO DE LOS ARCHIVOS DE LA INTERFAZ")
    lines.append("=" * 80)
    lines.append("")

    included = 0
    missing = 0
    seen: set[str] = set()

    for rel in RELEVANT_FILES:
        # Evitar duplicados (parche.py puede estar en raíz o en tools/)
        key = Path(rel).name
        if key in seen and key == "parche.py":
            continue
        if append_file(lines, rel):
            included += 1
            seen.add(key)
        else:
            if key not in seen:
                missing += 1

    lines.append("=" * 80)
    lines.append("FUENTES 8x8 (OPCIONAL — glifos usados por el render de texto)")
    lines.append("=" * 80)
    lines.append("")
    for rel in OPTIONAL_FONT_FILES:
        if append_file(lines, rel):
            included += 1

    lines.append("=" * 80)
    lines.append("NOTAS PARA REFINAMIENTO CON LLM")
    lines.append("=" * 80)
    lines.append("""
Estética objetivo (película "Her"):
  - Fondo #000000, sin barras de sistema ni paneles laterales visibles por defecto.
  - Un único orbe central (#7DD3FC) que gira ~30 fps con partículas arcoíris.
  - Input y output de texto "fantasma": solo aparecen al usarse.
  - Dock de 8 iconos centrado abajo, oculto hasta pulsar 'x'.

Estado runtime (variables en gui.c tras aplicar parche.py):
  her_icons_visible   — dock + ratón visibles
  her_angle           — rotación del orbe (0..255)
  her_parts[48]       — sistema de partículas
  her_win_x/y/w/h     — geometría libre de la ventana activa
  her_dragging        — arrastre por barra de título
  her_resizing        — resize por bordes (bitmask L/R/T/B)
  agent_global_cmd    — buffer de escritura del usuario (zona superior)
  agent_log           — respuestas del agente (zona inferior)

Flujo de interacción:
  1. Arranque: cmd_vsoma → pantalla negra + orbe animado.
  2. Usuario teclea → letras centradas encima del círculo.
  3. Enter → submit_global_agent_prompt → agent_run → log abajo.
  4. Tecla 'x' → dock horizontal + cursor.
  5. Clic icono → app en ventana con chrome (título, X, esquinas resize).
  6. Drag título / resize bordes / X cierra → vuelve al orbe.

Archivos críticos a no romper:
  - fb_fill_circle / fb_draw_circle (orbe y partículas)
  - font_draw_string (texto fantasma)
  - mouse_poll / mouse_draw_cursor (drag y dock)
  - agent_log + agent_global_cmd (zonas de texto)

Límites actuales:
  - Sin libm: seno/coseno por tabla de 64 entradas.
  - Partículas: 1 spawn/frame, máximo 48 vivas.
  - Ventana única (no multi-window stacking).
  - Resize mínimo 280×180.
""")

    full_output = "\n".join(lines)
    out_file = ROOT / "interfaz_proyecto.txt"
    out_file.write_text(full_output, encoding="utf-8")
    size_out = len(full_output.encode("utf-8"))

    if not sys.stdout.isatty():
        sys.stdout.write(full_output)
    else:
        print("\n[OK] Volcado de la interfaz completado.")
        print(f"  - Raíz del proyecto:  {ROOT}")
        print(f"  - Archivo generado:   {out_file} ({size_out} bytes)")
        print(f"  - Archivos incluidos: {included}  |  no encontrados: {missing}")
        print("  - Enviar al LLM o copiar:")
        print(f"      cat {out_file.name} | pbcopy   (macOS)")
        print(f"      cat {out_file.name} | xclip -selection clipboard  (Linux)\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())