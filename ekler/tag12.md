STAJ RAPORU – 12. GÜN
Linux Network Stack, sk_buff, net_device, Virtual Network Driver, MTU/Jumbo Frame, TX/RX Path ve NAPI Çalışmaları

On birinci gün sonunda networking çalışmaları Socket API seviyesinden packet ve kernel seviyesine taşınmış; AF_PACKET, PACKET_MMAP/TPACKET_V3 ve eBPF/XDP mekanizmaları incelenmişti. XDP'nin network driver RX path'inde erken bir hook noktası sağladığı ve XDP_PASS, XDP_DROP gibi kararlar verebildiği görülmüştü.

On ikinci gün çalışmalarında ise Linux network stack'in iç yapısına geçilerek struct sk_buff, struct net_device, network driver callback mekanizması, ndo_start_xmit, MTU/Jumbo Frame, TX/RX yolları, checksum ve NAPI mimarisi üzerinde çalışıldı.

1. Packet Capture Yöntemlerinin Karşılaştırılması

Önceki gün incelenen packet capture yöntemleri birlikte değerlendirilerek hangi seviyede çalıştıkları karşılaştırıldı.

Temel yöntemler:

tcpdump
Wireshark
libpcap
AF_PACKET / Raw Socket
PACKET_MMAP / TPACKET_V3
BPF/eBPF
XDP

olarak ele alındı.

tcpdump ve Wireshark'ın esas olarak packet gözlemleme ve analiz araçları olduğu, AF_PACKET ve PACKET_MMAP'ın userspace uygulamalarına düşük seviyeli packet erişimi sağladığı değerlendirildi.

Bunun yanında XDP/eBPF'nin yalnızca packet gözlemlemekle sınırlı olmadığı görüldü.

Temel ayrım:

tcpdump / Wireshark
        ↓
packet gözlemle




XDP/eBPF
        ↓
packet üzerinde karar ver
        ↓
XDP_PASS
XDP_DROP
...

şeklinde oluşturuldu.

2. Packet Drop Mekanizmalarının Karşılaştırılması

Capture filtresi ile gerçek network packet'ının düşürülmesinin farklı işlemler olduğu incelendi.

Örneğin classic BPF capture filter:

Packet
  ↓
BPF capture filter
  ↓
tcpdump'a ver / verme

kararı verebilirken, packet'ın normal network stack içerisindeki yolculuğu devam edebilir.

XDP tarafında ise:

NIC
 ↓
Driver
 ↓
XDP
 ↓
XDP_DROP
 ↓
packet sonlandırılır

şeklinde gerçek packet drop gerçekleştirilebilir.

Ayrıca TC/eBPF ve Netfilter mekanizmalarının da uygun hook noktalarında packet drop kararı verebildiği değerlendirildi.

3. Linux struct sk_buff Yapısının İncelenmesi

Linux kernel network stack içerisinde packet'ların temel temsil yapılarından biri olan:

struct sk_buff

incelendi.

sk_buff, yalnızca packet byte'larını değil packet'a ait metadata bilgilerini de taşımaktadır.

Önemli alanlar:

struct sk_buff
│
├── head
├── data
├── tail
├── end
├── len
├── protocol
├── dev
└── ip_summed

olarak değerlendirildi.

Özellikle:

skb->len

alanının packet'ın mevcut veri uzunluğunu takip ettiği görüldü.

Memory modeli:

head
 ↓
+-------------------------------------+
| headroom | packet data | tailroom   |
+-------------------------------------+
           ↑             ↑
          data          tail

şeklinde incelendi.

4. struct net_device Kavramı

Linux kernel içerisinde network interface'lerin:

struct net_device

ile temsil edildiği incelendi.

Örneğin:

wlo1
eth0
myeth0

gibi interface'lerin kernel tarafında birer net_device yapısıyla ilişkili olduğu değerlendirildi.

Temel model:

Network Interface
       ↓
struct net_device
       ↓
