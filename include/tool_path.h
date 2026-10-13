#ifndef OPENLOGI_TOOL_PATH_H
#define OPENLOGI_TOOL_PATH_H

#include <stdbool.h>
#include <stddef.h>

/* Find an executable next to program, resolving a bare program name through PATH. */
bool tool_sibling_path(const char *program, const char *search_path,
                       const char *sibling, char *out, size_t out_size);

#endif
