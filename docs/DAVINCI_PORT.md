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

### Soluzione scelta: autorefresh del pingpong

Dal driver DPU di Linux (`dpu_hw_pingpong.c`, `dpu_5_2_sm7150.h`, v6.12) e dal DT:

| Blocco | Indirizzo fisico | Registri usati |
| --- | --- | --- |
| MDP (`mdp_phys`) | 0xae00000 | |
| CTL_0..5 | 0xae02000 + n×0x200 | `CTL_TOP` 0x14, `CTL_START` 0x1c, `CTL_INTF_ACTIVE` 0xf4 |
| PINGPONG_0..3 | 0xae71000 + n×0x800 | `PP_TEAR_CHECK_EN` 0x0, `PP_AUTOREFRESH_CONFIG` 0x30 |
| SSPP (vig0, vig1, dma0, dma1, dma2) | 0xae05000, 0xae07000, 0xae25000, 0xae27000, 0xae29000 | `SRC_SIZE` 0x0, `SRC0_ADDR` 0x14, `SRC_YSTRIDE0` 0x24, `SRC_FORMAT` 0x30 |
| INTF_1 (DSI0) | 0xae6b800 | |

Il DPU ha una modalità **autorefresh**: con `PP_AUTOREFRESH_CONFIG = bit31 | N`
rilegge il framebuffer e invia un frame al pannello ogni N impulsi TE. Linux la
spegne quando prende il display dal bootloader. Noi facciamo il contrario: la
accendiamo una volta all'inizio dello shim (N = 1, cioè 60 Hz). Da quel momento
il pannello si comporta come quello in video mode di sweet, anche per XNU, che
quindi non dovrebbe aver bisogno di patch per il kick (ipotesi da verificare).

Codice: `shim/panel.c`. Lo shim trova il CTL con INTF_1 attivo, il pingpong con
il tear check acceso e la pipe SSPP che legge il buffer a 0x9c000000, poi accende
l'autorefresh. Il tutto è compilato solo con `BOARD=davinci bash shim/build.sh`
(`-DBOARD_DAVINCI`, `FB_H` = 2340, header boot.img con os_version 16.0.0 /
2025-12 come quello di LineageOS).

### Test 1: risultato (2026-11-05)

Foto: [fase 1](davinci/boot1-phase1.jpg), [fase 2](davinci/boot1-phase2.jpg).

Osservato:
- **Fase 1 (verde): la barra scorre.** Il pannello segue il framebuffer.
- Ma il testo riporta `pp=FFFFFFFF` e `AUTOREF rc=FFFFFFFF`: nessun pingpong
  ha `PP_TEAR_CHECK_EN` acceso, quindi lo shim **non** ha scritto
  `PP_AUTOREFRESH_CONFIG` né `CTL_START`. Lo schermo si aggiorna comunque: è ABL
  a lasciare la pipeline in aggiornamento continuo.
- Tutti i `PP` leggono `te=0 ar=0`. Il meccanismo che ABL usa per aggiornare non
  è quindi l'autorefresh del pingpong (ipotesi: il blocco TE/autorefresh di
  questo DPU sta altrove, oppure ABL guida l'aggiornamento in un altro modo).
  Non serve capirlo subito: basta non toccare la pipeline.
- **Fase 2 (blu): l'immagine si rovina** (righe verticali, bande nere, tearing
  diagonale) appena lo shim scrive `CTL_START` a ogni frame. Il kick manuale
  quindi va tolto.
- Dopo la fase 2 il telefono si riavvia da solo: il reset via PSCI funziona.

Registri letti (ABL, cont-splash):

