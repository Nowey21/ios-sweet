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
