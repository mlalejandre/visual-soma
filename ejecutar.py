#!/usr/bin/env python3
# SOMA DEVELOPMENT RUNNER - THE SINGULARITY LOOP (v2 Multi-Arch)
#
# Protocolo del buzon (LBA 1..16, 8 KiB):
#   [0..7]  "PATCHv02"
#   [8..11] longitud u32 little-endian
#   [12..]  texto: uno o mas bloques
#             FILE: src/archivo.c
#             <<<<<<< SEARCH
#             texto exacto (debe aparecer UNA vez)
#             =======
#             texto nuevo
#             >>>>>>> REPLACE
# Resultado para la IA: LBA 20..23 (2 KiB, texto ASCII). Leer con 'sector_read 20'.

from pathlib import Path
import argparse
import difflib
import re
import shutil
import subprocess
import time
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))
import llm_server
import search_proxy

ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
SRC_DIR = ROOT / "src"
BACKUP_DIR = ROOT / "build_backup_src"
HDD = ROOT / "hdd.img"
DOCKER_IMAGE = "soma-toolchain"

SECTOR = 512
MAILBOX_LBA = 1
MAILBOX_SIZE = 8192
MAILBOX_HDR = 12
MAGIC_V2 = b"PATCHv02"
MAGIC_V1 = b"PATCHv01"
RESULT_LBA = 20
RESULT_SIZE = 2048
BOOT_GATE_LBA = 24
QEMU_EXIT_GATE_OK = (0x20 << 1) | 1    # 0x41 = 65
QEMU_EXIT_GATE_FAIL = (0x22 << 1) | 1  # 0x45 = 69
QEMU_EXIT_PANIC = (0x23 << 1) | 1      # 0x47 = 71
QEMU_EXIT_PATCH = (0x10 << 1) | 1      # qemu_exit(0x10) -> codigo 0x21

ALLOWED_EXT = {".c", ".h", ".s"}
FILE_RE = re.compile(
    r"^FILE:[ \t]*(\S+)[ \t]*\n(.*?)(?=^FILE:[ \t]*\S+[ \t]*\n|\Z)",
    re.S | re.M,
)
BLOCK_RE = re.compile(
    r"<<<<<<< SEARCH\n(.*?)\n=======\n(.*?)\n?>>>>>>> REPLACE",
    re.S,
)


# ---------------------------------------------------------------- utilidades

def run(command: list[str], title: str) -> int:
    print(f"\n{'=' * 72}\n{title}\n{'=' * 72}\n\n$ {' '.join(command)}\n")
    return subprocess.run(command, cwd=ROOT).returncode


def get_host_uid_gid() -> tuple[str, str]:
    uid = subprocess.check_output(["id", "-u"], text=True).strip()
    gid = subprocess.check_output(["id", "-g"], text=True).strip()
    return uid, gid


