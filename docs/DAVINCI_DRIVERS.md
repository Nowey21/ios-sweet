# Mi 9T (davinci): piano per GPU, Wi-Fi e Bluetooth

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
