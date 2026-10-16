#ifndef CTX_TEST_HELPERS_H
#define CTX_TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define ctx_mkdir(p) _mkdir(p)
#define ctx_chdir(p) _chdir(p)
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define ctx_mkdir(p) mkdir((p), 0777)
#define ctx_chdir(p) chdir(p)
#endif

static inline int test_write_file(const char *path, const char *data,
    size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t w = fwrite(data, 1, len, f);
    return (fclose(f) == 0 && w == len) ? 0 : -1;
}

static inline char *test_read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    if (len)
        *len = got;
    return buf;
}

static inline void test_rm_rf(const char *path)
{
#ifdef _WIN32
    char win[2048];
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(win); i++)
        win[i] = path[i] == '/' ? '\\' : path[i];
    win[i] = '\0';
    char cmd[4200];
    snprintf(cmd, sizeof(cmd),
        "del /q /f \"%s\" 2>nul & rmdir /s /q \"%s\" 2>nul", win, win);
#else
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" 2>/dev/null", path);
#endif
    (void)system(cmd);
}

/* Run a shell command (e.g. git) and return its exit status. */
static inline int test_system(const char *cmd)
{
    int st = system(cmd);
#ifdef _WIN32
    return st;
#else
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

static inline int test_run(const char *bin, const char *args, const char *out,
    const char *err)
{
    char cmd[4096];
#ifdef _WIN32
    /* cmd.exe strips the first and last quote of the /c argument, so wrap the
       whole command in an extra pair to keep the executable's quotes intact. */
    snprintf(cmd, sizeof(cmd), "\"\"%s\" %s >\"%s\" 2>\"%s\"\"", bin, args, out,
        err);
    return system(cmd);
#else
    snprintf(cmd, sizeof(cmd), "\"%s\" %s >\"%s\" 2>\"%s\"", bin, args, out,
        err);
    int st = system(cmd);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

#endif