name
mtu
flags
MAC
netdev_ops
...

şeklinde oluşturuldu.

5. Virtual/Simple Network Driver Oluşturulması

Network driver mimarisini uygulamalı incelemek amacıyla basit bir sanal network driver üzerinde çalışıldı.

Driver içerisinde:

struct net_device_ops

callback tablosu oluşturuldu.

Temel yapı:

static const struct net_device_ops my_netdev_ops = {
    .ndo_open       = my_open,
    .ndo_stop       = my_stop,
    .ndo_start_xmit = my_start_xmit,
    .ndo_change_mtu = my_change_mtu,
};

şeklinde geliştirildi.

Böylece Linux kernel'in network driver'ı doğrudan belirli fonksiyon isimleriyle değil callback tablosu üzerinden kullandığı görüldü.

6. ndo_open ve ndo_stop

Interface'in açılması ve kapatılması için:

ndo_open
ndo_stop

callback'leri incelendi.

Temel akış:

ip link set myeth0 up
        ↓
kernel
        ↓
ndo_open
        ↓
my_open()

şeklinde değerlendirildi.

TX queue'nun:

netif_start_queue(dev);

ile başlatılabileceği ve:

netif_stop_queue(dev);

ile durdurulabileceği incelendi.

7. ndo_start_xmit ve TX Callback Mekanizması

Packet gönderme tarafında en önemli driver callback'lerinden biri olan:

ndo_start_xmit

incelendi.

xmit ifadesinin transmit, yani gönderme anlamına geldiği netleştirildi.

Temel fonksiyon:

static netdev_tx_t my_start_xmit(
    struct sk_buff *skb,
    struct net_device *dev)

şeklinde oluşturuldu.

Burada:

skb
 ↓
gönderilecek packet




dev
 ↓
kullanılacak network interface

olarak değerlendirildi.

Temel TX yolu:

Application
    ↓
send()
    ↓
TCP / UDP
    ↓
IP
    ↓
skb
    ↓
net_device
    ↓
ndo_start_xmit()
    ↓
Driver
    ↓
NIC

şeklinde oluşturuldu.

8. MTU Kavramının Ayrıntılı İncelenmesi

MTU'nun:

Maximum Transmission Unit

olduğu ve Ethernet üzerinde yaygın olarak:

MTU = 1500 byte

kullanıldığı incelendi.

Burada önemli olarak 1500 byte'ın Ethernet frame'in tamamını ifade etmediği görüldü.

Standart Ethernet frame:

Ethernet Header       14 B
IP packet           1500 B   ← MTU
FCS                    4 B
──────────────────────────
                     1518 B

şeklinde değerlendirildi.

Dolayısıyla switch veya NIC üzerinde MTU 1500 kullanılması, cihazın yalnızca toplam 1500 byte Ethernet frame kabul ettiği anlamına gelmemektedir.

9. IPv4, IPv6 ve MTU İlişkisi

IPv4 temel header uzunluğunun genellikle:

20 byte

IPv6 temel header uzunluğunun ise:

40 byte

olduğu tekrar değerlendirildi.

UDP için standart 1500 MTU altında:

IPv4:


1500
- 20 IPv4
-  8 UDP
─────────
1472 byte UDP payload

ve:

IPv6:


1500
- 40 IPv6
-  8 UDP
─────────
1452 byte UDP payload

hesapları yapıldı.

IPv6 için minimum link MTU gereksiniminin 1280 byte olduğu da incelendi.

10. Ethernet MAC Adresleri ve MTU İlişkisi

Source ve destination MAC adreslerinin Ethernet header içerisinde bulunduğu tekrar incelendi.

Ethernet Header


Destination MAC     6 B
Source MAC          6 B
EtherType           2 B
──────────────────────
                   14 B

Bu 14 byte'ın MTU 1500 değerinin içerisinde olmadığı görüldü.

Packet yapısı:

