/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MOS_LIBS_RPC_COMMON_SRC_MG_RPC_MG_RPC_H_
#define CS_MOS_LIBS_RPC_COMMON_SRC_MG_RPC_MG_RPC_H_

#include <inttypes.h>
#include <stdbool.h>

#include "mg_rpc_channel.h"

#include "common/mg_str.h"
#include "common/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mg_rpc;

struct mg_rpc_cfg {
  char *id;
  char *psk;
  int max_queue_length;
  int default_out_channel_idle_close_timeout;
};

struct mg_rpc_frame {
  int version;
  int64_t id;
  int error_code;
  struct mg_str src, dst, tag;
  struct mg_str method, args;
  struct mg_str result, error_msg;
  struct mg_str auth;
};

struct mg_rpc_authn {
  struct mg_str username;
};

/* Create mg_rpc instance. Takes over cfg, which must be heap-allocated. */
struct mg_rpc *mg_rpc_create(struct mg_rpc_cfg *cfg);

/*
 * Adds a channel to the instance.
 * If dst is empty, it will be learned when first frame arrives from the other
 * end. A "default" channel, if present, will be used for frames that don't have
 * a better match.
 * If is_trusted is true, certain privileged commands will be allowed.
 */
void mg_rpc_add_channel(struct mg_rpc *c, const struct mg_str dst,
                        struct mg_rpc_channel *ch, bool is_trusted);
#define MG_RPC_DST_DEFAULT "*"

/* Invokes connect method on all channels of this instance. */
void mg_rpc_connect(struct mg_rpc *c);

/* Invokes close method on all channels of this instance. */
void mg_rpc_disconnect(struct mg_rpc *c);

/* Auxiliary information about the request or response. */
struct mg_rpc_frame_info {
  const char *channel_type; /* Type of the channel this message arrived on. */
  bool channel_is_trusted;  /* Whether the channel is marked as trusted. */
};

/* Signature of the function that receives response to a request. */
typedef void (*mg_result_cb_t)(struct mg_rpc *c, void *cb_arg,
                               struct mg_rpc_frame_info *fi,
                               struct mg_str result, int error_code,
                               struct mg_str error_msg);

/*
 * Send a request.
 * cb is optional, in which case request is sent but response is not required.
 * opts can be NULL, in which case defaults are used.
 */
struct mg_rpc_call_opts {
  struct mg_str dst; /* Destination ID. If not provided, cloud is implied. */
};
bool mg_rpc_callf(struct mg_rpc *c, const struct mg_str method,
                  mg_result_cb_t cb, void *cb_arg,
                  const struct mg_rpc_call_opts *opts, const char *args_jsonf,
                  ...);

/*
 * Incoming request info.
 * This structure is passed to request handlers and must be passed back
 * when a response is ready.
 */
struct mg_rpc_request_info {
  struct mg_rpc *rpc;
  int64_t id;           /* Request id. */
  struct mg_str src;    /* Id of the request sender, if provided. */
  struct mg_str tag;    /* Request tag. Opaque, should be passed back as is. */
  struct mg_str method; /* RPC Method */
  struct mg_str auth;   /* Auth JSON */
  struct mg_rpc_authn authn_info; /* Parsed authn info; either from the
                                     underlying channel or from RPC layer */
  const char *args_fmt;           /* Arguments format string */
  void *user_data; /* Place to store user pointer. Not used by mg_rpc. */

  /*
   * Channel this request was received on. Will be used to route the response
   * if present and valid, otherwise src is used to find a suitable channel.
   */
  struct mg_rpc_channel *ch;
};

/*
 * Signature of an incoming request handler.
 * Note that only request_info remains valid after return from this function,
 * frame_info and args will be invalidated.
 */
typedef void (*mg_handler_cb_t)(struct mg_rpc_request_info *ri, void *cb_arg,
                                struct mg_rpc_frame_info *fi,
                                struct mg_str args);

/* Add a method handler. */
void mg_rpc_add_handler(struct mg_rpc *c, const char *method,
                        const char *args_fmt, mg_handler_cb_t cb, void *cb_arg);

/*
 * Signature of an incoming requests prehandler, which is called right before
 * calling the actual handler.
 *
 * If it returns false, the further request processing is not performed. It's
 * called for existing handlers only.
 */
typedef bool (*mg_prehandler_cb_t)(struct mg_rpc_request_info *ri, void *cb_arg,
                                   struct mg_rpc_frame_info *fi,
                                   struct mg_str args);

/* Set a generic method prehandler. */
void mg_rpc_set_prehandler(struct mg_rpc *c, mg_prehandler_cb_t cb,
                           void *cb_arg);

/*
 * Respond to an incoming request.
 * result_json_fmt can be NULL, in which case no result is included.
 * `ri` is freed by the call, so it's illegal to use it afterwards.
 */
bool mg_rpc_send_responsef(struct mg_rpc_request_info *ri,
                           const char *result_json_fmt, ...);

/*
 * Send and error response to an incoming request.
 * error_msg_fmt is optional and can be NULL, in which case only code is sent.
 * `ri` is freed by the call, so it's illegal to use it afterwards.
 */
bool mg_rpc_send_errorf(struct mg_rpc_request_info *ri, int error_code,
                        const char *error_msg_fmt, ...);

/*
 * Like mg_rpc_send_errorf, but uses JSON formatting, see json_printf().
 * NOTE: "error.message" will still be a string but will contain serialized
 * JSON formatted accordingly to error_json_fmt.
 */
bool mg_rpc_send_error_jsonf(struct mg_rpc_request_info *ri, int error_code,
                             const char *error_json_fmt, ...);

/* Returns true if the instance has an open default channel. */
bool mg_rpc_is_connected(struct mg_rpc *c);

/* Returns true if the instance has an open default channel
 * and it's not currently busy. */
bool mg_rpc_can_send(struct mg_rpc *c);

/* mg_rpc event observer. */
enum mg_rpc_event {
  MG_RPC_EV_CHANNEL_OPEN,   /* struct mg_str *dst */
  MG_RPC_EV_CHANNEL_CLOSED, /* struct mg_str *dst */
};
typedef void (*mg_observer_cb_t)(struct mg_rpc *c, void *cb_arg,
                                 enum mg_rpc_event ev, void *ev_arg);
void mg_rpc_add_observer(struct mg_rpc *c, mg_observer_cb_t cb, void *cb_arg);
void mg_rpc_remove_observer(struct mg_rpc *c, mg_observer_cb_t cb,
                            void *cb_arg);

void mg_rpc_free_request_info(struct mg_rpc_request_info *ri);
void mg_rpc_free(struct mg_rpc *c);

/* Enable RPC.List handler that returns a list of all registered endpoints */
void mg_rpc_add_list_handler(struct mg_rpc *c);

/*
 * Parses frame `f` and stores result into `frame`. Returns true in case of
 * success, false otherwise.
 */
bool mg_rpc_parse_frame(const struct mg_str f, struct mg_rpc_frame *frame);

bool mg_rpc_check_digest_auth(struct mg_rpc_request_info *ri);

#ifdef __cplusplus
}
#endif

#endif /* CS_MOS_LIBS_RPC_COMMON_SRC_MG_RPC_MG_RPC_H_ */
