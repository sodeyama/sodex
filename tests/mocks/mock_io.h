#ifndef MOCK_IO_H
#define MOCK_IO_H

#include <stdint.h>

typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;

static inline void out8(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline void out16(uint16_t port, uint16_t val) { (void)port; (void)val; }
static inline uint8_t in8(uint16_t port) { (void)port; return 0; }
static inline uint16_t in16(uint16_t port) { (void)port; return 0; }
static inline void enableInterrupt(void) {}
static inline void disableInterrupt(void) {}

#endif
