#!/usr/bin/env python3
"""
tools/estructuraimgview.py
Genera 'estructuraimgview.txt' con la estructura del proyecto y el subsistema de Imagen / Visor.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructuraimgview.txt"
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
            marker = "  <-- [SUBSISTEMA IMAGEN / VISOR]" if any(k in entry.name.lower() for k in ["image", "imgview"]) else ""
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
    print(f"[ESTRUCTURA] Analizando subsistema de Imagen / Visor en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE IMAGEN / VISOR")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DEL MOTOR DE IMAGEN Y VISOR INTERACTIVO")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                     FUENTES DE ARCHIVOS DE IMAGEN                      │
│   - Archivos locales en RamFS: /workspace/*.bmp, /workspace/*.ppm      │
│   - Descarga HTTP directa en el navegador web (browser.c)              │
│   - Generadores sintéticos de prueba: demo BMP y chip BMP (image.c)    │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│            DECODIFICADOR BARE-METAL UNIVERSAL (src/image.c / .h)       │
│   - Formatos: Windows Bitmap (BMP 16, 24, 32 bpp TrueColor) y Netpbm   │
│   - Reservas elásticas de memoria en KHeap para buffers de píxeles     │
│   - image_decode() -> Convierte cabecera DIB en buffer lineal ARGB     │
│   - image_render_alpha() -> Escalado con interpolación de color        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│              APLICACIÓN VISOR DE IMÁGENES (src/imgview.c / .h)         │
│   - Galería interactiva: navegación anterior [<] y siguiente [>]       │
│   - Zoom en tiempo real: [-], [+], [100%] hasta 400%                   │
│   - Viewport con recorte hardware y centrado automático en ventana     │
│   - Barra de telemetría de imagen (dimensiones, formato, zoom)         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│             ACELERACIÓN DE FRAMEBUFFER Y BLITTING (src/fb.c)           │
│   - fb_blit() / fb_blit_scale() -> Transferencia en ráfagas de 64 bits │
│   - fb_blend_color() -> Composición y canal alfa (división rápida 255) │
│   - fb_damage() -> Registro del área modificada para renderizar a 60FPS│
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA DEL MOTOR DE IMÁGENES (src/image.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("image.h")) + "\n")

    sections.append("=" * 80)
    sections.append("4. DECODIFICADOR Y RENDERIZADOR DE IMAGEN (src/image.c)")
    sections.append("=" * 80)
    img_code = read_file_safe(resolve_src("image.c"))
    sections.append("--- [Función: decode_bmp] ---")
    sections.append(extract_function(img_code, "decode_bmp") + "\n")
    sections.append("--- [Función: image_render] ---")
    sections.append(extract_function(img_code, "image_render") + "\n")
    sections.append("--- [Función: image_create_demo_bmp] ---")
    sections.append(extract_function(img_code, "image_create_demo_bmp") + "\n")

    sections.append("=" * 80)
    sections.append("5. APLICACIÓN VISOR DE IMÁGENES (src/imgview.h / .c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/imgview.h ---")
    sections.append(read_file_safe(resolve_src("imgview.h")) + "\n")
    iv_code = read_file_safe(resolve_src("imgview.c"))
    sections.append("--- [Función: imgview_load_file] ---")
    sections.append(extract_function(iv_code, "imgview_load_file") + "\n")
    sections.append("--- [Función: imgview_render_window] ---")
    sections.append(extract_function(iv_code, "imgview_render_window") + "\n")
    sections.append("--- [Función: imgview_handle_click] ---")
    sections.append(extract_function(iv_code, "imgview_handle_click") + "\n")

    sections.append("=" * 80)
    sections.append("6. PRIMITIVAS BLIT Y ALFA EN FRAMEBUFFER (src/fb.c)")
    sections.append("=" * 80)
    fb_code = read_file_safe(resolve_src("fb.c"))
    sections.append(extract_function(fb_code, "fb_blit_scale") + "\n")
    sections.append(extract_function(fb_code, "fb_blend_color") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()