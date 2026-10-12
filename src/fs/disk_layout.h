#ifndef SOMA_DISK_LAYOUT_H
#define SOMA_DISK_LAYOUT_H

/* ====================================================================
 * MAPA FÍSICO DE DISCO SOMA V2 (Sectores LBA de 512 bytes en virtio-blk)
 * Capacidad total estándar: 16 MiB (32.768 sectores)
 * ==================================================================== */

/* Zona 1: Buzón y Puente Host-Bridge (Singularity Loop) */
#define SOMA_LBA_MB_START       1      /* LBA 1..16: Buzón de parches (8 KiB) */
#define SOMA_LBA_MB_SECTORS     16
#define SOMA_LBA_RESULT_START   20     /* LBA 20..23: Resultado del host (2 KiB) */
#define SOMA_LBA_RESULT_SECTORS 4
#define SOMA_LBA_BOOT_GATE      24     /* LBA 24: Flag de prueba canaria */
#define SOMA_LBA_RESUME_FLAG    25     /* LBA 25: Flag de reanudación atómica */

/* Zona 2: Sistema de Archivos Persistente V2 (RamFS SOMAFS02) */
#define SOMA_LBA_FS_SUPER       1024   /* LBA 1024: Superbloque del VFS */
#define SOMA_LBA_FS_DATA        1025   /* LBA 1025..3137: 64 inodos x 33 sectores (16 KiB/archivo) */
#define SOMA_LBA_FS_LIMIT       3138

/* Zona 3: Scratch del Sistema y Zona de Usuario */
#define SOMA_LBA_SCRATCH_MIN    3500   /* LBA 3500..3508: Scratch para canary tests */
#define SOMA_LBA_USER_MIN       3600   /* LBA >= 3600: Libre para usuario / sector_write */

/* Zona 4: Checkpoints de Hardware */
#define SOMA_LBA_CHECKPOINT     4096   /* LBA 4096..6210: Checkpoint de respaldo RamFS */
#define SOMA_LBA_CHECKPOINT_END 6210

/* Zona 5: Fuentes del Sistema (SRCFS) */
#define SOMA_LBA_SRCFS_INDEX    8192   /* LBA 8192..8199: Índice de fuentes (8 sectores) */
#define SOMA_LBA_SRCFS_DATA     8200   /* LBA 8200..16383: Fuentes empaquetadas (4 MiB) */
#define SOMA_LBA_SRCFS_LIMIT    16384

#endif
