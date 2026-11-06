# Mi 9T (davinci): piano per GPU, Wi-Fi e Bluetooth

> **Stato (2026-11-05, ~16:40): la radio Wi-Fi funziona.** wland avvia il
> modem, carica il firmware WLAN (WCN3990, `WLAN.HL.3.0.1`), arriva a
> FW READY / WMI READY / HTT e le scansioni passive trovano le reti (2,4 e
> 5 GHz). Manca la connessione a una rete (serve `/var/local/wlan.conf`).

Stato al 2026-11-05: iOS 16.7.16 gira con schermata di blocco, touch e tasti
(vedi [DAVINCI_PORT.md](DAVINCI_PORT.md)). Grafica disegnata dalla CPU, niente
radio. Questo documento raccoglie cosa serve per i tre driver successivi, da
dove partire e cosa manca. Fonti: <https://hackiosx.dev/future>,
<https://hackiosx.dev/projects>, i README dei port citati, il device tree e la
partizione vendor di questo telefono.

## L'hardware (osservato nel device tree e su vendor)

| Parte | Chip | Nodo DT | Firmware sul telefono |
| --- | --- | --- | --- |
| GPU | **Adreno 618** (`qcom,chipid = 0x06010800`), con GMU | `qcom,kgsl-3d0` a 0x5000000, IRQ SPI 0x12c; `qcom,gpu-gmu` | vendor `/firmware`: `a630_sqe.fw` (microcodice CP), `a618_gmu.bin` (GMU), `a615_zap.mdt` + `.b00`–`.b02` (zap shader, firmato) |
| Wi-Fi | **WCN3990** (integrato, bus SNOC, driver `icnss`) | `qcom,icnss` a 0x18800000, IOVA SMMU 0xa0000000, MSA 1 MiB | `wlanmdsp.mbn` gira **sul modem** (Hexagon): sta nella partizione `modem` (sde51, FAT); `bdwlan.*` (board data) su vendor `/firmware/wlan` |
| Bluetooth | **WCN3990** (stesso chip, parte BT) | `bt_wcn3990`, rail `qca,bt-vdd-io/core/pa/xtal` | partizione `bluetooth` (sde26, FAT): di solito `crbtfw21.tlv`, `crnv21.bin` |

Nessuno di questi firmware può andare in git (sono di Qualcomm/Xiaomi): si
copiano dal proprio telefono, come per le altre parti del port.

## Lavoro comune a tutti e tre

I port che hanno GPU e Wi-Fi fanno girare parte del driver **in userland iOS**
(un demone o un bundle) e lasciano al kernel solo memoria, interrupt e accesso
ai registri. Serve quindi una catena per compilare codice iOS:

1. **Compilare per iOS**: serve Xcode con l'SDK di iOS. Su Windows non c'è; si
   può usare un runner macOS di GitHub Actions (lo stesso che compila il
   kernel), con un nuovo workflow per i binari userland.
2. **Portare i file sul telefono**: ios-sweet ha già la rete USB (patch
   0072–0077, `tools/sweet-usbnet.sh`). Il Pixel 4 aggiunge SSH (dropbear) e un
   file server del kernel. Va verificata su davinci: stesso controller DWC3
   (nel boot 3 i registri USB si leggevano).
3. **Firmware nella root di iOS**: copiati dalle partizioni del telefono in
   `/var/local/fw/...` dell'immagine HFS+.

## 1. GPU (Adreno 618)

