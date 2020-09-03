// SPDX-License-Identifier: GPL-2.0+
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "test_hisi_sec.h"
#include "wd_cipher.h"
#include "wd_digest.h"
#include "wd_alg_common.h"

#define HW_CTX_SIZE (24 * 1024)
#define BUFF_SIZE 1024
#define IV_SIZE   256
#define	NUM_THREADS	128

#define SCHED_SINGLE "sched_single"
#define SCHED_NULL_CTX_SIZE	4
#define TEST_WORD_LEN	4096

static struct wd_ctx_config g_ctx_cfg;
static struct wd_sched g_sched;
static struct wd_sched dg_sched;

//static struct wd_cipher_req g_async_req;
static long long int g_times;
static unsigned int g_thread_num;
static unsigned int alg_num;

typedef struct _thread_data_t {
	int     tid;
	int     flag;
	int	mode;
	struct wd_cipher_req	*req;
	int cpu_id;
	struct timeval start_tval;
	unsigned long long send_task_num;
	unsigned long long recv_task_num;
} thread_data_t;

//static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t system_test_thrds[NUM_THREADS];
static thread_data_t thr_data[NUM_THREADS];

static void hexdump(char *buff, unsigned int len)
{
	unsigned int i;
	if (!buff) {
		printf("input buff is NULL!");
		return;
	}

	for (i = 0; i < len; i++) {
		printf("\\0x%02x", buff[i]);
		if ((i + 1) % 8 == 0)
			printf("\n");
	}
	printf("\n");
}

static __u32 sched_single_pick_next_ctx(handle_t sched_ctx,
													  const void *req,
													  const struct sched_key *key)
{
	return 0;
}

static int sched_single_poll_policy(handle_t h_sched_ctx, const struct wd_ctx_config *cfg, __u32 expect, __u32 *count)
{
	return 0;
}

static int init_sigle_ctx_config(int type, int mode, struct wd_sched *sched)
{
	struct uacce_dev_list *list;
	int ret;

	list = wd_get_accel_list("cipher");
	if (!list)
		return -ENODEV;

	memset(&g_ctx_cfg, 0, sizeof(struct wd_ctx_config));
	g_ctx_cfg.ctx_num = 1;
	g_ctx_cfg.ctxs = calloc(1, sizeof(struct wd_ctx));
	if (!g_ctx_cfg.ctxs)
		return -ENOMEM;

	/* Just use first found dev to test here */
	g_ctx_cfg.ctxs[0].ctx = wd_request_ctx(list->dev);
	if (!g_ctx_cfg.ctxs[0].ctx) {
		ret = -EINVAL;
		printf("Fail to request ctx!\n");
		goto out;
	}
	g_ctx_cfg.ctxs[0].op_type = type;
	g_ctx_cfg.ctxs[0].ctx_mode = mode;

	sched->name = SCHED_SINGLE;
	sched->pick_next_ctx = sched_single_pick_next_ctx;

	sched->poll_policy = sched_single_poll_policy;
	/*cipher init*/
	ret = wd_cipher_init(&g_ctx_cfg, sched);
	if (ret) {
		printf("Fail to cipher ctx!\n");
		goto out;
	}

	wd_free_list_accels(list);

	return 0;
out:
	free(g_ctx_cfg.ctxs);

	return ret;
}

static void uninit_config(void)
{
	int i;

	wd_cipher_uninit();
	for (i = 0; i < g_ctx_cfg.ctx_num; i++)
		wd_release_ctx(g_ctx_cfg.ctxs[i].ctx);
	free(g_ctx_cfg.ctxs);
}

