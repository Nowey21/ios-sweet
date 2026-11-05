# Mi 9T patches on the plain ios-sweet series (superseded)

`0082-davinci-gt9886-touch`, `0083-davinci-touch-panel-size` and
`0084-sweet-usb-ncm` applied directly on ios-sweet 0001-0081. They gave the
first working davinci kernels (lock screen, touch, Home gesture, USB NCM;
CI runs 42025934193, 42027916731, 42031489208). Kept for reference: the
series in `xnu/patches/` is now ios-sweet 0001-0081 + ios-flame 0082-0124 +
davinci 0200-0202, where the same changes are rebuilt on top of ios-flame
(see docs/DAVINCI_DRIVERS.md).
