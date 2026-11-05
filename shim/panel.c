/*
 * davinci: the Samsung EA panel is a DSI *command-mode* panel. It keeps its
 * own copy of the image and only updates when the DPU sends a frame, so
 * writes to the splash buffer would stay invisible until something kicks the
 * pipeline. Boot 1 (docs/DAVINCI_PORT.md) showed that ABL leaves it refreshing
 * by itself, and that an extra manual CTL_START corrupts the picture. So this
 * only reads back what ABL configured, for the screen and the notes.
 *
 * Offsets: SM7150 DPU (downstream sde DT, relative to mdp_phys 0xae00000;
 * the same blocks as mainline's dpu_5_2_sm7150.h relative to 0xae01000).
 */
#include <stdint.h>
#include "panel.h"

#define MDP_BASE		0x0ae00000UL
#define CTL_OFF(n)		(0x2000 + (n) * 0x200)	/* qcom,sde-ctl-off */
#define PP_OFF(n)		(0x71000 + (n) * 0x800)	/* qcom,sde-pp-off */
static const uint32_t sspp_off[NR_SSPP] = { 0x5000, 0x7000, 0x25000, 0x27000, 0x29000 };

#define CTL_TOP			0x014
#define CTL_INTF_ACTIVE		0x0f4
#define INTF_1_BIT		(1U << 1)	/* INTF_1 = DSI0 (qcom,sde-intf-type) */

#define PP_TEAR_CHECK_EN	0x000
#define PP_AUTOREFRESH_CONFIG	0x030

#define SSPP_SRC_SIZE		0x000
#define SSPP_SRC0_ADDR		0x014
#define SSPP_SRC_YSTRIDE0	0x024
#define SSPP_SRC_FORMAT		0x030

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }

void panel_probe(struct panel_info *pi)
{
	pi->ctl = pi->pp = pi->sspp = -1;
	for (int n = 0; n < NR_CTL; n++) {
		pi->ctl_top[n] = rd32(MDP_BASE + CTL_OFF(n) + CTL_TOP);
		pi->ctl_intf[n] = rd32(MDP_BASE + CTL_OFF(n) + CTL_INTF_ACTIVE);
		if (pi->ctl < 0 && (pi->ctl_intf[n] & INTF_1_BIT))
			pi->ctl = n;
	}
	for (int n = 0; n < NR_PP; n++) {
		pi->pp_te[n] = rd32(MDP_BASE + PP_OFF(n) + PP_TEAR_CHECK_EN);
		pi->pp_ar[n] = rd32(MDP_BASE + PP_OFF(n) + PP_AUTOREFRESH_CONFIG);
		if (pi->pp < 0 && (pi->pp_te[n] & 1))
			pi->pp = n;
	}
	for (int n = 0; n < NR_SSPP; n++) {
		uintptr_t s = MDP_BASE + sspp_off[n];
		pi->sspp_addr[n] = rd32(s + SSPP_SRC0_ADDR);
		if (pi->sspp < 0 && pi->sspp_addr[n] >= 0x9c000000U && pi->sspp_addr[n] < 0x9d700000U) {
			pi->sspp = n;
			pi->src_size = rd32(s + SSPP_SRC_SIZE);
			pi->src_stride = rd32(s + SSPP_SRC_YSTRIDE0);
			pi->src_format = rd32(s + SSPP_SRC_FORMAT);
		}
	}
}
