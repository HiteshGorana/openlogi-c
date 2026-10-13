#include "tool_path.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool resolve_program(const char *program, const char *search_path, char *resolved)
{
    if (strchr(program, '/')) return realpath(program, resolved) != NULL;
    if (!search_path) return false;

    const char *entry = search_path;
    do {
        const char *end = strchr(entry, ':');
        size_t length = end ? (size_t)(end - entry) : strlen(entry);
        if (length >= PATH_MAX) {
            entry = end ? end + 1 : NULL;
            continue;
        }
        char candidate[PATH_MAX];
        int size = length == 0
            ? snprintf(candidate, sizeof(candidate), "./%s", program)
            : snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, entry, program);
        if (size > 0 && (size_t)size < sizeof(candidate) && access(candidate, X_OK) == 0 &&
            realpath(candidate, resolved)) return true;
        entry = end ? end + 1 : NULL;
    } while (entry);
    return false;
}

bool tool_sibling_path(const char *program, const char *search_path,
                       const char *sibling, char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!program || !program[0] || !sibling || !sibling[0] || strchr(sibling, '/')) return false;

    char resolved[PATH_MAX];
    if (!resolve_program(program, search_path, resolved)) return false;
    char *slash = strrchr(resolved, '/');
    if (!slash) return false;
    slash[1] = '\0';

    char candidate[PATH_MAX];
    int size = snprintf(candidate, sizeof(candidate), "%s%s", resolved, sibling);
    struct stat info;
    if (size < 0 || (size_t)size >= sizeof(candidate) || (size_t)size >= out_size ||
        stat(candidate, &info) != 0 || !S_ISREG(info.st_mode) || access(candidate, X_OK) != 0) return false;
    memcpy(out, candidate, (size_t)size + 1);
    return true;
}
