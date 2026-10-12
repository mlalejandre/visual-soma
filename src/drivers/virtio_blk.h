#ifndef MYOS_VIRTIO_BLK_H
#define MYOS_VIRTIO_BLK_H

#include <stdint.h>

/* Inicializa el controlador de disco duro VirtIO. Devuelve 1 si OK. */
int virtio_blk_init(void);

/* Lee un sector de 512 bytes del disco duro (LBA). 0 = OK, -1 = Error. */
int virtio_blk_read(uint64_t sector, void *buf);

/* Escribe un sector de 512 bytes en el disco duro (LBA). 0 = OK, -1 = Error. */
int virtio_blk_write(uint64_t sector, const void *buf);

#endif
