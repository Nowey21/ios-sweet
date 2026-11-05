# Credits for the davinci port

- **ios-sweet** (dipa-studio, MIT): base of everything here, patches 0001–0081,
  shim, tools.
- **ios-flame** (NWylynko and the ios-flame contributors, MIT; patched XNU
  files stay APSL 2.0): `xnu/patches/0082`–`0124` are copied **unchanged** from
  <https://github.com/NWylynko/ios-flame> (commit `745c434`). They contain parts
  of ios-topaz (MakrSas) and HackiOS (OOSEX GANG, APSL 2.0); see ios-flame's
  CREDITS.md for the full list.
- **davinci** patches `0200`–`0202`, `tools/mkadt.py --davinci`, the shim's
  `BOARD=davinci` code and `docs/DAVINCI_*.md`: this port.
- **XNU** (Apple, APSL 2.0), xnu-8796.141.3.
