// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 SUSE LLC <nstange@suse.de>
 * Copyright (C) 2020 SUSE LLC <mdoucha@suse.cz>
 *
 * CVE-2018-7566
 *
 * Test for race condition when initializing client pool on /dev/snd/seq
 * Kernel crash fixed in:
 *
 *  commit d15d662e89fc667b90cd294b0eb45694e33144da
 *  Author: Takashi Iwai <tiwai@suse.de>
 *  Date:   Mon Feb 12 15:20:51 2018 +0100
 *
 *  ALSA: seq: Fix racy pool initializations
 */

#include <linux/types.h>
#include <time.h>
#include <sound/asound.h>
#include <sound/asequencer.h>

#include "tst_test.h"
#include "tst_fuzzy_sync.h"
#include "tst_taint.h"

static int fd = -1;
static int client_id;
static struct tst_fzsync_pair fzsync_pair;

static void setup(void)
{
	struct snd_seq_queue_info qconf = { .queue = 0 };

	tst_taint_init(TST_TAINT_W | TST_TAINT_D);
	errno = 0;
	fd = open("/dev/snd/seq", O_RDWR);

	if (fd == -1 && (errno == ENOENT || errno == EACCES))
		tst_brk(TCONF | TERRNO, "Cannot open /dev/snd/seq");

	if (fd < 0)
		tst_brk(TBROK | TERRNO, "Cannot open /dev/snd/seq");

	SAFE_IOCTL(fd, SNDRV_SEQ_IOCTL_CLIENT_ID, &client_id);
	SAFE_IOCTL(fd, SNDRV_SEQ_IOCTL_CREATE_QUEUE, &qconf);

	fzsync_pair.exec_loops = 100000;
	tst_fzsync_pair_init(&fzsync_pair);
}

static void cleanup(void)
{
	if (fd >= 0)
		SAFE_CLOSE(fd);
	tst_fzsync_pair_cleanup(&fzsync_pair);
}

static void reinit_pool(int pool_size)
{
	struct snd_seq_client_pool pconf = {
		.output_pool = pool_size,
		.client = client_id
	};

	SAFE_IOCTL(fd, SNDRV_SEQ_IOCTL_SET_CLIENT_POOL, &pconf);
}

static void *thread_run(void *arg)
{
	while (tst_fzsync_run_b(&fzsync_pair)) {
		tst_fzsync_start_race_b(&fzsync_pair);
		reinit_pool(512);
		tst_fzsync_end_race_b(&fzsync_pair);
	}

	return arg;
}

static void run(void)
{
	tst_fzsync_pair_reset(&fzsync_pair, thread_run);

	while (tst_fzsync_run_a(&fzsync_pair)) {
		reinit_pool(1);
		tst_fzsync_start_race_a(&fzsync_pair);
		reinit_pool(2);
		tst_fzsync_end_race_a(&fzsync_pair);

		if (tst_taint_check()) {
			tst_res(TFAIL, "Kernel is vulnerable");
			return;
		}
	}

	tst_res(TPASS, "Nothing bad happened, probably");
}

static struct tst_test test = {
	.test_all = run,
	.setup = setup,
	.cleanup = cleanup,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "d15d662e89fc"},
		{"CVE", "2018-7566"},
		{}
	}
};