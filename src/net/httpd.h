#ifndef SOMA_HTTPD_H
#define SOMA_HTTPD_H

#include <stdint.h>

void httpd_init(void);
void httpd_thread_func(void *arg);
int  cmd_httpd(const char *args, char *out_buf, uint32_t max_out);

#endif