[ DST MAC ][ SRC MAC ][ TYPE ][ IP PACKET ][ FCS ]
<--------- 14 B --------->     <---1500--->   4 B

şeklinde değerlendirildi.

11. ndo_change_mtu Implementasyonu

Driver'ın MTU değişikliklerini yönetebilmesi için:

static int my_change_mtu(
    struct net_device *dev,
    int new_mtu)

callback'i oluşturuldu.

Callback tablosuna:

.ndo_change_mtu = my_change_mtu

eklendi.

Temel kullanıcı-kernel-driver akışı:

ip link set dev myeth0 mtu 9000
              ↓
            Kernel
              ↓
       struct net_device
              ↓
       ndo_change_mtu
              ↓
       my_change_mtu()

şeklinde incelendi.

Driver'ın desteklemediği MTU değerleri için:

return -EINVAL;

döndürebileceği görüldü.

12. Jumbo Frame Kavramı

Standart 1500 byte MTU'dan daha büyük frame kullanımına ilişkin Jumbo Frame kavramı incelendi.

Sık kullanılan örnek:

MTU ≈ 9000 byte

olarak ele alındı.

Jumbo Frame'in özellikle:

server ↔ server
storage network
backup
yüksek hızlı LAN
bulk data transfer

gibi büyük miktarda veri taşınan kontrollü network ortamlarında yararlı olabileceği değerlendirildi.

Buna karşılık küçük packet ağırlıklı uygulamalarda MTU'nun büyütülmesinin doğrudan fayda sağlamadığı görüldü.

Örneğin:

100 byte application data


MTU 1500 → 1 packet
MTU 9000 → 1 packet

olduğundan mouse click, küçük telemetry veya benzeri küçük mesajlarda Jumbo Frame kapasitesinin kullanılmayabileceği değerlendirildi.

13. Gerçek Driver'da MTU Artırmanın Etkileri

Gerçek Ethernet driver'da yalnızca:

dev->mtu = new_mtu;

yapmanın yeterli olmayabileceği incelendi.

MTU artırıldığında:

MTU artır
   ↓
NIC destekliyor mu?
   ↓
maximum frame size uygun mu?
   ↓
RX buffer yeterli mi?
   ↓
descriptor yapısı uygun mu?
   ↓
DMA buffer boyutu uygun mu?
   ↓
hardware register'ları güncellendi mi?

kontrollerinin gerekebileceği değerlendirildi.

Böylece MTU konusu ileride incelenecek DMA ve descriptor ring konularıyla ilişkilendirildi.

14. TX ve RX Path'in İncelenmesi

Linux network driver içerisinde iki temel veri yönü ayrıntılandırıldı:

TX = Transmit
RX = Receive

TX yolu:

Application
    ↓
send()
    ↓
TCP / UDP
    ↓
IP
    ↓
skb
    ↓
routing
    ↓
net_device
    ↓
ndo_start_xmit()
    ↓
Driver
    ↓
NIC
    ↓
Network

şeklinde oluşturuldu.

RX yolu ise:

Network
    ↓
NIC
    ↓
Driver
    ↓
skb
    ↓
Linux Network Stack
    ↓
IP
    ↓
TCP / UDP
    ↓
socket
    ↓
recv()
    ↓
Application

şeklinde incelendi.

Böylece driver'ın:

TX → Kernel'den hardware'e


RX → Hardware'den kernel'e

köprü görevi gördüğü netleştirildi.

15. Sanal RX Path Implementasyonu

Fiziksel NIC bulunmayan eğitim driver'ında RX yolunu gözlemlemek amacıyla TX skb'sinin clone edilerek RX tarafına verilmesi üzerinde çalışıldı.

rx_skb = skb_clone(
    skb,
    GFP_ATOMIC
);

kullanıldı.

GFP_ATOMIC seçiminin TX gibi sleep edilmemesi gereken context'lerde allocation yapılabilmesiyle ilişkisi tekrar edildi.

RX tarafında:

skb->dev = dev;


