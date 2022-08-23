/*
 * This file is part of the partake project
 * Copyright 2020-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

struct partaked_connection;
struct partaked_daemon;
struct partaked_config;

// Remove connection from the list to be closed upon server shutdown
void partaked_daemon_remove_connection(struct partaked_daemon *daemon,
                                       struct partaked_connection *conn);

int partaked_daemon_run(const struct partaked_config *config);
