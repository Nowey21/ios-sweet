/*
 * adsp: start the audio DSP of davinci (SM7150 LPASS Hexagon) from user space and talk to it,
 * step 1 of sound. The way is wland's for the modem (phone/wifi: /dev/pmem, SMEM, SMP2P, TrustZone
 * PAS, GLINK, QRTR, pd-mapper; its files are built in with the DSP's numbers), the steps and their
 * order HackiOS's for redwood (research/hackios-kernel drivers/audio/hakios.adsp.c).
 *
 * The DSP's numbers, from davinci's device tree:
 *   reserved memory pil_adsp_region 0x95700000, 0x2600000 (38 MB)
 *   SMEM host 2; SMP2P items 429 (ours) and 443 (its), doorbell APCS IPC bit 26
 *   GLINK edge "lpass" in our SMEM partition with it (items 478-480), doorbell APCS IPC bit 24;
 *   channels IPCRTR (QRTR, the DSP is node 5) and apr_audio_svc (APR, the audio services)
 *   TrustZone image 1; the AOP hears "adsp" (load_state)
 * Firmware: adsp.mdt, adsp.b00..b26 and the adsp*.jsn from the modem partition, in
 * /var/local/fw/adsp on the phone (tools/davinci-adsp-install.sh; never in the repository).
 *
 *   adsp STEP     steps 1..STEP, then stop (the DSP is left as it is):
 *     1  SMEM: our partition with the DSP; has it run since the phone started (its item 443)?
 *     2  the firmware: whole, and does it fit the reserved memory
 *     3  SMP2P: our item 429 (it must exist before the DSP starts)
 *     4  rails (cx at its top level, as Linux's proxy vote), AOP "adsp on"
 *     5  TrustZone: metadata (image 1), memory setup, the segments copied, authenticate and reset;
 *        then "ready" in its SMP2P entry
 *     6  GLINK and QRTR (pd-mapper served): the services the DSP announces, for 20 s
 * A DSP found already running (item 443 present) is not started again: the SoC has to be reset
 * first (topaz's lesson on Windows: never boot it over the state of an earlier run).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pmem.h"
#include "smem.h"
#include "link.h"
#include "pdmapper.h"

#define ADSP_PA		0x95700000ull
#define ADSP_SIZE	0x02600000ull
#define PAS_ADSP	1
#define FW_DIR		"/var/local/fw/adsp"
#define APCS_IPC	0x17c0000cull
#define IPC_ADSP_SMP2P	26
#define IPC_AOSS	0
#define SMP2P_OUT	429
#define SMP2P_IN	443
#define QMP_RAM		0x0c300000ull
#define QMP_UP		0x0000ffffu
#define SCM_PIL(cmd)	(0x42000000ull | (2u << 8) | (cmd))

static void kick(unsigned bit)
{
	wr32(APCS_IPC, 1u << bit);
}

/* ---- cmd-db and RPMh (wland.c) ---- */

static int cmddb_aux(const char *id, uint64_t *aux, uint32_t *len)
{
	uint64_t db = rd32(0x0c3f000cull);
	if (rd32(db + 4) != 0x0c0330db)
		return -1;
	uint64_t data = db + 4 + 4 + 8 * 16 + 4 + 4;
	for (uint32_t r = 0; r < 8; r++) {
		uint64_t rh = db + 8 + 16 * r;
		uint32_t w0 = rd32(rh), w1 = rd32(rh + 4);
		uint32_t hoff = w0 >> 16, doff = w1 & 0xffff, cnt = w1 >> 16;
		for (uint32_t e = 0; e < cnt; e++) {
			uint64_t ent = data + hoff + 24 * e;
			char name[9];
			for (uint32_t i = 0; i < 8; i++)
				name[i] = (char)(rd32(ent + (i & ~3u)) >> (8 * (i & 3)));
			name[8] = 0;
			if (strncmp(name, id, 8))
				continue;
			uint32_t w5 = rd32(ent + 20);
			*len = w5 & 0xffff;
			*aux = data + doff + (w5 >> 16);
			return 0;
		}
	}
	return -1;
}

static uint32_t aux16(uint64_t aux, uint32_t i)
{
	return (rd32(aux + 2 * (i & ~1u)) >> (16 * (i & 1))) & 0xffff;
}