skb->protocol = eth_type_trans(
    skb,
    dev
);


skb->ip_summed = CHECKSUM_NONE;


netif_rx(skb);

işlemleri incelendi.

Temel eğitim modeli:

Linux Network Stack
        ↓
      TX skb
        ↓
ndo_start_xmit()
        ↓
my_start_xmit()
        ↓
skb_clone()
        ↓
my_receive()
        ↓
netif_rx()
        ↓
Linux Network Stack

şeklinde oluşturuldu.

16. eth_type_trans() ve RX Protocol Belirleme

RX tarafında:

skb->protocol = eth_type_trans(skb, dev);

kullanımı incelendi.

Ethernet frame içerisindeki EtherType bilgisinin üst katman protocolünün belirlenmesinde kullanıldığı değerlendirildi.

Örneğin:

0x0800 → IPv4
0x86DD → IPv6

şeklindeki protocol ayrımı ile önceki raw packet parsing çalışmalarının kernel driver tarafındaki karşılığı arasında bağlantı kuruldu.

17. Checksum Kavramının İncelenmesi

Network packet'larının iletim sırasında bozulup bozulmadığını tespit etmek için kullanılan checksum mekanizması incelendi.

Temel model:

DATA
 ↓
checksum hesapla
 ↓
DATA + CHECKSUM
 ↓
NETWORK
 ↓
checksum tekrar kontrol edilir

şeklinde oluşturuldu.

Farklı network katmanlarında farklı hata kontrol mekanizmalarının bulunabileceği görüldü:

Ethernet → FCS / CRC
IPv4     → Header checksum
IPv6     → IP header checksum yok
TCP      → TCP checksum
UDP      → UDP checksum
18. Checksum Offload

Modern NIC'lerin bazı checksum işlemlerini CPU yerine hardware üzerinde gerçekleştirebildiği incelendi.

Bu mekanizma:

Checksum Offload

olarak değerlendirildi.

RX tarafında kullanılan:

skb->ip_summed = CHECKSUM_NONE;

ifadesinin checksum bulunmadığı anlamına gelmediği; driver'ın kernel'e hardware tarafından doğrulanmış bir checksum sonucu sunmadığını ifade ettiği üzerinde duruldu.

Temel ayrım:

CPU checksum
      ↓
CPU hesaplar/doğrular




Checksum offload
      ↓
işin bir kısmı NIC'e devredilir

şeklinde oluşturuldu.

19. IRQ ve RX Problemi

RX tarafında packet'ın herhangi bir zamanda NIC'e gelebileceği için driver'ın bundan nasıl haberdar olduğu incelendi.

Temel mekanizma:

Packet
 ↓
NIC
 ↓
IRQ
 ↓
Interrupt Handler
 ↓
Driver

şeklinde ele alındı.

Ancak her packet için interrupt üretilmesinin yüksek packet hızlarında CPU üzerinde ciddi maliyet oluşturabileceği değerlendirildi.

packet → IRQ
packet → IRQ
packet → IRQ
packet → IRQ
...

yaklaşımının yüksek trafik altında verimsiz olabileceği görüldü.

20. NAPI Mekanizmasının İncelenmesi

Linux network driver'larında RX processing için kullanılan NAPI mekanizmasına geçildi.

NAPI'nin temel yaklaşımı:

Interrupt + Polling

hibriti olarak değerlendirildi.

Akış:

NIC'e packet geldi
        ↓
       IRQ
        ↓
interrupt handler
        ↓
napi_schedule()
        ↓
NAPI poll
        ↓
bir grup packet işle
        ↓
RX işi bitti
        ↓
napi_complete_done()

şeklinde oluşturuldu.

Böylece interrupt'ın esas olarak:

"RX tarafında yapılacak iş var."

bildirimini yaptığı, yoğun packet processing'in ise NAPI polling tarafında gerçekleştirildiği görüldü.

21. NAPI Private Driver Yapısı

Driver private data içerisinde:

