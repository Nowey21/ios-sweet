# Port di ios-sweet su Mi 9T / Redmi K20 (davinci)

Documento di lavoro del port. Ogni passo, ogni boot e ogni decisione vengono
annotati qui (sezione "Diario"). Si distingue sempre tra **osservato** (visto sul
telefono o nei file) e **ipotesi** (da verificare).

## Obiettivo

Avviare iOS 16.7.16 (userland iPhone X, iPhone10,3 / D22AP) sul Mi 9T partendo da
[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet), il port per
Redmi Note 10 Pro (sweet), commit di partenza `45263be` (2026-10-23).
Guida generale: <https://hackiosx.dev/porting>.

Perché ios-sweet è la base giusta: sweet e davinci usano la **stessa famiglia di
SoC**, Qualcomm SM7150 ("sdmmagpie"; sweet = SM7150-AC / Snapdragon 732G,
davinci = SM7150-AA / Snapdragon 730). Il device tree lo conferma: gran parte
dei valori di sweet vale anche qui (tabella sotto).

## Il telefono (osservato, 2026-11-05)

| Voce | Valore |
| --- | --- |
| Prodotto | `davinci` (Mi 9T / Redmi K20, non Pro), `model = "DAVINCI"`, msm-id 0x16d, board-id 0x28 |
| Seriale | `REDACTED` |
| Bootloader | sbloccato, `verifiedbootstate=orange` |
| ROM installata | LineageOS userdebug (`lineage_davinci-userdebug`, BP2A.250805.005, Android 16), vendor MIUI V12.1.4.0.RFJMIXM |
| Accesso root | `adb root` dopo aver attivato "Debug come root" nelle Opzioni sviluppatore |
| Partizioni | dinamiche "retrofit" (`super-partition-name:system`), nessuno slot A/B |
| `userdata` | `/dev/block/sda32`, 0x1BC8DFB000 byte ≈ 111 GiB |
| `boot` | 128 MiB, `recovery` 64 MiB separata |

Modalità fastboot: all'inizio il telefono era in **fastbootd** (`is-userspace:yes`).
`fastboot boot` funziona solo nel fastboot di ABL: `fastboot reboot bootloader`
oppure telefono spento + Power + Volume giù.

## Backup (fatto, 2026-11-05)

In `backup/` (escluso da git, contiene IMEI e calibrazioni), letti con
`adb exec-out dd` da root. Lo SHA-256 di ogni file coincide con quello calcolato
sul telefono.

`persist` (64 MiB), `modemst1`, `modemst2`, `fsg` (8 MiB ciascuna), `fsc`
(256 KiB), `boot` (128 MiB), `dtbo` (32 MiB), `vbmeta` (128 KiB),
`recovery` (64 MiB).

**Copiali anche fuori dal PC** (chiavetta, cloud privato). Se si perdono
`persist`/`modemst*`, il telefono può perdere IMEI e calibrazioni per sempre.

In `dtb/` (escluso da git): `live.dtb` (`/sys/firmware/fdt`, 540 027 byte),
`live.txt` (stesso albero in testo, generato con `tools/fdt2txt.py`),
`cmdline.txt`, `iomem.txt`, `cpuinfo.txt`, `by-name.txt`.

## sweet contro davinci (osservato nel device tree)

| Parte | Valore sweet | Valore davinci | Lavoro |
| --- | --- | --- | --- |
| RAM | `<0x80000000 2G>, <0x1_00000000 0xfcc00000>` | identica; nessuna carve-out nel banco alto | nessuno: `--highmem` vale così |
| GICv3 | GICD 0x17a00000, GICR 0x17a60000 | stesso nodo `interrupt-controller@17a00000` | nessuno |
| Timer | 19,2 MHz, PPI 1/2/3/0 | `clock-frequency = 0x124f800` (19,2 MHz), stessi PPI | nessuno |
| CPU | 8 core, MPIDR 0x000..0x700 | uguale | nessuno |
| UFS | ufshc@1d84000 | ufshc@1d84000 | nessuno (da confermare che ABL lasci il link attivo) |
| Framebuffer | 0x9c000000, 0x1700000 byte | `cont_splash_region@9c000000`, 0x1700000 byte | solo dimensioni |
| Pannello | 1080×2400, **video** mode, DSC | Samsung "ss ea fhd" (`dsi_ss_fhd_ea_f10_cmd_display`), **1080×2340**, **command mode**, senza DSC, 60 Hz, un solo DSI | **nuovo**: serve un "kick" del display (vedi sotto) |
| Touch | Goodix GT9896, I²C 0x5d su SE7 `0xa84000`, rail PM6150L LDO11 | Goodix **GT9886** (`goodix,gt9889`), **stesso bus e indirizzo** (I²C 0x5d su `0xa84000`), rail **PM6150L LDO6**, AVDD tramite GPIO 5 del TLMM, reset GPIO 8, IRQ GPIO 9 | rail, GPIO AVDD, protocollo GT9886 da verificare |
| Range touch | | max-x 0x437 (1079), max-y 0x923 (2339), 10 dita | adattare |
| Power / Vol− | PON del PM6150 | PON del PM6150 (`pon_1` = 0x74 KEY_POWER, `pon_2` = 0x72 KEY_VOLUMEDOWN) | nessuno (da confermare) |
| Vol+ | GPIO2 del PM6150L | GPIO2 del PM6150L (`gpio_keys/vol_up`) | nessuno |
| Schermo per iOS | 1125×2436 su 1080×2400 | 1125×2436 su 1080×2340 | patch 0061 |

