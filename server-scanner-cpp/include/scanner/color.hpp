#pragma once

// Terminal color helpers. Colors are emitted only when output is a TTY;
// `col::enabled` is set from isatty() at startup. The macros expand to
// col::wrap(code), which returns the escape when enabled and "" otherwise.

namespace col {
extern bool enabled;
const char* wrap(const char* code);
}  // namespace col

#define RESET   col::wrap("\033[0m")
#define DIM     col::wrap("\033[2m")
#define RED     col::wrap("\033[91m")
#define GREEN   col::wrap("\033[92m")
#define YELLOW  col::wrap("\033[93m")
#define CYAN    col::wrap("\033[96m")
#define MAGENTA col::wrap("\033[95m")
#define BOLD    col::wrap("\033[1m")
