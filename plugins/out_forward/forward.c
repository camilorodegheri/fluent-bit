/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2016 Treasure Data Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include <msgpack.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_network.h>

#include "forward.h"

struct flb_output_plugin out_forward_plugin;

int cb_forward_init(struct flb_output_instance *ins, struct flb_config *config,
                    void *data)
{
    struct flb_out_forward_config *ctx;
    struct flb_upstream *upstream;
    struct flb_uri_field *f_tag = NULL;
    (void) data;

    ctx = calloc(1, sizeof(struct flb_out_forward_config));
    if (!ctx) {
        perror("calloc");
        return -1;
    }

    /* Set default network configuration */
    if (!ins->host.name) {
        ins->host.name = strdup("127.0.0.1");
    }
    if (ins->host.port == 0) {
        ins->host.port = 24224;
    }

    /* Prepare an upstream handler */
    upstream = flb_upstream_create(config,
                                   ins->host.name,
                                   ins->host.port,
                                   FLB_IO_TCP, NULL);
    if (!upstream) {
        free(ctx);
        return -1;
    }
    ctx->u = upstream;
    ctx->tag = FLB_CONFIG_DEFAULT_TAG;
    ctx->tag_len = sizeof(FLB_CONFIG_DEFAULT_TAG) - 1;

    if (ins->host.uri) {
        if (ins->host.uri->count > 0) {
            f_tag = flb_uri_get(ins->host.uri, 0);
            ctx->tag     = f_tag->value;
            ctx->tag_len = f_tag->length;
        }
    }

    flb_output_set_context(ins, ctx);
    return 0;
}

int cb_forward_exit(void *data, struct flb_config *config)
{
    (void) config;
    struct flb_out_forward_config *ctx = data;

    flb_upstream_destroy(ctx->u);
    free(ctx);

    return 0;
}

int cb_forward_flush(void *data, size_t bytes,
                     char *tag, int tag_len,
                     struct flb_input_instance *i_ins, void *out_context,
                     struct flb_config *config)
{
    int ret = -1;
    int entries = 0;
    size_t off = 0;
    size_t total;
    size_t bytes_sent;
    char *buf = NULL;
    msgpack_packer   mp_pck;
    msgpack_sbuffer  mp_sbuf;
    msgpack_unpacked result;
    struct flb_out_forward_config *ctx = out_context;
    struct flb_upstream_conn *u_conn;
    (void) i_ins;
    (void) config;

    /* Initialize packager */
    msgpack_sbuffer_init(&mp_sbuf);
    msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

    /* Count number of entries, is there a better way to do this ? */
    msgpack_unpacked_init(&result);
    while (msgpack_unpack_next(&result, data, bytes, &off)) {
        entries++;
    }
    msgpack_unpacked_destroy(&result);

    /* Output: root array */
    msgpack_pack_array(&mp_pck, 2);
    msgpack_pack_bin(&mp_pck, tag_len);
    msgpack_pack_bin_body(&mp_pck, tag, tag_len);
    msgpack_pack_array(&mp_pck, entries);

    /* Allocate a new buffer to merge data */
    buf = malloc(mp_sbuf.size + bytes);
    if (!buf) {
        perror("malloc");
        return -1;
    }

    memcpy(buf, mp_sbuf.data, mp_sbuf.size);
    memcpy(buf + mp_sbuf.size, data, bytes);
    total = mp_sbuf.size + bytes;
    msgpack_sbuffer_destroy(&mp_sbuf);

    u_conn = flb_upstream_conn_get(ctx->u);
    if (!u_conn) {
        flb_error("[out_forward] no upstream connections available");
        free(buf);
        return -1;
    }

    ret = flb_io_net_write(u_conn, buf, total, &bytes_sent);
    free(buf);

    flb_upstream_conn_release(u_conn);

    flb_trace("[out_forward] ended write()=%d bytes", bytes_sent);
    return ret;
}

/* Plugin reference */
struct flb_output_plugin out_forward_plugin = {
    .name         = "forward",
    .description  = "Forward (Fluentd protocol)",
    .cb_init      = cb_forward_init,
    .cb_pre_run   = NULL,
    .cb_flush     = cb_forward_flush,
    .cb_exit      = cb_forward_exit,
    .flags        = FLB_OUTPUT_NET,
};
