#!/usr/bin/env python3
"""
tools/estructurakernel.py
Genera 'estructurakernel.txt' con la arquitectura y el código del subsistema
del Núcleo / Kernel modular (src/core/ y src/arch/).
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructurakernel.txt"
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
            
            # Omitir código de generadores de estructura si se inspeccionan
            marker = ""
            if entry.name == "core" or any(k in entry.name.lower() for k in ["kernel", "boot", "idt", "pmm", "vmm", "mem.", "thread", "mutex"]):
                marker = "  <-- [NÚCLEO / CORE]"
            elif entry.name.startswith("estructura") and entry.name.endswith(".py"):
                marker = "  [SCRIPT DE ESTRUCTURA]"

            lines.append(f"{prefix}{connector}{entry.name}{marker}")
            if entry.is_dir():
                walk(entry, prefix + ("    " if is_last else "│   "))

    walk(root_dir)
    return "\n".join(lines)


def read_file_safe(path: Path) -> str:
    if not path.exists():
        # Fallback de búsqueda si la ruta varió
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
    print(f"[ESTRUCTURA] Analizando subsistema del Núcleo / Kernel modular en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y ARQUITECTURA DEL NÚCLEO (KERNEL CORE)")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DEL NÚCLEO MODULAR (src/core/ y src/arch/)")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                   ARRANQUE MULTIBOOT & TRANSICIÓN CPU                  │
│   - src/arch/x86_64/boot.s: Modo Protegido -> Long Mode 64-bit         │
│   - CR4 (PAE, OSFXSR), EFER (LME, NXE), CR0 (PG, PE, WP=1)             │
│   - GDT64: Selectores Ring 0 (Código 0x08, Datos 0x10, TSS 0x18)       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│              CPU RING 0 BLINDADO (src/arch/x86_64/idt.c / .h)          │
│   - IDT: 32 excepciones x86_64 + IRQ0 (PIT 1000Hz) + IRQ1 (Teclado)   │
│   - TSS de 64 bits con Pila de Emergencia IST1 (16 KiB):               │
│       -> Doble Falta (#DF, vector 8) y Fallo de Página (#PF, vector 14)│
│       -> Cero Triple Faults silenciosos                                │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│       GESTIÓN DE MEMORIA FÍSICA Y VIRTUAL (pmm.c, vmm.c, mem.c)        │
│   - PMM (src/arch/x86_64/pmm.c): Bitmap Allocator (marcos de 4 KiB)    │
│   - VMM (src/arch/x86_64/vmm.c): Paginación de 4 niveles, CR0.WP activo│
│       -> .text Read-Only, .rodata NX+RO, .data/.bss Read-Write+NX      │
│   - KHeap (src/core/mem.c): Asignador elástico en 0x20000000 (NX)      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│        PLANIFICADOR DE HILOS Y EXCLUSIÓN MUTUA (src/core/thread.c)     │
│   - KThreads: Planificador Round-Robin cooperativo en System V ABI     │
│   - Pilas aisladas en 0x80000000 con Guard Pages desmapeadas           │
│   - KMutex (src/core/mutex.c): Recursivo y con recolección de huérfanos│
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│         PUERTA DE ARRANQUE Y RECUPERACIÓN (src/core/boot_gate.c)       │
│   - Verificación canaria rápida (<1s): Heap, RamFS, Disco, Ping        │
│   - Rollback automático del host en Docker ante kernels rotos          │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. BOOTSTRAP DEL KERNEL (src/core/kernel.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "core" / "kernel.c") + "\n")

    sections.append("=" * 80)
    sections.append("4. ARRANCADOR EN ENSAMBLADOR (src/arch/x86_64/boot.s)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "arch" / "x86_64" / "boot.s") + "\n")

    sections.append("=" * 80)
    sections.append("5. IDT, TSS Y RESCATE IST1 (src/arch/x86_64/idt.h / idt.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "arch" / "x86_64" / "idt.h") + "\n")
    idt_code = read_file_safe(SRC / "arch" / "x86_64" / "idt.c")
    sections.append("--- [Función: tss_init (TSS 64-bit e IST1)] ---")
    sections.append(extract_function(idt_code, "tss_init") + "\n")
    sections.append("--- [Función: isr_exception_handler (Volcado de Registros)] ---")
    sections.append(extract_function(idt_code, "isr_exception_handler") + "\n")

    sections.append("=" * 80)
    sections.append("6. ASIGNADOR FÍSICO PMM (src/arch/x86_64/pmm.h / pmm.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "arch" / "x86_64" / "pmm.h") + "\n")
    pmm_code = read_file_safe(SRC / "arch" / "x86_64" / "pmm.c")
    sections.append("--- [Función: pmm_alloc_frame] ---")
    sections.append(extract_function(pmm_code, "pmm_alloc_frame") + "\n")
    sections.append("--- [Función: pmm_free_frame] ---")
    sections.append(extract_function(pmm_code, "pmm_free_frame") + "\n")

    sections.append("=" * 80)
    sections.append("7. MEMORIA VIRTUAL VMM (src/arch/x86_64/vmm.h / vmm.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "arch" / "x86_64" / "vmm.h") + "\n")
    vmm_code = read_file_safe(SRC / "arch" / "x86_64" / "vmm.c")
    sections.append("--- [Función: vmm_map_page] ---")
    sections.append(extract_function(vmm_code, "vmm_map_page") + "\n")
    sections.append("--- [Función: vmm_apply_protections (CR0.WP y NX)] ---")
    sections.append(extract_function(vmm_code, "vmm_apply_protections") + "\n")

    sections.append("=" * 80)
    sections.append("8. HEAP DINÁMICO KHEAP (src/core/mem.h / mem.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "core" / "mem.h") + "\n")
    mem_code = read_file_safe(SRC / "core" / "mem.c")
    sections.append("--- [Función: kmalloc (Alineación 16 bytes y expansión)] ---")
    sections.append(extract_function(mem_code, "kmalloc") + "\n")
    sections.append("--- [Función: kfree (Coalescencia de bloques)] ---")
    sections.append(extract_function(mem_code, "kfree") + "\n")

    sections.append("=" * 80)
    sections.append("9. MULTITAREA Y HILOS (src/core/thread.h / thread.c / switch.s)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "core" / "thread.h") + "\n")
    sections.append("--- [switch.s: Conmutador de Contexto CPU] ---")
    sections.append(read_file_safe(SRC / "arch" / "x86_64" / "switch.s") + "\n")
    th_code = read_file_safe(SRC / "core" / "thread.c")
    sections.append("--- [Función: schedule (Planificador Round-Robin)] ---")
    sections.append(extract_function(th_code, "schedule") + "\n")
    sections.append("--- [Función: thread_create] ---")
    sections.append(extract_function(th_code, "thread_create") + "\n")

    sections.append("=" * 80)
    sections.append("10. SINCRONIZACIÓN KMUTEX (src/core/mutex.h / mutex.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "core" / "mutex.h") + "\n")
    mtx_code = read_file_safe(SRC / "core" / "mutex.c")
    sections.append("--- [Función: kmutex_lock] ---")
    sections.append(extract_function(mtx_code, "kmutex_lock") + "\n")
    sections.append("--- [Función: kmutex_release_all_for_thread (Huérfanos)] ---")
    sections.append(extract_function(mtx_code, "kmutex_release_all_for_thread") + "\n")

    sections.append("=" * 80)
    sections.append("11. PUERTA DE ARRANQUE (src/core/boot_gate.h / boot_gate.c)")
    sections.append("=" * 80)
    sections.append(read_file_safe(SRC / "core" / "boot_gate.h") + "\n")
    bg_code = read_file_safe(SRC / "core" / "boot_gate.c")
    sections.append("--- [Función: boot_gate_check (Canary probe)] ---")
    sections.append(extract_function(bg_code, "boot_gate_check") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()