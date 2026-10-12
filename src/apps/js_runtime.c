#include "js_runtime.h"
#include "browser.h"

static const char *soma_strstr(const char *haystack, const char *needle) {
    if (!needle || !*needle) return haystack;
    if (!haystack) return 0;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

void js_runtime_init(void) {}

void js_runtime_eval(const char *code) {
    if (!code || !*code) return;

    const char *p = soma_strstr(code, "getElementById");
    if (p) {
        p += 14;
        while (*p && *p != '(') p++;
        if (*p == '(') p++;

        char quote = 0;
        if (*p == 39 || *p == 34) quote = *p++;

        char target_id[32];
        int tip = 0;
        while (*p && tip < 31) {
            if (quote && *p == quote) { p++; break; }
            if (!quote && *p == ')') break;
            target_id[tip++] = *p++;
        }
        target_id[tip] = '\0';

        const char *eq = soma_strstr(p, "=");
        if (eq) {
            eq++;
            while (*eq == ' ' || *eq == '\t') eq++;

            char val_quote = 0;
            if (*eq == 39 || *eq == 34) val_quote = *eq++;

            char new_text[128];
            int ntp = 0;
            while (*eq && ntp < 127) {
                if (val_quote && *eq == val_quote) break;
                if (!val_quote && (*eq == ';' || *eq == '\n' || *eq == '\r')) break;
                new_text[ntp++] = *eq++;
            }
            new_text[ntp] = '\0';

            if (target_id[0] && new_text[0]) {
                browser_set_element_text_by_id(target_id, new_text);
            }
        }
    }
}

void js_runtime_cleanup(void) {}
