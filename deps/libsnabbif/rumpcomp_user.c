/*-
 * Copyright (c) 2014 Antti Kantee <pooka@fixup.fi>
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Provide a mechanism for LuaJIT to pull/push packets off of a
 * rump kernel virtual networking interface.  This mechanism allows
 * Snabb Switch to attach to the networking stack provided by
 * a rump kernel.
 *
 * In many ways the implementation is version 1.0: works, but could
 * use performance and robustness improvements.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rump/rumpuser_component.h>

#include "if_virt.h"
#include "rumpcomp_user.h"
#include "snabbif.h"

struct pktstore {
	struct iovec pkt_iov;
	TAILQ_ENTRY(pktstore) pkt_entries;
};

struct virtif_user {
	pthread_t viu_rcvctx;
	TAILQ_HEAD(, pktstore) viu_pkt_in;
	TAILQ_HEAD(, pktstore) viu_pkt_out;
	pthread_mutex_t viu_pktmtx;
	pthread_cond_t viu_pktcv;

	char *viu_devstr;
	struct virtif_sc *viu_virtifsc;

	LIST_ENTRY(virtif_user) viu_entries;
};

static pthread_mutex_t viulist_mtx = PTHREAD_MUTEX_INITIALIZER;
static LIST_HEAD(, virtif_user) viulist = LIST_HEAD_INITIALIZER(viulist);

static struct virtif_user *
viu_lookup(const char *devstr)
{
	struct virtif_user *viu;

	pthread_mutex_lock(&viulist_mtx);
	LIST_FOREACH(viu, &viulist, viu_entries) {
		if (strcmp(devstr, viu->viu_devstr) == 0)
			break;
	}
	pthread_mutex_unlock(&viulist_mtx);

	return viu;
}

static void *rcvcontext(void *);

int
VIFHYPER_CREATE(const char *devstr, struct virtif_sc *vif_sc, uint8_t *enaddr,
	struct virtif_user **viup)
{
	struct virtif_user *viu = NULL;
	char devwithbase[32];
	void *cookie;
	int rv;

	cookie = rumpuser_component_unschedule();

	if (snprintf(devwithbase, sizeof(devwithbase),
	    "%s%s", VIF_NAME, devstr) >= sizeof(devwithbase)) {
		rv = ENAMETOOLONG;
		goto out;
	}

	viu = malloc(sizeof(*viu));
	if (viu == NULL) {
		rv = errno;
		goto out;
	}
	pthread_mutex_init(&viu->viu_pktmtx, NULL);
	pthread_cond_init(&viu->viu_pktcv, NULL);
	TAILQ_INIT(&viu->viu_pkt_in);
	TAILQ_INIT(&viu->viu_pkt_out);

	if ((rv = pthread_create(&viu->viu_rcvctx,
	    NULL, rcvcontext, viu)) != 0)
		goto out;

	viu->viu_devstr = strdup(devwithbase);
	viu->viu_virtifsc = vif_sc;
	rv = 0;

	pthread_mutex_lock(&viulist_mtx);
	LIST_INSERT_HEAD(&viulist, viu, viu_entries);
	pthread_mutex_unlock(&viulist_mtx);

 out:
	if (rv) {
		if (viu) {
			pthread_mutex_destroy(&viu->viu_pktmtx);
			pthread_cond_destroy(&viu->viu_pktcv);
			free(viu);
		}
	}
	*viup = viu;
	rumpuser_component_schedule(cookie);
	return rumpuser_component_errtrans(rv);
}

/*
 * queue packets for Lua to be able to pull them.
 * yes, this too needs sanity for high performance.
 */
