/*
 * An implementation of proquint (https://arxiv.org/html/0901.4016)
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "partake_proquint.h"

#define ERROR (-1)

static const char *const consonants = "bdfghjklmnprstvz";
static const char *const vowels = "aiou";


static inline char hi4bits_to_consonant(uint16_t *pi) {
    uint16_t j = (*pi) & 0xF000;
    *pi <<= 4;
    j >>= 12;
    return consonants[j];
}


static inline char hi2bits_to_vowel(uint16_t *pi) {
    uint16_t j = (*pi) & 0xC000;
    *pi <<= 2;
    j >>= 14;
    return vowels[j];
}


static inline void uint16_to_proquint(uint16_t i, char *dest6) {
    *dest6++ = hi4bits_to_consonant(&i);
    *dest6++ = hi2bits_to_vowel(&i);
    *dest6++ = hi4bits_to_consonant(&i);
    *dest6++ = hi2bits_to_vowel(&i);
    *dest6++ = hi4bits_to_consonant(&i);
    *dest6 = '\0';
}


void partake_proquint_from_uint64(uint64_t i, char *dest24) {
    uint16_to_proquint((uint16_t)((i & 0xFFFF000000000000ull) >> 48), dest24);
    dest24 += 5;
    *dest24++ = '-';
    uint16_to_proquint((uint16_t)((i & 0x0000FFFF00000000ull) >> 32), dest24);
    dest24 += 5;
    *dest24++ = '-';
    uint16_to_proquint((uint16_t)((i & 0x00000000FFFF0000ull) >> 16), dest24);
    dest24 += 5;
    *dest24++ = '-';
    uint16_to_proquint((uint16_t)(i & 0x000000000000FFFFull), dest24);
}


static inline int consonant_to_4bits(char ch, uint16_t *dest) {
    *dest <<= 4;
    switch (ch) {
    case 'b': *dest |= 0x0; break;
    case 'd': *dest |= 0x1; break;
    case 'f': *dest |= 0x2; break;
    case 'g': *dest |= 0x3; break;
    case 'h': *dest |= 0x4; break;
    case 'j': *dest |= 0x5; break;
    case 'k': *dest |= 0x6; break;
    case 'l': *dest |= 0x7; break;
    case 'm': *dest |= 0x8; break;
    case 'n': *dest |= 0x9; break;
    case 'p': *dest |= 0xA; break;
    case 'r': *dest |= 0xB; break;
    case 's': *dest |= 0xC; break;
    case 't': *dest |= 0xD; break;
    case 'v': *dest |= 0xE; break;
    case 'z': *dest |= 0xF; break;
    default: return ERROR;
    }
    return 0;
}


static inline int vowel_to_2bits(char ch, uint16_t *dest) {
    *dest <<= 2;
    switch (ch) {
    case 'a': *dest |= 0x0; break;
    case 'i': *dest |= 0x1; break;
    case 'o': *dest |= 0x2; break;
    case 'u': *dest |= 0x3; break;
    default: return ERROR;
    }
    return 0;
}


static inline int proquint_to_uint16(const char *pq, uint16_t *dest) {
    *dest = 0;
    int err;
    if ((err = consonant_to_4bits(*pq++, dest)) != 0)
        return err;
    if ((err = vowel_to_2bits(*pq++, dest)) != 0)
        return err;
    if ((err = consonant_to_4bits(*pq++, dest)) != 0)
        return err;
    if ((err = vowel_to_2bits(*pq++, dest)) != 0)
        return err;
    if ((err = consonant_to_4bits(*pq++, dest)) != 0)
        return err;
    return 0;
}


int partake_proquint_to_uint64(const char *pq, uint64_t *dest) {
    *dest = 0;
    uint16_t word;
    int err;

    if ((err = proquint_to_uint16(pq, &word)) != 0)
        return err;
    *dest <<= 16;
    *dest |= word;
    pq += 5;
    if (*pq++ != '-')
        return ERROR;

    if ((err = proquint_to_uint16(pq, &word)) != 0)
        return err;
    *dest <<= 16;
    *dest |= word;
    pq += 5;
    if (*pq++ != '-')
        return ERROR;

    if ((err = proquint_to_uint16(pq, &word)) != 0)
        return err;
    *dest <<= 16;
    *dest |= word;
    pq += 5;
    if (*pq++ != '-')
        return ERROR;

    if ((err = proquint_to_uint16(pq, &word)) != 0)
        return err;
    *dest <<= 16;
    *dest |= word;
    pq += 5;
    if (*pq != '\0')
        return ERROR;

    return 0;
}