| Registro | Valore | Significato |
| --- | --- | --- |
| CTL_0 `CTL_TOP` | 0x00020000 | bit 17 = command mode |
| CTL_0 `CTL_INTF_ACTIVE` | 0x2 | INTF_1 = DSI0 |
| CTL_1..5 `CTL_TOP` | 0x00001F00 | inattivi |
| SSPP 0 (VIG0) `SRC0_ADDR` | 0x9C000000 | il buffer di cont-splash |
| `SRC_SIZE` | 0x09240438 | 2340 × 1080 |
| `SRC_YSTRIDE0` | 0x10E0 | 4320 byte = 1080 × 4 |
| `SRC_FORMAT` | 0x000237FF | 4 byte per pixel (stesso a8r8g8b8 di sweet: il verde e il testo hanno i colori giusti) |

Conseguenza: per XNU il Mi 9T si comporta come sweet. Bastano indirizzo,
dimensioni e stride; **nessun kick**. Lo shim ora legge soltanto i registri e
li mostra per 8 secondi su sfondo verde.

### Test 2: memoria UFS (da fare)

Stesso shim, nuova versione: dopo la schermata verde dei registri del display
esegue la sonda UFS di sweet (`shim/ufs.c`). È **sola lettura**: nel file non
c'è nessun comando SCSI di scrittura. Mostra i registri UFSHCI lasciati da ABL
e, se il link è attivo, legge la LUN 0. Il risultato resta sullo schermo per
3 minuti, poi il telefono si riavvia. Serve a capire se ABL lascia la UFS
accesa, come su sweet (passo 5 della guida).

### Test 1: schermo (fatto)

`fastboot boot shim/out/shim-boot.img`, senza payload. Lo shim mostra:
1. **fase 1, sfondo verde, 15 s**: solo autorefresh. Se la barra bianca in basso
   scorre, l'autorefresh funziona;
2. **fase 2, sfondo blu, 15 s**: in più un `CTL_START` manuale a ogni frame;
3. poi si riavvia da solo (dopo circa 20 s).

In entrambe le fasi il testo riporta i registri letti (CTL, PP, SSPP, formato e
stride del buffer). Se lo schermo resta sul logo, nessuno dei due metodi
funziona (o gli indirizzi sono sbagliati): tenere Power + Volume giù per
tornare al fastboot.

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
- Ambiente di build: WSL aveva solo la distro di Docker. Installata Ubuntu
  (`wsl --install -d Ubuntu`, che oggi installa la 26.04.1 LTS). Al primo avvio
  errore `Wsl/Service/E_UNEXPECTED`: WSL 2.3.24 era troppo vecchio per questa
  build di Windows. Risolto con `wsl --update` (→ WSL 3.0.3, kernel 6.18.54).
- In Ubuntu, da root: `gcc-aarch64-linux-gnu` 15.2, `mkbootimg`,
  `android-sdk-libsparse-utils` (img2simg), `device-tree-compiler`, `cpio`,
  `build-essential`.
- Il primo build è fallito con `set: pipefail: invalid option name`: Git per
  Windows aveva convertito i file in CRLF. Il repo ora usa `core.autocrlf=false`
  e `core.eol=lf`, ed è stato ricaricato con LF.
- Lo shim originale di sweet compila. Header del `boot.img` di LineageOS:
  versione 2, page 4096, kernel 0x8000, ramdisk 0x1000000, tags 0x100,
  dtb 0x1f00000: gli stessi offset che usa `shim/build.sh`. Il dtb per lo shim
  viene da `dtb/boot_unpacked/dtb` (estratto dal backup di `boot`).
- Scritto `shim/panel.c` (autorefresh) e il test dello schermo. Build davinci
  pulito, senza warning.
- **Boot 1** (commit `a938508`): `adb reboot bootloader` → ABL fastboot
  (`is-userspace` non esiste, quindi non è fastbootd). `fastboot boot
  shim/out/shim-boot.img`: ABL accetta l'immagine (`Sending 524 KB OKAY`,
  `Booting OKAY`). Risultato: vedi "Test 1: risultato". La fase 1 scorre anche
  senza autorefresh; la fase 2 (kick manuale) rovina l'immagine; poi il
  telefono si riavvia da solo.
- Shim modificato: lettura dei registri del display e basta (tolti autorefresh
  e kick), poi la sonda UFS in sola lettura.
