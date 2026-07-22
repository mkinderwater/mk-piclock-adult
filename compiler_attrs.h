#ifndef MK_PICLOCK_COMPILER_ATTRS_H
#define MK_PICLOCK_COMPILER_ATTRS_H

#if defined(__GNUC__) || defined(__clang__)
#define MP_PRINTF_LIKE(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define MP_PRINTF_LIKE(format_index, first_argument)
#endif

#endif
