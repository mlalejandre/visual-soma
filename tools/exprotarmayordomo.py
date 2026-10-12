#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Exporta a `estructuramayordomo` el mapa de SOMA y el codigo fuente completo.

Uso desde la raiz del proyecto:
    python3 exportarmayordomo.py

O indicandola explicitamente:
    python3 exportarmayordomo.py --root /ruta/a/soma

Incluye todos los ficheros de texto activos detectados (fuentes, cabeceras,
ensamblador, scripts, pruebas, configuracion y documentacion), completos y con
SHA-256. Excluye imagenes de disco y artefactos generados/binarios; las copias
historicas y exclusiones se enumeran para hacer transparente el alcance.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import os
from pathlib import Path
import re
import sys
from typing import Iterable

OUTPUT_NAME = "estructuramayordomo"
TEXT_EXTENSIONS = {
    ".c", ".h", ".s", ".S", ".py", ".sh", ".bash", ".md", ".txt",
    ".json", ".html", ".htm", ".css", ".js", ".mjs", ".ts", ".tsx",
    ".jsx", ".xml", ".yml", ".yaml", ".toml", ".ini", ".cfg", ".conf",
    ".ld", ".mk", ".make", ".patch", ".diff", ".csv", ".rst", ".properties",
}
TEXT_NAMES = {
    "Makefile", "GNUmakefile", "Dockerfile", "Containerfile", "LICENSE",
    "NOTICE", "COPYING", ".gitignore", ".dockerignore", ".editorconfig",
    ".gitattributes", ".gitmodules", "protected.txt", "requirements.txt",
    "pyproject.toml", "setup.cfg", "setup.py", "CMakeLists.txt",
}
BINARY_SUFFIXES = {
    ".img", ".iso", ".elf", ".bin", ".o", ".a", ".so", ".dylib", ".dll",
    ".exe", ".pcap", ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico",
    ".bmp", ".pdf", ".zip", ".gz", ".xz", ".7z", ".tar", ".woff", ".woff2",
    ".ttf", ".otf", ".pyc", ".checkpoint", ".dump",
}
EXCLUDED_DIRS = {
    ".git", "build", "isodir", "__pycache__", ".parches_backup", "snapshots",
    "build_backup_src", "node_modules", ".venv", "venv", "dist", "target",
}
RELEVANT_FILES = [
    ("src/agent/agent.c", "Registro de agentes, entrada agent_run, clasificación y ejecución rápida de Mayordomo; orquestación y fallback LLM."),
    ("src/agent/agent.h", "Contrato público del agente y buffer de respuesta compartido."),
    ("src/agent/shell.c", "Despachador de comandos nativos, comandos de RAM/disco/archivos y puente dispatch_agent_command."),
    ("src/agent/shell.h", "Declaraciones del shell y API de ejecución de comandos por agentes."),
    ("src/agent/llm.c", "Cliente/puente del modelo utilizado por el camino general de SOMA."),
    ("src/agent/llm.h", "Contrato del cliente LLM."),
    ("src/fs/fs.c", "Implementación VFS/RamFS y semilla de perfiles persistentes."),
    ("src/fs/fs.h", "API y límites del sistema de archivos."),
    ("src/ui/gui.c", "Entrada del chat, interfaz gráfica y ventana jerárquica Agentes."),
    ("src/ui/gui.h", "API pública para abrir aplicaciones gráficas."),
    ("src/icons/icons.h", "Registro/selector de iconos."),
    ("src/icons/icon_agents.h", "Mapa de bits del icono de Agentes; lo crea el parche."),
    ("Makefile", "Objetos y reglas de compilación de ambas arquitecturas."),
    ("linker_x86_64.ld", "Mapa de memoria del kernel y ubicación protegida de la cola VirtIO x86_64."),
    ("linker_aarch64.ld", "Mapa de memoria y secciones de enlace para ARM64."),
    ("src/core/sysinfo.h", "Interfaz de telemetría del sistema, si existe en esta versión."),
    ("src/core/mem.c", "Gestión de heap usada por los comandos de memoria, si está presente."),
    ("src/arch/x86_64/sysinfo.c", "Implementación de telemetría x86_64, si está presente."),
    ("src/arch/aarch64/sysinfo.c", "Implementación de telemetría ARM64, si está presente."),
]


def find_root(requested: Path | None) -> Path:
    if requested is not None:
        return requested.expanduser().resolve()
    cwd = Path.cwd().resolve()
    if (cwd / "Makefile").is_file() and (cwd / "src/agent/agent.c").is_file():
        return cwd
    script_dir = Path(__file__).resolve().parent
    if (script_dir / "Makefile").is_file() and (script_dir / "src/agent/agent.c").is_file():
        return script_dir
    raise FileNotFoundError(
        "No encuentro la raiz de SOMA. Ejecuta el script desde el repositorio "
        "o indica --root /ruta/a/soma."
    )


