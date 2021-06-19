/*
 * Shared memory segments for partaked
 *
 * Copyright 2020-2021 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "partake_protocol_builder.h"


struct partake_daemon_config;
struct partake_segment;


struct partake_segment *partake_segment_create(
        const struct partake_daemon_config *config);

void partake_segment_destroy(struct partake_segment *segment);


void *partake_segment_addr(struct partake_segment *segment);

size_t partake_segment_size(struct partake_segment *segment);


void partake_segment_add_mapping_spec(struct partake_segment *segment,
        flatcc_builder_t *b);
