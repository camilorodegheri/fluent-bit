/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit Demo
 *  ===============
 *  Copyright (C) 2015 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <signal.h>
#include <unistd.h>

#include <fluent-bit/flb_engine.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_utils.h>

extern struct flb_input_plugin in_lib_plugin;

/*
 * The library initialization routine basically register the in_lib
 * plugin for the configuration context in question. This is a mandatory step
 * for callers who wants to ingest data directly into the engine.
 */
int flb_lib_init(struct flb_config *config, char *output)
{
    int ret;

    ret = flb_input_set(config, "lib");
    if (ret == -1) {
        return -1;
    }

    ret = pipe(config->ch_data);
    if (ret == -1) {
        perror("pipe");
        return -1;
    }

    ret = flb_output_set(config, output);
    if (ret == -1) {
        return -1;
    }

    return 0;
}

int flb_lib_push(struct flb_config *config, void *data, size_t len)
{
    return write(config->ch_data[1], data, len);
}

static void flb_lib_worker(void *data)
{
    struct flb_config *config = data;
    flb_engine_start(config);
}

int flb_lib_start(struct flb_config *config)
{
    pthread_t tid;

    tid = mk_utils_worker_spawn(flb_lib_worker, config);
    if (tid == -1) {
        return -1;
    }

    config->worker = tid;
    return 0;
}

int flb_lib_stop(struct flb_config *config)
{
    int ret;
    uint64_t val;
    int n;

    flb_debug("[lib] sending STOP signal to the engine");
    val = FLB_ENGINE_STOP;
    n = write(config->ch_manager[1], &val, sizeof(uint64_t));
    ret = pthread_join(config->worker, NULL);

    flb_debug("[lib] Fluent Bit engine stopped");
    return ret;
}