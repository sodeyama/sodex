/*
 * sys/types.h - Compatibility stub for BearSSL + Sodex coexistence
 *
 * Uses stdint.h types to match BearSSL's expectations while providing
 * Sodex's type names.
 */
#ifndef _BEARSSL_COMPAT_SYS_TYPES_H
#define _BEARSSL_COMPAT_SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Map Sodex type names to stdint types */
typedef uint8_t   u_int8_t;
typedef uint16_t  u_int16_t;
typedef uint32_t  u_int32_t;
typedef int8_t    int8_t_sodex;  /* avoid redefinition */
typedef int32_t   int32_t_sodex;
typedef int       pid_t;
typedef int       mode_t;
typedef int       off_t;
typedef int       ssize_t;

#endif
