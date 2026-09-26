#include "scanner/color.hpp"

namespace col {
bool enabled = true;  // set from isatty() in main
const char* wrap(const char* code) { return enabled ? code : ""; }
}  // namespace col