void
VIFHYPER_SEND(struct virtif_user *viu, struct iovec *iov, size_t iovlen)
{
	struct pktstore *pkt;
	uint8_t *pktdata;
	size_t pktlen, pktoff, i;

	/*
	 * calculate total len of iovec and allocate mem for whole packet
	 * (in the current scheme of things there is no refcounting on iov
	 * ... yesyesys)
	 */
	for (i = 0, pktlen = 0; i < iovlen; i++) {
		pktlen += iov[i].iov_len;
	}
	pkt = malloc(sizeof(*pkt));
	if (!pkt)
		return; /* drp */
	pktdata = malloc(pktlen);
	if (!pktdata) {
		free(pkt);
		return;
	}

	for (i = 0, pktoff = 0; i < iovlen; i++) {
		memcpy(pktdata + pktoff, iov[i].iov_base, iov[i].iov_len);
		pktoff += iov[i].iov_len;
	}

	pkt->pkt_iov.iov_base = pktdata;
	pkt->pkt_iov.iov_len = pktlen;
	pthread_mutex_lock(&viu->viu_pktmtx);
	TAILQ_INSERT_TAIL(&viu->viu_pkt_out, pkt, pkt_entries);
	pthread_mutex_unlock(&viu->viu_pktmtx);
}

int
snabbif_pull(const char *devstr, void **pktdata, size_t *pktlen)
{
	struct virtif_user *viu;
	struct pktstore *pkt;

	viu = viu_lookup(devstr);
	if (!viu) {
		fprintf(stderr, "devstr %s not found\n", devstr);
		abort();
	}

	pthread_mutex_lock(&viu->viu_pktmtx);
	if ((pkt = TAILQ_FIRST(&viu->viu_pkt_out)) != NULL)
		TAILQ_REMOVE(&viu->viu_pkt_out, pkt, pkt_entries);
	pthread_mutex_unlock(&viu->viu_pktmtx);

	if (!pkt)
		return 0;

	*pktdata = pkt->pkt_iov.iov_base;
	*pktlen = pkt->pkt_iov.iov_len;

	free(pkt);
	return 1;
}

void
VIFHYPER_DYING(struct virtif_user *viu)
{

	/* just kill the rump kernel */
	abort();
}

void
VIFHYPER_DESTROY(struct virtif_user *viu)
{

	/* ditto */
	abort();
}

/*
 * Push the packets into the rump kernel on a well-known thread context.
 * Perhaps later we'll make it possible to use the Lua calling context
 * directly, but that has too many open questions for now.
 */
void
snabbif_push(const char *devstr, void *pktdata, size_t pktlen)
{
	struct virtif_user *viu = viu_lookup(devstr);
	struct pktstore *pkt;

	if (!viu) {
		fprintf(stderr, "snabbif_push: device %s not found\n", devstr);
		abort();
	}

	if ((pkt = malloc(sizeof(*pkt))) == NULL)
		return; /* drop */
	if ((pkt->pkt_iov.iov_base = malloc(pktlen)) == NULL) {
		free(pkt);
		return; /* dropdrop */
	}

	memcpy(pkt->pkt_iov.iov_base, pktdata, pktlen);
	pkt->pkt_iov.iov_len = pktlen;

	/* off to the rcv thread with a proper rump kernel context */
	pthread_mutex_lock(&viu->viu_pktmtx);
	TAILQ_INSERT_TAIL(&viu->viu_pkt_in, pkt, pkt_entries);
	pthread_cond_signal(&viu->viu_pktcv);
	pthread_mutex_unlock(&viu->viu_pktmtx);
} 

static void *
rcvcontext(void *arg)
{
	struct virtif_user *viu = arg;
	struct pktstore *pkt;

	rumpuser_component_kthread();
	for (;;) {
		/*
		 * yea, one-by-one, but not the most glaringly off
		 * thing in the current arrangement
		 */
		pthread_mutex_lock(&viu->viu_pktmtx);
		while (TAILQ_EMPTY(&viu->viu_pkt_in))
			pthread_cond_wait(&viu->viu_pktcv, &viu->viu_pktmtx);
		pkt = TAILQ_FIRST(&viu->viu_pkt_in);
		TAILQ_REMOVE(&viu->viu_pkt_in, pkt, pkt_entries);
		pthread_mutex_unlock(&viu->viu_pktmtx);

		rumpuser_component_schedule(NULL);
		VIF_DELIVERPKT(viu->viu_virtifsc, &pkt->pkt_iov, 1);
		rumpuser_component_unschedule();

		free(pkt->pkt_iov.iov_base);
		free(pkt);
	}
	assert(0); /* EUNREACHABLE */

	return NULL;
}
