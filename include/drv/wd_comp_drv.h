/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __WD_COMP_DRV_H
#define __WD_COMP_DRV_H

#include "../wd_comp.h"

enum wd_comp_strm_pos {
	WD_COMP_STREAM_NEW,
	WD_COMP_STREAM_OLD,
};

enum wd_comp_strm_flush_type {
	WD_INVALID_FLUSH,
	WD_NO_FLUSH,
	WD_SYNC_FLUSH,
	WD_FINISH,
};

enum wd_comp_state {
	WD_COMP_STATEFUL,
	WD_COMP_STATELESS,
};

/* fixme wd_comp_msg */
struct wd_comp_msg {
	struct wd_comp_req req;
	__u32 tag;   	 /* request identifier */
	__u8 alg_type;   /* Denoted by enum wcrypto_comp_alg_type */
	__u8 op_type;    /* Denoted by enum wcrypto_comp_op_type */
	__u8 flush_type; /* Denoted by enum wcrypto_comp_flush_type */
	__u8 stream_mode;/* Denoted by enum wcrypto_comp_state */
	__u8 stream_pos; /* Denoted by enum wcrypto_stream_status */
	__u8 comp_lv;    /* Denoted by enum wcrypto_comp_level */
	__u8 data_fmt;   /* Data format, denoted by enum wd_buff_type */
	__u8 win_sz;     /* Denoted by enum wcrypto_comp_win_type */
	__u32 in_size;   /* Input data bytes */
	__u32 avail_out; /* Output buffer size */
	__u32 in_cons;   /* consumed bytes of input data */
	__u32 produced;  /* produced bytes of current operation */
	__u32 win_size;  /* Denoted by enum wcrypto_comp_win_type */
	__u32 status;    /* Denoted by error code and enum wcrypto_op_result */
	__u32 isize;	 /* Denoted by gzip isize */
	__u32 checksum;  /* Denoted by zlib/gzip CRC */
	void *ctx_buf;   /* Denoted HW ctx cache, for stream mode */
	struct wd_comp_sess *sess;
};

struct wd_comp_driver {
	const char *drv_name;
	const char *alg_name;
	__u32 drv_ctx_size;
	int (*init)(struct wd_ctx_config *config, void *priv);
	void (*exit)(void *priv);
	int (*comp_send)(handle_t ctx, struct wd_comp_msg *msg);
	int (*comp_recv)(handle_t ctx, struct wd_comp_msg *msg);
};

void wd_comp_set_driver(struct wd_comp_driver *drv);

#ifdef WD_STATIC_DRV
#define WD_COMP_SET_DRIVER(drv)						      \
extern const struct wd_comp_driver wd_comp_##drv __attribute__((alias(#drv)));\

#else
#define WD_COMP_SET_DRIVER(drv)						      \
static void __attribute__((constructor)) set_driver(void)		      \
{									      \
	wd_comp_set_driver(&drv);					      \
}
#endif

#endif /* __WD_COMP_DRV_H */
