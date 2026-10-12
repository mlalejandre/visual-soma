#!/usr/bin/env python3
"""
tools/estructuraagente.py
Genera 'estructuraagente.txt' con la arquitectura y el código del subsistema
de Inteligencia Artificial, Orquestación Multi-Agente (ReAct), Memoria y Puente LLM.
"""

from pathlib import Path
import re


def find_project_root() -> Path:
    here = Path(__file__).resolve().parent
    return here if (here / "src").is_dir() else here.parent


ROOT = find_project_root()
SRC = ROOT / "src"
OUTPUT_FILE = ROOT / "estructuraagente.txt"
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
            marker = "  <-- [SUBSISTEMA AGENTE IA]" if any(
                k in entry.name.lower() for k in ["agent", "llm", "search_proxy"]
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
    print(f"[ESTRUCTURA] Analizando subsistema del Agente IA en {ROOT}...")
    sections = []

    sections.append("=" * 80)
    sections.append("SOMA OS - ESTRUCTURA GENERAL Y SUBSISTEMA DE AGENTES IA (REACT)")
    sections.append("=" * 80)
    sections.append(f"Directorio raíz: {ROOT}")
    sections.append(f"Archivo de salida: {OUTPUT_FILE.name}\n")

    sections.append("=" * 80)
    sections.append("1. ARQUITECTURA DEL NÚCLEO MULTI-AGENTE (ReAct / IPC / LLM)")
    sections.append("=" * 80)
    diagram = """
┌────────────────────────────────────────────────────────────────────────┐
│                        ORQUESTACIÓN MULTI-AGENTE                       │
│   - 'soma'     -> Control del kernel, archivos, hardware, ejecución   │
│   - 'buscador' -> Investigación web en vivo, síntesis, árbol /mem/     │
│   - delegate <agente> "<submisión>" -> Delegación jerárquica (prof. 3) │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     BUCLE COGNITIVO ReAct (agent.c)                    │
│   1. Thought: Razonamiento paso a paso emitido por el modelo           │
│   2. Action: 'tool' (ejecución hardware), 'patch' o 'final' (dictamen) │
│   3. Feedback Hardware: Retorno real de comandos en /tmp/last_tool.log │
│   4. Memoria Dinámica: Ventana STM deslizante + /agent/blackboard.txt  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ llm_chat_json() / llm_chat()
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                ENLACE HTTP Y PARSEO JSON BARE-METAL (llm.c)            │
│   - Endpoint /v1/chat/completions en puerto 8087 (llama.cpp)           │
│   - Buffers elásticos de 32/64 KiB en KHeap (cero truncamiento)        │
│   - Captura íntegra de cadenas de pensamiento (<think> y reasoning)    │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│              HERRAMIENTAS Y POLÍTICA DE SEGURIDAD (agent.c)            │
│   - Permisos filtrados (agent_dispatch_mode): protección de Ring 0     │
│   - Automatización gráfica: apertura inmediata de Browser (HTML) o FM  │
│   - Motor semántico bare-metal BM25 en memoria persistente             │
└────────────────────────────────────────────────────────────────────────┘
"""
    sections.append(diagram.strip() + "\n")

    sections.append("=" * 80)
    sections.append("2. ARBOL DE DIRECTORIOS DEL PROYECTO")
    sections.append("=" * 80)
    sections.append(build_tree(ROOT) + "\n")

    sections.append("=" * 80)
    sections.append("3. CABECERA DEL AGENTE (src/agent.h)")
    sections.append("=" * 80)
    sections.append(read_file_safe(resolve_src("agent.h")) + "\n")

    sections.append("=" * 80)
    sections.append("4. MOTOR ReAct Y MEMORIA DEL AGENTE (src/agent.c - FRAGMENTOS CLAVE)")
    sections.append("=" * 80)
    ag_code = read_file_safe(resolve_src("agent.c"))
    sections.append("--- [Perfiles de Agentes Registrados: AGENT_REGISTRY] ---")
    reg_match = re.search(r'static const struct agent_profile AGENT_REGISTRY\[\]\s*=\s*\{.*?\n\};', ag_code, re.DOTALL)
    sections.append(reg_match.group(0) if reg_match else "/* AGENT_REGISTRY */\n")
    sections.append("\n--- [Función: agent_run (Bucle ReAct de 6 pasos y síntesis)] ---")
    sections.append(extract_function(ag_code, "agent_run") + "\n")
    sections.append("--- [Función: delegate_cmd (Delegación jerárquica)] ---")
    sections.append(extract_function(ag_code, "delegate_cmd") + "\n")
    sections.append("--- [Función: stm_append_interaction (Memoria a Corto Plazo STM)] ---")
    sections.append(extract_function(ag_code, "stm_append_interaction") + "\n")

    sections.append("=" * 80)
    sections.append("5. SUBSISTEMA DE COMUNICACIÓN CON EL LLM (src/llm.h / .c)")
    sections.append("=" * 80)
    sections.append(f"--- [ARCHIVO]: src/llm.h ---")
    sections.append(read_file_safe(resolve_src("llm.h")) + "\n")
    llm_code = read_file_safe(resolve_src("llm.c"))
    sections.append("--- [Función: llm_chat_json_unlocked] ---")
    sections.append(extract_function(llm_code, "llm_chat_json_unlocked") + "\n")
    sections.append("--- [Función: extract_json_content (Preservación de razonamiento)] ---")
    sections.append(extract_function(llm_code, "extract_json_content") + "\n")
    sections.append("--- [Función: build_chat_payload_mode] ---")
    sections.append(extract_function(llm_code, "build_chat_payload_mode") + "\n")

    sections.append("=" * 80)
    sections.append("6. GESTIÓN DEL SERVIDOR EN EL HOST (tools/llm_server.py)")
    sections.append("=" * 80)
    srv_code = read_file_safe(ROOT / "tools" / "llm_server.py")
    sections.append("--- [Lanzamiento de llama-server y parámetros -n 8192] ---")
    launch_match = re.search(r'def launch\(.*?\n\s*return True', srv_code, re.DOTALL)
    sections.append(launch_match.group(0) if launch_match else "/* launch */\n")

    report_text = "\n".join(sections)
    OUTPUT_FILE.write_text(report_text, encoding="utf-8")
    print(f"[OK] Generado: {OUTPUT_FILE} ({OUTPUT_FILE.stat().st_size} bytes)")


if __name__ == "__main__":
    generate_report()