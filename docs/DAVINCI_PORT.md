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

### Test 2: risultato (2026-11-05)

Foto: [boot2-ufs.jpg](davinci/boot2-ufs.jpg). **La UFS funziona come su sweet.**

| Riga | Valore | Significato |
| --- | --- | --- |
| `ver` / `cap` | 0x210 / 0x1587031F | UFSHCI 2.1, 32 slot (`nutrs=0x20`) |
| `hce` / `hcs` | 1 / 0x10F | host abilitato, dispositivo presente, liste pronte: ABL lascia il link acceso |
| `utrlba` | 0x9998D000 | lista di ABL; lo shim la sposta sulla propria (`ours=0x80095000`) |
| TUR, READ CAPACITY, READ | rc=0, ocs/st=0 | comandi SCSI riusciti |
| LUN 0 | lastlba 0x1D7CFFF, blocchi da 4096 byte | 0x1D7D000 × 4 KiB = 0x1D7D000000 byte = `partition-size:sda` di fastboot |
| MBR / GPT | 0xAA55, tipo 0xEE, "EFI PART", 32 voci | GPT protettiva normale |
| `userdata` | LBA 0x1B4200, 0x1BC8D MiB (≈ 111 GiB), fs "other" | uguale a fastboot (`0x1BC8DFB000`); "other" = f2fs di Android |

Conseguenze:
- Il driver UFS di XNU (patch 0028) cerca la partizione **per nome** nella GPT
  (`ufsroot=`, predefinito `userdata`): sul Mi 9T funziona senza modifiche.
- Lo strumento del log (`tools/sweet-log-pull.sh`) legge gli ultimi blocchi di
  `userdata` tramite adb da recovery: funziona uguale, ma serve una recovery con
  adb root (dopo l'installazione di iOS, LineageOS non si avvierà più).

### Cosa manca per il primo avvio di XNU

Con schermo, RAM, GIC, timer, CPU e UFS uguali a sweet, **il kernel di sweet
così com'è dovrebbe già arrivare alla schermata di blocco** (ipotesi). Senza
touch, e con l'immagine spostata di 60 righe (sweet disegna 2400 righe, il
pannello ne mostra 2340; il buffer di 0x1700000 byte basta per 2400 righe,
quindi non si scrive fuori dalla zona riservata). I tasti dovrebbero
funzionare, perché sono sugli stessi pin.

Le patch del kernel da adattare dopo il primo avvio:

