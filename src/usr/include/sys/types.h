/*
 *  Sodex: System Data Types
 */

#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

typedef char int8_t;
typedef short int int16_t;
typedef int int32_t;

typedef unsigned char u_int8_t;
typedef unsigned short int u_int16_t;
typedef unsigned int u_int32_t;


/* for GDT settings */
typedef u_int16_t   selno_t;
typedef unsigned int gdtno_t;

/* for IDT settings */
typedef u_int8_t    idtno_t;

typedef u_int32_t size_t;
typedef int ssize_t;

typedef u_int32_t off_t;
typedef u_int16_t mode_t;

typedef int16_t pid_t;

typedef unsigned int uintptr_t;
typedef int intptr_t;

#endif /* types.h */
