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