static int vote_top(const char *res)
{
	uint64_t aux;
	uint32_t len, top = 0;
	if (cmddb_aux(res, &aux, &len)) {
		fprintf(stderr, "%s: not in cmd-db\n", res);
		return -1;
	}
	for (uint32_t i = 0; i < len / 2; i++)
		if (aux16(aux, i))
			top = i;
	uint32_t off[1] = { 0 }, val[1] = { top };
	int rc = pm_rpmh(res, 1, off, val);
	printf("vote %s level index %u (%u) -> %d\n", res, top, aux16(aux, top), rc);
	return rc;
}

/* ---- AOSS QMP (wland.c) ---- */

static int wait_eq(uint64_t pa, uint32_t want, int ms)
{
	for (int i = 0; i < ms; i++) {
		if (rd32(pa) == want)
			return 0;
		usleep(1000);
	}
	return -1;
}

static int qmp_open(void)
{
	if (rd32(QMP_RAM) != 0x4d41494c || rd32(QMP_RAM + 4) != 1) {
		fprintf(stderr, "qmp: magic %08x version %u\n", rd32(QMP_RAM), rd32(QMP_RAM + 4));
		return -1;
	}
	if (rd32(QMP_RAM + 0x24) == QMP_UP && rd32(QMP_RAM + 0x2c) == QMP_UP && rd32(QMP_RAM + 0x30) == QMP_UP)
		return 0;	/* already open (wland opens it for the modem) */
	wr32(QMP_RAM + 0x10, rd32(QMP_RAM + 0x0c));
	wr32(QMP_RAM + 0x24, QMP_UP);
	kick(IPC_AOSS);
	if (wait_eq(QMP_RAM + 0x28, QMP_UP, 1000))
		return -1;
	wr32(QMP_RAM + 0x2c, QMP_UP);
	kick(IPC_AOSS);
	if (wait_eq(QMP_RAM + 0x14, QMP_UP, 1000))
		return -1;
	wr32(QMP_RAM + 0x18, QMP_UP);
	kick(IPC_AOSS);
	return wait_eq(QMP_RAM + 0x30, QMP_UP, 1000);
}

static int qmp_send(const char *msg)
{
	if (qmp_open())
		return -1;
	uint32_t off = rd32(QMP_RAM + 0x38), buf[16] = { 0 };
	strncpy((char *)buf, msg, sizeof(buf) - 1);
	for (int i = 0; i < 16; i++)
		wr32(QMP_RAM + off + 4 + 4 * i, buf[i]);
	wr32(QMP_RAM + off, sizeof(buf));
	(void)rd32(QMP_RAM + off);
	kick(IPC_AOSS);
	int rc = wait_eq(QMP_RAM + off, 0, 1000);
	printf("qmp: \"%s\" -> %s\n", msg, rc ? "TIMEOUT" : "acked");
	return rc;
}

/* ---- SMP2P with the DSP (wland.c's, host 2) ---- */

static void wr_name(uint64_t pa, const char *name)
{
	char n[16] = { 0 };
	strncpy(n, name, sizeof(n));
	for (int i = 0; i < 16; i += 4) {
		uint32_t w;
		memcpy(&w, n + i, 4);
		wr32(pa + i, w);
	}
}

static uint64_t smp2p_out_pa;

static int smp2p_create(void)
{
	uint64_t pa;
	if (smem_alloc(SMEM_HOST_ADSP, SMP2P_OUT, 20 + 16 * 20, &pa)) {
		fprintf(stderr, "smp2p: cannot allocate item %u\n", SMP2P_OUT);
		return -1;
	}
	if (rd32(pa) != 0x504d5324) {
		wr32(pa + 8, 0 | (2u << 16));		/* local pid 0, remote pid 2 */
		wr32(pa + 12, 16 | (1u << 16));		/* 16 entries, 1 valid */
		wr32(pa + 16, 0);
		wr_name(pa + 20, "master-kernel");
		wr32(pa + 36, 0);
		wr32(pa + 0, 0x504d5324);		/* "$SMP" */
		__asm__ volatile("dsb sy" ::: "memory");
		wr32(pa + 4, 1 | (1u << 8));		/* version 1, features: SSR ack */
	}
	smp2p_out_pa = pa;
	kick(IPC_ADSP_SMP2P);
	printf("smp2p: item %u at %llx\n", SMP2P_OUT, (unsigned long long)pa);
	return 0;
}

