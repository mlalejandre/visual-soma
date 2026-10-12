#ifndef MYOS_FS_H
#define MYOS_FS_H

#include <stdint.h>

#define FS_MAX_FILES 64
#define FS_NAME_MAX  48
#define FS_DATA_MAX  16384

struct vfs_file {
    char     name[FS_NAME_MAX]; /* 48 bytes (offset 0..47) */
    uint32_t size;              /* 4 bytes (offset 48..51) */
    uint8_t  used;              /* 1 byte (offset 52) */
    uint8_t  dirty;             /* 1 byte (offset 53) */
    uint8_t  pad[10];           /* 10 bytes (offset 54..63) -> cabecera = 64 B */
    char     *data;             /* 8 bytes -> buffer de 16 KiB en KHeap */
} __attribute__((aligned(8)));

void vfs_init(void);
int  vfs_create(const char *name, const char *initial_data);
int  vfs_write(const char *name, const char *data, uint32_t len);

/* SOMA_PARCHE_LIVE_V1: hook que el GUI registra para ver en vivo lo que
 * SOMA escribe en archivos de texto. NULL = desactivado. */
extern void (*vfs_live_hook)(const char *name, const char *data, uint32_t len);
int  vfs_read(const char *name, char *buf_out, uint32_t max_len);
int  vfs_delete(const char *name);

void vfs_list(void);
void vfs_list_ext(int show_all, const char *filter_dir);
int  vfs_format_list(char *out_buf, uint32_t max);
int  vfs_format_list_ext(int show_all, const char *filter_dir, char *out_buf, uint32_t max);

int  vfs_sync(void);
void vfs_format(void);
void vfs_tree(char *out_buf, uint32_t max);
void vfs_tree_ext(int show_all, char *out_buf, uint32_t max);

void vfs_get_stats(uint32_t *files_used, uint32_t *bytes_used, uint32_t *dirty_count);
int  vfs_is_hidden(const char *name);

/* Motor de Búsqueda Semántica y Ponderada BM25 */
int  vfs_search_bm25(const char *query, const char *path_prefix, char *out, uint32_t max);

int  vfs_grep_all(const char *pat, int ignore_case, int list_only,
                  char *out, uint32_t max);

int  vfs_checkpoint_save(void);
int  vfs_checkpoint_restore(void);

#endif