**Riferimenti**:
- [ios-flame](https://github.com/NWylynko/ios-flame) (Pixel 4, Adreno 640, A6xx come il 618): Metal → Vulkan
  con `metal2vulkan` sopra **Turnip** (Mesa). backboardd, SpringBoard e le app
  disegnano sulla GPU, 20–26 fps nello scorrimento della Home. Codice in
  `phone/metal2`, `phone/turnip`, `phone/gpu`; patch kernel 0082–0124.
- [ios-topaz](https://github.com/MakrSas/ios-topaz) (Redmi Note 12, Adreno 610): OpenGL ES con Mesa
  **freedreno** (`userland/mesa`, `userland/gpu`) più patch kernel 0901–0906 (memoria
  GPU nel banco alto, firmware incorporato…).

**Perché è fattibile**: l'Adreno 618 è uno dei chip A6xx meglio supportati da
Mesa (è la GPU dei Chromebook SC7180), sia in freedreno che in Turnip. La
generazione è la stessa del 640 del Pixel 4: il driver kernel di ios-flame è il
punto di partenza più vicino.

**Cosa cambia rispetto al Pixel 4**: indirizzi e IRQ della GPU e della GMU
(dal DT), il firmware GMU (`a618_gmu.bin` invece di `a640_gmu.bin`), lo zap
shader (`a615_zap`), le frequenze/livelli di potenza (`qcom,gpu-pwrlevels`), il
clock controller `gpucc-sdmmagpie`.

**Ordine proposto**: (a) patch kernel GPU di ios-flame adattate, verifica che la
GMU parta e che il CP esegua un comando di prova; (b) Turnip compilato per
iOS; (c) il bridge Metal di ios-flame. Lavoro stimato: il più lungo dei tre.

## 2. Wi-Fi (WCN3990)

**Riferimento diretto**: il **Pixel 4 usa lo stesso WCN3990** e ha Wi-Fi
funzionante (Impostazioni → Wi-Fi, Safari, WPA2-PSK). Il loro `wland` gira in
userland e richiede:
- il **modem acceso**: sul WCN3990 il firmware Wi-Fi (`wlanmdsp.mbn`) gira sul
  DSP del modem, quindi serve avviare il modem (PIL/remoteproc con
  `modem.mdt`/`.bNN`), i servizi QMI e **rmtfs** con i propri `modemst1`,
  `modemst2`, `fsg`, `fsc` (già nel nostro `backup/`);
- il board data `bdwlan` del telefono;
- l'interfaccia `en0` per `wifid` (patch kernel di ios-flame; ios-topaz ha
  l'equivalente `0900-en0-Apple80211-for-wifid`).

**Cosa cambia**: indirizzi SNOC/CE e IRQ (DT `qcom,icnss`), regione MSA, i nomi
dei firmware del modem di davinci, il board data. Il resto del protocollo
(WMI/HTT del WCN3990) dovrebbe essere lo stesso.

**Ordine proposto**: (a) copiare `modem` e `bluetooth` (FAT) e il board data
dal telefono; (b) portare `wland` e le patch en0 di ios-flame; (c) avvio del
modem e QMI; (d) scansione, poi connessione.

## 3. Bluetooth (WCN3990)

**Nessun progetto ha ancora il Bluetooth** (hackiosx, "Open problems"): iOS si
aspetta un controller che si comporti come quello di Apple, e sul WCN3990 BT
condivide alimentazione e firmware con il Wi-Fi. Il lato hardware (UART HCI
del QUP, caricamento di `crbtfw21.tlv`/`crnv21.bin` come fa Linux con
`hci_qca`) è documentato; il lato iOS (bluetoothd, che ora riparte in loop) è
lavoro di ricerca nuovo. Da affrontare **dopo** il Wi-Fi, perché ne condivide
le rail e lo stato del chip.

## Riassunto

| Driver | Base | Difficoltà | Prerequisiti |
| --- | --- | --- | --- |
| Wi-Fi | ios-flame (stesso chip) | media | build userland iOS (macOS), rete USB, firmware modem dal telefono |
| GPU | ios-flame (stessa generazione), Mesa Turnip/freedreno | alta | come sopra + Mesa per iOS |
| Bluetooth | nessuna | molto alta (problema aperto) | Wi-Fi funzionante |

## Diario

### 2026-11-05

- Rete USB: già nel kernel di sweet (patch 0072–0077), si accende con il
  boot-arg `sweetusb=2`: il telefono si presenta come `1209:0001` con una
  funzione **CDC-ECM** (telefono 192.168.7.2, PC 192.168.7.1, log del kernel
  dal vivo su UDP 5140, `tools/sweet-usbnet.sh`). Windows non ha un driver
  CDC-ECM, quindi la scheda va passata a WSL con **usbipd-win**. Il kernel di
  WSL ha `CONFIG_USBIP_VHCI_HCD=m`, `CONFIG_USB_USBNET=m`,
  `CONFIG_USB_NET_CDCETHER=m`; i moduli corrispondenti (`vhci-hcd.ko`,
  `usbip-core.ko`, `usbnet.ko`, `cdc_ether.ko`) sono già compilati con gcc 13
  in `/root/wslk` (stesso build del modulo hfsplus).
- Immagine `shim/out/shim-boot-0083-usb.img` = 0083 + `sweetusb=2`.
- usbipd-win 5.3.0 installato (winget, UAC accettato dall'utente).
- **Boot 9** (`shim-boot-0083-usb.img`): Windows vede `1209:0001 Sweet iOS
  (USB Ethernet)` → la funzione USB di sweet parte anche su davinci.
  `usbipd bind` (UAC) + `usbipd attach --wsl`.
- In WSL servono i moduli **ufficiali** (`modprobe vhci-hcd cdc_ether`):
  quelli compilati da noi non si caricano perché `usbcore` è un modulo
  (`CONFIG_USB=m`) e non era caricato. Attenzione: la VM di WSL si spegne
  quando nessun processo la usa e perde moduli e collegamento USB; si tiene
  accesa con `wsl -d Ubuntu -- sleep infinity` in background.
- `cdc_ether` crea `enx02005e77ee01` (MAC host 02:00:5e:77:ee:01), alt setting
  1 della data interface selezionato, endpoint bulk 0x81/0x01 da 512 byte. Ma
  il ping non passa: ARP `INCOMPLETE`, TX 0 pacchetti / 19 errori, nel dmesg
  `vhci_hcd: urb->status -104` (URB bulk annullati dopo il timeout). Il
  controllo (EP0) funziona, i trasferimenti bulk no. Da capire dal log del
  telefono se il lato iOS arma gli endpoint ("ECM endpoints configured").
- **Log di boot 9** (`logs/boot9.txt`, da recovery): lato telefono tutto
  regolare fino alla configurazione: enumerazione, `SET_CONFIGURATION 1` →
  "ECM endpoints configured" (DEPCFG/XFERRSC senza errori, DALEPENA con
  EP 2/3/5), `SET_INTERFACE 1 alt 0` e `alt 1`, `SET_ETHERNET_PACKET_FILTER`,
  notifica NETWORK_CONNECTION. Poi **nessun evento sugli endpoint bulk**: né
  XferComplete né XferNotReady su EP 2/3. Il frame in uscita del telefono
  (`tx 1`) resta pendente, l'host annulla i suoi URB e alla fine resetta il
  bus. Il controllo EP0 invece funziona per tutto il tempo. Stesso codice che
  su sweet funziona con un PC Linux nativo: il sospetto è la catena
  Windows/usbipd con una funzione CDC-ECM.
- Decisione: invece di indagare usbipd, rendere il telefono **CDC-NCM**, che
  Windows 10 (2004+) e 11 gestiscono nativamente (`UsbNcm.sys`). Così il PC
  Windows parla direttamente con il telefono, senza usbipd e WSL.
- Patch `0084-sweet-usb-ncm.patch` (boot-arg **`sweetusbncm=1`**, senza il
  boot-arg nulla cambia):
  - descrittori NCM: sottoclasse 0x0D, descrittore funzionale NCM 1.0,
    protocollo dati 0x01 (NTB); PID **1209:0002** e prodotto "Sweet iOS (USB
    NCM)", perché Windows non riusi la cache dei descrittori ECM;
  - richieste di classe: `GET_NTB_PARAMETERS` (solo NTB16, 2 KiB in entrambe
    le direzioni, allineamento 4, **un datagramma per NTB** dall'host),
    `GET_NTB_INPUT_SIZE`, `GET_NTB_FORMAT`; le richieste OUT
    (`SET_NTB_INPUT_SIZE`, filtri, reset) accettate;
  - ricezione: parsing di NTH16 + catena di NDP16, ogni datagramma passato
    allo stack come prima;
  - trasmissione: un frame per NTB dietro un'intestazione fissa di 28 byte
    (NTH16 + NDP16), con un byte di riempimento se il blocco è multiplo di 512.
- CI run `42031489208` (0082–0084) riuscita in 6 min 12 s → `xnu/out-0084/`.
  Immagine `shim/out/shim-boot-0084-ncm.img` = cmdline di 0083 +
  `sweetusb=2 sweetusbncm=1`.
- **Boot 10: la rete USB funziona su Windows senza driver aggiuntivi.**
  Windows carica il suo driver NCM: scheda "Ethernet 8", "ios-sweet Sweet
  iOS (USB NCM)", `USB\VID_1209&PID_0002`, MAC 02-00-5E-77-EE-01, stato Up.
  IP statico con `netsh interface ipv4 set address ... static 192.168.7.1
  255.255.255.0` (UAC; `New-NetIPAddress` da una PowerShell elevata non aveva
  avuto effetto). **`ping 192.168.7.2`: 4/4, 1–2 ms.** Il log del kernel dal
  vivo arriva su UDP 5140 (backboardd/SpringBoard campionati, exec dei
  demoni).
- Nuovo `tools/davinci-usbnet.ps1`: aspetta la scheda, imposta l'IP una volta,
  stampa (e salva con `-Log`) il log dal vivo. usbipd e WSL non servono più
  per la rete.

### Strategia per Wi-Fi e GPU: partire dalla serie del Pixel 4

- Clonato [NWylynko/ios-flame](https://github.com/NWylynko/ios-flame) (commit `745c434`, 2026-10-22) in
  `research/ios-flame` (escluso da git).
- Albero di lavoro in WSL `/root/xnu-work` (git): base = albero della CI con
  0001–0081 di sweet; ramo `master` = base + nostre 0082–0084; ramo `flame` =
  base + **tutta la serie di ios-flame 0082–0124, applicata in ordine senza
  conflitti** (39 patch, 51 file, +9579 righe).
- File toccati da entrambe le serie: `sweet_touch.c`, `sweet_rpmh.c`,
  `sweet_usb.c`, `SweetHID.cpp`.
- Cosa contiene la serie flame (dal suo NOTES.md e dal diff):
  - **infrastruttura**: `sweet_pmem.c` (/dev/pmem: memoria fisica e chiamate
    SCM per i driver userland), `sweet_files.c` (file server TCP), comandi
    UDP, RPMh generico da userland (`sweet_rpmh.c`), log dei panic, RTC,
    cpufreq, fallback di clonefile su HFS, AMFI per dyld/trust cache;
  - **GPU**: `sweet_a6xx.c`/`sweet_gpu.c` (core A640, GMU, page table,
    submit), `SweetAccelerator.cpp` (IOAcceleratorES per Metal),
    `SweetGPUABI.h` (ABI per Turnip), maschera degli angoli del display;
  - **Wi-Fi**: `flame_wlan.c`, `redwood_apple80211.c`, `redwood_net.c`,
    `redwood_sta.c`, `RedwoodWLAN.cpp`, `MiniIONetworkStack.cpp` (en0 +
    Apple80211 per wifid), più il demone `wland` in userland
    (`phone/wifi`, ~5500 righe: smem, glink, QMI, SCM, rmtfs, pdmapper, wlfw,
    ath10k per WCN3990).
- Parti **specifiche del Pixel 4** da adattare a davinci:
  - `pexpert/pexpert/arm64/SWEET.h`, `start.s`, `sweet_early.c`,
    `arm_vm_init.c`: framebuffer 0x9c400000 / 2280 righe (davinci 0x9c000000 /
    2340), costanti a tempo di compilazione;
  - 0085 touch ST FTS su SPI → su davinci il nostro GT9886 (0082/0083);
  - 0086 cpufreq: SM8150 1+3+4 core → SM7150 2 A76 + 6 A55;
  - 0088 offset USB di SM8150 → quelli di SM7150 di sweet, più il nostro NCM;
  - 0099 RTC del PM8150 → PM6150;
  - tasti: vol+ su PM8150 GPIO6 → su davinci PM6150L GPIO2 (come sweet);
  - GPU A640 → **A618** (GMU `a618_gmu.bin`, zap `a615_zap`, livelli di
    potenza, clock `gpucc-sdmmagpie`, numero di SP/cluster);
  - Wi-Fi in userland: regione del modem 0x8dc00000 → **0x8b000000**
    (0x8400000 byte), MSA 0x8bc00000 → **0x97d00000** (DT `wlan_msa_region`;
    `qcom,wlan-msa-memory` = 1 MiB), firmware e board data di davinci.
    Uguali: smem 0x86000000, WCSS 0x18800000, apps SMMU 0x15000000, IPC
    APCS 0x17c0000c.
- Il codice userland (wland, Turnip, metal2vulkan) si compila con Xcode +
  SDK iOS: serve un workflow sui runner macOS di GitHub Actions.

### Adattamento della serie flame a davinci: inventario (2026-11-05)

Ramo `davinci` in `/root/xnu-work` = `flame` + le nostre modifiche. Le tre
patch davinci (0082–0084) vanno in conflitto in `sweet_rpmh.c` (flame ha già
ammesso `ldoa14`/`ldoa17` per il suo touch), `SweetHID.cpp` (flame usa costanti
`SWEET_PANEL_W/H`) e `sweet_usb.c` (flame ha riscritto molto per il suo PHY e
per l'SMMU): vanno riscritte sopra flame, non applicate.

Valori **fissi del Pixel 4** nella serie flame e valore per davinci (DT di
questo telefono):

| Dove | flame (SM8150) | davinci (SM7150) | Note |
| --- | --- | --- | --- |
| `SWEET.h` `SWEET_EARLY_FB_PA` / `_BLOCK_PA` / `_HEIGHT` | 0x9c400000 / 0x9c000000 / 2280 | 0x9c000000 / 0x9c000000 / **2340** | console di avvio, `start.s`, `arm_vm_init.c` |
| `SweetHID.cpp` `SWEET_PANEL_H` | 2280 | **2340** | |
| `sweet_keys.c` vol+ | PM8150 GPIO6, SID 0, 0xc500 | **PM6150L GPIO2, SID 4, 0xc100** (come sweet) | |
| `sweet_fts.c` touch ST FTS | SPI su SE4 0x890000, GPIO 122/126/127/128 | **da spegnere**: su davinci 0x890000 è un altro I²C; touch = GT9886 (nostra 0082) | flame spegne il driver Redmi di default (`sweettouch=1` lo forza) |
| `sweet_cpufreq.c` OSM | 0x18323000 / 0x18325800 / 0x18327800 (4+3+1) | da verificare nel DT (2 A76 + 6 A55); intanto **spento** (`flamecpufreq`) | |
| `sweet_usb.c` | PHY Synopsys femto a 0x088e2000, DWC3 dietro l'apps SMMU (`sweetusbsmmu`) | **QUSB2 v2** a 0x088e2000 (sweet), senza SMMU; più la nostra NCM (0084) | |
| `SweetPlatformExpert.cpp` watchdog APSS | 0x17c10000 | 0x17c10000 (uguale) | |
| RTC | PM8150 SID 0, 0x6000 | PM6150 `qcom,qpnp-rtc` 0x6000 (uguale) | |
| `sweet_ufs.c` `SWEET_MIRROR_PA` (copia del log in RAM) | 0x1fe000000 | **fuori dalla RAM**: davinci finisce a 0x1fcc00000 | da spostare sotto la fine e togliere dalla RAM di XNU |
| `mkadt.py` RAM di XNU / `pram` | 0x100000000…0x1fcc00000 / 0x1fd000000 (flame ha RAM fino a 0x200000000) | da ridurre: p.es. XNU fino a 0x1f9c00000, `pram` 0x1fa000000, mirror 0x1fb000000 | |
| `sweet_a6xx.h` GPU | base 0x02c00000, GPUCC 0x02c90000, SMMU 0x02ca0000, chip 0x06040001 (A640) | **base 0x05000000, GMU 0x0506a000, GPU SMMU 0x05040000, chip 0x06010800 (A618)**, GPUCC da DT | GMU `a618_gmu.bin`, zap `a615_zap` |
| `sweet_a6xx.h` `A6_FB_PA` | 0x9c400000 | 0x9c000000 | |
| zap shader `ZAP_REGION` | 0x9a915000 (`pil_gpu_region`) | **0x97f15000** (`gpu_region`, 0x2000) | |
| `phone/wifi` modem / MSA | 0x8dc00000 (0xb400000) / 0x8bc00000 | **0x8b000000 (0x8400000) / 0x97d00000** | uguali: smem 0x86000000, WCSS 0x18800000, apps SMMU 0x15000000, APCS IPC 0x17c0000c |
| `phone/wifi/radio.c` `POOL_PA` | 0xE0000000 (16 MiB, banco basso non usato da XNU) | probabilmente uguale: su davinci XNU usa solo il banco alto e 0xE0000000 non è riservato | da verificare |

Prossimo passo: una patch "board davinci" sopra flame con queste modifiche,
più la riscrittura delle nostre 0082–0084; poi un avvio che deve dare lo
stesso risultato di oggi (schermata di blocco, touch, tasti, rete USB) prima
di accendere GPU e Wi-Fi.

### Serie di patch per davinci con flame (2026-11-05)

`xnu/patches/` ora è: ios-sweet 0001–0081 + **ios-flame 0082–0124 invariate**
+ davinci:
- `0200-davinci-board.patch` (9 file, da `research/davinci-flame/board.py`):
  console di avvio 0x9c000000 / 2340 righe (`SWEET.h`), pannello touch 2340 e
  147 mm (`SweetHID.cpp`), vol+ su PM6150L GPIO2 SID 4 (`sweet_keys.c`),
  default spenti per l'FTS (`flametouch`), il cpufreq SM8150 (`flamecpufreq`)
  e il PHY femto (`sweethsphy`), touch Redmi/davinci acceso (`sweettouch`),
  copia del log a 0x1fb000000 (`sweet_ufs.c`), `A6_FB_PA` 0x9c000000.
- `0201-davinci-gt9886-touch.patch`: il GT9886 (stesso codice di prima) sopra
  la versione flame di `sweet_touch.c`; `ldoc6` aggiunto ai rail ammessi da
  `sweet_rpmh.c`.
- `0202-sweet-usb-ncm.patch`: la NCM sopra la versione flame di
  `sweet_usb.c` (che registra anche i frame con `ud_log_frame`).
- Le vecchie 0082–0084 (sulla sola base sweet) sono in
  `xnu/patches-davinci-sweetbase/`.
- `tools/mkadt.py --davinci`: RAM di XNU 0x100000000 + 0xf9c00000, `/pram`
  0x1fa000000 (512 KiB), nodo `/arm-io/wlan`, MAC per MobileGestalt.
  Senza `--davinci` l'ADT di sweet è identico a prima (12 772 byte).
- Attenzione: ios-flame dice che la sua serie è stata compilata solo in
  locale con Xcode 16 e mai nel workflow della CI (macos-14, Xcode 15). La
  0082-flame (IORPC.kernelContent) serve con Xcode 16; con Xcode 15 da
  verificare.
- **CI run `42034829990`: compilata ma senza la serie.** Il kernel aveva la
  stessa dimensione del primo kernel di sweet e nessuna stringa di flame o
  davinci. Nel log: `Skipping patch ... 0082-flame-...: patch does not apply`
  per tutte le patch flame e quindi anche per 0200–0202; `build.sh` di
  darwin-xnu-build **avvisa e compila lo stesso**. Causa: le patch flame
  copiate avevano fine riga **CRLF** (clone di ios-flame fatto con
  `core.autocrlf` di Git per Windows). GNU `patch`, usato in locale, toglie i
  CR da solo; `git apply`, usato in CI, no.
- Correzioni: patch flame convertite in LF; clone `research/ios-flame`
  ricaricato con `core.autocrlf=false`; verificato in locale che tutte le 42
  patch (0082–0124, 0200–0202) si applicano **con `git apply`** in ordine.
  Il workflow `xnu.yml` ora salva l'output di `build.sh` e **fallisce** se una
  patch `NNNN-sweet|flame|davinci-*` viene saltata.

### Userland: wland e workflow macOS (2026-11-05)

- `phone/wifi/`: `wland` di ios-flame (MIT; `radio.c` da ios-topaz) copiato al
  commit `745c434`, con i valori davinci: `MPSS_PA/SIZE` 0x8b000000/0x8400000,
  `MSA_PA` 0x97d00000, board data `bdwlan.bin`/`bdwlan.b0c` (nomi da
  confermare quando avremo la partizione `modem`). Dettagli in
  `phone/wifi/README.md`.
- Il board data **non è su vendor** (`/firmware/wlan/qca_cld` ha solo
  `WCNSS_qcom_cfg.ini` e `wlan_mac.bin`): sul WCN3990 sta nella partizione
  `modem` (FAT, `sde51`) insieme a `wlanmdsp.mbn` e `modem.mdt/.bNN`. Va
  copiata dal telefono (in sola lettura, da recovery).
- `.github/workflows/userland.yml`: compila `phone/wifi` con
  `xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0`,
  firma ad hoc, segnala gli import deboli (simboli che iOS 16 potrebbe non
  avere), carica l'artifact `phone-userland`.
- CI run `42036527881`: **la serie completa è nel kernel** (17 891 304 byte;
  stringhe di flame, `/dev/pmem`, GT9886 e NCM presenti) → `xnu/out-0202/`.
- `wland` compilato dal workflow userland (run `42036708394`, 16 s): Mach-O
  arm64, firma ad hoc, SDK iPhoneOS 17.5 con minimo 16.0, nessun import
  debole → `research/userland-out/wland` (locale).
- Immagine `shim/out/shim-boot-0202.img`: kernel 0202 + ADT `--davinci` +
  cmdline di 0084-ncm (`sweetboard=davinci sweetusb=2 sweetusbncm=1`).
  **Boot 11** in attesa del fastboot: deve dare lo stesso risultato di prima
  (schermata di blocco, touch, tasti, rete USB) più il file server flame
  (TCP 2223).
- **Boot 11** (`shim-boot-0202.img`, serie completa): foto (locale
  `docs/davinci/boot11.jpg`): 6/6 CPU, UFS in lettura e scrittura, log
  persistente, AMFI/Sandbox/HFS registrati, Skywalk OK. Il cpufreq di flame
  legge comunque le tabelle OSM (sola lettura, "index … kept") e i valori
  sono quelli di davinci: A55 300–1804 MHz, A76 300–2208 MHz → gli indirizzi
  OSM di SM8150 coincidono su SM7150.
- **Ma l'USB non parte**: nessun dispositivo `1209:*` su Windows dopo 4
  minuti. Causa: la 0088 di flame sostituisce gli offset dei clock GCC
  dell'USB con quelli di SM8150 (`cfg-noc-axi` 0x0f078 ecc.); il driver avvia
  il dispositivo solo se `cbcr[0]` (master) e `cbcr[3]` (cfg-noc-axi) sono
  accesi, e su davinci a quell'offset legge un altro registro.
- Correzione in `0200-davinci-board`: tabella dei clock USB di sweet
  (`cfg-noc-axi` 0x0502c, `aggre-axi` 0x8201c, `ahb2phy` 0x6a004, `clkref`
  0x8c010). Patch 0200–0202 rigenerate (LF).
- **Boot 12** (`shim-boot-0202b.img`, clock USB corretti): ancora niente USB.
  Foto: stesso schermo di boot 11. **Log da recovery** (`logs/boot12.txt`,
  121 righe): il kernel si ferma a 7,76 s **subito dopo** `SWEET CPUFREQ:
  perfcl ... index 0 kept`; poi solo i campionamenti `SMP --- sample N` (il
  timer gira), nessun `exec`, nessuna riga di `SWEET PMEM`, tasti, touch o
  USB. Ordine in `sweet_ufs.c`: log, sampler, **cpufreq**, pmem, wlan, gpu,
  keys, touch, fts, usb. Il cpufreq di flame legge sempre **tre** blocchi OSM
  (anche senza cambiare nulla); il terzo, `0x18327800` (core prime di
  SM8150), su SM7150 non esiste e la lettura blocca il bus. Era quindi questo
  anche il blocco di boot 11, non solo i clock USB.
- Correzione in `0200-davinci-board`: solo i due cluster di davinci (A55×6 a
  0x18323000, A76×2 a 0x18325800). Patch 0200–0202 rigenerate.
- **Firmware dal telefono** (da recovery, sola lettura, SHA-256 verificati):
  `backup/modem.img` (320 MiB), `backup/bluetooth.img` (1 MiB),
  `backup/dsp.img` (64 MiB); FAT con settori da 4096 byte, letti con `mtools`.
  Estratti in `research/fw/` (escluso da git):
  - `modem/`: `modem.mdt` + `modem.bNN`, `modemr.jsn`, **`modemuw.jsn`**,
    `wlanmdsp.mbn` (firmware Wi-Fi che gira sul modem), `bdwlan.bin` e 33
    varianti `bdwlan.1xx`/`2xx`/`bxx` (il driver sceglie per board ID, che
    arriva dal firmware via QMI: il nome giusto si vedrà dal log di `wland`);
    anche `adsp*.jsn`, `cdspr.jsn`, `qdsp6m.qdb`;
  - `bluetooth/`: `crbtfw11/20/21.tlv`, `crnv11/20/21.bin`.
  Sono gli stessi file che ios-flame mette in `/var/local/fw/modem` sul
  telefono.
- **Boot 13** (`shim-boot-0202c.img`, cluster OSM corretti): **funziona con
  la serie completa.** Dopo il fastboot bloccato ("no link") il telefono era
  finito in fastbootd (`USB\VID_18D1&PID_D00D`, `fastboot boot` → "unknown
  command"): `fastboot reboot bootloader` e di nuovo `fastboot boot`. La
  scheda NCM compare dopo 5 s, ping 1–2 ms, **file server flame aperto (TCP
  2223)**, log dal vivo: backboardd presenta frame (~4,4 fps), tocchi
  registrati (anche `edge` dal bordo in basso).
- File server: funziona solo con i percorsi reali (`/private/var/...`, non
  segue i link come `/var`). Da Git Bash serve `MSYS_NO_PATHCONV=1`,
  altrimenti `/private/...` diventa un percorso Windows e il telefono
  risponde `ERR 2`. Nuovi `tools/davinci-files.py` (= flame-files.py verso
  192.168.7.2) e `tools/davinci-cmd.py` (comandi UDP 5141, p.es. `sync`).
- `tools/davinci-wifi-install.sh`: caricati in 84 s `/private/var/local/bin/
  wland`, 69 file in `/private/var/local/fw/modem`, le 4 immagini rmtfs
  (`modemst1/2`, `fsg`, `fsc`) in `/private/var/local/fw/rmtfs`; verificati
  scaricandoli di nuovo (`modem.mdt`, `wland`, `fsc.img` identici).
- `tools/davinci-wland-job.py add`: job launchd `org.davinci.wland`
  (`wland auto`, root, log `/var/local/wland.log` + `wland.out`) aggiunto a
  `/System/Library/xpc/launchd.plist`; l'originale è sul telefono come
  `launchd.plist.orig` e in `research/launchd/`. `remove` lo toglie.
  Senza `/var/local/wlan.conf` wland non si collega a nessuna rete: avvia il
  modem, WLFW e fa le scansioni passive. Poi `sync` e riavvio (boot 14).
- **Boot 14** (job wland attivo): **il touch non funziona più** (riferito
  dall'utente). `/var/local/wland.log`:
  `smmu: WLAN 0x640/0x1 -> CB5 via SMR6 ... SMR 00000000 ... NOT as written`
  (l'SMR scritto rilegge 0), `up: no WLAN SMMU stream`, poi
  `wlan power: ldoa1 752 mV on -> 0`, `ldoa7 1800 mV on -> 60` (ETIMEDOUT) e
  fine. Cause:
  - stream SMMU del Pixel 4 (0x640); su davinci il DT dice `iommus = <0x30
    0x240 0x1>`;
  - rail del Pixel 4: su davinci (DT `icnss`) sono `vdd-cx-mx` = L9A (640 mV,
    `qcom,vdd-cx-mx-config`), `vdd-1.8-xo` = L1C, `vdd-1.3-rfa` = L2C,
    `vdd-3.3-ch0` = L10C. `ldoa1`/`ldoa7` sono altre linee del PM6150;
  - dopo un voto RPMh in timeout `sweet_rpmh.c` rifiuta ogni voto successivo
    ("previous uncertain transaction"): wland parte con launchd (~10 s) prima
    del touch (45 s), e il voto L6C del touch non passa più.
- Subito: `/var/local/wland.off` sul telefono (wland auto esce) + `sync`.
- `phone/wifi`: rail di davinci, `WLAN_SID` 0x240, e `wland auto` aspetta
  90 s dall'avvio, così il touch vota il suo rail per primo.
- **🎉 Boot 15: la radio Wi-Fi del Mi 9T funziona.** `wland.log` (locale,
  `logs/wland-boot15.log`; contiene i nomi delle reti vicine, quindi fuori da
  git):
  - `wland auto: 14 s since boot, waiting for 90` → poi
    `smmu: WLAN 0x240/0x1 -> CB5 via SMR6: SMR 80010240 ... -> ok`;
  - rail `ldoa9` 640, `ldoc1` 1800, `ldoc2` 1304, `ldoc10` 3304 mV,
    `rfclka2`: tutti `-> 0`; QMP `qdss` acked; voti `cx[6]`, `mss[7]`;
  - modem: metadati 9100 byte, `mem setup 8b000000+8400000 -> 0/0`,
    61 509 533 byte di segmenti, auth and reset → **modem ready after
    100 ms**; SMP2P state 6;
  - glink (VERSION, OPEN `IPCRTR` e altri canali), QRTR; `rmtfs` legge
    `modemst1/2` e `fsg` (dati del telefono); `pd-mapper`; `tftp`: il modem
    chiede `/readonly/firmware/image/wlanmdsp.mbn` e lo riceve (3 811 792
    byte); i file `miui_*`, `cust_variant`, `mbn_*.dig`, `ota_firewall`
    mancano senza conseguenze;
  - WLFW: `MSA region 0: 97d00000+4000 secure`, `region 1: 97d04000+fc000`
    (hyp_assign ok), **MSA READY**, `chip 0x140 family 0x4002 board 0 soc
    0x40090000 fw 0x3012006b`, board data **`bdwlan.bin`** (19 152 byte, board
    ID 0: nessuna variante necessaria), MAC, cal report, **FW READY**, wlan
    config, mission mode;
  - radio: 12 ring CE, `/dev/wlan` aperto (en0 + wifid), **HTC READY**,
    WMI SERVICE_READY (1 catena RF), **WMI READY**, **HTT VERSION_CONF
    3.61**, lista canali (37), **scansioni passive: 14 reti** su 2,4 e 5 GHz
    (WPA2/WPA3), segnale fino a −24 dBm. Nessun panic.
- Il touch: da verificare con l'utente in questo avvio (wland ora parte dopo
  il voto L6C del touch).
- Boot 15: l'utente conferma che **il touch funziona** insieme a wland (che
  ora parte dopo 90 s). Il problema di boot 14 era solo l'ordine dei voti RPMh.
- Per collegarsi: `/var/local/wlan.conf` con `ssid=...` e `psk=...` (WPA2-PSK;
  senza `psk` rete aperta), scritto **dall'utente** sul suo PC e caricato con
  `tools/davinci-files.py put wlan.conf /private/var/local/wlan.conf 600`;
  `wlan.conf` è in `.gitignore`. wland lo legge all'avvio (`auto` gira una
  volta per avvio del telefono): dopo il caricamento serve un riavvio.
- **Collegamento da Impostazioni (boot 15)**: l'utente sceglie una rete e
  inserisce la password; la rotella gira all'infinito. `wland.log`: `bridge:
  join '…' (PMK)`, auth 0, assoc 0, **WPA2 keys installed**, `joined`, `link
  up`: la radio è collegata. Ma `radio: stats ... tx 2 sent` (solo i 2 EAPOL
  dell'handshake) e nel log del kernel `net: so far 0 frames sent ..., 314
  received`: **iOS non manda nulla su en0**, nemmeno il DHCP. wifid continua
  a scansionare.
- Causa (stessa del Pixel 4, ios-flame HANDOFF "configd had no network
  services"): `/var/preferences/SystemConfiguration/preferences.plist` sul
  telefono ha solo `System` e `__VERSION__`, **0 NetworkServices, nessun Set**:
  configd non ha un servizio per en0 e non avvia il DHCP. Fatto: copia in
  `/private/var/local/sc-preferences.plist.bak` (e `research/`), file tolto,
  `sync`. Al prossimo avvio configd crea la configurazione predefinita con il
  servizio Wi-Fi per en0. **Da verificare al prossimo riavvio.**
- Anche nel log: `RedwoodWLAN: a user client of type 0 is asked for: there is
  none` — atteso: il servizio IOKit non ha user client di proposito e wifid
  ripiega sugli ioctl (ios-flame HANDOFF).

### GPU Adreno 618: patch 0203 (2026-11-05, scritta mentre l'utente era via)

Il driver kernel di ios-flame (`bsd/dev/sweet_a6xx.[ch]`, `sweet_gpu.c`,
~2300 righe) accende CX/GMU, carica il firmware della GMU, parla HFI, prepara
gli SMMU, carica SQE e zap shader, avvia il CP e fa il blit 2D verso il
pannello. Valori del Pixel 4 sostituiti con quelli di davinci
(`research/davinci-flame/gpu-0203.py` → `xnu/patches/0203-davinci-a618-gpu.patch`):

| Cosa | A640 (SM8150) | A618 (davinci) | Fonte |
| --- | --- | --- | --- |
| GPU / GPUCC / GPU SMMU | 0x02c00000 / 0x02c90000 / 0x02ca0000 | **0x05000000 / 0x05090000 / 0x05040000** | DT kgsl-3d0, gpucc-sdmmagpie, kgsl-iommu |
| RSCC (GMU + 0x23000) | 0x02c8d000 | **0x0508d000** | DT gmu@506a000 |
| PDC / PDC seq | 0x0b290000 / 0x0b490000 | uguali | DT gmu reg |
| GCC `ddrss_gpu_axi` | 0x71154 | **0x4452c** | Linux gcc-sm7150.c (gli altri 3 clock coincidono) |
| chip id / HFI SFR | 0x06040001 / 0x06040100 | **0x06010800 / 0x06018000** | DT qcom,chipid; calcolo di a6xx_gmu.c |
| firmware | a640_gmu.bin, a630_sqe.fw, a640_zap | **a618_gmu.bin**, a630_sqe.fw, **a615_zap** (.mdt + .b02) | vendor /firmware |
| zap region | 0x9a915000 | **0x97f15000** (0x2000) | DT gpu_region |
| frequenze (5 livelli) | 257–585 MHz | **180, 267, 355, 430, 565 MHz** (MIN_SVS…NOM) | DT gpu-pwrlevels (180…825); livelli ARC delle stesse OPP dell'A618 su SC7180 |
| GMU | 200 MHz MIN_SVS | uguale | DT gmu-pwrlevels |
| tabella banda HFI | a640 (3 comandi CNOC) | **a618_build_bw_table** (1 comando CNOC 0x5007c) | Linux a6xx_hfi.c |
| GMEM | 1 MB | **512 KB** (range fino a 0x17ffff) | Linux a6xx_catalog.c |
| CP_ROQ_THRESHOLDS_2 | 0x02000140 | **0x010000c0** | Linux a6xx_gpu.c |
| UBWC | hbb 15, AMSBC | **hbb 14, senza AMSBC** | Linux a6xx_gpu.c |
| registri "magic" del blit | A640 | **A618** (TPL1 0x108000, GRAS 0x880, SP_CHICKEN 0x430, HLSQ 0x80000, VPC 0, PC_POWER 0) | Mesa freedreno_devices.py a6xx_gen1 |
| RB_CCU_CNTL sysmem | 0x10000000 (2 CCU) | **0x08000000** (1 CCU) | Mesa: offset colore = num_ccu × 64 KB |
| pannello per il blit | 1080×2280 | 1080×2340 | |
| SMMU SID | GPU 0, GMU 5 | uguali (DT: gfx3d_user 0, gmu kernel 5) | DT iommus |
| identità per SweetMetal/Turnip (`SweetAccelerator.cpp`: selettore 0, GetDeviceInfo, `model`) | 640, 0x06040001, GMEM 1 MB, hbb 15, "Adreno 640" | **618, 0x06010800, GMEM 512 KB, hbb 14, "Adreno 618"** | deve coincidere con hw_init |
| pool dei buffer GPU (RAM fuori da XNU, `sweet_gpu.c`) | 0xc4000000+448 MB, 0xe1000000+496 MB | uguale: su davinci il banco basso sopra 0xa1600000 ha solo `ramdump_fb_region` 0xaf000000 (prima del pool) | DT reserved-memory |

Firmware estratti da vendor in `research/fw/gpu/` (escluso da git).
`tools/mkpayload.py` ora è quello di ios-flame (MIT): `--gpufw DIR` mette i
firmware in `/chosen/gpufw,<nome>` dell'ADT (il kernel li legge da lì) con i
nomi di davinci; aggiunge anche gli indirizzi dell'anello del log in fondo alla
cmdline. Il vecchio è in `research/mkpayload-sweet.py.bak`.

La GPU resta **spenta** di default (`sweetgpu=0`): la patch non cambia nulla
negli avvii normali. Da provare con `sweetgpu=1` + `--gpufw`. Non verificato:
tutto il resto (GMU boot, HFI, CP) va visto sul telefono; i punti più incerti
sono i livelli ARC (presi dall'A618 di SC7180) e lo stato del blit.

### Turnip per iOS in CI (2026-11-05)

- `phone/turnip/` copiato da ios-flame (MIT): `NativeAdrenoABI.h` (ABI di
  NativeAdreno, MIT, **identica** a `SweetGPUABI.h` del nostro kernel),
  `vktest.c`, `v2test.cpp`, shader di prova, `patches/0100-sweet-fetch-gmem-
  min-draws.patch`.
- `phone/turnip/build-turnip-ci.sh` + workflow manuale
  `.github/workflows/gpu-userland.yml` (macos-14): Mesa **85fda3b3** + le
  patch Mesa di **UniXNU al commit `2b88cfc9`** (21 ottobre, le stesse 8 patch
  di ios-flame; oggi UniXNU main ne ha 17 e potrebbe avere un'ABI diversa
  dal nostro kernel), scaricate al momento della build e non copiate, più la
  0100 di flame; cross-build meson per `arm64-apple-ios16.0`, backend
  `-Dfreedreno-kmds=darwin`; `vktest` compilato e firmato. Artifact
  `turnip-ios`.
- L'A618 è supportato da Mesa senza modifiche (a6xx_gen1). Turnip legge
  l'identità dal kernel (GetDeviceInfo), che la 0203 ora riporta come A618.
- **Turnip per iOS compilato** (gpu-userland run `42060145285`, 2 min 20 s):
  tutte le 8 patch UniXNU e la 0100 si applicano; `libvulkan_freedreno.dylib`
  17 877 616 byte (ios-flame: ~18 MB), esporta `vkGetInstanceProcAddr`,
  contiene il backend darwin (`IOAcceleratorES`); i "weak external" segnalati
  sono solo template C++ interni `tu_*`, nessun simbolo di libc. `vktest`
  69 056 byte. Artifact scaricato in `research/turnip-out/` (locale).
- **v2test compilato** (userland run `42060242739`): header IOKit dal SDK
  macOS dello stesso Xcode (non sono pubblici nel SDK iPhoneOS), linkato con
  l'IOKit di iOS. In `research/userland-out/` con wland.
- `tools/davinci-launchd-job.py add NAME PROGRAM ARGS...`: job launchd di una
  volta (org.davinci.NAME, root, output in `/var/local/NAME.out`,
  `DYLD_LIBRARY_PATH=/var/local/lib`). Sul telefono non c'è una shell: i
  test girano così.

### Immagini di prova GPU e job di test (2026-11-05, utente via)

- **Kernel con la 0203 corretta** (xnu run `42059651456`, 7 min 55 s, nessuna
  patch saltata) in `xnu/out-0203/` (locale).
- Due immagini, stessa cmdline di `0202c` (stessa ADT `davinci-flame.adt`) e
  firmware GPU nell'ADT (`--gpufw research/fw/gpu`):
  - `shim/out/shim-boot-0203g.img` (sha256 `c8a281be…`): **`sweetgpu=1`**,
    la GPU si accende al boot;
  - `shim/out/shim-boot-0203.img` (sha256 `95af21ae…`): GPU spenta, per
    controllare che il nuovo kernel avvii come `0202c`.
  `0202c` resta l'immagine sicura.
- Sul telefono (già caricati e verificati con SHA-256): `/private/var/local/
  bin/{v2test,vktest}`, `/private/var/local/lib/libvulkan_freedreno.dylib`.
- `tools/davinci-launchd-job.py` ha ora `--interval SEC` (launchd
  `StartInterval`: il job parte dopo SEC secondi e poi ogni SEC secondi,
  output in coda). Job aggiunti (poi `sync`):
  - `org.davinci.v2test` all'avvio → `/var/local/v2test.out`;
  - `org.davinci.vktest` ogni 300 s → `/var/local/vktest.out` (il primo
    dopo ~5 minuti, così non usa la GPU insieme a v2test).
  Con la GPU spenta falliscono subito senza conseguenze. Toglierli dopo la
  prova: `davinci-launchd-job.py remove v2test` / `remove vktest`.
- **Prova al ritorno dell'utente**:
  1. `fastboot boot shim/out/shim-boot-0203g.img`;
  2. live log (`tools/davinci-usbnet.ps1`): cercare `SWEET GPU`/`A6XX`
     (GMU boot, HFI, CP, blit);
  3. dopo ~6 minuti leggere `/private/var/local/v2test.out` e
     `vktest.out` con `tools/davinci-files.py get`.
  Se il boot si blocca: Power+Vol- e `fastboot boot` di `0202c`.

### Metal su Turnip: SweetMetal2.bundle (2026-11-05, utente via)

- `phone/metal2/` copiato da ios-flame (MIT; il frontend Metal e il bridge
  sono di UniXNU/NativeMetal, MIT) + `phone/metal2/build-ci.sh`: metal2vulkan
  **720ef1ce** (scaricato al build, non copiato) + le 8 patch di
  `bridge/patches`, staticlib Rust (toolchain 1.98.0, `aarch64-apple-ios`),
  poi bundle e `m2vtest` con xcrun clang, firmati ad hoc. Passo aggiunto a
  `gpu-userland.yml`.
- I push di quel pomeriggio non avviavano i workflow (GitHub: "fatal error in
  commit_refs_rules", dispatch HTTP 500): run avviato a mano,
  **`42061876301`, riuscito in 8 min 22 s**. `SweetMetal2` 21 984 144 byte,
  Turnip nel bundle 17 193 552 (strippato), `m2vtest` 17 897 392. Gli unici
  "weak external" sono `__isOSVersionAtLeast`/`__isPlatformVersionAtLeast`
  (definiti nel binario stesso, compiler-rt) e un template libc++: nessun
  simbolo mancante su iOS 16. Artifact in `research/metal2-out/` (locale; il
  dSYM da 190 MB non va sul telefono).
- **Installato sul telefono** (verificato con SHA-256, poi `sync`):
  `/System/Library/Extensions/SweetMetal2.bundle/{SweetMetal2,
  libvulkan_freedreno.dylib,Info.plist,_CodeSignature/CodeResources}`,
  `/private/var/local/bin/m2vtest`, `/private/var/local/m2v-cache/` (777),
  `/private/var/local/mtl2.log` (666), `/private/var/local/mtl2-clients` =
  `SpringBoard` (su flame senza SpringBoard lo sblocco non funziona).
  Il kernel lo usa **solo** con `sweetmtl=2` (patch flame 0116): negli altri
  avvii non cambia nulla.
- `shim/out/shim-boot-0203m.img` (sha256 `731dd64e…`): `sweetgpu=1
  sweetmtl=2`, backboardd compone l'interfaccia con Metal → metal2vulkan →
  Turnip → A618. **Da provare solo dopo che `0203g` mostra GPU, v2test e
  vktest funzionanti.** I primi avvii sono lenti: ~270 pipeline compilate sul
  telefono (cache in `/var/local/m2v-cache/<processo>`). Log:
  `/var/local/mtl2.log` e righe `MTL2[...]` nel live log.

Ordine di prova: `0203` (GPU spenta, controllo) → `0203g` (GPU + test) →
`0203m` (interfaccia su GPU). Sempre `0202c` come ritorno sicuro.

### Boot 16 (`0203g`, GPU accesa) e patch 0204 (2026-11-05, con l'utente)

- Riavvio da remoto (`davinci-cmd.py reboot`): fastboot bloccato in "no link"
  (il PC non vede nessuna periferica USB), Power+Vol- dell'utente, poi
  `fastboot boot shim-boot-0203g.img`. iOS parte normalmente (live log da 73 s).
- **Log completo del kernel senza recovery**: il ring della console
  (`_sweet_log_ring`, 4 MB, e `_sweet_log_total`) si legge da `/dev/pmem` con
  il file server. Indirizzi presi dalla tabella dei simboli Mach-O del kernel
  (il `nm` di WSL non legge Mach-O): PA = VA − 0xfffffe0006000000 +
  0x100000000; per il kernel 0203 `total` 0x101ce6710, `ring` 0x101ce6718
  → `logs/boot16-ring.txt` (253 KB, tutto l'avvio).
- **GPU, cosa funziona già**: voti di banda, clock GCC/GPUCC, CX GDSC acceso,
  GMU che risponde, SMMU programmato (`smr0 → cb0`).
- **Dove si ferma**: `gmu fw: bad block f1f00060 size 80bf00b9` → `bring-up
  FAILED in 264 ms`. `a618_gmu.bin` (32 768 byte) è un'**immagine grezza per
  il Cortex-M3** (vettori: SP 0x00043f80, reset 0x9), non il formato a blocchi
  dell'A640. In Linux l'A618 è una GMU "legacy" (famiglia a630:
  `gmu->legacy = true`, "HFI v1, has sptprac").
- **Patch 0204** (`research/davinci-flame/gmu-0204.py` →
  `xnu/patches/0204-davinci-a618-gmu-legacy.patch`), tutti i rami legacy di
  Linux v6.12 (`a6xx_gmu.c`, `a6xx_hfi.c`, `a6xx_gmu.xml`, verificati sui
  sorgenti, non a memoria):
  - firmware intero in ITCM (≤ 0x8000 byte);
  - dopo l'avvio della GMU: `gfx_rail_on` (BOOT_SLUMBER_OPTION 0, voto GX/MX
    del livello più alto, OOB BOOT_SLUMBER 22/30) e `sptprac_enable`
    (0x778000, attesa `(stato & 0x38) == 0x28`);
  - HFI: 2 code (risposta con id 4), niente riempimento 0xfafafafa né
    allineamento in lettura; HFI v1: INIT (buffer di debug 16 KB a GMU VA
    0x60009000, avvio a freddo), FW_VERSION, PERF_TABLE v1 ({voto, kHz}),
    BW_TABLE, TEST;
  - frequenza con i registri DCVS + OOB DCVS_SET 23/31 (non HFI 30);
  - OOB GPU_SET con i bit legacy 16/24 (A640: 30/31).
  Registri: offset di `a6xx_gmu.xml` + 0x1a800 (numerazione di questo driver).
  Controllare sui sorgenti è servito: BOOT_SLUMBER/VOTE_IDX/DCVS sono a
  0x23f8–0x23ff (Linux), e l'ack di BOOT_SLUMBER è il bit 30.
  Kernel: xnu run `42076587178` (7 min 12 s) → `xnu/out-0204/`.
- **Wi-Fi**: wifid parte e interroga il driver, ma configd non ha **nessun
  set né servizio di rete** (`preferences.plist` di 192 byte, solo `System`),
  anche se `NetworkInterfaces.plist` ha `en0` (IEEE80211, "Wi-Fi"): configd
  scrive le preferenze a 58 s, `en0` compare solo dopo l'avvio di wland
  (~70 s), e senza un set non aggiunge le nuove interfacce. Scritto un set
  standard ("Automatic") con un servizio Wi-Fi su `en0` (IPv4 DHCP, IPv6
  automatico); copie in `research/sc-preferences-boot16.plist` (prima) e
  `research/sc-preferences-wifi.plist` (dopo). Effetto al prossimo avvio.

### Boot 17–18 (`0204g`) e patch 0205

- Boot 17 (avviato da me): **blocco a 22 s** (niente ping). Boot 18 avviato
  dall'utente da fastbootd (`18D1:D00D`): `fastboot boot` lì non funziona,
  serve prima `fastboot reboot bootloader`.
- Con la 0204 la GMU **parte**: `1 firmware blocks loaded`, `init result
  babeface BOOTED`, `gfx rail oob acked, sptprac on (28)`. Poi **tutti** i
  messaggi HFI, già il primo (INIT), restano senza risposta con GMU2HOST info
  `0x800000` = bit 23 **CM3_FAULT**: il processore della GMU va in fault.
  Il driver proseguiva comunque (DCVS e GPU_SET in timeout, GX GDSC
  `f8222000`) e configurava anche l'SMMU della GPU: è lì che boot 17 si è
  bloccato.
- **Causa**: `smmu: smr0 0 -> cb0`. L'SMR scritto rilegge **0**, quindi lo
  stream della GMU non arriva al suo context bank: la GMU parte dalla TCM,
  ma al primo accesso alle code HFI in RAM va in fault. Come per il Wi-Fi
  (`NOT as written` con lo stream del Pixel 4), l'SMMU di davinci accetta
  solo lo stream esatto del DT: `gmu_kernel iommus = <&kgsl_smmu 0x5>`,
  `gfx3d_user <&kgsl_smmu 0x0>`, **senza maschera** (flame: 0x400 e 0x401).
- **Patch 0205** (`research/davinci-flame/smmu-0205.py` →
  `xnu/patches/0205-davinci-gpu-smmu-streams.patch`): maschere a 0;
  `smmu_bank` controlla che l'SMR si rilegga uguale, altrimenti fallisce;
  se l'HFI fallisce il driver **non tocca la GPU** e stampa FSR/FAR/FSYNR0
  del context bank della GMU (niente più blocco del telefono).

### Boot 19 (`0205g`): Wi-Fi funzionante, patch 0206

- **Wi-Fi con DHCP: funziona** (confermato dall'utente). configd tiene il set
  scritto al boot 16 (1 servizio, `CurrentSet` presente); dopo il join da
  Impostazioni: `configd en0 add address 192.168.1.53`, gateway
  192.168.1.1, rotte della LAN. Il problema del join "che gira" era quindi
  la mancanza del servizio di rete in configd, non la radio.
- GPU: `smr0 80000005` (accettato), il driver si ferma pulito (`bring-up
  FAILED in 4805 ms`, il telefono resta usabile). FSR 0x400 (solo il campo
  formato) e FAR 0: **nessun fault SMMU**. Letti da `/dev/pmem` il log della
  GMU, le code HFI e il buffer di debug (pool 0x104800000 + 0x91000): tabella
  e code corrette, ma la GMU **non ha mai letto** la coda comandi (read 0,
  write 0xd2), log e debug vuoti. Va in CM3_FAULT al primo doorbell.
- Linux, prima del primo messaggio, esegue `a6xx_gmu_hfi_start`
  (`HFI_CTRL_INIT` = 1, attesa del bit 0 di `HFI_CTRL_STATUS`); il driver di
  flame non lo faceva mai. **Patch 0206** (`research/davinci-flame/
  hfi-0206.py`): aggiunge quel passo (0x5186 + 0x1a800 = 0x1f986), con log
  dello stato e di GMU2HOST prima e dopo.

### Boot 20 (`0206g`): **LA GPU ADRENO 618 FUNZIONA**

Kernel 0206 (xnu run `42083706807`), `shim/out/shim-boot-0206g.img`
(sha256 `69fa7950…`, `sweetgpu=1`). Log da `/dev/pmem`, `logs/boot20-ring.txt`:

```
gmu: 1 firmware blocks loaded
gmu: init result babeface BOOTED, hfi_ctrl 0
gmu: gfx rail oob acked, sptprac on (28)
gmu: hfi queues started (status 1; g2h info before 0, after 0)
hfi v1: init, version, perf/bw tables -> 0; dcvs 430 MHz oob acked return 0
oob gpu_set acked, gx gdsc f8222000
smmu: smr1 80000000 -> cb1 ...
ring me_init: rptr 9 wptr 9 status 1 int 0 hwfault 0 cp_status1 1000000 idle
zap: auth and reset rc 0 r1 0
ring secure mode off: rptr b wptr b ... idle
bring-up done, GPU ready in 311 ms
surface ... -> slot 0 / slot 1; panel stride 4352
5s: 49 submits (0 missed), GPU busy 123 ms (1%), longest 3555 us, mean 2516 us
```

GMU (firmware legacy, HFI v1), CP, zap shader in TrustZone e blit 2D verso il
pannello funzionano: backboardd presenta i frame attraverso la GPU (submit
da 2,5–3,5 ms, nessuno perso). Mancava solo `HFI_CTRL_INIT` (0206).

- I job di test non erano mai partiti: li avevo aggiunti da Git Bash senza
  `MSYS_NO_PATHCONV=1` e il programma era diventato `C:/Program Files/Git/
  var/local/bin/v2test`. Rifatti (`v2test` all'avvio, `vktest` ogni 120 s);
  `davinci-launchd-job.py` ora rifiuta un PROGRAM che non inizia con `/`.
  Girano al prossimo avvio.

### Boot 21–22: test GPU **ALL PASS**; Metal per l'interfaccia: schermo nero

- Boot 21 (`0206g`): v2test `FAIL open`, vktest `no physical device`.
  `IOAcceleratorES` (SweetAccelerator) viene pubblicato **solo con
  `sweetmtl=1|2`** (SweetAccelerator.cpp): senza, né Metal né i test trovano
  la GPU.
- Boot 22 (`shim/out/shim-boot-0206m.img`, sha256 `57827f33…`, `sweetgpu=1
  sweetmtl=2`): `IOAcceleratorES published (MetalPluginName SweetMetal2)`.
  - **v2test: ALL PASS** (`logs/boot22-v2test.out`): coerenza CPU/GPU su
    16384 parole, seconda mappatura, `CP_MEM_WRITE` in buffer WC e in
    memoria importata, 0 fault.
  - **vktest: ALL PASS** (`logs/boot22-vktest.out`): `Turnip Adreno (TM) 618,
    Vulkan 1.3.354`, device in 28–92 ms, submit vuoto 0,11 ms, fill+copy 1 MB
    esatto in 0,95 ms, compute shader compilato sul telefono (3–16 ms)
    esatto, triangolo 256×256 con pipeline compilata sul telefono (7–10 ms)
    con i pixel giusti.
  - Metal: `device Turnip Adreno (TM) 618` creato in backboardd (1,9 s) e
    SpringBoard (6,6 s); 19 pipeline di backboardd compilate (`ok=1`).
  - **Schermo nero** (riferito dall'utente). Gli swap arrivano (fino a
    60 fps, copie sul pannello), ma backboardd non manda quasi nessun
    lavoro alla GPU (`5s: 1–6 submits`), quindi le superfici restano vuote.
    Le superfici sono `fmt 0x62336138` ('b3a8', 10 bit) 1125×2436 e
    `GPU present not used: surface format/size/pitch`. Da indagare: perché
    il renderer Metal di CoreAnimation non invia draw (patch m2v 0006
    "skip draws without a pipeline"?) e il formato 10 bit. Per ora resta
    `0206g` (GPU accesa, interfaccia in software) come immagine di uso.

### Boot 23–24: **L'INTERFACCIA DI iOS SU METAL → TURNIP → ADRENO 618**

- Causa dello schermo nero: il framebuffer 'b3a8' (BGRA10_XR). Le note di
  ios-flame (HANDOFF, "Stage 3") lo spiegano: con il framebuffer profondo
  CoreAnimation non disegna nulla, e servono le chiavi **globali**
  `CAEnableDeepFramebuffer`, `CAEnableFramebufferCompression`,
  `CAEnableIOSurfaceCompression` = NO. SweetMetal2 le scrive solo come
  preferenze dell'app (già presenti in `com.apple.backboardd.plist`), e non
  bastano. Aggiunte a `/private/var/mobile/Library/Preferences/
  .GlobalPreferences.plist` (backboardd e SpringBoard girano come mobile),
  modo 644; originale in `research/GlobalPreferences-mobile.orig.plist`.
- Boot 23 (`0206m`): framebuffer `fmt 0x42475241` (BGRA8). backboardd invia
  lavoro vero (fino a 191 submit ogni 5 s, GPU al 30–47%) e presenta
  tramite GPU (`present 72/179ms`), 15–18 fps. **Interfaccia visibile**
  (confermato dall'utente), ma "lagga tantissimo". Al primo avvio vengono
  compilate sul telefono 137 pipeline di backboardd e 12 di SpringBoard
  (cache in `/var/local/m2v-cache/<processo>`).
- **CPU lente**: `pwrcl (A55 x6) index 6 kept` (1497 MHz), `perfcl (A76 x2)
  index 0 kept` (300 MHz). Con `cpus=6` (scelta di ios-sweet) iOS usa solo i
  sei A55. Boot-arg **`flamecpufreq=1`** (frequenza all-core più alta, senza
  ricompilare; tensione e limiti termici restano all'OSM):
  `A55 index 6 -> 9 (1804 MHz)`, `A76 index 0 -> 12 (2208 MHz)`, entrambi
  rilevati correttamente. Immagine `shim/out/shim-boot-0206mf.img`
  (sha256 `51c9f510…`, `sweetgpu=1 sweetmtl=2 flamecpufreq=1`). Boot 24:
  **fino a 27 fps** (prima 18), GPU al 9–49%, submit da 4–10 ms in media,
  picchi di 40–105 ms.
- Prossimi passi per la fluidità: `cpus=8` (usare anche i due A76, mai
  provati su davinci), GPU a 565 MHz (`sweetgpufreq=5`), profilare i
  picchi lunghi.

### Notte 5→6 novembre (utente via, telefono lasciato collegato)

Problemi riferiti dall'utente su boot 24 (`0206mf`): ancora lag, **sfondo
nero**, Wi-Fi che si accende tardi e va **ricollegato a mano** a ogni avvio,
**Safari non disegna le pagine**, niente audio, Bluetooth, fotocamera.

- **Telefono irraggiungibile** da ~20:30: la scheda NCM è enumerata
  (`1209:0002`) ma Windows la dà "Disconnected", niente ping né fastboot.
  Nessun live log attivo in quel momento (l'ultimo ascoltatore era morto
  al riavvio precedente), quindi la causa non è nota: blocco del kernel o
  solo del collegamento USB. `pnputil /restart-device` richiede i permessi di
  amministratore. Serve Power+Vol- dell'utente. **Ipotesi da controllare**:
  instabilità con `flamecpufreq=1` (A55 1804 / A76 2208 MHz) dopo diversi
  minuti; se si ripete, provare `flamecpufreq=1708`.
- **Soluzioni trovate in ios-flame (HANDOFF)**, preparate senza poterle
  provare:
  - Safari: WebContent senza un device Metal disegna nero; su flame
    `/var/local/mtl2-clients` = `* -chronod` (tutti con Metal tranne
    chronod, i cui widget con Metal sono bianchi). Da noi c'era solo
    `SpringBoard`. La 0124 (superfici 10 bit di WebContent come BGRA) è già
    nella serie.
  - Sfondo: su flame era nero perché PosterBoard non trovava le estensioni
    dei poster. Fix: kernel 0103 SweetAMFI (in serie), MAC nell'ADT (già in
    `mkadt --davinci`) e **una volta `lsreg -r`** (ricostruisce il database
    di LaunchServices: 515 estensioni, tutti i 10 provider di poster). I
    poster (AegirPoster) vogliono anche un device Metal: coperto da `*`.
  - Wi-Fi: su flame wifid si ricollega da solo dopo un join da Impostazioni
    (password nel portachiavi). Da noi wland partiva a 90 s e la radio era
    pronta a 123 s: **wland ora parte a 60 s** (il voto L6C del touch arriva
    a 53 s). Per capire perché wifid non si ricollega: job `oslogwifi`
    (log unificato di wifid per 15 minuti).
- **Nuovo in git**:
  - `phone/tools/` = gli strumenti di ios-flame (MIT): `lsreg`, `prefs`
    (defaults), `oslog` (log unificato), `psx`, `pstack`, `nettest`,
    `openapp`, `exq`, `expt`, `imgtest`, `membw`, `netsvc` (senza
    `runmigrator`), più il nostro **`delayrun SECS PROGRAM`** (lancia una
    volta, SECS secondi dopo l'avvio: launchd sa solo "all'avvio" o "ogni N
    secondi"). Compilati in CI (`userland.yml`), nessun simbolo weak.
  - GitHub da stasera rifiuta `macos-14` ("The configuration
    'macos-14-arm64-us-default' is not supported"): `userland.yml` passato a
    **macos-15** (run `42095942973` riuscito). Anche `xnu.yml` e
    `gpu-userland.yml` ora su macos-15: il kernel 0206 si compila con
    Xcode 16 (run `42097564172`, 8 min 49 s; la 0082 di flame copre l'iig
    nuovo), ma il binario (17 874 056 byte, in `xnu/out-0206-x16/`) **non è
    ancora stato avviato**: le immagini del mattino usano quello di macos-14.
    gpu-userland su macos-15 (run `42097524131`) non è partito per mancanza
    di runner GitHub ("not acquired by Runner"), da rilanciare quando serve.
  - `tools/davinci-batch1-install.sh USERLAND_DIR`: installa in un colpo
    wland nuovo, gli strumenti, `mtl2-clients` = `* -chronod`, toglie i job
    v2test/vktest (vktest girava ogni 120 s), aggiunge i job `lsreg`
    (`delayrun 150 lsreg -r`) e `oslogwifi`, poi `sync`. Artifact in
    `research/userland-out-n/` (locale).
- **Immagini per la fluidità**, tutte sul kernel 0206 con `sweetgpu=1
  sweetmtl=2 flamecpufreq=1`:
  - `shim-boot-0206mf8.img` (sha256 `9b780d37…`): **`cpus=8`** (ios-sweet
    0078 ha già avviato 8 core su questo SoC; l'ADT ha 8 nodi CPU);
  - `shim-boot-0206mf8g.img` (sha256 `a232774c…`): anche
    `sweetgpufreq=5` (GPU 565 MHz).
- **Audio, Bluetooth, fotocamera: valutazione** (corretta la mattina del 6
  novembre, leggendo `dtb/live.txt` e le patch di ios-topaz).
  - **Percorso dell'altoparlante sul Mi 9T:** ADSP (LPASS) → porta AFE
    **PRI MI2S** (nodo `sound`: `qcom,pri-mi2s-gpios`, pin gpio49… funzione
    `pri_mi2s`; il master MI2S è il SoC) → amplificatore **NXP TFA98xx**
    (`nxp,tfa98xx`, I2C QUP `0x890000` indirizzo `0x34`, reset gpio 12, irq
    gpio 56). Il WCD9340 (`tavil`, SLIMbus) serve per cuffie e microfoni,
    non per l'altoparlante: per un primo suono basta ADSP + MI2S + TFA.
  - **ios-topaz non si porta così com'è:** SM6225 usa AudioReach (GPR/SPF,
    "APM GET_SPF_STATE"); il firmware ADSP di SM7150 parla il vecchio **APR**
    (q6core/q6afe/q6asm/q6adm, nomi `msm-dai-q6-*` nel DT). Riutilizzabili
    solo gli strati bassi: boot dell'ADSP via PAS, SMP2P, GLINK sul bordo
    `lpass`, QRTR/pd-mapper (gli stessi che `wland` fa già per il modem).
    Da scrivere: APR sopra GLINK (`apr_audio_svc`), AFE port start per PRI
    MI2S (clock, 48 kHz, 16 bit), un ASM stream con buffer condiviso, e il
    TFA9874, che ha un DSP proprio e vuole il file di contenitore Xiaomi
    (`tfa98xx.cnt` nel vendor): senza, al massimo in modalità bypass.
  - Stima: più giorni, e ogni passo va verificato ascoltando, con l'utente
    vicino al telefono.
  - **Bluetooth:** il chip BT del WCN3990 sta su UART (QUP `0x88c000`,
    `msm-geni-serial-hs`); i firmware `crbtfw21.tlv`/`crnv21.bin` sono già
    in `research/fw/bluetooth` (non in git). Servirebbero un driver UART,
    il caricamento del firmware Qualcomm (comandi HCI vendor, come
    `btqca.c` di Linux) e soprattutto far accettare a `bluetoothd`, scritto
    per chip Broadcom/Apple, un controller Qualcomm. Nessun port lo ha.
    Fotocamera: nessun port (CAMSS + sensori + ISP), fuori portata.
- **Il blocco delle 20:30:** il log UDP (`logs/boot21-live.txt`) si ferma
  alle 20:02 e l'ultimo ring salvato (`boot24-ring.txt`, a 192 s) non mostra
  errori: pool GPU a 190–241 MB su 944, nessun "no bytes free", niente
  jetsam. La causa resta sconosciuta. Per escludere la frequenza CPU:
  `shim-boot-0206mf8s.img` (sha256 `8f8e5cda…`) = `cpus=8 flamecpufreq=1708`
  (entrambi i cluster alla voce più alta ≤ 1708 MHz).

**Piano del mattino** (con l'utente):
1. Power+Vol- → fastboot; `fastboot boot shim/out/shim-boot-0206mf8.img`
   (se non parte: `0206mf`).
2. Appena c'è la rete: `bash tools/davinci-batch1-install.sh
   research/userland-out-n`, poi `davinci-cmd.py reboot` e di nuovo
   `fastboot boot` della stessa immagine.
3. Controllare: sfondo dopo ~3 minuti (job `lsreg`), Safari, Wi-Fi da solo
   (`/var/local/oslogwifi.out`), fps nel ring.
4. Se va: provare `0206mf8g` (GPU 565 MHz). Poi togliere i job `lsreg` e
   `oslogwifi`.
5. Se il telefono si blocca di nuovo: `0206mf8s` (CPU a ~1,7 GHz).

### Mattina 6 novembre: batch 1, 8 core, sfondo, editor degli sfondi

- **Boot 25 (`0206mf8`, `cpus=8 flamecpufreq=1`)** + batch 1
  (`tools/davinci-batch1-install.sh`, corretto: Python di Windows non apre i
  `/tmp` di Git Bash, ora `cygpath -m`). Con gli A76 accesi il frame rate
  arriva a **50–56 fps** (27 la sera prima). `lsreg -r` a 162 s →
  `rebuild -> 1`: **lo sfondo funziona** (confermato dall'utente).
- **Wi-Fi che non si ricollega da solo:** nell'oslog di wifid (job
  `oslogwifi`, file non in git: contiene il nome della rete) l'auto-join
  scansiona bene e vede la rete a -31 dBm (5 GHz) / -35 dBm (2,4 GHz), poi
  scarta ogni candidato con "Below minimum RSSI threshold". I nostri
  risultati di scansione hanno noise 0 / snr 0 / noise_known 0 e
  `APPLE80211_IOC_NOISE` (17) risponde EOPNOTSUPP. **Patch 0207**
  (`0207-davinci-wifi-noise-floor.patch`): noise -95 dBm, snr = rssi + 95
  nei risultati, GET NOISE con lo stesso layout di GET RSSI. Provata: non
  basta (vedi sotto).
- **Kernel compilati con Xcode 16: partono solo con `-v`.** Il build di
  0207 su macos-15 (run 42178564570, Xcode_16.app; sul runner ci sono solo
  Xcode 16.x e 26.x) con la cmdline solita si ferma dopo "SWEET EARLY EL:
  kprintf startup complete" con la barra di avanzamento a schermo
  (`shim-boot-0207mf8.img`, sha256 `59c19ec1…`, due volte su due). Con
  `-v` (avvio in modalità testo, niente barra) parte normalmente: rete,
  interfaccia, tutto. `shim-boot-0207v.img` (console accesa: a schermo il
  testo esce storto sopra l'interfaccia) e **`shim-boot-0207mf8v.img`**
  (sha256 `a38b49f7…`): `-v` + `sweetfb_noconsole=1`, schermo pulito, è
  l'immagine da usare. La causa precisa nel codice della barra non è
  cercata. Da qui in poi tutte le immagini con kernel Xcode 16 vogliono `-v`.
- **0207 non basta per il Wi-Fi:** con noise -95 dBm e GET NOISE risposto,
  wifid continua a scartare la rete con "Below minimum RSSI threshold". Altre
  richieste che rifiutiamo: `RSSI_BOUNDS` (106, 16 byte; formato non
  documentato, itlwm su Mac non la gestisce e lì l'auto-join funziona),
  `COUNTRY_CODE` (51), 383, `PHY_MODE` (14), `RATE`, `MCS`. Serve guardare in
  CoreWiFi da dove viene la soglia (dyld cache). Ripiego intanto: wland si
  collega da solo all'avvio se c'è `/var/local/wlan.conf` (scritto
  dall'utente, fuori da git).
- **Editor degli sfondi bloccato:** il poster "Terra" (AegirPoster) andava in
  loop: due shader (`aegir_star_fsh`, `aegir_earth_fsh`) scrivono in
  `BGR10_XR_sRGB` con blending, metal2vulkan rifiuta
  `ExtendedRangeBlend(SrgbForm)` in fast math → pipeline nil →
  "command buffer refused: foreign render pipeline" → check-in timeout ogni
  5 s, SpringBoard fermo. **Patch bridge 0102**
  (`phone/metal2/bridge/patches/0102-sweet-xr-srgb-blend-rounded.patch`):
  in quel caso usa il modello "rounded" (che secondo i commenti della
  libreria vale per ogni blend in una forma `_sRGB`) invece di rifiutare.
  Build gpu-userland 42179048183 (Xcode 16: per lo userland va bene),
  installato in `SweetMetal2.bundle`; copia del vecchio in
  `/private/var/local/metal2-backup-0206/`. Risultato: zero rifiuti,
  **l'editor degli sfondi si apre** (confermato dall'utente).
- Ancora aperti: PridePoster e assetsd crashano nello stesso punto (sig 6,
  non GPU); i servizi di sintesi vocale (KonaSynthesizer, MacinTalkAUSP,
  SiriTTSSynthesizer) si riavviano in loop; Safari da verificare.

### 6 novembre, giorno/notte: Wi-Fi da solo, Safari, strumenti, luminosità

- **Wi-Fi si ricollega da solo (0208).** Trovato nel codice: `wifid`
  (16.7.16, 0x100128a20) confronta `scanResult.RSSI` con la soglia del
  device e, se minore, "Below minimum RSSI threshold". La soglia è
  `RSSI_THRESHOLD`, che wifid legge all'avvio del device (0x10004b8d0) con
  `Apple80211CopyValue(106 = RSSI_BOUNDS)`; IO80211.framework (0x1d3ec6394)
  la costruisce dalla risposta di 16 byte: `u32 version`, poi SInt32
  `RSSI_MIN` @4, `RSSI_MAX` @8, `RSSI_THRESHOLD` @12. Noi rifiutavamo la
  106 → soglia 0 dBm → ogni rete scartata. **Patch 0208** risponde
  -100 / 0 / -80. Provato (`0208mf8v`): rete ripresa da sola 7 s dopo la
  radio, senza toccare niente. La 0207 (noise) resta, innocua.
  Metodo: dyld cache letta dal telefono a pezzi (`dyld_shared_cache_arm64`
  + `.01`…`.42`, parser in Python), `wifid` scaricato intero,
  `llvm-objdump` in WSL.
- **Kernel con Xcode 16:** le immagini vogliono `-v`; nuovo
  `tools/davinci-mkimage.sh KERNEL NOME [BOOTARGS]` (da WSL) con la
  cmdline giornaliera già dentro.
- **Strumenti nuovi:**
  - `runq` (job launchd) + `tools/davinci-run.py PROGRAMMA ARG...`:
    comandi a richiesta senza shell (file `.cmd` in `/var/local/runq`, ogni
    comando in un processo figlio).
  - `tools/davinci-shot OUT.png`: screenshot dal framebuffer via
    `/dev/pmem` (0x9c000000, 1080x2340, righe da 4352 byte).
  - `bright` (BackBoardServices): la richiesta viene ignorata (manca un
    entitlement), utile solo per leggere il livello.
- **Safari funziona:** `openapp com.apple.mobilesafari https://www.wikipedia.org`
  → pagina completa (screenshot). Il primo avvio di Safari è lento (9 s al
  primo frame). Restano errori innocui del processo Networking
  (`set source app uuid failed [13]`, privilegio 10001 negato dal sandbox).
- **Luminosità, analisi:** backboardd/SpringBoard cercano un servizio con
  `backlight-control`; CoreBrightness accetta per il display interno solo
  un servizio di classe **`AppleARMBacklight`** ("unsupported service" con
  AppleCLCD, 0209) e legge `IODisplayParameters.brightness.{min,max,value}`.
  Pannello: Samsung EA, `bl_ctrl_dcs`, DCS `0x51` + 2 byte (11 bit,
  4..0x7ff). L'host DSI0 (0xae94000, v2.3, command mode) manda comandi dalla
  memoria (DMA); il context bank MDSS dell'SMMU (cb3, SID 0x800) ha la
  traduzione spenta, quindi l'indirizzo è fisico. L'ultimo pacchetto di ABL
  (0xfffd4000) era `c0390003` + `51 01 0e` = luminosità 270. Provato a mano
  con poke: il nostro pacchetto (pagina 0x9d6ff000, fine della
  cont_splash_region) termina (INT_CTRL bit 0) senza errori.
  **Patch 0210**: servizio `AppleARMBacklight` + setProperties → DCS 0x51.
- **Telefono finito nella recovery LineageOS** (6 nov ~10:00): dopo un
  `davinci-cmd.py reboot` (PSCI reset) ABL ha fatto un avvio normale invece
  di tornare in fastboot; Android non parte (userdata è di iOS) e la
  recovery ha `adb unauthorized`. Serve l'utente (Power+Vol-). **Patch
  0211**: prima del reset il comando `reboot` scrive in IMEM
  `0x146aa65c = 0x77665500` (la ragione "bootloader" di Linux,
  `qcom,msm-imem-restart_reason`), così ABL torna in fastboot. Il PON del
  PMIC non è scritto (il codice SPMI resta in sola lettura). Da provare.
- **Batteria reale:** SweetBattery legge già il PM6150 (tensione, carica,
  corrente) ma solo con `sweetbattery=1`, che le nostre immagini non
  avevano: iOS vedeva sempre 100%. Aggiunto alle immagini 0210b/0212*.

### Audio, vibrazione, Bluetooth, fotocamera: cosa c'è e cosa manca

- **HackiOS** (POCO X5 Pro "redwood", `github.com/oosexgang/hackios-xnu-kernel`,
  kernel APSL 2.0, userland MIT; copia locale `research/hackios-kernel`) ha
  driver XNU per: ADSP + APR (`drivers/audio/hakios.adsp.c`), percorso
  audio fino agli amplificatori (`hakios.sound.c`, `hakios.speaker.c`),
  microfono, **torcia** (`camera/hakios.torch.c`), **vibrazione**
  (`haptic/hakios.haptic.c`), **fotocamera** (`camera/hakios.camera.c`), e
  un primo sguardo al Bluetooth. Redwood usa APR come SM7150 (AFE 4, ASM 7,
  ADM 8): la parte protocollo è riutilizzabile; cambiano indirizzi, porta
  (noi PRI MI2S), amplificatore (noi TFA9874) e i campanelli (noi APCS, loro
  IPCC).
- **Plug-in audio di iOS:** `phone/audio` = `RedwoodAudio.driver` (HackiOS,
  MIT, via ios-topaz): dà al server audio "Speaker", "Codec" (microfono) e
  "Actuator" (Taptic Engine); scrive in `/dev/redwood-audio`, legge
  `/dev/redwood-mic`, manda le vibrazioni a `/dev/redwood-haptic`. Compilato
  in CI (userland). Installazione reversibile: `tools/davinci-audio-install.sh
  USERLAND_DIR` (`--undo` per toglierlo); rimette anche VirtualAudio (che
  senza dispositivi audio faceva crashare mediaserverd).
- **Patch 0212** (`bsd/dev/davinci_audio.c`): i tre nodi; l'audio per ora è
  accettato e scartato (niente suono finché non c'è l'ADSP); il microfono dà
  silenzio; la vibrazione accende il **motore del PM6150** (LDO a 3,0 V,
  SID 1 periferica 0x53: VSET 0x40/0x41, EN_CTL 0x46 bit 7) per ogni tratto
  di campioni sopra soglia, 15–400 ms, massimo 1 s poi pausa. Scritture SPMI
  solo per quei tre registri, solo se il canale dell'arbitro appartiene agli
  apps, ognuna riletta; al primo errore niente più scritture. Boot-arg
  `dvhaptic`: 1 legge e basta, 2 un impulso di prova di 120 ms a 30 s
  dall'avvio, 3 serve iOS. **Non ancora provata.**
- **Suono vero (da fare, più giorni):** firmware ADSP estratto dal backup
  del modem in `research/fw/adsp` (`adsp.mdt` + `b00`…`b26`, fuori da git);
  DT davinci: carve-out `0x95700000` (38 MB), SMP2P 443/429 con campanello
  APCS `0x17c0000c` bit 26, GLINK `lpass` bit 24, canali `IPCRTR` e
  `apr_audio_svc`. Strada: un programma `adsp` sui moduli di `wland` (che fa
  già PAS, SMEM, SMP2P, GLINK, QRTR per il modem), poi APR (AFE PRI MI2S,
  ASM, ADM) seguendo `hakios.sound.c`, poi il TFA9874.
- **Amplificatore TFA9874:** il contenitore di Xiaomi
  (`/vendor/firmware/tfa98xx.cnt`, estratto in `research/fw/tfa`, 510 byte,
  formato `PM3_01`) contiene solo scritture di registri ("bitfield" TFA2) e
  un `9874_dummy.patch`: **nessun firmware DSP da caricare**, quindi basta
  riprodurre quelle scritture via I2C (QUP `0x890000`, indirizzo `0x34`).
- **Bluetooth:** `bluetoothd` supporta il trasporto UART
  (`/dev/uart.bluetooth`, H4) ma conosce solo chip Broadcom (BCM_4377/4378/
  4387/4388, 43430) e si ferma su un chip sconosciuto. Servirebbe un driver
  UART GENI, il caricamento del firmware Qualcomm (`crbtfw21.tlv`/
  `crnv21.bin`) e un'emulazione dei comandi Broadcom. HackiOS non l'ha fatto.
  Fuori portata per ora.
- **Torcia/fotocamera:** su HackiOS la torcia passa da una libreria iniettata
  in mediaserverd e da MobileGestalt (camera-flash); la fotocamera da un
  driver da 4000 righe più userland. Sul Mi 9T il flash è un modulo del
  PM6150L: fattibile dopo la vibrazione (stesso percorso SPMI).

**Al ritorno dell'utente** (telefono nella recovery: Power+Vol-):
1. `fastboot boot shim/out/shim-boot-0212t.img` (kernel 0212: luminosità
   0210, reboot→fastboot 0211, batteria reale, impulso di vibrazione a 30 s).
2. Controllare nel live log: `SWEET BL:` (CoreBrightness trova
   AppleARMBacklight? arrivano setProperties?), `DAVINCI HAPTIC:` (motore
   letto, impulso), `SWEET BATTERY: PM6150` (percentuale reale).
3. Provare lo slider della luminosità; poi `davinci-cmd.py reboot` per
   vedere se torna in fastboot (0211).
4. Se la vibrazione va: `tools/davinci-audio-install.sh research/userland-out-a`,
   riavvio con `0212d`, controllare mediaserverd e la vibrazione della
   tastiera.