static int smp2p_in_value(const char *name, uint32_t *v)
{
	uint64_t pa;
	uint32_t sz;
	if (smem_get(SMEM_HOST_ADSP, SMP2P_IN, &pa, &sz))
		return -1;
	uint32_t n = rd32(pa + 12) >> 16;
	for (uint32_t i = 0; i < n && i < 16; i++) {
		uint64_t e = pa + 20 + 20ull * i;
		char nm[17];
		for (int k = 0; k < 16; k++)
			nm[k] = (char)(rd32(e + (k & ~3)) >> (8 * (k & 3)));
		nm[16] = 0;
		if (!strcmp(nm, name)) {
			*v = rd32(e + 16);
			return 0;
		}
	}
	return -1;
}

/* SSR ack, as wland: mirror the DSP's RESTART_DONE (flags bit 0) into our RESTART_ACK (bit 1) */
static void smp2p_service(void)
{
	uint64_t ip;
	uint32_t sz;
	if (!smp2p_out_pa || smem_get(SMEM_HOST_ADSP, SMP2P_IN, &ip, &sz))
		return;
	uint32_t want = (rd32(ip + 16) & 1) ? 2 : 0;
	if ((rd32(smp2p_out_pa + 16) & 2) != want) {
		wr32(smp2p_out_pa + 16, (rd32(smp2p_out_pa + 16) & ~2u) | want);
		kick(IPC_ADSP_SMP2P);
	}
}

/* ---- firmware and TrustZone PAS (wland.c's boot_modem with the DSP's numbers) ---- */

