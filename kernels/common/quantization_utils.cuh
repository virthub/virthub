// virthub/kernels/common/quantization_utils.cuh

#ifndef PSP_KV_QUANTIZATION_UTILS_CUH
#define PSP_KV_QUANTIZATION_UTILS_CUH

#include <cuda_runtime.h>
#include <math.h>
#include <stdint.h>

// Portable float-to-uint32 bit conversion for both host and device.
__host__ __device__ inline uint32_t float_as_uint(float value) {
    union { float f; uint32_t u; } conv;
    conv.f = value;
    return conv.u;
}

// FP8 E4M3 (1 sign, 4 exponent, 3 mantissa, bias 7)
__host__ __device__ inline uint8_t quantize_fp8_e4m3(float value) {
    if (isnan(value)) return 0x7F;
    if (value == 0.0f) return 0x00;

    uint32_t bits = float_as_uint(value);
    uint8_t sign = (bits >> 31) & 0x1;
    float abs_value = fabsf(value);

    if (isinf(abs_value)) return (sign << 7) | 0x78;

    int f32_exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t f32_mant = bits & 0x7FFFFF;

    int fp8_exp = f32_exp + 7;
    uint8_t mant = f32_mant >> (23 - 3);
    uint8_t round_bit = (f32_mant >> (23 - 3 - 1)) & 1;
    uint8_t sticky = (f32_mant & ((1 << (23 - 3 - 1)) - 1)) != 0;

    if (round_bit && (sticky || (mant & 1))) {
        mant++;
        if (mant == 0x8) {
            mant = 0;
            fp8_exp++;
        }
    }

    if (fp8_exp >= 15) {
        return (sign << 7) | 0x78;  // infinity
    } else if (fp8_exp <= 0) {
        if (fp8_exp < -7) {
            return sign << 7;       // underflow to zero
        }
        // subnormal
        mant = ((0x8 | mant) >> (1 - fp8_exp)) & 0x7;
        fp8_exp = 0;
    }

    return (sign << 7) | ((fp8_exp & 0xF) << 3) | mant;
}

__host__ __device__ inline float dequantize_fp8_e4m3(uint8_t q) {
    uint8_t sign = (q >> 7) & 0x1;
    uint8_t exp = (q >> 3) & 0xF;
    uint8_t mant = q & 0x7;

    float value;
    if (exp == 0) {
        value = mant * powf(2.0f, -6.0f);
    } else {
        value = (1.0f + mant / 8.0f) * powf(2.0f, (float)(exp - 7));
    }

    return sign ? -value : value;
}

// FP8 E5M2 (1 sign, 5 exponent, 2 mantissa, bias 15)
__host__ __device__ inline uint8_t quantize_fp8_e5m2(float value) {
    if (isnan(value)) return 0x7F;
    if (value == 0.0f) return 0x00;

    uint32_t bits = float_as_uint(value);
    uint8_t sign = (bits >> 31) & 0x1;
    float abs_value = fabsf(value);

    if (isinf(abs_value)) return (sign << 7) | 0x7C;

    int f32_exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t f32_mant = bits & 0x7FFFFF;

    int fp8_exp = f32_exp + 15;
    uint8_t mant = f32_mant >> (23 - 2);
    uint8_t round_bit = (f32_mant >> (23 - 2 - 1)) & 1;
    uint8_t sticky = (f32_mant & ((1 << (23 - 2 - 1)) - 1)) != 0;

    if (round_bit && (sticky || (mant & 1))) {
        mant++;
        if (mant == 0x4) {
            mant = 0;
            fp8_exp++;
        }
    }

    if (fp8_exp >= 31) {
        return (sign << 7) | 0x7C;  // infinity
    } else if (fp8_exp <= 0) {
        if (fp8_exp < -1) {
            return sign << 7;       // underflow to zero
        }
        // subnormal
        mant = ((0x4 | mant) >> (1 - fp8_exp)) & 0x3;
        fp8_exp = 0;
    }

    return (sign << 7) | ((fp8_exp & 0x1F) << 2) | mant;
}

__host__ __device__ inline float dequantize_fp8_e5m2(uint8_t q) {
    uint8_t sign = (q >> 7) & 0x1;
    uint8_t exp = (q >> 2) & 0x1F;
    uint8_t mant = q & 0x3;

    float value;
    if (exp == 0) {
        value = mant * powf(2.0f, -14.0f);
    } else {
        value = (1.0f + mant / 4.0f) * powf(2.0f, (float)(exp - 15));
    }

    return sign ? -value : value;
}

// FP4 E2M1 (1 sign, 2 exponent, 1 mantissa, bias 1)
__host__ __device__ inline uint8_t quantize_fp4_e2m1(float value) {
    if (isnan(value)) return 0x7;
    if (value == 0.0f) return 0x0;

    uint32_t bits = float_as_uint(value);
    uint8_t sign = (bits >> 31) & 0x1;
    float abs_value = fabsf(value);

    if (isinf(abs_value)) return (sign << 3) | 0x6;

    int f32_exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t f32_mant = bits & 0x7FFFFF;

    int fp4_exp = f32_exp + 1;
    uint8_t mant = f32_mant >> (23 - 1);
    uint8_t round_bit = (f32_mant >> (23 - 1 - 1)) & 1;
    uint8_t sticky = (f32_mant & ((1 << (23 - 1 - 1)) - 1)) != 0;

    if (round_bit && (sticky || (mant & 1))) {
        mant++;
        if (mant == 0x2) {
            mant = 0;
            fp4_exp++;
        }
    }

    if (fp4_exp >= 3) {
        return (sign << 3) | 0x6;  // infinity
    } else if (fp4_exp <= 0) {
        if (fp4_exp < -1) {
            return sign << 3;      // underflow to zero
        }
        // subnormal
        mant = ((0x2 | mant) >> (1 - fp4_exp)) & 0x1;
        fp4_exp = 0;
    }

    return (sign << 3) | ((fp4_exp & 0x3) << 1) | mant;
}

__host__ __device__ inline float dequantize_fp4_e2m1(uint8_t q) {
    uint8_t sign = (q >> 3) & 0x1;
    uint8_t exp = (q >> 1) & 0x3;
    uint8_t mant = q & 0x1;

    float value;
    if (exp == 0) {
        value = mant * 0.5f;  // subnormal
    } else if (exp == 3) {
        value = INFINITY;     // infinity
    } else {
        value = (1.0f + mant / 2.0f) * powf(2.0f, (float)(exp - 1));
    }

    return sign ? -value : value;
}

// FP4 pair packing / unpacking
__host__ __device__ inline uint8_t pack_fp4_pair(uint8_t a, uint8_t b) {
    return (a << 4) | (b & 0x0F);
}

__host__ __device__ inline void unpack_fp4_pair(uint8_t packed, uint8_t *high, uint8_t *low) {
    *high = packed >> 4;
    *low = packed & 0x0F;
}

// E8M0 scale exponent conversion
__host__ __device__ inline float e8m0_to_f32(uint8_t exponent) {
    return powf(2.0f, (float)exponent);
}

__host__ __device__ inline uint8_t f32_to_e8m0(float scale) {
    if (isnan(scale)) return 0;
    if (isinf(scale) && scale > 0) return 255;
    if (scale <= 0.0f) return 0;
    float exp = log2f(scale);
    int e = (int)roundf(exp);
    if (e < 0) return 0;
    if (e > 255) return 255;
    return (uint8_t)e;
}

#endif
