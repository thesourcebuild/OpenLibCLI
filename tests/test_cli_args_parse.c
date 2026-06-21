/**
 * @file test_cli_args_parse.c
 * @brief Unit tests for cli/utils/cli_args_parse.c — all three parser families
 *        (hand-rolled, strtol-backed) and all format functions.
 *
 * Self-contained test harness — no external framework required.
 *
 *
 * Edge cases handled:

 * .5 (no leading digits)
 * 5. (no trailing digits)
 * 1e10, 1.5e-3, -3.14e+2
 * Overflow → ±HUGE_VALF / ±HUGE_VAL → returns false (like the hand-rolled integer parsers)
 * Underflow → 0.0 → returns true
 * Trailing junk → false
 * Empty string → false
 * Multiple decimal points → false

 * Does not handle (rejected as invalid):

 * inf, infinity, nan — hand-rolled, no strcmp dependency needed
 * 0x hex floats — beyond scope

 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>

#include "utils/cli_args_parse.h"

/*=======================================================================================
 * Test Harness Macros
 *=======================================================================================*/

static int s_tests_run = 0;
static int s_tests_fail = 0;
static int s_asserts = 0;
static const char *s_cur_test = NULL;

#define TEST_START(name)                                                                           \
  do {                                                                                             \
    s_cur_test = name;                                                                             \
    s_tests_run++;                                                                                 \
  } while (0)
#define TEST_END()                                                                                 \
  do {                                                                                             \
    s_cur_test = NULL;                                                                             \
  } while (0)

