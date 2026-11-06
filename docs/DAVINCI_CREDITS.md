# Credits for the davinci port

- **ios-sweet** (dipa-studio, MIT): base of everything here, patches 0001–0081,
  shim, tools.
- **ios-flame** (NWylynko and the ios-flame contributors, MIT; patched XNU
  files stay APSL 2.0): `xnu/patches/0082`–`0124` are copied **unchanged** from
  <https://github.com/NWylynko/ios-flame> (commit `745c434`). They contain parts
  of ios-topaz (MakrSas) and HackiOS (OOSEX GANG, APSL 2.0); see ios-flame's
  CREDITS.md for the full list. `phone/wifi` (wland), `phone/tools` (most
  tools) and `phone/metal2` come from ios-flame too (MIT).
- **HackiOS** (OOSEX GANG): user space MIT License, Copyright (c) 2026 OOSEX
  GANG: `phone/audio` (RedwoodAudio.driver, as carried by ios-topaz) and
  `phone/tools/sound_test.c`. Its kernel (APSL 2.0,
  <https://github.com/oosexgang/hackios-xnu-kernel>) was read as a reference
  for the ADSP bring-up and the sound path; no file of it is copied here.
- **ios-topaz** (MakrSas): `RedwoodAudio.driver` as packaged there, notes on the
  VirtualAudio crash and on AudioReach vs. APR.
- **metal2vulkan** (steelbrain) and **Mesa/Turnip**: fetched and built in CI,
  not stored here.
- **NXP tfa98xx** sources (Apache 2.0): read as a reference for the TFA9874
  container format only; not copied.
- **Linux** (GPL): register layouts and protocol constants were looked up in it
  (qcom pinctrl-sm7150, q6afe, apr, qcom_mdt_loader, spmi-pmic-arb,
  qpnp-vibrator-ldo); no code is copied.
- **davinci** patches `0200`–`0213`, `tools/mkadt.py --davinci`, the shim's
  `BOARD=davinci` code, `phone/adsp`, `tools/davinci-*` and `docs/DAVINCI_*.md`:
  this port.
- **XNU** (Apple, APSL 2.0), xnu-8796.141.3.

Nothing from Apple's iOS (IPSW, root file system, dyld cache, binaries) and no
phone firmware (Qualcomm, Xiaomi, NXP) is in this repository: they stay on the
owner's PC and phone, and every tool here reads them from there.