### Il problema nuovo: pannello in command mode

Sweet ha un pannello in video mode: il controller del display rilegge il
framebuffer da solo, quindi basta scrivere in memoria. Il Mi 9T ha un pannello in
**command mode**, come Pixel 4 e Pixel 6a: il pannello tiene una copia
dell'immagine e si aggiorna solo quando il controller (MDSS/DPU) invia un nuovo
frame. Senza un "kick" lo schermo resta fermo sul logo di avvio.

Cosa serve (ipotesi, da verificare sul telefono):
1. trovare quale CTL/INTF/pingpong del DPU (SM7150, DPU 5.x, MDSS a 0xae00000)
   ABL lascia configurato per il pannello;
2. dopo ogni scrittura del framebuffer, scrivere il registro di avvio del CTL
   (`CTL_START`) per inviare il frame, rispettando il segnale TE del pannello
   (TE sul GPIO 10 del TLMM);
3. farlo prima nello shim (test: un colore che cambia sullo schermo), poi nel
   kernel (patch 0010/0012 per la console, 0056 per la copia a 30 Hz).

Il port per Pixel 4 (Snapdragon 855, DPU della stessa generazione) l'ha già
risolto: è il riferimento da leggere.

## Piano

1. ✅ Accesso root, backup, device tree.
2. ⬜ Shim per davinci: dimensioni del framebuffer, kick del command mode.
   Prova con `fastboot boot` (non scrive niente sul telefono).
3. ⬜ ADT (`tools/mkadt.py`): 1080×2340, il resto come sweet.
4. ⬜ Patch del kernel: 0061 (dimensioni), 0060/0062/0063 (touch GT9886,
   LDO6, GPIO AVDD), console e copia del framebuffer con kick.
5. ⬜ Build di XNU su GitHub Actions (fork del repo sull'account dell'utente).
6. ⬜ Root di iOS: IPSW iPhone X 16.7.16 da Apple, `mkiosroot.sh` in WSL.
7. ⬜ Flash su `userdata` (**cancella Android**), primo boot, log da recovery.

## Diario

### 2026-11-05

- Letti ios-sweet (README, NOTES, AGENTS) e la guida di porting di hackiosx.dev.
- Repo clonato in `ios-sweet/`, ramo locale `davinci`.
- Telefono trovato in fastbootd. Letto `fastboot getvar all`.
- `fastboot reboot recovery`: adb `unauthorized`. Dopo il riavvio in Android
  il telefono si è presentato in modalità diagnostica Qualcomm `05C6:90DB`
  (Diagnostics COM7, modem, RmNet, DPL, QDSS + ADB) e `adb devices` restava
  vuoto. Risolto rimettendo la modalità USB normale.
- In Android: LineageOS userdebug, shell senza root. L'utente ha attivato
  "Debug come root" → `adb root` funziona (`uid=0`, contesto `su`).
- Backup delle 9 partizioni, verificati con SHA-256 (tutti OK).
- Prima copia di `/sys/firmware/fdt` fallita: Git Bash convertiva il percorso
  in `C:/Program Files/Git/sys/...`. Rifatta con `MSYS_NO_PATHCONV=1`.
- Aggiunto `tools/fdt2txt.py` (stampa un DTB senza `dtc`, che manca su Windows).
- Analisi del device tree: tabella sopra. Scoperto che il pannello è in
  command mode, unica differenza grossa rispetto a sweet.