#define ASSERT(cond)                                                                               \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    if (!(cond)) {                                                                                 \
      fprintf(stderr, "  FAIL [%s] line %d: %s\n", s_cur_test ? s_cur_test : "?", (int)__LINE__,   \
              #cond);                                                                              \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define ASSERT_TRUE(cond)  ASSERT(cond)
#define ASSERT_FALSE(cond) ASSERT(!(cond))

#define ASSERT_EQ(a, b)                                                                            \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    if ((a) != (b)) {                                                                              \
      fprintf(stderr, "  FAIL [%s] line %d: expected %lld, got %lld\n",                            \
              s_cur_test ? s_cur_test : "?", (int)__LINE__, (long long)(b), (long long)(a));       \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define ASSERT_STR_EQ(a, b)                                                                        \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    if (strcmp((a), (b)) != 0) {                                                                   \
      fprintf(stderr, "  FAIL [%s] line %d: expected \"%s\", got \"%s\"\n",                        \
              s_cur_test ? s_cur_test : "?", (int)__LINE__, (b), (a));                             \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define ASSERT_DBL_NEAR(a, b, eps)                                                                 \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    double diff = fabs((double)(a) - (double)(b));                                                 \
    if (diff > (double)(eps)) {                                                                    \
      fprintf(stderr, "  FAIL [%s] line %d: %g != %g (diff=%g, eps=%g)\n",                         \
              s_cur_test ? s_cur_test : "?", (int)__LINE__, (double)(a), (double)(b), diff,        \
              (double)(eps));                                                                      \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define ASSERT_FLT_NEAR(a, b, eps)                                                                 \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    float diff = fabsf((float)(a) - (float)(b));                                                   \
    if (diff > (float)(eps)) {                                                                     \
      fprintf(stderr, "  FAIL [%s] line %d: %g != %g (diff=%g, eps=%g)\n",                         \
              s_cur_test ? s_cur_test : "?", (int)__LINE__, (float)(a), (float)(b), (double)diff,  \
              (double)(eps));                                                                      \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define RUN_SUITE(suite)                                                                           \
  do {                                                                                             \
    int before = s_tests_fail;                                                                     \
    suite();                                                                                       \
    int failed = s_tests_fail - before;                                                            \
    printf("  %s: %s\n", failed ? "FAIL" : "PASS", #suite);                                        \
  } while (0)

/*=======================================================================================
 * Hand-rolled Parser Tests
 *=======================================================================================*/

static void test_parse_u64(void) {
  TEST_START("parse_u64");
  uint64_t v;

  ASSERT_TRUE(parse_u64("0", &v));
  ASSERT_EQ(v, 0ULL);
  ASSERT_TRUE(parse_u64("1", &v));
  ASSERT_EQ(v, 1ULL);
  ASSERT_TRUE(parse_u64("42", &v));
  ASSERT_EQ(v, 42ULL);
  ASSERT_TRUE(parse_u64("18446744073709551615", &v));
  ASSERT_EQ(v, 18446744073709551615ULL);
  ASSERT_TRUE(parse_u64("00000", &v));
  ASSERT_EQ(v, 0ULL);
  ASSERT_TRUE(parse_u64("00042", &v));
  ASSERT_EQ(v, 42ULL);

  ASSERT_FALSE(parse_u64("", &v));
  ASSERT_FALSE(parse_u64("-1", &v));
  ASSERT_FALSE(parse_u64("+1", &v));
  ASSERT_FALSE(parse_u64("+", &v));
  ASSERT_FALSE(parse_u64("-", &v));
  ASSERT_FALSE(parse_u64("12a", &v));
  ASSERT_FALSE(parse_u64("a12", &v));
  ASSERT_FALSE(parse_u64("18446744073709551616", &v));
  ASSERT_FALSE(parse_u64(NULL, &v));
  ASSERT_FALSE(parse_u64(" 1", &v));
  ASSERT_FALSE(parse_u64("1 ", &v));
  TEST_END();
}

static void test_parse_i64(void) {
  TEST_START("parse_i64");
  int64_t v;

  ASSERT_TRUE(parse_i64("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i64("1", &v));
  ASSERT_EQ(v, 1);
  ASSERT_TRUE(parse_i64("-1", &v));
  ASSERT_EQ(v, -1);
  ASSERT_TRUE(parse_i64("+5", &v));
  ASSERT_EQ(v, 5);
  ASSERT_TRUE(parse_i64("9223372036854775807", &v));
  ASSERT_EQ(v, 9223372036854775807LL);
  ASSERT_TRUE(parse_i64("-9223372036854775808", &v));
  ASSERT_EQ(v, (-9223372036854775807LL - 1));
  ASSERT_TRUE(parse_i64("-0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i64("+0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i64("00042", &v));
  ASSERT_EQ(v, 42);
  ASSERT_TRUE(parse_i64("-00042", &v));
  ASSERT_EQ(v, -42);

  ASSERT_FALSE(parse_i64("", &v));
  ASSERT_FALSE(parse_i64("--1", &v));
  ASSERT_FALSE(parse_i64("+-1", &v));
  ASSERT_FALSE(parse_i64("-+1", &v));
  ASSERT_FALSE(parse_i64("1a", &v));
  ASSERT_FALSE(parse_i64("9223372036854775808", &v));
  ASSERT_FALSE(parse_i64("-9223372036854775809", &v));
  ASSERT_FALSE(parse_i64(NULL, &v));
  ASSERT_FALSE(parse_i64(" 1", &v));
  /* Sign-only: sign consumed, empty remainder succeeds with value 0 */
  ASSERT_TRUE(parse_i64("+", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i64("-", &v));
  ASSERT_EQ(v, 0);
  TEST_END();
}

static void test_parse_u32(void) {
  TEST_START("parse_u32");
  uint32_t v;

  ASSERT_TRUE(parse_u32("0", &v));
  ASSERT_EQ(v, 0U);
  ASSERT_TRUE(parse_u32("4294967295", &v));
  ASSERT_EQ(v, 4294967295U);
  ASSERT_TRUE(parse_u32("00001", &v));
  ASSERT_EQ(v, 1U);

  ASSERT_FALSE(parse_u32("4294967296", &v));
  ASSERT_FALSE(parse_u32("-1", &v));
  ASSERT_FALSE(parse_u32("+", &v));
  ASSERT_FALSE(parse_u32("-", &v));
  ASSERT_FALSE(parse_u32("", &v));
  TEST_END();
}

static void test_parse_i32(void) {
  TEST_START("parse_i32");
  int32_t v;

  ASSERT_TRUE(parse_i32("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i32("2147483647", &v));
  ASSERT_EQ(v, 2147483647);
  ASSERT_TRUE(parse_i32("-2147483648", &v));
  ASSERT_EQ(v, (-2147483647 - 1));

  ASSERT_FALSE(parse_i32("2147483648", &v));
  ASSERT_FALSE(parse_i32("-2147483649", &v));
  /* Sign-only: parse_i32 delegates to parse_i64 which accepts sign-only as 0 */
  ASSERT_TRUE(parse_i32("+", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i32("-", &v));
  ASSERT_EQ(v, 0);
  ASSERT_FALSE(parse_i32("", &v));
  TEST_END();
}

static void test_parse_u16(void) {
  TEST_START("parse_u16");
  uint16_t v;

  ASSERT_TRUE(parse_u16("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_u16("65535", &v));
  ASSERT_EQ(v, 65535);
  ASSERT_TRUE(parse_u16("100", &v));
  ASSERT_EQ(v, 100);

  ASSERT_FALSE(parse_u16("65536", &v));
  ASSERT_FALSE(parse_u16("-1", &v));
  ASSERT_FALSE(parse_u16("", &v));
  TEST_END();
}

static void test_parse_i16(void) {
  TEST_START("parse_i16");
  int16_t v;

  ASSERT_TRUE(parse_i16("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i16("32767", &v));
  ASSERT_EQ(v, 32767);
  ASSERT_TRUE(parse_i16("-32768", &v));
  ASSERT_EQ(v, -32768);

  ASSERT_FALSE(parse_i16("32768", &v));
  ASSERT_FALSE(parse_i16("-32769", &v));
  ASSERT_FALSE(parse_i16("", &v));
  TEST_END();
}

static void test_parse_u8(void) {
  TEST_START("parse_u8");
  uint8_t v;

  ASSERT_TRUE(parse_u8("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_u8("255", &v));
  ASSERT_EQ(v, 255);
  ASSERT_TRUE(parse_u8("128", &v));
  ASSERT_EQ(v, 128);
  ASSERT_TRUE(parse_u8("000", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_u8("00128", &v));
  ASSERT_EQ(v, 128);

  ASSERT_FALSE(parse_u8("256", &v));
  ASSERT_FALSE(parse_u8("-1", &v));
  ASSERT_FALSE(parse_u8("", &v));
  ASSERT_FALSE(parse_u8("abc", &v));
  TEST_END();
}

static void test_parse_i8(void) {
  TEST_START("parse_i8");
  int8_t v;

  ASSERT_TRUE(parse_i8("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(parse_i8("127", &v));
  ASSERT_EQ(v, 127);
  ASSERT_TRUE(parse_i8("-128", &v));
  ASSERT_EQ(v, -128);

  ASSERT_FALSE(parse_i8("128", &v));
  ASSERT_FALSE(parse_i8("-129", &v));
  ASSERT_FALSE(parse_i8("", &v));
  TEST_END();
}

static void test_parse_hex_u64(void) {
  TEST_START("parse_hex_u64");
  uint64_t v;

  ASSERT_TRUE(parse_hex_u64("0", &v));
  ASSERT_EQ(v, 0ULL);
  ASSERT_TRUE(parse_hex_u64("FF", &v));
  ASSERT_EQ(v, 255ULL);
  ASSERT_TRUE(parse_hex_u64("ff", &v));
  ASSERT_EQ(v, 255ULL);
  ASSERT_TRUE(parse_hex_u64("0xFF", &v));
  ASSERT_EQ(v, 255ULL);
  ASSERT_TRUE(parse_hex_u64("0x0", &v));
  ASSERT_EQ(v, 0ULL);
  ASSERT_TRUE(parse_hex_u64("DEADBEEF", &v));
  ASSERT_EQ(v, 0xDEADBEEFULL);
  ASSERT_TRUE(parse_hex_u64("FFFFFFFFFFFFFFFF", &v));
  ASSERT_EQ(v, 0xFFFFFFFFFFFFFFFFULL);
  ASSERT_TRUE(parse_hex_u64("0XFF", &v));
  ASSERT_EQ(v, 255ULL);
  ASSERT_TRUE(parse_hex_u64("aBcDeF", &v));
  ASSERT_EQ(v, 0xABCDEFULL);
  ASSERT_TRUE(parse_hex_u64("0000FF", &v));
  ASSERT_EQ(v, 255ULL);

  ASSERT_FALSE(parse_hex_u64("", &v));
  ASSERT_FALSE(parse_hex_u64("0x", &v));
  ASSERT_FALSE(parse_hex_u64("0xG", &v));
  ASSERT_FALSE(parse_hex_u64("0xGG", &v));
  ASSERT_FALSE(parse_hex_u64("GH", &v));
  ASSERT_FALSE(parse_hex_u64("0x0xFF", &v));
  ASSERT_FALSE(parse_hex_u64(NULL, &v));
  ASSERT_FALSE(parse_hex_u64("-1", &v));
  TEST_END();
}

static void test_parse_hex_u32(void) {
  TEST_START("parse_hex_u32");
  uint32_t v;

  ASSERT_TRUE(parse_hex_u32("ABCD", &v));
  ASSERT_EQ(v, 0xABCDU);
  ASSERT_TRUE(parse_hex_u32("0xFFFF", &v));
  ASSERT_EQ(v, 0xFFFFU);
  ASSERT_TRUE(parse_hex_u32("0", &v));
  ASSERT_EQ(v, 0U);

  ASSERT_FALSE(parse_hex_u32("100000000", &v));
  ASSERT_FALSE(parse_hex_u32("FFFFFFFFF", &v));
  TEST_END();
}

static void test_parse_f64(void) {
  TEST_START("parse_f64");
  double v;

  ASSERT_TRUE(parse_f64("0", &v));
  ASSERT_DBL_NEAR(v, 0.0, 1e-15);
  ASSERT_TRUE(parse_f64("1", &v));
  ASSERT_DBL_NEAR(v, 1.0, 1e-15);
  ASSERT_TRUE(parse_f64("-1", &v));
  ASSERT_DBL_NEAR(v, -1.0, 1e-15);
  ASSERT_TRUE(parse_f64("+2.5", &v));
  ASSERT_DBL_NEAR(v, 2.5, 1e-15);
  ASSERT_TRUE(parse_f64(".5", &v));
  ASSERT_DBL_NEAR(v, 0.5, 1e-15);
  ASSERT_TRUE(parse_f64("3.14159", &v));
  ASSERT_DBL_NEAR(v, 3.14159, 1e-12);
  ASSERT_TRUE(parse_f64("1e10", &v));
  ASSERT_DBL_NEAR(v, 1e10, 1e-5);
  ASSERT_TRUE(parse_f64("1.5e-3", &v));
  ASSERT_DBL_NEAR(v, 0.0015, 1e-12);
  ASSERT_TRUE(parse_f64("0.", &v));
  ASSERT_DBL_NEAR(v, 0.0, 1e-15);
  ASSERT_TRUE(parse_f64("0.0", &v));
  ASSERT_DBL_NEAR(v, 0.0, 1e-15);
  ASSERT_TRUE(parse_f64("-0", &v));
  ASSERT_DBL_NEAR(v, 0.0, 1e-15);
  ASSERT_TRUE(parse_f64("123.", &v));
  ASSERT_DBL_NEAR(v, 123.0, 1e-12);
  ASSERT_TRUE(parse_f64(".25", &v));
  ASSERT_DBL_NEAR(v, 0.25, 1e-12);
  ASSERT_TRUE(parse_f64("-3e+2", &v));
  ASSERT_DBL_NEAR(v, -300.0, 1e-12);
  ASSERT_TRUE(parse_f64("1e-400", &v));
  ASSERT_DBL_NEAR(v, 0.0, 1e-15);

  ASSERT_FALSE(parse_f64("", &v));
  ASSERT_FALSE(parse_f64("e10", &v));
  ASSERT_FALSE(parse_f64("-", &v));
  ASSERT_FALSE(parse_f64("+", &v));
  ASSERT_FALSE(parse_f64("1.2.3", &v));
  ASSERT_FALSE(parse_f64(NULL, &v));
  ASSERT_FALSE(parse_f64("1e", &v));
  ASSERT_FALSE(parse_f64("1e-", &v));
  ASSERT_FALSE(parse_f64("1e+", &v));
  TEST_END();
}

static void test_parse_f32(void) {
  TEST_START("parse_f32");
  float v;

  ASSERT_TRUE(parse_f32("0", &v));
  ASSERT_FLT_NEAR(v, 0.0f, 1e-7f);
  ASSERT_TRUE(parse_f32("3.14", &v));
  ASSERT_FLT_NEAR(v, 3.14f, 1e-5f);
  ASSERT_TRUE(parse_f32("-2.5", &v));
  ASSERT_FLT_NEAR(v, -2.5f, 1e-6f);
  ASSERT_TRUE(parse_f32("0.", &v));
  ASSERT_FLT_NEAR(v, 0.0f, 1e-7f);
  ASSERT_TRUE(parse_f32("-0", &v));
  ASSERT_FLT_NEAR(v, 0.0f, 1e-7f);

  ASSERT_FALSE(parse_f32("", &v));
  ASSERT_FALSE(parse_f32("abc", &v));
  ASSERT_FALSE(parse_f32("1e", &v));
  TEST_END();
}

/*=======================================================================================
 * strtol-backed Parser Tests
 *=======================================================================================*/

static void test_strtol_parse_u64(void) {
  TEST_START("strtol_parse_u64");
  uint64_t v;

  ASSERT_TRUE(strtol_parse_u64("0", &v));
  ASSERT_EQ(v, 0ULL);
  ASSERT_TRUE(strtol_parse_u64("  42", &v));
  ASSERT_EQ(v, 42ULL);
  ASSERT_TRUE(strtol_parse_u64("0xFF", &v));
  ASSERT_EQ(v, 255ULL);
  ASSERT_TRUE(strtol_parse_u64("012", &v));
  ASSERT_EQ(v, 10ULL);
  ASSERT_TRUE(strtol_parse_u64("+5", &v));
  ASSERT_EQ(v, 5ULL);

  ASSERT_FALSE(strtol_parse_u64("", &v));
  ASSERT_FALSE(strtol_parse_u64("12a", &v));
  ASSERT_FALSE(strtol_parse_u64(NULL, &v));
  ASSERT_FALSE(strtol_parse_u64("  ", &v));
  ASSERT_FALSE(strtol_parse_u64("+", &v));
  ASSERT_FALSE(strtol_parse_u64("-", &v));
  ASSERT_FALSE(strtol_parse_u64("0xGG", &v));
  ASSERT_FALSE(strtol_parse_u64("0x", &v));
  /* strtoull("-1") wraps to UINT64_MAX — not rejected */
  ASSERT_TRUE(strtol_parse_u64("-1", &v));
  ASSERT_EQ(v, 18446744073709551615ULL);
  TEST_END();
}

static void test_strtol_parse_i64(void) {
  TEST_START("strtol_parse_i64");
  int64_t v;

  ASSERT_TRUE(strtol_parse_i64("-1", &v));
  ASSERT_EQ(v, -1);
  ASSERT_TRUE(strtol_parse_i64("0xFF", &v));
  ASSERT_EQ(v, 255);
  ASSERT_TRUE(strtol_parse_i64("+42", &v));
  ASSERT_EQ(v, 42);

  ASSERT_FALSE(strtol_parse_i64("", &v));
  ASSERT_FALSE(strtol_parse_i64("1a", &v));
  ASSERT_FALSE(strtol_parse_i64(NULL, &v));
  ASSERT_FALSE(strtol_parse_i64("  ", &v));
  ASSERT_FALSE(strtol_parse_i64("+", &v));
  ASSERT_FALSE(strtol_parse_i64("-", &v));
  ASSERT_FALSE(strtol_parse_i64("0xGG", &v));
  TEST_END();
}

static void test_strtol_parse_u32(void) {
  TEST_START("strtol_parse_u32");
  uint32_t v;

  ASSERT_TRUE(strtol_parse_u32("42", &v));
  ASSERT_EQ(v, 42U);
  ASSERT_TRUE(strtol_parse_u32("0xFF", &v));
  ASSERT_EQ(v, 255U);

  ASSERT_FALSE(strtol_parse_u32("", &v));
  ASSERT_FALSE(strtol_parse_u32("  ", &v));
  ASSERT_FALSE(strtol_parse_u32("+", &v));
  ASSERT_FALSE(strtol_parse_u32("-", &v));
  ASSERT_FALSE(strtol_parse_u32("0xGG", &v));
  /* strtoul("-1") wraps to ULONG_MAX — not rejected */
  ASSERT_TRUE(strtol_parse_u32("-1", &v));
  ASSERT_EQ(v, 4294967295U);
  TEST_END();
}

static void test_strtol_parse_i32(void) {
  TEST_START("strtol_parse_i32");
  int32_t v;

  ASSERT_TRUE(strtol_parse_i32("-100", &v));
  ASSERT_EQ(v, -100);
  ASSERT_TRUE(strtol_parse_i32("+200", &v));
  ASSERT_EQ(v, 200);
  ASSERT_TRUE(strtol_parse_i32("0xA", &v));
  ASSERT_EQ(v, 10);

  ASSERT_FALSE(strtol_parse_i32("", &v));
  ASSERT_FALSE(strtol_parse_i32("  ", &v));
  ASSERT_FALSE(strtol_parse_i32("+", &v));
  ASSERT_FALSE(strtol_parse_i32("-", &v));
  TEST_END();
}

static void test_strtol_parse_u16(void) {
  TEST_START("strtol_parse_u16");
  uint16_t v;

  ASSERT_TRUE(strtol_parse_u16("255", &v));
  ASSERT_EQ(v, 255);
  ASSERT_TRUE(strtol_parse_u16("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_FALSE(strtol_parse_u16("65536", &v));
  ASSERT_FALSE(strtol_parse_u16("", &v));
  TEST_END();
}

static void test_strtol_parse_i16(void) {
  TEST_START("strtol_parse_i16");
  int16_t v;

  ASSERT_TRUE(strtol_parse_i16("-32768", &v));
  ASSERT_EQ(v, -32768);
  ASSERT_TRUE(strtol_parse_i16("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_FALSE(strtol_parse_i16("32768", &v));
  ASSERT_FALSE(strtol_parse_i16("", &v));
  TEST_END();
}

static void test_strtol_parse_u8(void) {
  TEST_START("strtol_parse_u8");
  uint8_t v;

  ASSERT_TRUE(strtol_parse_u8("255", &v));
  ASSERT_EQ(v, 255);
  ASSERT_TRUE(strtol_parse_u8("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_FALSE(strtol_parse_u8("256", &v));
  ASSERT_FALSE(strtol_parse_u8("", &v));
  TEST_END();
}

static void test_strtol_parse_i8(void) {
  TEST_START("strtol_parse_i8");
  int8_t v;

  ASSERT_TRUE(strtol_parse_i8("127", &v));
  ASSERT_EQ(v, 127);
  ASSERT_TRUE(strtol_parse_i8("-128", &v));
  ASSERT_EQ(v, -128);
  ASSERT_TRUE(strtol_parse_i8("0", &v));
  ASSERT_EQ(v, 0);
  ASSERT_FALSE(strtol_parse_i8("128", &v));
  ASSERT_FALSE(strtol_parse_i8("", &v));
  TEST_END();
}

static void test_strtol_parse_float(void) {
  TEST_START("strtol_parse_float");
  float v;

  ASSERT_TRUE(strtol_parse_float("3.14", &v));
  ASSERT_FLT_NEAR(v, 3.14f, 1e-5f);
  ASSERT_TRUE(strtol_parse_float("-2e3", &v));
  ASSERT_FLT_NEAR(v, -2000.0f, 1e-3f);
  ASSERT_TRUE(strtol_parse_float("0", &v));
  ASSERT_FLT_NEAR(v, 0.0f, 1e-7f);

  ASSERT_FALSE(strtol_parse_float("", &v));
  ASSERT_FALSE(strtol_parse_float("abc", &v));
  ASSERT_FALSE(strtol_parse_float("  ", &v));
  ASSERT_FALSE(strtol_parse_float("+", &v));
  ASSERT_FALSE(strtol_parse_float("-", &v));
  ASSERT_FALSE(strtol_parse_float("1e", &v));
  TEST_END();
}

static void test_strtol_parse_double(void) {
  TEST_START("strtol_parse_double");
  double v;

  ASSERT_TRUE(strtol_parse_double("1.5e-2", &v));
  ASSERT_DBL_NEAR(v, 0.015, 1e-12);
  ASSERT_TRUE(strtol_parse_double("0", &v));
  ASSERT_DBL_NEAR(v, 0.0, 1e-12);

  ASSERT_FALSE(strtol_parse_double("", &v));
  ASSERT_FALSE(strtol_parse_double("  ", &v));
  ASSERT_FALSE(strtol_parse_double("+", &v));
  ASSERT_FALSE(strtol_parse_double("-", &v));
  ASSERT_FALSE(strtol_parse_double("1e", &v));
  TEST_END();
}

/*=======================================================================================
 * Format Function Tests
 *=======================================================================================*/

static void test_format_u64(void) {
  TEST_START("format_u64");
  char buf[32];

  ASSERT_EQ(format_u64(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  ASSERT_EQ(format_u64(buf, sizeof(buf), 1), 1);
  ASSERT_STR_EQ(buf, "1");
  ASSERT_EQ(format_u64(buf, sizeof(buf), 42), 2);
  ASSERT_STR_EQ(buf, "42");
  ASSERT_EQ(format_u64(buf, sizeof(buf), 18446744073709551615ULL), 20);
  ASSERT_STR_EQ(buf, "18446744073709551615");
  ASSERT_EQ(format_u64(buf, 4, 12345), 3);
  ASSERT_STR_EQ(buf, "123");
  ASSERT_EQ(format_u64(buf, 1, 999), 0);
  ASSERT_STR_EQ(buf, "");
  ASSERT_EQ(format_u64(buf, 2, 5), 1);
  ASSERT_STR_EQ(buf, "5");
  TEST_END();
}

static void test_format_i64(void) {
  TEST_START("format_i64");
  char buf[32];

  ASSERT_EQ(format_i64(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  ASSERT_EQ(format_i64(buf, sizeof(buf), -1), 2);
  ASSERT_STR_EQ(buf, "-1");
  ASSERT_EQ(format_i64(buf, sizeof(buf), 123), 3);
  ASSERT_STR_EQ(buf, "123");
  ASSERT_EQ(format_i64(buf, sizeof(buf), -9223372036854775807LL - 1), 20);
  ASSERT_STR_EQ(buf, "-9223372036854775808");
  ASSERT_EQ(format_i64(buf, 3, -10), 2);
  ASSERT_STR_EQ(buf, "-1");
  ASSERT_EQ(format_i64(buf, 4, -100), 3);
  ASSERT_STR_EQ(buf, "-10");
  ASSERT_EQ(format_i64(buf, 1, 0), 0);
  ASSERT_STR_EQ(buf, "");
  TEST_END();
}

static void test_format_hex64(void) {
  TEST_START("format_hex64");
  char buf[32];

  ASSERT_EQ(format_hex64(buf, sizeof(buf), 0), 3);
  ASSERT_STR_EQ(buf, "0x0");
  ASSERT_EQ(format_hex64(buf, sizeof(buf), 255), 4);
  ASSERT_STR_EQ(buf, "0xFF");
  ASSERT_EQ(format_hex64(buf, sizeof(buf), 0xDEADBEEF), 10);
  ASSERT_STR_EQ(buf, "0xDEADBEEF");
  ASSERT_EQ(format_hex64(buf, 3, 255), 2);
  ASSERT_STR_EQ(buf, "0x");
  ASSERT_EQ(format_hex64(buf, 1, 255), 0);
  ASSERT_STR_EQ(buf, "");
  ASSERT_EQ(format_hex64(buf, sizeof(buf), 0xFFFFFFFFFFFFFFFFULL), 18);
  ASSERT_STR_EQ(buf, "0xFFFFFFFFFFFFFFFF");
  TEST_END();
}

static void test_format_u32(void) {
  TEST_START("format_u32");
  char buf[16];

  ASSERT_EQ(format_u32(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  ASSERT_EQ(format_u32(buf, sizeof(buf), 4000000000U), 10);
  ASSERT_STR_EQ(buf, "4000000000");
  TEST_END();
}

static void test_format_i32(void) {
  TEST_START("format_i32");
  char buf[16];

  ASSERT_EQ(format_i32(buf, sizeof(buf), -100), 4);
  ASSERT_STR_EQ(buf, "-100");
  ASSERT_EQ(format_i32(buf, sizeof(buf), 2147483647), 10);
  ASSERT_STR_EQ(buf, "2147483647");
  ASSERT_EQ(format_i32(buf, sizeof(buf), INT32_MIN), 11);
  ASSERT_STR_EQ(buf, "-2147483648");
  TEST_END();
}

static void test_format_hex32(void) {
  TEST_START("format_hex32");
  char buf[16];

  ASSERT_EQ(format_hex32(buf, sizeof(buf), 0xABCD), 6);
  ASSERT_STR_EQ(buf, "0xABCD");
  ASSERT_EQ(format_hex32(buf, sizeof(buf), 0), 3);
  ASSERT_STR_EQ(buf, "0x0");
  TEST_END();
}

static void test_format_u16(void) {
  TEST_START("format_u16");
  char buf[16];

  ASSERT_EQ(format_u16(buf, sizeof(buf), 65535), 5);
  ASSERT_STR_EQ(buf, "65535");
  ASSERT_EQ(format_u16(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  TEST_END();
}

static void test_format_i16(void) {
  TEST_START("format_i16");
  char buf[16];

  ASSERT_EQ(format_i16(buf, sizeof(buf), -32768), 6);
  ASSERT_STR_EQ(buf, "-32768");
  ASSERT_EQ(format_i16(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  TEST_END();
}

static void test_format_hex16(void) {
  TEST_START("format_hex16");
  char buf[16];

  ASSERT_EQ(format_hex16(buf, sizeof(buf), 0xABCD), 6);
  ASSERT_STR_EQ(buf, "0xABCD");
  ASSERT_EQ(format_hex16(buf, sizeof(buf), 0), 3);
  ASSERT_STR_EQ(buf, "0x0");
  TEST_END();
}

static void test_format_u8(void) {
  TEST_START("format_u8");
  char buf[16];

  ASSERT_EQ(format_u8(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  ASSERT_EQ(format_u8(buf, sizeof(buf), 255), 3);
  ASSERT_STR_EQ(buf, "255");
  ASSERT_EQ(format_u8(buf, 2, 5), 1);
  ASSERT_STR_EQ(buf, "5");
  TEST_END();
}

static void test_format_i8(void) {
  TEST_START("format_i8");
  char buf[16];

  ASSERT_EQ(format_i8(buf, sizeof(buf), -128), 4);
  ASSERT_STR_EQ(buf, "-128");
  ASSERT_EQ(format_i8(buf, sizeof(buf), 127), 3);
  ASSERT_STR_EQ(buf, "127");
  ASSERT_EQ(format_i8(buf, sizeof(buf), 0), 1);
  ASSERT_STR_EQ(buf, "0");
  TEST_END();
}

static void test_format_hex8(void) {
  TEST_START("format_hex8");
  char buf[16];

  ASSERT_EQ(format_hex8(buf, sizeof(buf), 0xAB), 2);
  ASSERT_STR_EQ(buf, "AB");
  ASSERT_EQ(format_hex8(buf, sizeof(buf), 0x0), 2);
  ASSERT_STR_EQ(buf, "00");
  ASSERT_EQ(format_hex8(buf, sizeof(buf), 0x5), 2);
  ASSERT_STR_EQ(buf, "05");
  ASSERT_EQ(format_hex8(buf, sizeof(buf), 0xFF), 2);
  ASSERT_STR_EQ(buf, "FF");
  ASSERT_EQ(format_hex8(buf, 2, 0xFF), 0);
  ASSERT_STR_EQ(buf, "");
  ASSERT_EQ(format_hex8(buf, 1, 0xFF), 0);
  ASSERT_STR_EQ(buf, "");
  TEST_END();
}

static void test_format_bytes(void) {
  TEST_START("format_bytes");
  char buf[64];
  const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t single[] = {0xA5};

  ASSERT_EQ(format_bytes(buf, sizeof(buf), data, 0), 0);
  ASSERT_STR_EQ(buf, "");

  ASSERT_EQ(format_bytes(buf, sizeof(buf), data, 4), 11);
  ASSERT_STR_EQ(buf, "DE AD BE EF");

  ASSERT_EQ(format_bytes(buf, 6, data, 4), 5);
  ASSERT_STR_EQ(buf, "DE AD");

  ASSERT_EQ(format_bytes(buf, 4, data, 4), 2);
  ASSERT_STR_EQ(buf, "DE");

  ASSERT_EQ(format_bytes(buf, sizeof(buf), single, 1), 2);
  ASSERT_STR_EQ(buf, "A5");

  ASSERT_EQ(format_bytes(buf, sizeof(buf), NULL, 0), 0);
  ASSERT_STR_EQ(buf, "");
  TEST_END();
}

/*=======================================================================================
 * Main
 *=======================================================================================*/

int main(void) {
  printf("=== cli_args_parse unit tests ===\n\n");

  /* Hand-rolled parsers */
  printf("--- hand-rolled ---\n");
  RUN_SUITE(test_parse_u64);
  RUN_SUITE(test_parse_i64);
  RUN_SUITE(test_parse_u32);
  RUN_SUITE(test_parse_i32);
  RUN_SUITE(test_parse_u16);
  RUN_SUITE(test_parse_i16);
  RUN_SUITE(test_parse_u8);
  RUN_SUITE(test_parse_i8);
  RUN_SUITE(test_parse_hex_u64);
  RUN_SUITE(test_parse_hex_u32);
  RUN_SUITE(test_parse_f64);
  RUN_SUITE(test_parse_f32);

  /* strtol-backed parsers */
  printf("\n--- strtol-backed ---\n");
  RUN_SUITE(test_strtol_parse_u64);
  RUN_SUITE(test_strtol_parse_i64);
  RUN_SUITE(test_strtol_parse_u32);
  RUN_SUITE(test_strtol_parse_i32);
  RUN_SUITE(test_strtol_parse_u16);
  RUN_SUITE(test_strtol_parse_i16);
  RUN_SUITE(test_strtol_parse_u8);
  RUN_SUITE(test_strtol_parse_i8);
  RUN_SUITE(test_strtol_parse_float);
  RUN_SUITE(test_strtol_parse_double);

  /* Formatters */
  printf("\n--- formatters ---\n");
  RUN_SUITE(test_format_u64);
  RUN_SUITE(test_format_i64);
  RUN_SUITE(test_format_hex64);
  RUN_SUITE(test_format_u32);
  RUN_SUITE(test_format_i32);
  RUN_SUITE(test_format_hex32);
  RUN_SUITE(test_format_u16);
  RUN_SUITE(test_format_i16);
  RUN_SUITE(test_format_hex16);
  RUN_SUITE(test_format_u8);
  RUN_SUITE(test_format_i8);
  RUN_SUITE(test_format_hex8);
  RUN_SUITE(test_format_bytes);

  /* Summary */
  printf("\n=== Results: %d tests, %d asserts, %d failures ===\n", s_tests_run, s_asserts,
         s_tests_fail);

  return s_tests_fail > 0 ? 1 : 0;
}
