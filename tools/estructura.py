#!/usr/bin/env python3

from pathlib import Path
import mimetypes


# ------------------------------------------------------------
# Configuración
# ------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parent.parent
OUTPUT_FILE = PROJECT_ROOT / "estructura_proyecto.txt"

# Directorios que normalmente no necesitamos incluir.
EXCLUDED_DIRS = {
    ".git",
    ".idea",
    ".vscode",
    "__pycache__",
    "build",
    "dist",
    "tools",
}

# Archivos que pueden contener credenciales o secretos.
# Se excluyen deliberadamente.
EXCLUDED_FILES = {
    ".env",
    ".env.local",
    ".env.production",
    ".env.development",
    "estructura_proyecto.txt",
}

# Extensiones que consideramos texto/código.
TEXT_EXTENSIONS = {
    ".c",
    ".h",
    ".s",
    ".asm",
    ".S",
    ".ld",
    ".mk",
    ".make",
    ".py",
    ".sh",
    ".bash",
    ".zsh",
    ".txt",
    ".md",
    ".json",
    ".yaml",
    ".yml",
    ".toml",
    ".cfg",
    ".conf",
    ".ini",
    ".xml",
    ".html",
    ".css",
    ".js",
    ".ts",
    ".rs",
    ".go",
    ".cpp",
    ".cc",
    ".hpp",
}

# Archivos sin extensión que queremos tratar como texto.
TEXT_FILENAMES = {
    "Dockerfile",
    "Makefile",
    "LICENSE",
    "README",
    "README.md",
}


# ------------------------------------------------------------
# Utilidades
# ------------------------------------------------------------

def is_excluded(path: Path) -> bool:
    """Indica si un archivo/directorio debe excluirse."""
    if path.name in EXCLUDED_FILES:
        return True

    return any(part in EXCLUDED_DIRS for part in path.parts)


def is_probably_text(path: Path) -> bool:
    """Determina si un archivo parece ser texto."""
    if path.name in TEXT_FILENAMES:
        return True

    if path.suffix in TEXT_EXTENSIONS:
        return True

    mime_type, _ = mimetypes.guess_type(str(path))

    if mime_type and mime_type.startswith("text/"):
        return True

    # Para archivos sin extensión, hacemos una comprobación pequeña.
    try:
        data = path.read_bytes()[:4096]

        if b"\x00" in data:
            return False

        data.decode("utf-8")
        return True

    except (OSError, UnicodeDecodeError):
        return False


def get_files() -> list[Path]:
    """Obtiene todos los archivos relevantes del proyecto."""
    files = []

    for path in PROJECT_ROOT.rglob("*"):
        if not path.is_file():
            continue

        relative = path.relative_to(PROJECT_ROOT)

        if is_excluded(relative):
            continue

        files.append(path)

    return sorted(files, key=lambda p: str(p.relative_to(PROJECT_ROOT)).lower())


def build_tree(files: list[Path]) -> str:
    """Construye una representación tipo árbol."""
    lines = [PROJECT_ROOT.name + "/"]

    tree = {}

    for file_path in files:
        relative = file_path.relative_to(PROJECT_ROOT)
        parts = relative.parts

        current = tree

        for part in parts:
            current = current.setdefault(part, {})

    def render(node: dict, prefix: str = "") -> list[str]:
        result = []

        names = sorted(node.keys(), key=str.lower)

        for index, name in enumerate(names):
            is_last = index == len(names) - 1
            connector = "└── " if is_last else "├── "

            result.append(prefix + connector + name)

            children = node[name]

            if children:
                new_prefix = prefix + ("    " if is_last else "│   ")
                result.extend(render(children, new_prefix))

        return result

    lines.extend(render(tree))

    return "\n".join(lines)


def read_file(path: Path) -> str:
    """Lee un archivo intentando conservar al máximo su contenido."""
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        try:
            return path.read_text(encoding="latin-1")
        except Exception as exc:
            return f"[ERROR AL LEER ARCHIVO: {exc}]"
    except Exception as exc:
        return f"[ERROR AL LEER ARCHIVO: {exc}]"


# ------------------------------------------------------------
# Generación del informe
# ------------------------------------------------------------

def generate_report() -> None:
    files = get_files()

    tree = build_tree(files)

    output = []

    output.append("=" * 80)
    output.append("ESTRUCTURA Y CÓDIGO DEL PROYECTO MYOS")
    output.append("=" * 80)
    output.append("")
    output.append(f"Proyecto: {PROJECT_ROOT}")
    output.append(f"Archivos encontrados: {len(files)}")
    output.append("")

    output.append("=" * 80)
    output.append("ESTRUCTURA")
    output.append("=" * 80)
    output.append("")
    output.append(tree)
    output.append("")

    output.append("=" * 80)
    output.append("CONTENIDO DE LOS ARCHIVOS")
    output.append("=" * 80)
    output.append("")

    for path in files:
        relative = path.relative_to(PROJECT_ROOT)

        output.append("")
        output.append("#" * 80)
        output.append(f"# ARCHIVO: {relative}")
        output.append(f"# TAMAÑO: {path.stat().st_size} bytes")
        output.append("#" * 80)
        output.append("")

        if not is_probably_text(path):
            output.append("[ARCHIVO BINARIO - CONTENIDO OMITIDO]")
            output.append("")
            continue

        # Omitir el contenido de los generadores de estructura
        if path.name.startswith("estructura") and path.suffix == ".py":
            output.append("[CONTENIDO OMITIDO - SCRIPT DE ESTRUCTURA]")
            output.append("")
            continue

        content = read_file(path)

        output.append(content)

        if not content.endswith("\n"):
            output.append("")

    OUTPUT_FILE.write_text(
        "\n".join(output),
        encoding="utf-8",
    )

    print()
    print("Informe generado correctamente.")
    print()
    print(f"Proyecto : {PROJECT_ROOT}")
    print(f"Archivos : {len(files)}")
    print(f"Salida   : {OUTPUT_FILE}")
    print()


# ------------------------------------------------------------
# Main
# ------------------------------------------------------------

if __name__ == "__main__":
    generate_report()