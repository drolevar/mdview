#pragma once

namespace mdview {

// Returns the canonical detect string used to register file types
// with Total Commander. Lifetime: program lifetime.
const char* detect_string() noexcept;

// Writes the detect string into `buffer`, bounded by `maxlen` (which is the
// total buffer size in bytes including space for the null terminator).
// Always null-terminates if `buffer` is non-null and `maxlen > 0`.
void write_detect_string(char* buffer, int maxlen) noexcept;

}