struct my_priv
{
    struct napi_struct napi;
};

yapısı oluşturuldu.

Network device allocation sırasında:

alloc_etherdev(sizeof(struct my_priv));

kullanılarak net_device ile driver private datasının ilişkilendirilmesi incelendi.

Private alana:

netdev_priv(dev);

ile erişildi.

Temel yapı:

struct net_device
       │
       └── private data
               ↓
          struct my_priv
               │
               └── napi

şeklinde oluşturuldu.

22. NAPI Poll ve Budget Mekanizması

NAPI poll callback'i:

static int my_poll(
    struct napi_struct *napi,
    int budget)

üzerinde çalışıldı.

budget değerinin bir poll çalışmasında işlenebilecek packet sayısını sınırladığı görüldü.

Örneğin:

budget = 64


RX ring:
[P][P][P][P] ... 500 packet


        ↓


bir poll turunda
en fazla 64 packet

şeklinde değerlendirildi.

İşlenen gerçek packet sayısının:

work_done

ile tutulduğu incelendi.

Eğer:

work_done < budget

ise mevcut RX işinin tamamlanmış olabileceği ve:

napi_complete_done(napi, work_done);

ile NAPI poll döngüsünün tamamlanabileceği görüldü.

Gün Sonu Değerlendirmesi

On ikinci gün sonunda Linux networking çalışmaları packet capture seviyesinden network driver iç mimarisine taşındı.

Günün başında packet capture, packet filtering ve gerçek packet drop arasındaki farklar karşılaştırıldı. Ardından Linux kernel içerisinde packet'ların temel temsil yapılarından struct sk_buff ve network interface'lerin temsil edildiği struct net_device ayrıntılandırıldı.

Basit bir virtual network driver üzerinden:

ndo_open
ndo_stop
ndo_start_xmit
ndo_change_mtu

callback mekanizmaları incelendi ve TX/RX yollarıyla ilişkilendirildi.

MTU ve Jumbo Frame konusu ayrıntılı olarak ele alınarak Ethernet MTU 1500 değerinin toplam Ethernet frame uzunluğu olmadığı; standart durumda 14-byte Ethernet header ve 4-byte FCS ile frame'in 1518 byte'a ulaşabileceği değerlendirildi.

Günün devamında sanal RX yolu oluşturularak:

skb_clone()
↓
eth_type_trans()
↓
netif_rx()

akışı incelendi. Checksum ve checksum offload kavramları network driver perspektifinden ele alındı.

Son bölümde RX tarafının gerçek hardware mimarisine geçiş yapılarak IRQ ve NAPI mekanizmaları incelendi. Her packet için interrupt üretmek yerine NAPI'nin interrupt ile polling yaklaşımını birleştirdiği ve budget kullanarak RX processing miktarını sınırlandırdığı görüldü.

Günün sonunda ulaşılan genel model:

                            APPLICATION
                              ↑       ↓
                            recv()   send()
                              ↑       ↓
                           TCP / UDP
                              ↑       ↓
                              IP
                              ↑       ↓
                         NETWORK STACK
                              ↑       ↓
                              │      skb
                              │       ↓
                              │  ndo_start_xmit()
                              │       ↓
                              │     DRIVER
                              │       ↓
                              │    TX PATH
                              │       ↓
                              │      NIC
                              │       ↓
                           NETWORK
                              │
                              ↓
                             NIC
                              ↓
                         RX interrupt
                              ↓
                             IRQ
                              ↓
                       napi_schedule()
                              ↓
                         NAPI poll()
                              ↓
                      RX packet processing
                              ↓
                             skb
                              ↓
                       NETWORK STACK

şeklinde oluşturuldu.

Bir sonraki çalışma aşaması olarak NAPI'nin gerçek RX descriptor ring üzerinden packet toplama mekanizmasının anlaşılması; ardından MMIO, DMA, TX/RX descriptor ring ve NIC-driver memory iletişiminin incelenmesi planlanmıştır.