def docker_image_exists() -> bool:
    res = subprocess.run(["docker", "image", "inspect", DOCKER_IMAGE],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return res.returncode == 0


def git(*args: str) -> None:
    if not (ROOT / ".git").exists() or not shutil.which("git"):
        return
    subprocess.run(["git", *args], cwd=ROOT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def git_snapshot(message: str) -> None:
    git("add", "src")
    git("commit", "-m", message)


def build(uid: str, gid: str, arch: str = "x86_64") -> tuple[int, str]:
    cmd = ["docker", "run", "--rm", "--platform", "linux/amd64",
           "-u", f"{uid}:{gid}", "-v", f"{ROOT}:/soma", DOCKER_IMAGE, "make", f"ARCH={arch}"]
    print(f"\n{'=' * 72}\nCOMPILANDO SOMA BARE-METAL ({arch})\n{'=' * 72}\n\n$ {' '.join(cmd)}\n")
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    print(out)
    return p.returncode, out


def summarize_errors(out: str, limit: int = 6) -> list[str]:
    hits = [l.strip() for l in out.splitlines()
            if re.search(r"error|undefined reference|\*\*\*", l, re.I)]
    return hits[:limit] or out.strip().splitlines()[-limit:]


# ---------------------------------------------------------------- buzon

def read_mailbox() -> tuple[str | None, str]:
    if not HDD.exists():
        return None, ""
    with open(HDD, "rb") as f:
        f.seek(MAILBOX_LBA * SECTOR)
        raw = f.read(MAILBOX_SIZE)
    magic = raw[:8]
    if magic == MAGIC_V1:
        return "v1", ""
    if magic != MAGIC_V2:
        return None, ""
    n = int.from_bytes(raw[8:12], "little")
    if n == 0 or n > MAILBOX_SIZE - MAILBOX_HDR:
        return "bad", ""
    return "ok", raw[MAILBOX_HDR:MAILBOX_HDR + n].decode("utf-8", errors="replace")


def clear_mailbox() -> None:
    with open(HDD, "rb+") as f:
        f.seek(MAILBOX_LBA * SECTOR)
        f.write(b"\x00" * MAILBOX_SIZE)


def write_result(status: str, detail: str = "") -> None:
    text = f"[PARCHE-RESULTADO] {status}"
    if detail:
        text += " | " + " | ".join(str(detail).splitlines())
    data = text.encode("ascii", "replace")[:RESULT_SIZE - 1]
    data += b"\x00" * (RESULT_SIZE - len(data))
    with open(HDD, "rb+") as f:
        f.seek(RESULT_LBA * SECTOR)
        f.write(data)
    print(f"[RESULTADO -> LBA {RESULT_LBA}] {text[:300]}")


# ---------------------------------------------------------------- parches

def arm_boot_gate() -> None:
    with open(HDD, "rb+") as f:
        f.seek(BOOT_GATE_LBA * SECTOR)
        data = b"GATE_TEST_REQ\x00" + b"\x00" * (SECTOR - 14)
        f.write(data)


def disarm_boot_gate() -> None:
    with open(HDD, "rb+") as f:
        f.seek(BOOT_GATE_LBA * SECTOR)
        f.write(b"\x00" * SECTOR)

RESUME_FLAG_LBA = 25

def arm_agent_resume() -> None:
    with open(HDD, "rb+") as f:
        f.seek(RESUME_FLAG_LBA * SECTOR)
        f.write(b"RESUME_REQ\x00" + b"\x00" * (SECTOR - 11))


PROTECTED_FILES = {
    'boot.s', 'idt.c', 'idt.h', 'io.h', 'srcfs.c', 'srcfs.h',
    'virtio_blk.c', 'virtio_blk.h', 'boot_gate.c', 'boot_gate.h'
}


def is_target_protected(path: Path) -> bool:
    if path.name in PROTECTED_FILES:
        return True
    prot_txt = ROOT / 'protected.txt'
    if prot_txt.exists():
        for line in prot_txt.read_text(encoding='utf-8').splitlines():
            line = line.strip()
            if line and not line.startswith('#') and Path(line).name == path.name:
                return True
    return False


def safe_target(name: str) -> Path | None:
    if "\x00" in name:
        return None
    p = (ROOT / name).resolve()
    src = SRC_DIR.resolve()
    if not (p.is_relative_to(src) and p.is_file()):
        base = Path(name).name
        candidates = [f for f in src.rglob(base) if f.is_file() and f.suffix.lower() in ALLOWED_EXT]
        if len(candidates) == 1:
            p = candidates[0]
    if p.is_relative_to(src) and p.suffix.lower() in ALLOWED_EXT and p.is_file():
        if is_target_protected(p):
            print(f"[SEGURIDAD HOST] Modificacion denegada: '{p.name}' es un archivo protegido.")
            return None
        return p
    return None


def compute_patch(text: str):
    text = text.replace("\r\n", "\n")
    sections = FILE_RE.findall(text)
    if not sections:
        return None, "No hay secciones 'FILE: src/...'"

    contents: dict[Path, list[str]] = {}
    for name, body in sections:
        target = safe_target(name)
        if target is None:
            return None, f"FILE invalido '{name}' (solo src/*.c|h|s existentes)"
        if target not in contents:
            old = target.read_text(encoding="utf-8")
            contents[target] = [old, old]
        blocks = BLOCK_RE.findall(body)
        if not blocks:
            return None, f"Sin bloques SEARCH/REPLACE para {name}"
        for search, repl in blocks:
            cur = contents[target][1]
            n = cur.count(search) if search else 0
            if n != 1:
                return None, (f"SEARCH aparece {n} veces (debe ser 1) en {name}: "
                              f"{search[:60]!r}")
            contents[target][1] = cur.replace(search, repl, 1)

    if len(sections) > 2:
        return None, f"Radio de mutacion excedido: maximo 2 archivos por parche (propuestos: {len(sections)})"

    for p, (o, n) in contents.items():
        rel_name = p.relative_to(ROOT)
        body = next((b for name, b in sections if safe_target(name) == p), "")
        blocks = BLOCK_RE.findall(body)
        if len(blocks) > 3:
            return None, f"Radio de mutacion excedido en {rel_name}: maximo 3 bloques por archivo (propuestos: {len(blocks)})"
        for search, repl in blocks:
            if not search.strip():
                return None, f"Bloque SEARCH vacio no permitido en {rel_name}"
            s_lines = len(search.splitlines())
            r_lines = len(repl.splitlines())
            if s_lines > 120:
                return None, f"Bloque SEARCH demasiado grande en {rel_name} ({s_lines} lineas > max 120)"
            if r_lines > 150:
                return None, f"Bloque REPLACE demasiado grande en {rel_name} ({r_lines} lineas > max 150)"

    changes = {p: (o, n) for p, (o, n) in contents.items() if o != n}
    if not changes:
        return None, "El parche no cambia nada"
    return changes, None


def show_diff(changes) -> None:
    for p, (old, new) in changes.items():
        rel = p.relative_to(ROOT)
        diff = difflib.unified_diff(old.splitlines(), new.splitlines(),
                                    f"a/{rel}", f"b/{rel}", lineterm="")
        print("\n".join(diff))


def apply_changes(changes) -> None:
    shutil.rmtree(BACKUP_DIR, ignore_errors=True)
    shutil.copytree(SRC_DIR, BACKUP_DIR)
    for p, (_, new) in changes.items():
        p.unlink(missing_ok=True)
        p.write_text(new, encoding="utf-8")
    time.sleep(0.6)


def restore_src() -> None:
    shutil.copytree(BACKUP_DIR, SRC_DIR, dirs_exist_ok=True)
    time.sleep(0.4)


def handle_mailbox(approve: bool) -> bool | None:
    state, text = read_mailbox()
    if state is None:
        return None
    clear_mailbox()

    print("\n" + "=" * 72)
    print("!!! PUENTE HOST-BRIDGE: LA IA PROPONE MODIFICAR EL CODIGO !!!")
    print("=" * 72)

    if state == "v1":
        write_result("REJECTED", "formato PATCHv01 obsoleto; usa FILE/SEARCH/REPLACE")
        return False
    if state == "bad":
        write_result("REJECTED", "longitud de parche invalida")
        return False

    changes, err = compute_patch(text)
    if err:
        print(f"[RECHAZADO] {err}")
        write_result("REJECTED", err)
        return False

    show_diff(changes)

    if approve:
        ans = input("\nAplicar este parche? [y/N] ").strip().lower()
        if ans != "y":
            write_result("REJECTED", "rechazado por el humano")
            return False

    git_snapshot("SOMA: snapshot antes de parche de la IA")
    auto_snap = ROOT / "snapshots" / f"auto_pre_patch_{int(time.time())}"
    auto_snap.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SRC_DIR, auto_snap / "src", dirs_exist_ok=True)
    if HDD.exists(): shutil.copy2(HDD, auto_snap / "hdd.img")
    print(f"[REPRODUCIBILIDAD] Snapshot automatico guardado en snapshots/{auto_snap.name}/")
    apply_changes(changes)
    names = ", ".join(str(p.relative_to(ROOT)) for p in changes)
    print(f"\n[OK] Parche aplicado a: {names}")
    return True


# ---------------------------------------------------------------- SRCFS

SRCFS_LBA = 8192
SRCFS_INDEX_SECTORS = 8
SRCFS_ENTRY = 64
SRCFS_LIMIT_LBA = 16384


# PARCHE 047: el indice SRCFS solo admite 63 entradas; no empaquetar la otra arquitectura.
PACK_ARCH = None


def _otra_arch(p: Path) -> bool:
    if PACK_ARCH is None:
        return False
    parts = p.relative_to(SRC_DIR).parts
    return len(parts) >= 2 and parts[0] == "arch" and parts[1] != PACK_ARCH


def pack_sources() -> None:
    files = sorted(p for p in SRC_DIR.rglob("*")
                   if p.is_file() and p.suffix.lower() in ALLOWED_EXT and not _otra_arch(p))
    max_entries = SRCFS_INDEX_SECTORS * SECTOR // SRCFS_ENTRY - 1
    if len(files) > max_entries:
        print(f"[SRCFS] AVISO: {len(files)} archivos; solo caben {max_entries}")
        files = files[:max_entries]

    index = bytearray(SRCFS_INDEX_SECTORS * SECTOR)
    index[0:8] = b"SRCFSv01"
    index[8:12] = len(files).to_bytes(4, "little")

    lba = SRCFS_LBA + SRCFS_INDEX_SECTORS
    blobs = []
    for i, p in enumerate(files, 1):
        data = p.read_bytes()
        rel_path = str(p.relative_to(SRC_DIR)).replace("\\", "/")
        name = rel_path.encode("utf-8")[:47]
        off = i * SRCFS_ENTRY
        index[off:off + len(name)] = name
        index[off + 48:off + 52] = lba.to_bytes(4, "little")
        index[off + 52:off + 56] = len(data).to_bytes(4, "little")
        blobs.append((lba, data))
        lba += (len(data) + SECTOR - 1) // SECTOR

    if lba > SRCFS_LIMIT_LBA:
        print(f"[SRCFS] AVISO: las fuentes ocupan hasta LBA {lba} (limite {SRCFS_LIMIT_LBA}); truncando empaquetado seguro.")
        # Ajustar para escribir solo hasta el límite seguro sin colisionar con RamFS
        lba = SRCFS_LIMIT_LBA

    with open(HDD, "rb+") as f:
        f.seek(SRCFS_LBA * SECTOR)
        f.write(index)
        for blba, data in blobs:
            f.seek(blba * SECTOR)
            f.write(data + b"\x00" * (-len(data) % SECTOR))
    print(f"[SRCFS] {len(files)} fuentes empaquetadas en LBA {SRCFS_LBA}..{lba - 1}")


# ---------------------------------------------------------------- main

def choose_arch() -> str:
    print("\nPlataforma de destino:")
    print("  1) x86_64   [Intel / AMD - PC estándar con GRUB / ISO]")
    print("  2) aarch64  [ARM64 - Cortex-A72 / QEMU virt ELF]")
    while True:
        try:
            choice = input("\nElige plataforma [1-2, Enter=1]: ").strip()
        except (EOFError, KeyboardInterrupt):
            return "x86_64"
        if choice in ("", "1"):
            return "x86_64"
        if choice == "2":
            return "aarch64"
        print("Opción no válida. Introduce 1 o 2.")


def get_qemu_commands(arch: str) -> tuple[str, list[str], list[str]]:
    if arch == "x86_64":
        qemu_bin = "qemu-system-x86_64"
        canary = [
            qemu_bin, "-machine", "pc", "-m", "256M", "-no-reboot",
            "-cdrom", "build/soma.iso", "-netdev", "user,id=net0,hostfwd=tcp::8090-:80",
            "-device", "virtio-net-pci,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=virtio",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-serial", "stdio", "-display", "none",
        ]
        main_cmd = [
            qemu_bin, "-machine", "pc", "-m", "256M", "-no-reboot", "-d", "guest_errors,cpu_reset", "-D", "build/qemu.log",
            "-cdrom", "build/soma.iso", "-netdev", "user,id=net0,hostfwd=tcp::8090-:80", "-device", "virtio-net-pci,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=virtio",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-object", "filter-dump,id=f1,netdev=net0,file=build/net.pcap",
            "-serial", "stdio",
        ]
    else:  # aarch64 – virtio-mmio (más simple y fiable en QEMU virt)
        qemu_bin = "qemu-system-aarch64"
        canary = [
            qemu_bin, "-M", "virt", "-cpu", "cortex-a72", "-m", "256M", "-no-reboot", "-semihosting",
            "-kernel", "build/aarch64/soma.elf",
            "-netdev", "user,id=net0,hostfwd=tcp::8090-:80",
            "-device", "virtio-net-device,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=none,id=hd0",
            "-device", "virtio-blk-device,drive=hd0",
            "-serial", "stdio", "-display", "none",
        ]
        main_cmd = [
            qemu_bin, "-M", "virt", "-cpu", "cortex-a72", "-m", "256M", "-no-reboot", "-semihosting",
            "-kernel", "build/aarch64/soma.elf",
            "-netdev", "user,id=net0,hostfwd=tcp::8090-:80",
            "-device", "virtio-net-device,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=none,id=hd0",
            "-device", "virtio-blk-device,drive=hd0",
            "-serial", "stdio",
        ]
    return qemu_bin, canary, main_cmd


def main() -> int:
    ap = argparse.ArgumentParser(description="SOMA runner (Singularity Loop v2 Multi-Arch)")
    ap.add_argument("-a", "--arch", choices=["x86_64", "aarch64"], default=None,
                    help="arquitectura objetivo (x86_64 o aarch64)")
    ap.add_argument("--aprobar", action="store_true",
                    help="mostrar el diff y pedir confirmacion antes de aplicar")
    ap.add_argument("--max-ciclos", type=int, default=10,
                    help="maximo de ciclos compilar/arrancar (por defecto 10)")
    ap.add_argument("--snapshot", type=str, metavar="NAME",
                    help="guarda un snapshot completo reproducible (src + hdd.img)")
    ap.add_argument("--restore", type=str, metavar="NAME",
                    help="restaura un snapshot completo (src + hdd.img)")
    ap.add_argument("--snapshots", action="store_true",
                    help="lista todos los snapshots guardados")
    llm_server.add_arguments(ap)
    search_proxy.add_arguments(ap)
    args = ap.parse_args()

    SNAPSHOT_DIR = ROOT / "snapshots"

    if args.snapshots:
        if not SNAPSHOT_DIR.exists() or not list(SNAPSHOT_DIR.iterdir()):
            print("No hay snapshots guardados en snapshots/.")
        else:
            print("\nSnapshots disponibles en snapshots/:")
            for s in sorted(SNAPSHOT_DIR.iterdir()):
                if s.is_dir():
                    has_hdd = (s / "hdd.img").exists()
                    has_src = (s / "src").exists()
                    print(f"  - {s.name} (src: {'OK' if has_src else 'NO'}, hdd: {'OK' if has_hdd else 'NO'})")
            print()
        return 0

    if args.snapshot:
        s_target = SNAPSHOT_DIR / args.snapshot
        s_target.mkdir(parents=True, exist_ok=True)
        shutil.copytree(SRC_DIR, s_target / "src", dirs_exist_ok=True)
        if HDD.exists():
            shutil.copy2(HDD, s_target / "hdd.img")
        print(f"\n[OK] Snapshot '{args.snapshot}' guardado con exito en {s_target.relative_to(ROOT)}/\n")
        return 0

    if args.restore:
        s_target = SNAPSHOT_DIR / args.restore
        if not s_target.exists():
            print(f"\n[ERROR] El snapshot '{args.restore}' no existe en snapshots/.\n")
            return 1
        shutil.copytree(s_target / "src", SRC_DIR, dirs_exist_ok=True)
        if (s_target / "hdd.img").exists():
            shutil.copy2(s_target / "hdd.img", HDD)
        print(f"\n[OK] Snapshot '{args.restore}' restaurado con exito (src y hdd.img sincronizados).\n")
        return 0

    print("\n" + "=" * 72 + "\nSOMA DEVELOPMENT RUNNER - THE SINGULARITY LOOP v2\n" + "=" * 72 + "\n")

    # Selección de arquitectura (CLI o Interactiva)
    if args.arch:
        arch = args.arch
    elif sys.stdin.isatty():
        arch = choose_arch()
    else:
        arch = "x86_64"

    qemu_bin, canary_cmd, qemu_command = get_qemu_commands(arch)
    globals()["PACK_ARCH"] = arch

    if not shutil.which("docker") or not shutil.which(qemu_bin):
        print(f"ERROR: Faltan dependencias (Docker o {qemu_bin}).")
        return 1

    if not docker_image_exists():
        if run(["docker", "build", "--platform", "linux/amd64", "-t", DOCKER_IMAGE, "."],
               "CONSTRUYENDO TOOLCHAIN") != 0:
            return 1
    uid, gid = get_host_uid_gid()

    if not HDD.exists():
        print("Preparando disco fisico persistente (hdd.img de 16 MiB) fuera de build/ ...")
        with open(HDD, "wb") as f:
            f.write(b"\x00" * (16 * 1024 * 1024))

    llm_server.ensure_server(args)
    search_proxy.ensure_proxy(args)

    patched = False
    cycles = 0

    while True:
        cycles += 1
        if cycles > args.max_ciclos:
            print(f"Limite de {args.max_ciclos} ciclos alcanzado. Fin.")
            break

        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
        BUILD_DIR.mkdir(exist_ok=True)

        code, out = build(uid, gid, arch)

        if code != 0:
            if patched:
                errs = " ; ".join(summarize_errors(out))
                print("\n[ROLLBACK] El parche de la IA no compila. Restaurando src/ ...")
                restore_src()
                write_result("COMPILE_FAILED (rollback aplicado)", errs)
                arm_agent_resume()
                patched = False
                continue
            print("ERROR de compilacion. Rompiendo bucle.")
            return code

        if patched:
            print("\n" + "=" * 72)
            print(f"PUERTA DE ARRANQUE: PROBANDO KERNEL ({arch}) EN MODO CANARY")
            print("=" * 72)
            precanary = HDD.with_name(HDD.name + ".checkpoint_precanary")
            shutil.copy2(HDD, precanary)
            print(f"[REPRODUCIBILIDAD] Disco guardado antes del canary: {precanary.name}")
            pack_sources()
            arm_boot_gate()

            print("$ " + " ".join(canary_cmd) + "\n")
            try:
                canary_rc = subprocess.run(canary_cmd, cwd=ROOT, timeout=45).returncode
            except subprocess.TimeoutExpired:
                print("\n[BOOT-GATE TIMEOUT] El kernel se colgo o tardo demasiado (>45s).")
                canary_rc = -1

            disarm_boot_gate()

            if canary_rc == QEMU_EXIT_GATE_OK:
                print("\n[BOOT-GATE OK] El nuevo kernel paso el health-check. Parche validado.")
                write_result("BOOT_OK", "kernel nuevo verificado y operativo")
                git_snapshot("SOMA: parche verificado por puerta de arranque")
                arm_agent_resume()
                patched = False
            else:
                print(f"\n[BOOT-GATE FAIL] El kernel fallo en el arranque (rc={canary_rc}).")
                print("[ROLLBACK] Restaurando src/ a la version sana anterior...")
                restore_src()
                shutil.copy2(precanary, HDD)
                print("[ROLLBACK] Disco hdd.img restaurado al estado previo al canary.")
                write_result("BOOT_FAILED (rollback aplicado)", f"canary_rc={canary_rc}")
                arm_agent_resume()
                patched = False
                continue

        pack_sources()

        print("\n" + "=" * 72 + f"\nARRANCANDO SOMA BARE-METAL ({arch})\n" + "=" * 72 + "\n")
        try:
            rc = subprocess.run(qemu_command, cwd=ROOT).returncode
        except KeyboardInterrupt:
            print("\nQEMU detenido por el usuario.")
            break

        outcome = handle_mailbox(args.aprobar)

        if outcome is None:
            if rc == QEMU_EXIT_PANIC:
                print("\n[HOST MONITOR] !!! KERNEL PANIC DETECTADO EN EL GUEST (exit 0x23) !!!\n")
            if rc == QEMU_EXIT_PATCH:
                print("AVISO: el kernel pidio un parche (exit 0x21) pero el buzon esta vacio o corrupto.")
            print(">>> QEMU cerrado sin peticion de auto-modificacion. Fin del script.")
            break

        print(">>> Reiniciando el ciclo de compilacion...")
        patched = outcome

    return 0


if __name__ == "__main__":
    sys.exit(main())