def backup_name(name: str) -> bool:
    lower = name.lower()
    return (
        ".bak" in lower
        or lower.endswith(("~", ".orig", ".rej", ".old", ".swp", ".tmp"))
        or lower.startswith(".#")
    )


def decode_text(raw: bytes, path: Path) -> tuple[str | None, str | None]:
    if b"\x00" in raw:
        return None, "contenido binario (byte NUL)"
    known_text = path.name in TEXT_NAMES or path.suffix in TEXT_EXTENSIONS
    try:
        text = raw.decode("utf-8")
        encoding = "UTF-8"
    except UnicodeDecodeError:
        # Para textos historicos no UTF-8 conserva todos los bytes con Latin-1.
        text = raw.decode("latin-1")
        encoding = "Latin-1"
    if not known_text:
        controls = sum(1 for ch in text if ord(ch) < 32 and ch not in "\t\r\n\f")
        if controls > max(2, len(text) // 100):
            return None, "fichero no textual/no fuente"
    return text, encoding


def discover(root: Path, output_path: Path) -> tuple[list[dict], list[tuple[str, str]]]:
    included: list[dict] = []
    excluded: list[tuple[str, str]] = []
    output = output_path.resolve()

    for current, dirs, names in os.walk(root, topdown=True, followlinks=False):
        cur = Path(current)
        kept_dirs: list[str] = []
        for dirname in sorted(dirs):
            candidate = cur / dirname
            rel = candidate.relative_to(root).as_posix()
            if dirname in EXCLUDED_DIRS or dirname.startswith((".backup_", ".backup-", ".parche_backup")):
                excluded.append((rel + "/", "directorio generado, binario o de copias historicas"))
            else:
                kept_dirs.append(dirname)
        dirs[:] = kept_dirs

        for name in sorted(names):
            path = cur / name
            try:
                resolved = path.resolve()
            except OSError:
                excluded.append((path.relative_to(root).as_posix(), "no se pudo resolver la ruta"))
                continue
            rel = path.relative_to(root).as_posix()
            if resolved == output or name == OUTPUT_NAME:
                excluded.append((rel, "archivo de salida; se excluye para evitar autorreferencia"))
                continue
            if backup_name(name):
                excluded.append((rel, "copia historica/temporal; no es una fuente activa de compilacion"))
                continue
            suffix = path.suffix.lower()
            if suffix in BINARY_SUFFIXES:
                try:
                    size = path.stat().st_size
                    excluded.append((rel, f"binario/artefacto no textual ({size} bytes)"))
                except OSError:
                    excluded.append((rel, "binario/artefacto no textual"))
                continue
            try:
                raw = path.read_bytes()
            except OSError as exc:
                excluded.append((rel, f"error de lectura: {exc}"))
                continue
            text, encoding_or_reason = decode_text(raw, path)
            if text is None:
                excluded.append((rel, encoding_or_reason or "no textual"))
                continue
            included.append({
                "path": rel,
                "raw": raw,
                "text": text,
                "bytes": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "encoding": encoding_or_reason or "desconocida",
            })

    included.sort(key=lambda item: (item["path"].split("/")[0] not in {"src", "tools", "tests"}, item["path"].lower()))
    excluded.sort(key=lambda item: item[0].lower())
    return included, excluded


def registry_agents(agent_c: str) -> list[tuple[str, str]]:
    match = re.search(r"AGENT_REGISTRY\[\]\s*=\s*\{(.*?)\n\};", agent_c, re.S)
    if not match:
        return []
    return re.findall(r'^\s*\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,', match.group(1), re.M)


def write_report(root: Path, output: Path, records: list[dict], excluded: list[tuple[str, str]]) -> None:
    by_path = {item["path"]: item for item in records}
    agent_item = by_path.get("src/agent/agent.c")
    agent_c = agent_item["text"] if agent_item else ""
    installed = "AGENTE_MAYORDOMO_PATCH_V1" in agent_c and '"mayordomo"' in agent_c
    agents = registry_agents(agent_c)
    relevant_existing = [(path, description) for path, description in RELEVANT_FILES if path in by_path]
    generated_at = dt.datetime.now().astimezone().isoformat(timespec="seconds")

    with output.open("w", encoding="utf-8", newline="") as out:
        out.write("=" * 100 + "\n")
        out.write("ESTRUCTURA GENERAL DE SOMA Y DOSSIER COMPLETO DEL AGENTE MAYORDOMO\n")
        out.write("=" * 100 + "\n")
        out.write(f"Proyecto: {root}\nGenerado: {generated_at}\nSalida: {output.name}\n")
        out.write(f"Parche Mayordomo detectado: {'SI' if installed else 'NO'}\n")
        out.write(f"Archivos de texto incluidos íntegramente: {len(records)}\n")
        out.write(f"Archivos/rutas excluidos con motivo: {len(excluded)}\n")
        out.write("Algoritmo: cada fuente incluida conserva su texto completo; se registra tamaño en bytes y SHA-256 del original.\n")

        out.write("\n\n" + "=" * 100 + "\n1. ESQUEMA GENERAL DEL SISTEMA\n" + "=" * 100 + "\n")
        out.write(r'''SOMA OS (kernel experimental freestanding, x86_64 y AArch64)
|
+-- Arranque y portabilidad
|   +-- grub/ + boot.s + linker_*.ld + Makefile
|   +-- HAL por arquitectura: interrupciones, reloj, MMU/PMM y dispositivos
|
+-- Kernel / core
|   +-- memoria y heap (mem.c), hilos cooperativos (thread.c), mutex (mutex.c)
|   +-- información/telemetría del sistema, puerta de arranque y recuperación
|
+-- Entrada/salida del sistema
|   +-- shell.c: tabla de comandos, políticas de ejecución y puente para agentes
|   +-- fs.c: VFS/RamFS y acceso al almacenamiento persistente
|   +-- virtio_blk.c: dispositivo de bloque virtual
|   +-- red TCP/IP, DNS, HTTP y servicios auxiliares
|
+-- Interfaz de usuario
|   +-- gui.c/gui.h: escritorio, chat global de SOMA y ventanas de aplicaciones
|   +-- icons/: mapas de bits y registro de iconos
|   +-- apps/: navegador, gestor de archivos, notas, memoria, imagen y maquetador
|   +-- Agentes: vista jerárquica de los agentes disponibles (cuando se aplica el parche)
|
+-- Subsistema agentico (src/agent/)
    +-- agent.c: registro, entrada agent_run, contexto/memoria, delegación y ciclo LLM
    +-- shell.c: comandos deterministas invocables por el agente
    +-- llm.c: integración del modelo para consultas no resueltas por herramientas nativas
''')

        out.write("\n" + "=" * 100 + "\n2. ESQUEMA JERÁRQUICO DE AGENTES\n" + "=" * 100 + "\n")
        if agents:
            out.write("Registro detectado en src/agent/agent.c:\n")
            for name, title in agents:
                out.write(f"  - {name}: {title}\n")
        else:
            out.write("No se pudo analizar automáticamente AGENT_REGISTRY; consulta el fuente íntegro en el apartado 6.\n")
        out.write("\nJerarquía operativa de referencia:\n")
        out.write("SOMA (orquestador principal; clasifica la petición y coordina)\n")
        out.write("|-- Mayordomo (operaciones locales: RAM/heap, disco/VFS, listar/leer/buscar/crear archivos y carpetas)\n")
        out.write("|   `-- Ejecuta código nativo / dispatch_agent_command; no necesita ciclo ReAct para las operaciones reconocidas.\n")
        out.write("|-- Buscador (búsqueda web, actualidad e investigación; usa las capacidades de red disponibles)\n")
        out.write("`-- Maquetador (transformación y composición de documentos mediante el formato SOMA-LAYOUT)\n")
        out.write("\nESTADO: ")
        if installed:
            out.write("el marcador del parche se ha encontrado en agent.c; Mayordomo está instalado en el código fuente.\n")
        else:
            out.write("el parche no se detectó en agent.c. La jerarquía anterior describe el destino previsto; no implica que Mayordomo esté instalado.\n")

        out.write("\n" + "=" * 100 + "\n3. FLUJO DE UNA PETICIÓN AL SISTEMA\n" + "=" * 100 + "\n")
        out.write(r'''USUARIO
  -> GUI/chat SOMA o shell
     -> agent_run("soma", petición)
        -> clasificador determinista de Mayordomo (si está instalado)
           -> agent_run("mayordomo", petición)
              -> mayordomo_run()
                 -> operación concreta
                    +-- RAM/heap: comando nativo `free`
                    +-- almacenamiento/VFS: comando nativo `df`
                    +-- contenido/listado del disco lógico: `ls`/VFS
                    +-- búsqueda por nombre: listado VFS + coincidencia de texto
                    +-- leer/crear/eliminar archivo: comandos/API VFS con las políticas del shell
                    `-- carpeta: marcador de directorio porque el VFS representa carpetas implícitamente
              -> agent_last_response / pizarra compartida
           -> el chat muestra el resultado
        -> si no es una tarea de sistema reconocida: camino habitual ReAct/LLM y delegación
           +-- Buscador para investigación
           `-- Maquetador para composición de documentos

Principio de latencia: las tareas reconocidas salen antes del bucle de razonamiento LLM;
la ejecución es una clasificación por cadenas + comandos/aplicaciones nativos. Peticiones
ambiguas no se deben tratar como confirmación de cambios destructivos; especifica la ruta.
''')

        out.write("\n" + "=" * 100 + "\n4. ARCHIVOS QUE FORMAN EL CAMINO DE MAYORDOMO\n" + "=" * 100 + "\n")
        for path, description in relevant_existing:
            out.write(f"  {path}\n    {description}\n")
        out.write("\nTodos los ficheros de texto activos detectados aparecen íntegros en el apartado 6, incluidos los restantes módulos usados indirectamente.\n")

        out.write("\n" + "=" * 100 + "\n5. INVENTARIO COMPLETO Y SHA-256\n" + "=" * 100 + "\n")
        for index, item in enumerate(records, start=1):
            out.write(f"{index:04d}. {item['path']} | bytes={item['bytes']} | encoding={item['encoding']} | sha256={item['sha256']}\n")
        out.write("\nEXCLUSIONES EXPLÍCITAS (no son código fuente activo o no son texto):\n")
        for path, reason in excluded:
            out.write(f"  - {path}: {reason}\n")
        out.write("\nCriterio de cobertura: no se recortan archivos de texto incluidos. Los artefactos binarios, imagen del disco virtual, directorios generados y copias históricas se excluyen por motivo y ruta arriba.\n")

        out.write("\n" + "=" * 100 + "\n6. CÓDIGO FUENTE ÍNTEGRO DE TODOS LOS ARCHIVOS DE TEXTO ACTIVOS\n" + "=" * 100 + "\n")
        out.write("Cada bloque contiene la totalidad del archivo indicado. No se omiten secciones ni se sustituyen cuerpos por puntos suspensivos.\n")
        for index, item in enumerate(records, start=1):
            out.write("\n" + "#" * 100 + "\n")
            out.write(f"BEGIN SOURCE {index:04d}/{len(records):04d}: {item['path']}\n")
            out.write(f"ORIGINAL_BYTES={item['bytes']} ENCODING={item['encoding']} SHA256={item['sha256']}\n")
            out.write("#" * 100 + "\n")
            out.write(item["text"])
            if item["text"] and not item["text"].endswith("\n"):
                out.write("\n")
            out.write("\n" + "-" * 100 + "\n")
            out.write(f"END SOURCE {index:04d}: {item['path']}\n")
        out.write("\n" + "=" * 100 + "\nFIN DEL DOSSIER\n" + "=" * 100 + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Exporta la arquitectura de SOMA y el código íntegro a estructuramayordomo")
    parser.add_argument("--root", type=Path, default=None, help="raíz del proyecto SOMA; por defecto se usa el directorio actual si es un repositorio")
    parser.add_argument("--output", type=Path, default=None, help="ruta de salida opcional; por defecto <raiz>/estructuramayordomo")
    args = parser.parse_args()
    try:
        root = find_root(args.root)
    except FileNotFoundError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    output = args.output.expanduser().resolve() if args.output else root / OUTPUT_NAME
    required = [root / "src/agent/agent.c", root / "src/agent/shell.c", root / "src/fs/fs.c"]
    missing = [str(path.relative_to(root)) for path in required if not path.is_file()]
    if missing:
        print("ERROR: faltan fuentes esenciales para reconocer el proyecto SOMA: " + ", ".join(missing), file=sys.stderr)
        return 2
    records, excluded = discover(root, output)
    if not records:
        print("ERROR: no se encontraron archivos de texto para exportar.", file=sys.stderr)
        return 2
    try:
        write_report(root, output, records, excluded)
    except OSError as exc:
        print(f"ERROR: no se pudo escribir {output}: {exc}", file=sys.stderr)
        return 1
    size = output.stat().st_size
    agent_file = root / "src/agent/agent.c"
    installed = "AGENTE_MAYORDOMO_PATCH_V1" in agent_file.read_text(encoding="utf-8", errors="replace")
    print(f"[OK] Informe creado: {output}")
    print(f"[OK] Fuentes/documentos completos incluidos: {len(records)}")
    print(f"[OK] Rutas excluidas con motivo: {len(excluded)}")
    print(f"[OK] Tamaño del informe: {size:,} bytes")
    print(f"[INFO] Mayordomo detectado en agent.c: {'sí' if installed else 'no; aplica parche.py para instalarlo'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
