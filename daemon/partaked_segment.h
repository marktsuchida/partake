/*
 * Shared memory segments for partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partake_protocol_builder.h"


struct partaked_daemon_config;
struct partaked_segment;


struct partaked_segment *partaked_segment_create(
        const struct partaked_daemon_config *config);

void partaked_segment_destroy(struct partaked_segment *segment);


void *partaked_segment_addr(struct partaked_segment *segment);

size_t partaked_segment_size(struct partaked_segment *segment);


void partaked_segment_add_mapping_spec(struct partaked_segment *segment,
        flatcc_builder_t *b);
