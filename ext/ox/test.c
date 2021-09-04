// Copyright (c) 2021 Peter Ohler. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root for license details.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "intern.h"
#include "ox.h"
#include "oxcache.h"

#define MIN_SLEEP (1.0 / (double)CLOCKS_PER_SEC)
#ifndef CLOCK_REALTIME_COURSE
#define CLOCK_REALTIME_COURSE CLOCK_REALTIME
#endif

typedef struct _word {
    uint8_t len;
    char    text[31];
} * Word;

static double dtime() {
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME_COURSE, &ts);

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static Word load_words(const char *path, size_t *cntp) {
    FILE *  f;
    char *  line = malloc(256);
    size_t  len  = 256;
    size_t  cnt  = 0;
    ssize_t rcnt;
    Word    words;
    Word    wp;
    ssize_t max = 0;

    if (NULL == (f = fopen(path, "r"))) {
        return NULL;
    }
    while (0 <= (rcnt = getline(&line, &len, f))) {
        if (0 < rcnt) {
            if (max < rcnt) {
                max            = rcnt;
                line[rcnt - 1] = '\0';
            }
            cnt++;
        }
    }
    *cntp = cnt;
    if (NULL == (words = malloc((cnt + 1) * sizeof(struct _word)))) {
        return NULL;
    }
    rewind(f);
    wp = words;
    while (0 <= (rcnt = getline(&line, &len, f))) {
        if (0 < rcnt) {
            wp->len = rcnt - 1;
            memcpy(wp->text, line, wp->len);
            wp->text[wp->len] = '\0';
            wp++;
        }
    }
    wp->len   = 0;
    *wp->text = '\0';
    fclose(f);
    free(line);

    return words;
}

static long memuse() {
    long  psize = sysconf(_SC_PAGESIZE);
    long  rss   = 0;
    FILE *f;

    if (NULL == (f = fopen("/proc/self/statm", "r"))) {
        return 0;
    }
    if (1 != fscanf(f, "%*s%ld", &rss)) {
        rss = 0;
    }
    fclose(f);

    return rss * psize / 1024 / 1024;
}

#define KEEP_SIZE 800000

static VALUE intern_ruby_sym(const char *str, int len) {
    return rb_to_symbol(rb_enc_interned_str(str, len, rb_utf8_encoding()));
}

static VALUE intern_ruby_str(const char *str, int len) {
#if HAVE_RB_ENC_INTERNED_STR
    return rb_enc_interned_str(str, len, rb_utf8_encoding());
#else
    return rb_utf8_str_new(wp->text, wp->len);
#endif
}

static VALUE intern_oj_sym(const char *str, int len) {
    return ox_sym_intern(str, len, NULL);
}

static VALUE intern_oj_str(const char *str, int len) {
    return ox_str_intern(str, len);
}

static VALUE intern_ox_sym(const char *str, int len) {
    VALUE *slot;
    VALUE  sym;

    if (Qundef == (sym = ox_cache_get(ox_symbol_cache, str, &slot, NULL))) {
        sym   = rb_to_symbol(rb_utf8_str_new(str, len));
        *slot = sym;
    }
    return sym;
}

static VALUE intern_ox_str(const char *str, int len) {
    VALUE *slot;
    VALUE  sym;

    if (Qundef == (sym = ox_cache_get(ox_symbol_cache, str, &slot, NULL))) {
        sym   = rb_utf8_str_new(str, len);
        *slot = sym;
    }
    return sym;
}

void ox_cache_test(int which, bool sym) {
    size_t         cnt   = 0;
    Word           words = load_words("words", &cnt);
    volatile VALUE keep[KEEP_SIZE];
    Word           wp;
    double         start;
    double         dur;
    int            i;
    long           base_mem                   = memuse();
    VALUE (*intern)(const char *str, int len) = intern_ruby_str;
    const char *label                         = "?";

    words[10000].len = 0;
    switch (which) {
    case 0:
        intern = sym ? intern_ruby_sym : intern_ruby_str;
        label  = sym ? "ruby intern symbol" : "ruby intern string";
        break;
    case 1:
        intern = sym ? intern_oj_sym : intern_oj_str;
        label  = sym ? "oj intern symbol" : "oj intern string";
        break;
    case 2:
        intern = sym ? intern_ox_sym : intern_ox_str;
        label  = sym ? "ox intern symbol" : "ox intern string";
        break;
    }
    for (i = 0; i < 2; i++) {
        memset((void *)keep, 0, sizeof(keep));
        start = dtime();
        for (wp = words; 0 != wp->len; wp++) {
            keep[wp - words] = intern(wp->text, wp->len);
        }
        dur = dtime() - start;
        printf("%s %d: %ld in %f seconds (%0.1fK/sec)\n",
               label,
               i,
               cnt,
               dur,
               (double)cnt / dur / 1000.0);
    }
    memset((void *)keep, 0, sizeof(keep));
    for (i = 20; 0 < i; i--) {
        rb_gc_start();
    }
    printf("%s memory use: %ld MB\n", label, memuse() - base_mem);
}
