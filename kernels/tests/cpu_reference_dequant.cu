// virthub/kernels/tests/cpu_reference_dequant.cu

#include <cstdio>
#include <cmath>
#include <cstdint>
#include "quantization_utils.cuh"

static bool close_enough(float a, float b, float eps = 0.01f) {
    if (isnan(a) && isnan(b)) return true;
    if (isinf(a) && isinf(b) && (signbit(a) == signbit(b))) return true;
    return fabsf(a - b) <= eps;
}

static int test_fp8_e4m3_roundtrip() {
    printf("Testing FP8 E4M3 roundtrip...\n");
    float values[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 0.125f, 6.0f, -0.25f, 0.3333f};
    int num_values = sizeof(values) / sizeof(values[0]);
    int failures = 0;

    for (int i = 0; i < num_values; ++i) {
        float v = values[i];
        uint8_t q = quantize_fp8_e4m3(v);
        float dq = dequantize_fp8_e4m3(q);
        if (!close_enough(v, dq, 0.1f)) {
            printf("  FAIL: %f -> q=%02x -> %f (diff %f)\n", v, q, dq, fabsf(v - dq));
            failures++;
        } else {
            printf("  OK:   %f -> q=%02x -> %f\n", v, q, dq);
        }
    }
    if (failures == 0) {
        printf("FP8 E4M3 roundtrip PASSED\n\n");
        return 0;
    } else {
        printf("FP8 E4M3 roundtrip FAILED (%d failures)\n\n", failures);
        return 1;
    }
}

static int test_fp8_e5m2_roundtrip() {
    printf("Testing FP8 E5M2 roundtrip...\n");
    float values[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 0.25f, 8.0f, -0.125f, 3.0f};
    int num_values = sizeof(values) / sizeof(values[0]);
    int failures = 0;

    for (int i = 0; i < num_values; ++i) {
        float v = values[i];
        uint8_t q = quantize_fp8_e5m2(v);
        float dq = dequantize_fp8_e5m2(q);
        if (!close_enough(v, dq, 0.2f)) {
            printf("  FAIL: %f -> q=%02x -> %f (diff %f)\n", v, q, dq, fabsf(v - dq));
            failures++;
        } else {
            printf("  OK:   %f -> q=%02x -> %f\n", v, q, dq);
        }
    }
    if (failures == 0) {
        printf("FP8 E5M2 roundtrip PASSED\n\n");
        return 0;
    } else {
        printf("FP8 E5M2 roundtrip FAILED (%d failures)\n\n", failures);
        return 1;
    }
}

static int test_fp4_e2m1_roundtrip() {
    printf("Testing FP4 E2M1 roundtrip...\n");
    float values[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, -0.5f, -2.0f};
    int num_values = sizeof(values) / sizeof(values[0]);
    int failures = 0;

    for (int i = 0; i < num_values; ++i) {
        float v = values[i];
        uint8_t q = quantize_fp4_e2m1(v);
        float dq = dequantize_fp4_e2m1(q);
        if (!close_enough(v, dq, 0.4f)) {
            printf("  FAIL: %f -> q=%x -> %f (diff %f)\n", v, q, dq, fabsf(v - dq));
            failures++;
        } else {
            printf("  OK:   %f -> q=%x -> %f\n", v, q, dq);
        }
    }
    if (failures == 0) {
        printf("FP4 E2M1 roundtrip PASSED\n\n");
        return 0;
    } else {
        printf("FP4 E2M1 roundtrip FAILED (%d failures)\n\n", failures);
        return 1;
    }
}

static int test_fp4_pair_packing() {
    printf("Testing FP4 pair packing...\n");
    int failures = 0;
    uint8_t a = 0x5, b = 0xA;
    uint8_t packed = pack_fp4_pair(a, b);
    uint8_t high, low;
    unpack_fp4_pair(packed, &high, &low);
    if (high != a || low != b) {
        printf("  FAIL: pack/unpack mismatch: (%x,%x) -> %02x -> (%x,%x)\n",
               a, b, packed, high, low);
        failures++;
    } else {
        printf("  OK:   (%x,%x) -> %02x -> (%x,%x)\n", a, b, packed, high, low);
    }
    if (failures == 0) {
        printf("FP4 pair packing PASSED\n\n");
        return 0;
    } else {
        printf("FP4 pair packing FAILED\n\n");
        return 1;
    }
}

static int test_e8m0_conversion() {
    printf("Testing E8M0 conversion (unsigned exponent, scale >= 1)...\n");
    int failures = 0;

    // Test known values
    if (e8m0_to_f32(0) != 1.0f) { printf("  FAIL: e8m0_to_f32(0) != 1.0\n"); failures++; }
    if (e8m0_to_f32(1) != 2.0f) { printf("  FAIL: e8m0_to_f32(1) != 2.0\n"); failures++; }
    if (e8m0_to_f32(3) != 8.0f) { printf("  FAIL: e8m0_to_f32(3) != 8.0\n"); failures++; }

    // Test roundtrip for representable power-of-two scales (>= 1)
    float scales[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
    int num_scales = sizeof(scales) / sizeof(scales[0]);
    for (int i = 0; i < num_scales; ++i) {
        uint8_t exp = f32_to_e8m0(scales[i]);
        float recovered = e8m0_to_f32(exp);
        float expected = scales[i];
        if (!close_enough(recovered, expected, 0.01f)) {
            printf("  FAIL: scale %f -> exp %d -> %f (expected %f)\n",
                   scales[i], exp, recovered, expected);
            failures++;
        } else {
            printf("  OK:   scale %f -> exp %d -> %f\n", scales[i], exp, recovered);
        }
    }

    // Test invalid / non-representable values
    if (f32_to_e8m0(0.0f) != 0) { printf("  FAIL: f32_to_e8m0(0) should be 0\n"); failures++; }
    if (f32_to_e8m0(-1.0f) != 0) { printf("  FAIL: f32_to_e8m0(-1) should be 0\n"); failures++; }
    if (f32_to_e8m0(0.5f) != 0) { printf("  FAIL: f32_to_e8m0(0.5) should be 0 (clamped)\n"); failures++; }
    if (f32_to_e8m0(NAN) != 0) { printf("  FAIL: f32_to_e8m0(NaN) should be 0\n"); failures++; }
    if (f32_to_e8m0(INFINITY) != 255) { printf("  FAIL: f32_to_e8m0(INFINITY) should be 255\n"); failures++; }

    if (failures == 0) {
        printf("E8M0 conversion PASSED\n\n");
        return 0;
    } else {
        printf("E8M0 conversion FAILED\n\n");
        return 1;
    }
}

int main() {
    printf("=== CPU Reference Dequantization Validation ===\n\n");

    int total_failures = 0;
    total_failures += test_fp8_e4m3_roundtrip();
    total_failures += test_fp8_e5m2_roundtrip();
    total_failures += test_fp4_e2m1_roundtrip();
    total_failures += test_fp4_pair_packing();
    total_failures += test_e8m0_conversion();

    if (total_failures == 0) {
        printf("All CPU reference tests PASSED.\n");
        return 0;
    } else {
        printf("Some CPU reference tests FAILED (%d test groups failed).\n", total_failures);
        return 1;
    }
}
