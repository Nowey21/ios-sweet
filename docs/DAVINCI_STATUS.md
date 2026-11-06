# iOS 16 on the Xiaomi Mi 9T / Redmi K20 (davinci): final state

Branch `davinci` of this fork. Work stopped on 2026-11-06; the phone went back
to Android. Everything below was seen on the phone; the details, with dates,
CI runs and the reasons behind each patch, are in `DAVINCI_DRIVERS.md` and
`DAVINCI_PORT.md` (Italian).

## What works

| | How |
| --- | --- |
| Boot to the home screen, lock screen, apps | XNU (sweet + flame series + `0200`–`0213`), booted with `fastboot boot` (nothing flashed but the iOS root in `userdata`) |
| GPU (Adreno 618) | GMU legacy firmware, HFI v1, `HFI_CTRL_INIT` (`0204`–`0206`); Turnip through metal2vulkan, CoreAnimation on Metal |
| Fluid UI | 8 cores at the OSM top frequency (`cpus=8 flamecpufreq=1`), 50–56 fps |
| Wallpaper and its editor | `lsreg -r`; metal2vulkan patch `0102` for extended-range sRGB blends |
| Wi-Fi, rejoining by itself | `wland` (from ios-flame) + `0208`: wifid's auto-join threshold comes from `RSSI_BOUNDS` |
| Safari | Metal for every process (`mtl2-clients = * -chronod`) |
| Brightness slider | `0210`: an `AppleARMBacklight` for CoreBrightness, DCS 0x51 to the Samsung panel over DSI DMA |
| Vibration | `0212`/`0213`: the PM6150 vibrator LDO behind `/dev/redwood-haptic`; HackiOS's HAL plug-in; `/product/haptics` in the ADT |
| Battery level | `sweetbattery=1` (PM6150 fuel gauge) |
| Remote reboot back to fastboot | `0211` (IMEM restart reason), most of the time |

## Started, not finished

- **Speaker**: iOS sees a speaker (HAL plug-in, `/dev/redwood-audio`). The
  audio DSP starts from user space (`phone/adsp`: PAS, SMP2P, GLINK, QRTR,
  pd-mapper with PD restart) and answers APR (AVCS: services up, AFE/ASM/ADM
  listed), but the AFE never answered a command (classic and version-1 APR
  headers both tried). Not yet tried: APR on the `apr_apps2` channel, and a
  FastRPC listener for the sensor PD (its watchdog brings the DSP down after a
  few minutes). Then: PRI MI2S clock and port, ASM/ADM stream, TFA9874.
- Kernels built with Xcode 16 (the only one GitHub's runners still have) need
  `-v` on the command line; CI accepts `runner=macos-15-intel` when the arm64
  queue is full.

## Not done

Bluetooth (bluetoothd only knows Broadcom chips), camera, flash, cellular.

## Not in the repository

Apple's software (IPSW, root image, dyld cache), the phone's firmware (GPU,
Wi-Fi, ADSP, amplifier, Bluetooth), partition backups, device dumps and logs
stay on the owner's PC. See `DAVINCI_CREDITS.md` for what comes from whom.
