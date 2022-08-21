/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <unity.h>

#include "partake_proquint.c"

void setUp(void) {}
void tearDown(void) {}

void test_to_proquint(void) {
    uint64_t i;
    char pq[24];

    i = 0;
    partake_proquint_from_uint64(i, pq);
    TEST_ASSERT_EQUAL_STRING("babab-babab-babab-babab", pq);

    i = -1;
    partake_proquint_from_uint64(i, pq);
    TEST_ASSERT_EQUAL_STRING("zuzuz-zuzuz-zuzuz-zuzuz", pq);

    i = 0x3F54DCC18C62C18D;
    partake_proquint_from_uint64(i, pq);
    TEST_ASSERT_EQUAL_STRING("gutih-tugad-mudof-sakat", pq);
}

void test_16_from_proquint(void) {
    uint16_t i;
    int err;

    err = proquint_to_uint16("babab", &i);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT64(0, i);

    err = proquint_to_uint16("zuzuz", &i);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT64(0xFFFF, i);

    err = proquint_to_uint16("gutih", &i);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT64(0x3F54, i);
}

void test_from_proquint(void) {
    uint64_t i;
    int err;

    err = partake_proquint_to_uint64("babab-babab-babab-babab", &i);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT64(0, i);

    err = partake_proquint_to_uint64("zuzuz-zuzuz-zuzuz-zuzuz", &i);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT64(-1, i);

    err = partake_proquint_to_uint64("gutih-tugad-mudof-sakat", &i);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT64(0x3F54DCC18C62C18D, i);
}

void test_invalid_proquint(void) {
    uint64_t i;
    int err;

    err = partake_proquint_to_uint64("babab-babab-babab-baba", &i);
    TEST_ASSERT_NOT_EQUAL_INT(0, err);

    err = partake_proquint_to_uint64("babab-babab-babab-babab-", &i);
    TEST_ASSERT_NOT_EQUAL_INT(0, err);

    err = partake_proquint_to_uint64("Babab-babab-babab-babab", &i);
    TEST_ASSERT_NOT_EQUAL_INT(0, err);

    err = partake_proquint_to_uint64("babab-babab.babab-babab", &i);
    TEST_ASSERT_NOT_EQUAL_INT(0, err);
}

void test_roundtrip(void) {
    uint64_t i, j;
    char pq[24];
    int err;

    uint64_t samples[] = {
        -2, -1, 0, 1, 2, 0xF0, 0xF00, 0xF000, 0xF0000,
    };

    for (int n = 0; n < sizeof(samples) / sizeof(uint64_t); ++n) {
        i = samples[n];
        partake_proquint_from_uint64(i, pq);
        err = partake_proquint_to_uint64(pq, &j);
        TEST_ASSERT_EQUAL_INT(0, err);
        TEST_ASSERT_EQUAL_UINT64(i, j);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_to_proquint);
    RUN_TEST(test_16_from_proquint);
    RUN_TEST(test_from_proquint);
    RUN_TEST(test_invalid_proquint);
    RUN_TEST(test_roundtrip);
    return UNITY_END();
}
