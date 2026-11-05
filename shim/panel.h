#pragma once
#include <stdint.h>

#define NR_CTL	6
#define NR_PP	4
#define NR_SSPP	5

struct panel_info {
	int ctl, pp, sspp;		/* -1 if not found */
	uint32_t ctl_top[NR_CTL], ctl_intf[NR_CTL];
	uint32_t pp_te[NR_PP], pp_ar[NR_PP];
	uint32_t sspp_addr[NR_SSPP];
	uint32_t src_size, src_stride, src_format;	/* of the SSPP scanning the splash */
};

void panel_probe(struct panel_info *pi);
void panel_kick(const struct panel_info *pi);
int panel_autorefresh(const struct panel_info *pi);
