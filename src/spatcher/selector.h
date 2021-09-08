#pragma once

#include <stddef.h>

#if defined(_WIN32)
extern int browse_for_directory(const char *prompt, char *output, size_t len);
#endif
