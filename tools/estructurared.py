#!/usr/bin/env python3
"""
tools/estructurared.py
Genera 'estructurared.txt' con la estructura del proyecto y el subsistema
de Red TCP/IP nativa y Servidor Web HTTPD / REST API en Ring 0.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructurared.txt"
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
            marker = "  <-- [SUBSISTEMA RED / HTTPD]" if any(
                k in entry.name.lower() for k in ["net", "ip", "tcp", "udp", "dns", "http"]
            ) else ""
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
    print(f"[ESTRUCTURA] Analizando subsistema de Red y HTTPD en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE RED / HTTPD")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DE LA PILA DE RED Y SERVIDOR HTTPD RING 0")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                   DRIVER HARDWARE (virtio_net.c / .h)                  │
│   - VirtIO-NET PCI (Legacy BAR) / MMIO (AArch64)                       │
│   - Colas Virtqueue: RX (virtq0) y TX (virtq1) con buffers de 2 KiB    │
│   - net_poll() -> Lee tramas Ethernet y despacha a la pila de red      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               CAPA DE ENLACE E INTERNET (net.c / ip.c)                 │
│   - Tabla dinámica ARP: arp_resolve() y auto-aprendizaje con peticiones│
│   - IPv4: Verificación de checksum RFC 1071, enrutamiento gateway      │
│   - ICMP: Respuestas Echo Reply nativas y comando 'ping'               │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│             CAPA DE TRANSPORTE (tcp.c / udp.c / dns.c)                 │
│   - UDP: Datagramas sin conexión y sockets efímeros                    │
│   - DNS: Cliente dinámico (10.0.2.3:53) con resolución A               │
│   - TCP: Máquina de estados completa (LISTEN, SYN_RCVD, ESTABLISHED,   │
│          CLOSE_WAIT, LAST_ACK, FIN_WAIT), retransmisión y RTO          │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│           APLICACIONES Y DEMONIOS DE RED (http.c / httpd.c)            │
│   - Cliente HTTP (http.c): Peticiones GET / POST JSON dinámicas        │
│   - Demonio HTTPD (httpd.c): Hilo en Ring 0 (TID 4) en puerto 80 nativo│
│       -> Dashboard HTML5 interactivo (telemetría en vivo, RAM, inodos) │
│       -> Endpoints REST: /api/telemetry, /api/fs, /api/blackboard      │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. SERVIDOR WEB EN RING 0 (src/httpd.h / .c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/httpd.h ---")
    sections.append(read_file_safe(resolve_src("httpd.h")) + "\n")
    httpd_code = read_file_safe(resolve_src("httpd.c"))
    sections.append("--- [Función: serve_client (Endpoints REST y Dashboard)] ---")
    sections.append(extract_function(httpd_code, "serve_client") + "\n")
    sections.append("--- [Función: render_dashboard (HTML5 bare-metal)] ---")
    sections.append(extract_function(httpd_code, "render_dashboard") + "\n")

    sections.append("=" * 80)
    sections.append("4. MÁQUINA DE ESTADOS TCP (src/tcp.h / .c)")
    sections.append("=" * 80)
    tcp_code = read_file_safe(resolve_src("tcp.c"))
    sections.append("--- [Función: tcp_input (Manejo de estados pasivos/activos)] ---")
    sections.append(extract_function(tcp_code, "tcp_input") + "\n")
    sections.append("--- [Función: tcp_listen / tcp_accept] ---")
    sections.append(extract_function(tcp_code, "tcp_listen") + "\n")
    sections.append(extract_function(tcp_code, "tcp_accept") + "\n")

    sections.append("=" * 80)
    sections.append("5. CLIENTE HTTP (src/http.h / .c)")
    sections.append("=" * 80)
    http_code = read_file_safe(resolve_src("http.c"))
    sections.append("--- [Función: http_request_host_unlocked] ---")
    sections.append(extract_function(http_code, "http_request_host_unlocked") + "\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()