static int test_sec_cipher_sync_once(void)
{
	struct cipher_testvec *tv = &aes_cbc_tv_template_128[0];
	handle_t	h_sess = 0;
	struct wd_cipher_sess_setup	setup;
	struct wd_cipher_req req;
	int cnt = g_times;
	int ret;

	/* config setup */
	ret = init_sigle_ctx_config(CTX_TYPE_ENCRYPT, CTX_MODE_SYNC, &g_sched);
	if (ret) {
		printf("Fail to init sigle ctx config!\n");
		return ret;
	}
	/* config arg */
	memset(&req, 0, sizeof(struct wd_cipher_req));
	req.alg = WD_CIPHER_AES;
	req.mode = WD_CIPHER_CBC;
	req.op_type = WD_CIPHER_ENCRYPTION;

	req.src  = malloc(BUFF_SIZE);
	if (!req.src) {
		printf("req src mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	memcpy(req.src, tv->ptext, tv->len);
	req.in_bytes = tv->len;

	printf("req src--------->:\n");
	hexdump(req.src, tv->len);
	req.dst = malloc(BUFF_SIZE);
	if (!req.dst) {
		printf("req dst mem malloc failed!\n");
		ret = -1;
		goto out;
	}

	req.key = malloc(BUFF_SIZE);
	if (!req.key) {
		printf("req key mem malloc failed!\n");
		ret = -1;
		goto out;
	}

	req.iv = malloc(IV_SIZE);
	if (!req.iv) {
		printf("req iv mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	if (tv->iv)
		memcpy(req.iv, tv->iv, strlen(tv->iv));
	req.iv_bytes = strlen(tv->iv);

	printf("cipher req iv--------->:\n");
	hexdump(req.iv, req.iv_bytes);

	h_sess = wd_cipher_alloc_sess(&setup);
	if (!h_sess) {
		ret = -1;
		goto out;
	}

	/* set key */
	ret = wd_cipher_set_key(&req, (const __u8*)tv->key, tv->klen);
	if (ret) {
		printf("req set key failed!\n");
		goto out;
	}
	printf("cipher req key--------->:\n");
	hexdump(req.key, tv->klen);

	while (cnt) {
		ret = wd_do_cipher_sync(h_sess, &req);
		cnt--;
	}

	printf("Test cipher sync function: output dst-->\n");
	hexdump(req.dst, req.in_bytes);

out:
	if (req.src)
		free(req.src);
	if (req.dst)
		free(req.dst);
	if (req.iv)
		free(req.iv);
	if (req.key)
		free(req.key);
	if (h_sess)
		wd_cipher_free_sess(h_sess);
	uninit_config();

	return ret;
}

static void *async_cb(void *data)
{
	// struct wd_cipher_req *req = (struct wd_cipher_req *)data;
	// memcpy(&g_async_req, req, sizeof(struct wd_cipher_req));

	return NULL;
}

static int test_sec_cipher_async_once(void)
{
	struct cipher_testvec *tv = &aes_cbc_tv_template_128[0];
	struct wd_cipher_sess_setup	setup;
	handle_t	h_sess = 0;
	struct wd_cipher_req req;
	int cnt = g_times;
	unsigned int recv = 0;
	int ret;

	/* config setup */
	ret = init_sigle_ctx_config(CTX_TYPE_ENCRYPT, CTX_MODE_ASYNC, &g_sched);
	if (ret) {
		printf("Fail to init sigle ctx config!\n");
		return ret;
	}
	/* config arg */
	memset(&req, 0, sizeof(struct wd_cipher_req));
	req.alg = WD_CIPHER_AES;
	req.mode = WD_CIPHER_CBC;
	req.op_type = WD_CIPHER_ENCRYPTION;

	req.src  = malloc(BUFF_SIZE);
	if (!req.src) {
		printf("req src mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	memcpy(req.src, tv->ptext, tv->len);
	req.in_bytes = tv->len;

	printf("req src--------->:\n");
	hexdump(req.src, tv->len);
	req.dst = malloc(BUFF_SIZE);
	if (!req.dst) {
		printf("req dst mem malloc failed!\n");
		ret = -1;
		goto out;
	}

	req.key = malloc(BUFF_SIZE);
	if (!req.key) {
		printf("req key mem malloc failed!\n");
		ret = -1;
		goto out;
	}

	req.iv = malloc(IV_SIZE);
	if (!req.iv) {
		printf("req iv mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	if (tv->iv) {
		memcpy(req.iv, tv->iv, strlen(tv->iv));
		req.iv_bytes = strlen(tv->iv);
	}
	h_sess = wd_cipher_alloc_sess(&setup);
	if (!h_sess) {
		ret = -1;
		goto out;
	}

	/* set key */
	ret = wd_cipher_set_key(&req, (const __u8*)tv->key, tv->klen);
	if (ret) {
		printf("req set key failed!\n");
		goto out;
	}
	printf("cipher req key--------->:\n");
	hexdump(req.key, tv->klen);

	while (cnt) {
		req.cb = async_cb;
		ret = wd_do_cipher_async(h_sess, &req);
		if (ret < 0)
			goto out;

		cnt--;
	}

	/* poll thread */
	ret = wd_cipher_poll_ctx(g_ctx_cfg.ctxs[0].ctx, g_times);

	printf("Test cipher async : %u pkg ; output dst-->\n", recv);
	hexdump(req.dst, req.in_bytes);

out:
	if (req.src)
		free(req.src);
	if (req.dst)
		free(req.dst);
	if (req.iv)
		free(req.iv);
	if (req.key)
		free(req.key);
	if (h_sess)
		wd_cipher_free_sess(h_sess);
	uninit_config();

	return ret;
}

static int test_sec_cipher_sync(void *arg)
{
	int thread_id = (int)syscall(__NR_gettid);
	struct cipher_testvec *tv = &aes_cbc_tv_template_128[0];
	thread_data_t *pdata = (thread_data_t *)arg;
	struct wd_cipher_req *req = pdata->req;
	struct wd_cipher_sess_setup setup;
	struct timeval cur_tval;
	unsigned long Perf = 0, pktlen;
	handle_t	h_sess;
	float speed, time_used;
	int pid = getpid();
	int cnt = g_times;
	int ret;

	h_sess = wd_cipher_alloc_sess(&setup);
	if (!h_sess) {
		ret = -1;
		return ret;
	}

	pktlen = req->in_bytes;
	printf("cipher req src--------->:\n");
	hexdump(req->src, tv->len);

	printf("ivlen = %d, cipher req iv--------->:\n", req->iv_bytes);
	hexdump(req->iv, req->iv_bytes);
	/* set key */
	ret = wd_cipher_set_key(req, (const __u8*)tv->key, tv->klen);
	if (ret) {
		printf("req set key failed!\n");
		goto out_cipher;
	}
	printf("cipher req key--------->:\n");
	hexdump(req->key, tv->klen);

	pthread_mutex_lock(&mutex);
	// pthread_cond_wait(&cond, &mutex);
	/* run task */
	while (cnt) {
		ret = wd_do_cipher_sync(h_sess, req);
		cnt--;
		pdata->send_task_num++;
	}

	gettimeofday(&cur_tval, NULL);
	time_used = (float)((cur_tval.tv_sec - pdata->start_tval.tv_sec) * 1000000 +
				cur_tval.tv_usec - pdata->start_tval.tv_usec);
	printf("time_used:%0.0f us, send task num:%lld\n", time_used, pdata->send_task_num++);
	speed = pdata->send_task_num / time_used * 1000000;
	Perf = speed * pktlen / 1024; //B->KB
	printf("Pro-%d, thread_id-%d, speed:%0.3f ops, Perf: %ld KB/s\n", pid,
			thread_id, speed, Perf);

#if 0
	printf("Test cipher sync function: output dst-->\n");
	hexdump(req->dst, req->in_bytes);
	printf("Test cipher sync function thread_id is:%d\n", thread_id);
#endif
	pthread_mutex_unlock(&mutex);

	ret = 0;

out_cipher:
	if (h_sess)
		wd_cipher_free_sess(h_sess);

	return ret;
}

static void *_test_sec_cipher_sync(void *data)
{
	test_sec_cipher_sync(data);

	return NULL;
}
/*
 * Create 2 threads. one threads are enc/dec, and the other
 * is polling.
 */
static int test_sync_create_threads(int thread_num, struct wd_cipher_req *reqs)
{
	pthread_attr_t attr;
	int i, ret;

	if (thread_num > NUM_THREADS - 1) {
		printf("can't creat %d threads", thread_num - 1);
		return -EINVAL;
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (i = 0; i < thread_num; i++) {
		thr_data[i].tid = i;
		thr_data[i].req = &reqs[i];
		gettimeofday(&thr_data[i].start_tval, NULL);
		ret = pthread_create(&system_test_thrds[i], &attr, _test_sec_cipher_sync, &thr_data[i]);
		if (ret) {
			printf("Failed to create thread, ret:%d\n", ret);
			return ret;
		}
	}

	thr_data[i].tid = i;
	pthread_attr_destroy(&attr);
	for (i = 0; i < thread_num; i++) {
		ret = pthread_join(system_test_thrds[i], NULL);
	}

	return 0;
}

static int sec_cipher_sync_test(void)
{
	struct wd_cipher_req	req[NUM_THREADS];
	void *src = NULL, *dst = NULL, *iv = NULL, *key = NULL;
	int	parallel = g_thread_num;
	int i;
	memset(&req, 0, sizeof(struct wd_cipher_req) * NUM_THREADS);
	struct cipher_testvec *tv = &aes_cbc_tv_template_128[0];
	int ret;

	int step = sizeof(char) * TEST_WORD_LEN;
	src = malloc(step * NUM_THREADS);
	if (!src) {
		ret = -ENOMEM;
		goto out_thr;
	}
	dst = malloc(step * NUM_THREADS);
	if (!dst) {
		ret = -ENOMEM;
		goto out_thr;
	}
	iv = malloc(step * NUM_THREADS);
	if (!iv) {
		ret = -ENOMEM;
		goto out_thr;
	}

	key = malloc(step * NUM_THREADS);
	if (!key) {
		ret = -ENOMEM;
		goto out_thr;
	}

	for (i = 0; i < parallel; i++) {
		req[i].src = src + i * step;
		memset(req[i].src, 0, step);
		memcpy(req[i].src, tv->ptext, tv->len);
		req[i].in_bytes = tv->len;

		req[i].dst = dst + i * step;
		req[i].out_bytes = tv->len;

		req[i].iv = iv + i * step;
		memset(req[i].iv, 0, step);
		memcpy(req[i].iv, tv->iv, strlen(tv->iv));
		req[i].iv_bytes = strlen(tv->iv);

		req[i].key = key + i * step;
		memset(req[i].key, 0, step);

		/* config arg */
		req[i].alg = WD_CIPHER_AES;
		req[i].mode = WD_CIPHER_CBC;
		req[i].op_type = WD_CIPHER_ENCRYPTION;
	}

	ret = init_sigle_ctx_config(CTX_TYPE_ENCRYPT, CTX_MODE_SYNC, &g_sched);
	if (ret) {
		printf("fail to init sigle ctx config!\n");
		goto out_thr;
	}

	ret = test_sync_create_threads(parallel, req);
	if (ret < 0)
		goto out_config;

out_config:
	uninit_config();
out_thr:
	if (src)
		free(src);
	if (dst)
		free(dst);
	if (iv)
		free(iv);
	if (key)
		free(key);

	return ret;
}

static int test_sec_cipher_async(void *arg)
{
	int thread_id = (int)syscall(__NR_gettid);
	struct cipher_testvec *tv = &aes_cbc_tv_template_128[0];
	thread_data_t *pdata = (thread_data_t *)arg;
	struct wd_cipher_req *req = pdata->req;
	struct wd_cipher_sess_setup setup;
	int cnt = g_times;
	handle_t h_sess;
	int ret;

	h_sess = wd_cipher_alloc_sess(&setup);
	if (!h_sess) {
		ret = -1;
		return ret;
	}

	printf("cipher req src--------->:\n");
	hexdump(req->src, tv->len);

	printf("cipher req iv--------->:\n");
	hexdump(req->iv, req->iv_bytes);
	/* set key */
	ret = wd_cipher_set_key(req, (const __u8*)tv->key, tv->klen);
	if (ret) {
		printf("req set key failed!\n");
		goto out_cipher;
	}
	printf("cipher req key--------->:\n");
	hexdump(req->key, tv->klen);

	pthread_mutex_lock(&mutex);
	// pthread_cond_wait(&cond, &mutex);
	/* run task */
	while (cnt) {
		ret = wd_do_cipher_async(h_sess, req);
		cnt--;
	}

	printf("Test cipher async function: output dst-->\n");
	hexdump(req->dst, req->in_bytes);
	printf("Test cipher async function thread_id is:%d\n", thread_id);

	pthread_mutex_unlock(&mutex);

	ret = 0;

out_cipher:
	if (h_sess)
		wd_cipher_free_sess(h_sess);

	return ret;
}

static void *_test_sec_cipher_async(void *data)
{
	test_sec_cipher_async(data);

	return NULL;
}

/* create poll threads */
static void *poll_func(void *arg)
{
	int ret;

	while (1) {
		ret = wd_cipher_poll_ctx(g_ctx_cfg.ctxs[0].ctx, 1);
		if (ret < 0) {
			break;
		}
	}

	return NULL;
}
/*
 * Create 2 threads. one threads are enc/dec, and the other
 * is polling.
 */
static int test_async_create_threads(int thread_num, struct wd_cipher_req *reqs)
{
	pthread_attr_t attr;
	int i, ret;

	if (thread_num > NUM_THREADS - 1) {
		printf("can't creat %d threads", thread_num - 1);
		return -EINVAL;
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (i = 0; i < thread_num; i++) {
		thr_data[i].tid = i;
		thr_data[i].req = &reqs[i];
		gettimeofday(&thr_data[i].start_tval, NULL);
		ret = pthread_create(&system_test_thrds[i], &attr, _test_sec_cipher_async, &thr_data[i]);
		if (ret) {
			printf("Failed to create thread, ret:%d\n", ret);
			return ret;
		}
	}

	ret = pthread_create(&system_test_thrds[i], &attr, poll_func, &thr_data[i]);

	pthread_attr_destroy(&attr);
	for (i = 0; i < thread_num + 1; i++) {
		ret = pthread_join(system_test_thrds[i], NULL);
	}

	return 0;
}

static int sec_cipher_async_test(void)
{
	struct wd_cipher_req	req[NUM_THREADS];
	void *src = NULL, *dst = NULL, *iv = NULL, *key = NULL;
	int	parallel = g_thread_num;
	int i;
	memset(&req, 0, sizeof(struct wd_cipher_req) * NUM_THREADS);
	struct cipher_testvec *tv = &aes_cbc_tv_template_128[0];
	int ret;

	int step = sizeof(char) * TEST_WORD_LEN;
	src = malloc(step * NUM_THREADS);
	if (!src) {
		ret = -ENOMEM;
		goto out_thr;
	}
	dst = malloc(step * NUM_THREADS);
	if (!dst) {
		ret = -ENOMEM;
		goto out_thr;
	}
	iv = malloc(step * NUM_THREADS);
	if (!iv) {
		ret = -ENOMEM;
		goto out_thr;
	}

	key = malloc(step * NUM_THREADS);
	if (!key) {
		ret = -ENOMEM;
		goto out_thr;
	}

	for (i = 0; i < parallel; i++) {
		req[i].src = src + i * step;
		memset(req[i].src, 0, step);
		memcpy(req[i].src, tv->ptext, tv->len);
		req[i].in_bytes = tv->len;

		req[i].dst = dst + i * step;
		req[i].out_bytes = tv->len;

		req[i].iv = iv + i * step;
		memset(req[i].iv, 0, step);
		memcpy(req[i].iv, tv->iv, strlen(tv->iv));
		req[i].iv_bytes = strlen(tv->iv);

		req[i].key = key + i * step;
		memset(req[i].key, 0, step);

		/* config arg */
		req[i].alg = WD_CIPHER_AES;
		req[i].mode = WD_CIPHER_CBC;
		req[i].op_type = WD_CIPHER_ENCRYPTION;
		req[i].cb = async_cb;
	}

	ret = init_sigle_ctx_config(CTX_TYPE_ENCRYPT, CTX_MODE_ASYNC, &g_sched);
	if (ret) {
		printf("fail to init sigle ctx config!\n");
		goto out_thr;
	}

	ret = test_async_create_threads(parallel, req);
	if (ret < 0)
		goto out_config;

out_config:
	uninit_config();
out_thr:
	if (src)
		free(src);
	if (dst)
		free(dst);
	if (iv)
		free(iv);
	if (key)
		free(key);

	return ret;
}

static __u32 sched_digest_pick_next_ctx(handle_t h_sched_ctx, const void *req, const struct sched_key *key)
{
	return 0;
}

static int init_digest_ctx_config(int type, int mode, struct wd_sched *sched)
{
	struct uacce_dev_list *list;
	int ret;

	list = wd_get_accel_list("digest");
	if (!list)
		return -ENODEV;


	memset(&g_ctx_cfg, 0, sizeof(struct wd_ctx_config));
	g_ctx_cfg.ctx_num = 1;
	g_ctx_cfg.ctxs = calloc(1, sizeof(struct wd_ctx));
	if (!g_ctx_cfg.ctxs)
		return -ENOMEM;

	/* Just use first found dev to test here */
	g_ctx_cfg.ctxs[0].ctx = wd_request_ctx(list->dev);
	if (!g_ctx_cfg.ctxs[0].ctx) {
		ret = -EINVAL;
		printf("Fail to request ctx!\n");
		goto out;
	}
	g_ctx_cfg.ctxs[0].op_type = type;
	g_ctx_cfg.ctxs[0].ctx_mode = mode;

	sched->name = SCHED_SINGLE;
	sched->pick_next_ctx = sched_digest_pick_next_ctx;

	sched->poll_policy = sched_single_poll_policy;
	/*cipher init*/
	ret = wd_digest_init(&g_ctx_cfg, sched);
	if (ret) {
		printf("Fail to cipher ctx!\n");
		goto out;
	}

	wd_free_list_accels(list);

	return 0;
out:
	free(g_ctx_cfg.ctxs);

	return ret;
}


static int test_sec_digest_sync_once(void)
{
	struct hash_testvec *tv = &sha256_tv_template[0];
	handle_t	h_sess = 0;
	struct wd_digest_sess_setup	setup;
	struct wd_digest_req req;
	int cnt = g_times;
	int ret;

	/* config setup */
	ret = init_digest_ctx_config(CTX_TYPE_ENCRYPT, CTX_MODE_SYNC, &dg_sched);
	if (ret) {
		printf("Fail to init sigle ctx config!\n");
		return ret;
	}

	/* config arg */
	memset(&req, 0, sizeof(struct wd_digest_req));
	req.alg = WD_DIGEST_SHA256;
	req.mode = WD_DIGEST_NORMAL;
	printf("test alg: %s\n", "normal(sha256)");

	req.in  = malloc(BUFF_SIZE);
	if (!req.in) {
		printf("req src in mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	memcpy(req.in, tv->plaintext, tv->psize);
	req.in_bytes = tv->psize;

	printf("req src in--------->:\n");
	hexdump(req.in, tv->psize);
	req.out = malloc(BUFF_SIZE);
	if (!req.out) {
		printf("req dst out mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	req.out_bytes = tv->dsize;

	req.key = malloc(BUFF_SIZE);
	if (!req.key) {
		printf("req key mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	req.has_next = 0;

	h_sess = wd_digest_alloc_sess(&setup);
	if (!h_sess) {
		ret = -1;
		goto out;
	}

	/* if mode is HMAC, should set key */
	//ret = wd_digest_set_key(&req, (const __u8*)tv->key, tv->ksize);
	//if (ret) {
	//	printf("req set key failed!\n");
	//	goto out;
	//}
	//printf("digest req key--------->:\n");
	//hexdump(req.key, tv->klen);

	while (cnt) {
		ret = wd_do_digest_sync(h_sess, &req);
		cnt--;
	}

	printf("Test digest sync function: output dst-->\n");
	hexdump(req.out, 64);

out:
	if (req.in)
		free(req.in);
	if (req.out)
		free(req.out);
	if (req.key)
		free(req.key);
	if (h_sess)
		wd_digest_free_sess(h_sess);
	uninit_config();

	return ret;
}


static void *digest_async_cb(void *data)
{
	// struct wd_digest_req *req = (struct wd_digest_req *)data;
	// memcpy(&g_async_req, req, sizeof(struct wd_digest_req));

	return NULL;
}

static int test_sec_digest_async_once(void)
{
	struct hash_testvec *tv = &sha256_tv_template[0];
	struct wd_digest_sess_setup	setup;
	handle_t	h_sess = 0;
	struct wd_digest_req req;
	int cnt = g_times;
	unsigned int recv = 0;
	int ret;

	/* config setup */
	ret = init_digest_ctx_config(CTX_TYPE_ENCRYPT, CTX_MODE_SYNC, &dg_sched);
	if (ret) {
		printf("Fail to init sigle ctx config!\n");
		return ret;
	}

	/* config arg */
	memset(&req, 0, sizeof(struct wd_digest_req));
	req.alg = WD_DIGEST_SHA256;
	req.mode = WD_DIGEST_NORMAL;
	printf("test alg: %s\n", "normal(sha256)");

	req.in  = malloc(BUFF_SIZE);
	if (!req.in) {
		printf("req src in mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	memcpy(req.in, tv->plaintext, tv->psize);
	req.in_bytes = tv->psize;

	printf("req src in--------->:\n");
	hexdump(req.in, tv->psize);
	req.out = malloc(BUFF_SIZE);
	if (!req.out) {
		printf("req dst out mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	req.out_bytes = tv->dsize;

	req.key = malloc(BUFF_SIZE);
	if (!req.key) {
		printf("req key mem malloc failed!\n");
		ret = -1;
		goto out;
	}
	req.has_next = 0;

	h_sess = wd_digest_alloc_sess(&setup);
	if (!h_sess) {
		ret = -1;
		goto out;
	}

	/* if mode is HMAC, should set key */
	//ret = wd_digest_set_key(&req, (const __u8*)tv->key, tv->ksize);
	//if (ret) {
	//	printf("req set key failed!\n");
	//	goto out;
	//}
	//printf("digest req key--------->:\n");
	//hexdump(req.key, tv->klen);

	while (cnt) {
		req.cb = digest_async_cb;
		ret = wd_do_digest_async(h_sess, &req);
		if (ret < 0)
			goto out;
		cnt--;
	}

	/* poll thread */
	ret = wd_digest_poll_ctx(g_ctx_cfg.ctxs[0].ctx, g_times, &recv);

	printf("Test digest async : %u pkg ; output dst-->\n", recv);
	hexdump(req.out, 64);

out:
	if (req.in)
		free(req.in);
	if (req.out)
		free(req.out);
	if (req.key)
		free(req.key);
	if (h_sess)
		wd_digest_free_sess(h_sess);
	uninit_config();

	return ret;
}

int main(int argc, char *argv[])
{
	printf("this is a hisi sec test.\n");
	unsigned int algtype_class;
	g_thread_num = 1;

	if (!strcmp(argv[1], "-cipher")) {
		algtype_class = CIPHER_CLASS;
		alg_num = strtoul((char*)argv[2], NULL, 10);
	} else if (!strcmp(argv[1], "-digest")) {
		algtype_class = DIGEST_CLASS;
		alg_num = strtoul((char*)argv[2], NULL, 10);
	} else {
		printf("alg_class type error.\n");
		return 0;
	}

	if (!strcmp(argv[3], "-times")) {
		g_times = strtoul((char*)argv[4], NULL, 10);
	} else {
		g_times = 1;
	}
	printf("set global times is %lld\n", g_times);

	if (!strcmp(argv[5], "-sync")) {
		if (algtype_class == CIPHER_CLASS) {
			if (!strcmp(argv[6], "-multi")) {
				g_thread_num = strtoul((char*)argv[7], NULL, 10);
				printf("currently cipher test is synchronize multi -%d threads!\n", g_thread_num);
				sec_cipher_sync_test();
			} else {
				test_sec_cipher_sync_once();
				printf("currently cipher test is synchronize once, one thread!\n");
			}
		} else if (algtype_class == DIGEST_CLASS) {
			if (!strcmp(argv[6], "-multi")) {
				g_thread_num = strtoul((char*)argv[7], NULL, 10);
				printf("currently digest test is synchronize multi -%d threads!\n", g_thread_num);
				//sec_digest_sync_test();
			} else {
				test_sec_digest_sync_once();
				printf("currently digest test is synchronize once, one thread!\n");
			}
		}
	} else if (!strcmp(argv[5], "-async")) {
		if (algtype_class == CIPHER_CLASS) {
			if (!strcmp(argv[6], "-multi")) {
				g_thread_num = strtoul((char*)argv[7], NULL, 10);
				printf("currently cipher test is asynchronous multi -%d threads!\n", g_thread_num);
				sec_cipher_async_test();
			} else {
				test_sec_cipher_async_once();
				printf("currently cipher test is asynchronous one, one thread!\n");
			}
		} else if (algtype_class == DIGEST_CLASS) {
			if (!strcmp(argv[6], "-multi")) {
				g_thread_num = strtoul((char*)argv[7], NULL, 10);
				printf("currently digest test is asynchronous multi -%d threads!\n", g_thread_num);
				//sec_digest_async_test();
			} else {
				test_sec_digest_async_once();
				printf("currently digest test is asynchronous one, one thread!\n");
			}
		}
	} else {
		printf("Please input a right session mode, -sync or -aync!\n");
		// ./test_hisi_sec -time 2 -sync -multi
	}

	return 0;
}
