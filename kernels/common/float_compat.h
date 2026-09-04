// virthub/kernels/common/float_compat.h
//
// Compatibility typedefs for glibc declarations that reference
// _Float32, _Float64, _Float128, _Float32x, _Float64x in C++17.
// These types are not built-in in C++17 (they are ISO/IEC TS 18661
// extensions). Defining them as typedefs prevents compilation errors
// when glibc headers are included.

#ifndef PSP_KV_FLOAT_COMPAT_H
#define PSP_KV_FLOAT_COMPAT_H

typedef float _Float32;
typedef double _Float64;
typedef __float128 _Float128;
typedef float _Float32x;
typedef double _Float64x;

#endif