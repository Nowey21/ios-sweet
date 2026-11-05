# wland for davinci

The userland Wi-Fi driver of [ios-flame](https://github.com/NWylynko/ios-flame) (`phone/wifi`, MIT;
`radio.c` from ios-topaz), copied at commit `745c434` with the Mi 9T values:

| | flame (SM8150) | davinci (SM7150) |
| --- | --- | --- |
| `MPSS_PA` / `MPSS_SIZE` (`wland.c`, modem_region) | 0x8dc00000 / 0xb400000 | 0x8b000000 / 0x8400000 |
| `MSA_PA` (`wlfw.c`, WLAN MSA) | 0x8bc00000 | 0x97d00000 (`wlan_msa_region`) |
| board data (`wlfw.c`) | `bdwlan-flame*.bin` | `bdwlan.bin`, `bdwlan.b0c` from the modem partition (names to confirm) |

Unchanged because the SoC family is the same (checked in davinci's device tree): SMEM 0x86000000,
WCSS 0x18800000, apps SMMU 0x15000000, APCS IPC 0x17c0000c, AOP QMP 0x0c300000, cmd-db doorbell
0x0c3f000c. `radio.c` uses 16 MiB at 0xE0000000, below 4 GiB, which XNU does not use on davinci either.

Build on macOS: `phone/wifi/build.sh` (or the `userland` workflow). See docs/DAVINCI_DRIVERS.md.