| Patch | Cosa cambiare |
| --- | --- |
| 0010, 0012 | `EARLY_HEIGHT` 2400 → 2340 (console di debug) |
| 0046 | `SWEET_FB_DOT_PITCH` 395 → 403 ppi (6,39", 1080×2340) |
| 0061 | adattamento 1125×2436 → 1080×2340 e normalizzazione del touch |
| 0058 | normalizzazione del touch su 2340 righe |
| 0060, 0062, 0063 | touch GT9886: rail PM6150L **LDO6** al posto di LDO11, AVDD con il GPIO 5 del TLMM, protocollo GT9886 da confrontare con il GT9896 |

Le patch si applicano in sequenza (la 0012 modifica righe della 0010), quindi
le modifiche per davinci andranno in una patch finale separata, generata
sull'albero xnu già patchato.

### Test 3: primo avvio di XNU, risultato (2026-11-05)

Foto (locali, fuori da git): `docs/davinci/boot3-xnu-1.jpg` … `-5.jpg`.
**Il kernel XNU di sweet, senza modifiche, gira sul Mi 9T fino al montaggio
della root.**

Osservato sullo schermo (console del kernel, testo leggibile):
- **SMP**: `SWEET CPU: 6/6 CPUs initialized, SGI1 verified`; ogni core
  `PSCI CPU_ON rc=0`, MPIDR 0x0…0x500, EL1.
- BSD e IOKit partono: copyright BSD, `MAC Framework successfully initialized`,
  mbinit, Skywalk self-test tutti `PASSED`.
- `SWEET FB: AppleCLCD 1080x2340 stride 4320 at 0x9c000000`, presentato come
  1125x2436: le dimensioni arrivano dallo shim (boot-args video), quindi il
  framebuffer di XNU ha già l'altezza giusta.
- `SWEET HID: buttons registered`, `touch registered`;
  `SWEET BATTERY: static fallback, 100% 4200mV`.
- `SWEET UFS: "userdata" on LUN 0: first block 0x1b4200, 113805 MiB,
  read-only`: partizione trovata per nome, **sola lettura** come voluto.
- `SWEET KEYS: arb=0x50000000 apids=512 pon-apid=199 gpio4-apid=240 …`: il
  bus SPMI e il PON rispondono (tasti da provare con la root di iOS).
- `SWEET USB`: DWC3 e PHY QUSB2 leggibili, clock accesi.
- Root: `hfs_ValidateHFSPlusVolumeHeader: unknown Volume Signature`,
  `cannot mount root, errno = 19`, `mount(N) failed` ripetuto (≥ 19
  tentativi). **Previsto**: `userdata` contiene ancora l'f2fs di Android. Il
  kernel riprova all'infinito senza panic e senza scrivere.
- **Touch**: dopo 45 s il driver vota la rail **L11C a 3200 mV** via RPMh
  (quella di sweet) e prova il GT9896 a I²C 0x5d: `err=6`, `pid=a5a5a5a5`
  (nessuna risposta), poi il fallback a 0x38: `err=6/6`. **Previsto**: su
  davinci il touch è alimentato da PM6150L **LDO6** e serve AVDD sul GPIO 5 del
  TLMM, più il reset sul GPIO 8. Il bus GENI SE7 invece funziona
  (`se7 fw=0x303 proto=3`).

Conclusione: RAM, GIC, timer, PSCI/SMP, framebuffer, UFS e SPMI sono a posto
con i valori di sweet. Per arrivare alla schermata di blocco serve solo la
root di iOS su `userdata` (passo distruttivo). Per il touch servono le
modifiche già elencate (LDO6, GPIO AVDD/reset, protocollo GT9886).

### Test 2: memoria UFS (fatto)

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
2. ✅ Shim per davinci: schermo (nessun kick necessario) e UFS verificati.
   Prova con `fastboot boot` (non scrive niente sul telefono).
3. 🔄 Build di XNU su GitHub Actions: fork
   [Nowey21/ios-sweet](https://github.com/Nowey21/ios-sweet), ramo `davinci`,
   prima build = kernel SWEET senza modifiche (run `42010014603`).
4. 🔄 Root di iOS: IPSW iPhone X 16.7.16 da Apple (download in corso),
   poi `mkiosroot.sh` in WSL.
5. ⬜ ADT (`tools/mkadt.py`) per il primo boot: come sweet (ipotesi: va bene
   così); poi 1080×2340.
6. ⬜ Flash su `userdata` (**cancella Android**, da confermare con l'utente
   prima di farlo), primo boot, log da recovery.
7. ⬜ Patch del kernel per davinci (tabella sopra): schermo 2340, touch GT9886.

## Handoff (per chi riprende il lavoro)

- Questo file è il registro del port davinci; `NOTES.md` resta quello di sweet.
- L'approvazione a cancellare Android citata in `AGENTS.md` (2026-10-15) è
  dell'autore di sweet, **non** dell'utente di questo port: ogni passo
  distruttivo su davinci va confermato di nuovo.
- File locali esclusi da git: `backup/` (partizioni), `dtb/` (device tree,
  cmdline), `research/ipsw-x/` (IPSW di Apple), `docs/davinci/*.jpg` (foto
  dei test, ritraggono la scrivania dell'utente).
- Build Ubuntu in WSL: `wsl -d Ubuntu -u root`, repo in
  `/mnt/c/Users/ciaoc/Desktop/ios mi9t k20/ios-sweet`,
  shim con `BOARD=davinci bash shim/build.sh`.
- Remote: `origin` = dipa-studio/ios-sweet, `fork` = Nowey21/ios-sweet (gh
  autenticato come Nowey21).

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
- **Boot 2** (commit `c492e31`): `adb reboot bootloader`, `fastboot boot
  shim/out/shim-boot.img` → `Booting OKAY`. Schermata verde dei registri, poi
  la sonda UFS: link attivo, letture riuscite, GPT e `userdata` trovate
  (vedi "Test 2: risultato").
- Installato GitHub CLI (`winget install GitHub.cli`, gh 2.103.3). Login web
  fatto dall'utente: account `Nowey21`, scope `repo` e `workflow`.
- Foto dei test tolte dalla cronologia git (`git filter-branch` sui commit
  davinci, mai pubblicati prima) e aggiunte a `.gitignore`. Attenzione:
  filter-branch le aveva cancellate anche dalla cartella; ripristinate dagli
  upload della chat.
- Fork pubblico `Nowey21/ios-sweet` creato, ramo `davinci` caricato. Actions
  abilitate sul fork. Avviato `gh workflow run xnu.yml --ref davinci`: run
  `42010014603` (kernel SWEET DEVELOPMENT, nessuna patch davinci).
- IPSW: `iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw`, 6 171 035 648
  byte, da `updates.cdn-apple.com` (URL e SHA-1 `5606c298…0193` da
  api.ipsw.me). Download in corso in `research/ipsw-x/`.
- Strumenti per la root di iOS in Ubuntu: installati `hfsprogs` (mkfs/fsck
  HFS+), `rsync`, `fuse3`, `cmake`, `g++`. Due ostacoli:
  1. `apfs-fuse` non è nei pacchetti di Ubuntu 26.04: va compilato dal
     sorgente (github.com/sgan81/apfs-fuse).
  2. Il kernel di WSL (6.18.54.1) è senza HFS+ (`# CONFIG_HFSPLUS_FS is not
     set`), ma `sweet-root-tweaks.py` deve montare l'immagine HFS+ in
     scrittura. Soluzione prevista: compilare solo il modulo `hfsplus.ko` dai
     sorgenti del kernel WSL di Microsoft (stessa versione) e caricarlo.
  La compilazione di codice esterno è stata bloccata dal controllo dei
  permessi: in attesa del consenso dell'utente.
- `apfs-fuse` (sgan81, commit 66b86bd) compilato in `/root/apfs-fuse` e
  installato in `/usr/local/bin`. Due correzioni per gli strumenti nuovi:
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (CMake 4) e `#include <cstdint>` in
  `ApfsLib/PList.h` (gcc 15).
- Modulo `hfsplus`: sorgenti `microsoft/WSL2-Linux-Kernel`, tag
  `linux-msft-wsl-6.18.54.1`, `.config` da `/proc/config.gz` con
  `CONFIG_HFSPLUS_FS=m`. Il build del solo modulo fallisce in modpost
  (`CONFIG_MODVERSIONS=y`, manca `Module.symvers`): in corso il build completo
  del kernel per generarlo.
- IPSW scaricato e verificato (SHA-1 OK). Dal BuildManifest (D22AP, Erase):
  OS = `087-86698-044.dmg` (UDIF con blocchi LZFSE, 4,7 GB),
  SystemOS = `087-86593-047.dmg`, AppOS = `087-86715-047.dmg`,
  DeviceTree = `Firmware/all_flash/DeviceTree.d22ap.im4p`. Estratti in
  `/root/ios` (WSL).
- Device tree D22AP: IM4P `dtre` con payload LZFSE (`bvx2`). Estratto con un
  parser DER in Python e decompresso con `lzfse -decode` (pacchetto Ubuntu)
  al posto dello strumento `ipsw` → `research/ipsw-x/d22ap.adt`, 186 084 byte,
  18 nodi alla radice (`chosen`, `cpus`, `arm-io`, `product`…).
- **Kernel**: run `42010014603` riuscita in 7 min 42 s.
  `xnu/out/kernel.development.sweet`, SHA-256 `3ab7d0b8…02d8`.
- Immagini di avvio (`BOARD=davinci`, ADT `--highmem` di sweet):
  - `shim/out/shim-boot.img`: cmdline del README (root in scrittura).
  - `shim/out/shim-boot-xnu-ro.img`: **test sicuro** senza `ufsrw=1`
    `rootrw=1` e con `sweetlog=0`. Il driver UFS resta in sola lettura (patch
    0030: `su.writable = (rw != 0)`) e il log persistente non parte (patch
    0047 richiede `su.writable`). `userdata` contiene ancora l'f2fs di
    Android: il montaggio come HFS+ deve fallire, quindi ci si aspetta che il
    kernel si fermi (panic "root device" o simile) senza scrivere nulla.
    Senza `sweetfb_noconsole=1`, il testo del kernel compare sullo schermo.
- **Boot 3** (commit `adbec6f`): `fastboot boot shim/out/shim-boot-xnu-ro.img`
  (16 748 KB) → `Booting OKAY`. Primo avvio di XNU sul Mi 9T, sola lettura.
  Risultato: vedi "Test 3". XNU arriva al montaggio della root, che fallisce
  come previsto; il touch non risponde (rail sbagliata).
- Primo `hfsplus.ko` (gcc 15) rifiutato: `disagrees about version of symbol
  module_layout`. Causa: il kernel WSL di Microsoft è compilato con gcc 13.2,
  e con gcc 15 cambiano alcune opzioni `CONFIG_CC_HAS_*` / `GCC_PLUGINS`
  e quindi i CRC dei simboli.
- Ricompilato tutto con `gcc-13` (13.4) e `gcc-13-plugin-dev`, config
  originale + `CONFIG_HFSPLUS_FS=m`, `LOCALVERSION_AUTO` spento: vermagic
  `6.18.54.1-microsoft-standard-WSL2`, `insmod` riuscito, `hfsplus` in
  `/proc/filesystems`. Si ricarica con
  `insmod /root/wslk/fs/hfsplus/hfsplus.ko` dopo ogni riavvio di WSL.
- L'utente ha **confermato** (2026-11-05) che si può cancellare `userdata`
  per scriverci la root di iOS.
- Avviato `tools/mkiosroot.sh` in `/root/build` (WSL): OS + SystemOS + AppOS
  → `ios16-root.img` da 16 GiB. `sweet-root-tweaks.py` non contiene nulla di
  specifico per sweet: si usa così.
- `mkiosroot.sh` completato fino alla copia dei file (12 GB su 16 GB usati),
  poi interrotto da `install: invalid user: '272'`: in Ubuntu 26.04 `install`
  è quello di uutils (Rust) e rifiuta un uid numerico senza voce in
  `/etc/passwd`. I passi saltati (cartelle di logd di proprietà 272 con
  `.migrated`, keybag di sistema, fsck) fatti a mano con mkdir/chown/chmod.
  `tools/mkiosroot.sh` corretto allo stesso modo per le prossime volte
  (`install -m … /dev/stdin` per la keybag funziona e resta).
- Attenzione con WSL: i mount non restano tra due invocazioni di `wsl.exe`
  (l'istanza si ferma) e Git Bash converte i percorsi `/mnt/...` se non si usa
  `MSYS_NO_PATHCONV=1`. Ogni sequenza mount → modifica → umount va in un unico
  script.
- `sweet-root-tweaks.py`: launchd (7 `_PanicOnCrash` rinominati, 2 job
  CommCenter tolti), containers, VirtualAudio spento, StartRecorderServer off,
  preferenze di migrazione/SpringBoard/Setup. Avviso: `no mobileactivationd
  system container in the image; skipped data ark` (lo stato attivato arriva
  comunque dal ramo Hactivation scelto da `mkadt.py`).
- fsck: `-f -y` dice "appears to be OK", ma `-f -n` subito dopo segnala sempre
  "Volume header needs minor repair". Il README di sweet non fa il controllo
  `-n`; si procede (XNU riproduce il journal all'avvio).
- `img2simg` → `research/ios16-root.simg`, 12 145 275 164 byte, SHA-256
  `8d3236cd…b25b` (identico dopo la copia su Windows).
- Immagine di avvio definitiva `shim/out/shim-boot-xnu-rw.img` (cmdline del
  README, root in scrittura, log persistente attivo).
