#ifndef SOMA_JS_RUNTIME_H
#define SOMA_JS_RUNTIME_H

void js_runtime_init(void);
void js_runtime_eval(const char *code);
void js_runtime_cleanup(void);

#endif