static uint8_t *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*len = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = malloc(*len + 1);
	if (b && fread(b, 1, *len, f) != *len) {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

static int64_t scm(uint32_t cmd, uint64_t arginfo, uint64_t a, uint64_t b, uint64_t c, uint64_t *r1)
{
	uint64_t x[8] = { SCM_PIL(cmd), arginfo, a, b, c, 0, 0, 0 };
	pm_smc(x);
	if (r1)
		*r1 = x[1];
	return (int64_t)x[0];
}

struct phdr { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; };

static uint8_t *mdt;
static size_t mdt_len;
static struct phdr *ph;
static uint32_t phnum, phoff;
static int hash_seg = -1;
/* a relocatable image (Linux qcom_mdt_loader: p_flags bit 27) is loaded at its lowest address's place
 * in the reserved memory, every segment keeping its distance from it */
static int64_t reloc;

static uint64_t seg_pa(uint32_t i)
{
	return (uint64_t)((int64_t)ph[i].paddr + reloc);
}

static int fw_check(void)
{
	mdt = slurp(FW_DIR "/adsp.mdt", &mdt_len);
	if (!mdt || mdt_len < 0x40) {
		fprintf(stderr, "fw: no %s/adsp.mdt\n", FW_DIR);
		return -1;
	}
	memcpy(&phoff, mdt + 0x1c, 4);
	phnum = 0;
	memcpy(&phnum, mdt + 0x2c, 2);
	ph = (struct phdr *)(mdt + phoff);
	uint64_t total = 0, lowest = ~0ull;
	int relocatable = 0;
	for (uint32_t i = 0; i < phnum; i++) {
		if (((ph[i].flags >> 24) & 7) == 2)
			hash_seg = (int)i;
		if (ph[i].type != 1 || !ph[i].memsz || (int)i == hash_seg)
			continue;
		if (ph[i].flags & (1u << 27))
			relocatable = 1;
		if (ph[i].paddr < lowest)
			lowest = ph[i].paddr;
	}
	reloc = relocatable ? (int64_t)ADSP_PA - (int64_t)lowest : 0;
	printf("fw: %srelocatable, linked at %llx, loaded at %llx\n", relocatable ? "" : "not ",
	    (unsigned long long)lowest, (unsigned long long)(lowest + reloc));
	for (uint32_t i = 0; i < phnum; i++) {
		if (ph[i].type != 1 || !ph[i].memsz || (int)i == hash_seg)
			continue;
		if (seg_pa(i) < ADSP_PA || seg_pa(i) + ph[i].memsz > ADSP_PA + ADSP_SIZE) {
			fprintf(stderr, "fw: segment %u at %llx+%x outside %llx+%llx\n", i, (unsigned long long)seg_pa(i),
			    ph[i].memsz, ADSP_PA, ADSP_SIZE);
			return -1;
		}
		char path[128];
		snprintf(path, sizeof(path), FW_DIR "/adsp.b%02u", i);
		FILE *f = ph[i].filesz ? fopen(path, "rb") : NULL;
		if (ph[i].filesz && !f) {
			fprintf(stderr, "fw: %s missing\n", path);
			return -1;
		}
		if (f)
			fclose(f);
		total += ph[i].memsz;
	}
	printf("fw: %u segments, hash segment %d, %llu bytes to load into %llx+%llx\n", phnum, hash_seg,
	    (unsigned long long)total, ADSP_PA, ADSP_SIZE);
	return hash_seg < 0 ? -1 : 0;
}

static int boot_adsp(void)
{
	size_t ehsz = phoff + 32ull * phnum, meta_len = ehsz + ph[hash_seg].filesz;
	uint8_t *meta = malloc(meta_len);
	memcpy(meta, mdt, ehsz);
	if (ph[hash_seg].offset + ph[hash_seg].filesz <= mdt_len) {
		memcpy(meta + ehsz, mdt + ph[hash_seg].offset, ph[hash_seg].filesz);
	} else {
		char path[128];
		size_t hl;
		snprintf(path, sizeof(path), FW_DIR "/adsp.b%02d", hash_seg);
		uint8_t *h = slurp(path, &hl);
		if (!h || hl < ph[hash_seg].filesz) {
			fprintf(stderr, "boot: hash segment %s missing\n", path);
			return -1;
		}
		memcpy(meta + ehsz, h, ph[hash_seg].filesz);
		free(h);
	}
	printf("boot: %zu bytes of metadata: allocating\n", meta_len);
	uint64_t meta_pa = pm_alloc(meta_len, 0x1000), r1 = 0;
	if (!meta_pa)
		return -1;
	printf("boot: metadata at %llx: writing\n", (unsigned long long)meta_pa);
	pm_wr(meta_pa, meta, meta_len);
	printf("boot: asking TrustZone (init image 1)\n");
	int64_t rc = scm(1, 2 | (2u << 6), PAS_ADSP, meta_pa, 0, &r1);
	printf("boot: init image 1 (%zu bytes of metadata) -> %lld/%llx\n", meta_len, (long long)rc, (unsigned long long)r1);
	if (rc || r1)
		return -1;
	rc = scm(2, 3, PAS_ADSP, ADSP_PA, ADSP_SIZE, &r1);
	printf("boot: mem setup %llx+%llx -> %lld/%llx\n", ADSP_PA, ADSP_SIZE, (long long)rc, (unsigned long long)r1);
	if (rc || r1)
		return -1;
	static uint8_t zeros[0x10000];
	uint64_t total = 0;
	for (uint32_t i = 0; i < phnum; i++) {
		if (ph[i].type != 1 || (int)i == hash_seg || !ph[i].memsz)
			continue;
		uint64_t pa = seg_pa(i);
		if (ph[i].filesz) {
			char path[128];
			size_t sl;
			snprintf(path, sizeof(path), FW_DIR "/adsp.b%02u", i);
			uint8_t *seg = slurp(path, &sl);
			if (!seg || sl < ph[i].filesz) {
				fprintf(stderr, "boot: %s missing or short\n", path);
				return -1;
			}
			pm_wr(pa, seg, (ph[i].filesz + 3) & ~3u);
			free(seg);
			total += ph[i].filesz;
		}
		for (uint64_t o = (ph[i].filesz + 3) & ~3ull; o < ph[i].memsz; o += sizeof(zeros)) {
			uint64_t n = ph[i].memsz - o < sizeof(zeros) ? ph[i].memsz - o : sizeof(zeros);
			pm_wr(pa + o, zeros, (n + 3) & ~3ull);
		}
	}
	printf("boot: %llu bytes of segments loaded\n", (unsigned long long)total);
	rc = scm(5, 1, PAS_ADSP, 0, 0, &r1);
	printf("boot: auth and reset -> %lld/%llx\n", (long long)rc, (unsigned long long)r1);
	if (rc || r1)
		return -1;
	for (int t = 0; t < 150; t++) {
		uint32_t v;
		smp2p_service();
		if (smp2p_in_value("slave-kernel", &v) == 0 && (v & 2) && !(v & 1)) {
			printf("boot: the DSP is ready after %d00 ms\n", t);
			return 0;
		}
		usleep(100000);
	}
	printf("boot: no ready from the DSP in 15 s\n");
	return -1;
}

/* ---- QRTR with the DSP ---- */

static void net_cb(const struct qrtr_hdr *h, const uint8_t *d, uint32_t n)
{
	uint32_t w[5] = { 0 };
	memcpy(w, d, n < sizeof(w) ? n : sizeof(w));
	if (h->type == QRTR_TYPE_DATA) {
		if (h->confirm_rx)
			qrtr_resume_tx(h);
		if (h->dst_port == PDMAPPER_PORT) {
			pdmapper_handle(h, d, n);
			return;
		}
		printf("qrtr: < DATA %u:%x -> %u:%x, %u bytes\n", h->src_node, h->src_port, h->dst_node, h->dst_port, n);
		return;
	}
	if (w[0] == QRTR_TYPE_HELLO) {
		uint32_t hello[5] = { QRTR_TYPE_HELLO };
		qrtr_send(QRTR_TYPE_HELLO, QRTR_PORT_CTRL, h->src_node, QRTR_PORT_CTRL, hello, sizeof(hello));
		printf("qrtr: hello from node %u\n", h->src_node);
	} else if (w[0] == QRTR_TYPE_NEW_SERVER) {
		printf("qrtr: the DSP serves service %#x version %u instance %#x at %u:%x\n", w[1], w[2] & 0xff, w[2] >> 8,
		    w[3], w[4]);
	} else {
		printf("qrtr: ctrl %u from %u:%x\n", w[0], h->src_node, h->src_port);
	}
}

/* ---- APR on apr_audio_svc (Linux drivers/soc/qcom/apr.c, include/linux/soc/qcom/apr.h) ---- */

void link_aux(const char *name, void (*cb)(const uint8_t *, uint32_t));	/* link2.c */
int link_aux_ready(void);
int link_aux_send(const void *data, uint32_t len);

struct apr_hdr {
	uint16_t hdr_field, pkt_size;
	uint8_t src_svc, src_domain;
	uint16_t src_port;
	uint8_t dest_svc, dest_domain;
	uint16_t dest_port;
	uint32_t token, opcode;
};
#define APR_HDR_FIELD_CMD	((1u << 8) | (5u << 4))	/* SEQ_CMD, 20-byte header, version 0 */
#define APR_DOMAIN_ADSP		4
#define APR_DOMAIN_APPS		5
#define APR_SVC_CORE		3	/* AVCS */
#define APR_BASIC_RSP		0x000110e8
#define AVCS_GET_STATE		0x0001290c	/* AVCS_CMD_ADSP_EVENT_GET_STATE */
#define AVCS_GET_STATE_RSP	0x0001290d
#define AVCS_GET_VERSIONS	0x00012905
#define AVCS_GET_VERSIONS_RSP	0x00012906

static uint32_t apr_got_opcode, apr_got_token;
static uint32_t apr_got[64];
static uint32_t apr_got_len;

static void apr_cb(const uint8_t *d, uint32_t n)
{
	struct apr_hdr h;
	if (n < sizeof(h))
		return;
	memcpy(&h, d, sizeof(h));
	uint32_t plen = n - sizeof(h);
	printf("apr: < svc %u/%u port %u -> %u/%u: opcode %#x token %u, %u bytes:", h.src_svc, h.src_domain, h.src_port,
	    h.dest_svc, h.dest_domain, h.opcode, h.token, plen);
	for (uint32_t i = 0; i < plen / 4 && i < 16; i++) {
		uint32_t w;
		memcpy(&w, d + sizeof(h) + 4 * i, 4);
		printf(" %08x", w);
	}
	printf("\n");
	apr_got_opcode = h.opcode;
	apr_got_token = h.token;
	apr_got_len = plen < sizeof(apr_got) ? plen : sizeof(apr_got);
	memcpy(apr_got, d + sizeof(h), apr_got_len);
}

static int apr_cmd(uint8_t svc, uint16_t port, uint32_t opcode, const void *payload, uint32_t len, uint32_t want,
    unsigned ms)
{
	static uint32_t token = 1;
	uint8_t pkt[512];
	struct apr_hdr h = {
		.hdr_field = APR_HDR_FIELD_CMD, .pkt_size = (uint16_t)(sizeof(h) + len), .src_svc = svc,
		.src_domain = APR_DOMAIN_APPS, .src_port = 0, .dest_svc = svc, .dest_domain = APR_DOMAIN_ADSP,
		.dest_port = port, .token = ++token, .opcode = opcode,
	};
	memcpy(pkt, &h, sizeof(h));
	if (len)
		memcpy(pkt + sizeof(h), payload, len);
	apr_got_opcode = 0;
	if (link_aux_send(pkt, sizeof(h) + len))
		return -1;
	uint64_t end = link_ms() + ms;
	while (link_ms() < end) {
		link_poll(5);
		if (apr_got_opcode == want || (apr_got_opcode == APR_BASIC_RSP && apr_got_token == h.token))
			return 0;
	}
	printf("apr: no answer to %#x in %u ms\n", opcode, ms);
	return -1;
}

int main(int argc, char **argv)
{
	int steps = argc > 1 ? atoi(argv[1]) : 1;
	setvbuf(stdout, NULL, _IOLBF, 0);
	pm_init();
	if (smem_init())
		return 1;
	/* 1 */
	uint64_t pa;
	uint32_t sz;
	int ran = smem_get(SMEM_HOST_ADSP, SMP2P_IN, &pa, &sz) == 0;
	printf("step 1: SMEM ok; the DSP's SMP2P item %u is %s\n", SMP2P_IN, ran ? "there: it has run since the start" : "absent");
	if (ran) {
		uint32_t v = 0;
		smp2p_in_value("slave-kernel", &v);
		printf("step 1: its state %x%s%s\n", v, v & 2 ? " ready" : "", v & 1 ? " ERR-FATAL" : "");
		/* its last words (Linux qcom_q6v5: the SFR, SMEM item 423 in the global partition) */
		uint64_t sfr;
		uint32_t sfr_len;
		if (smem_get(SMEM_HOST_GLOBAL, 423, &sfr, &sfr_len) == 0 && sfr_len > 0) {
			char text[257] = { 0 };
			pm_rd(sfr, text, sfr_len < 256 ? (sfr_len + 3) & ~3u : 256);
			text[sfr_len < 256 ? sfr_len : 256] = 0;
			printf("step 1: its last words (item 423, %u bytes): \"%s\"\n", sfr_len, text);
		}
	}
	if (steps < 2)
		return 0;
	/* 2 */
	if (fw_check())
		return 1;
	if (steps < 3)
		return 0;
	if (ran) {
		uint32_t v = 0;
		/* a DSP that is up and well (started by an earlier run of ours): talk to it, from step 6 */
		if (steps >= 6 && smp2p_in_value("slave-kernel", &v) == 0 && (v & 2) && !(v & 1)) {
			uint32_t sz2;
			smem_get(SMEM_HOST_ADSP, SMP2P_OUT, &smp2p_out_pa, &sz2);
			printf("the DSP is up already: attaching (step 6)\n");
			goto link;
		}
		fprintf(stderr, "the DSP ran in this power cycle: not starting it over its old state\n");
		return 1;
	}
	/* 3 */
	if (smp2p_create())
		return 1;
	if (steps < 4)
		return 0;
	/* 4 */
	if (vote_top("cx.lvl"))
		return 1;
	qmp_send("{class: image, res: load_state, name: adsp, val: on}");
	if (steps < 5)
		return 0;
	/* 5 */
	if (boot_adsp())
		return 1;
	if (steps < 6)
		return 0;
	/* 6 */
link:
	link_aux("apr_audio_svc", apr_cb);
	if (link_up(net_cb, 10000)) {
		fprintf(stderr, "glink/qrtr: not up\n");
		return 1;
	}
	pdmapper_init();
	if (steps >= 7) {
		/* 7: APR to the DSP's core service (AVCS): are the audio services up, and which are there */
		uint64_t wait = link_ms() + 10000;
		while (!link_aux_ready() && link_ms() < wait)
			link_poll(10);
		printf("step 7: apr_audio_svc %s\n", link_aux_ready() ? "open both ways" : "NOT open");
		if (link_aux_ready()) {
			apr_cmd(APR_SVC_CORE, 0, AVCS_GET_STATE, NULL, 0, AVCS_GET_STATE_RSP, 2000);
			apr_cmd(APR_SVC_CORE, 0, AVCS_GET_VERSIONS, NULL, 0, AVCS_GET_VERSIONS_RSP, 2000);
		}
	}
	uint64_t end = link_ms() + 120000;	/* the DSP's user PDs ask while they start: keep answering */
	while (link_ms() < end) {
		smp2p_service();
		link_poll(200);
	}
	printf("step 6: done; the DSP stays up\n");
	return 0;
}
