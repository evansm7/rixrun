#ifndef UTILS_H
#define UTILS_H

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void    panic(const char *format, ...);

#endif
