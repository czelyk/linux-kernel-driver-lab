STAJ RAPORU – 18. GÜN
Linux Networking – Gerçek TCP State Analizi, RTT/BDP Deneyi, Netfilter Framework ve MAC Filtering

On yedinci gün sonunda TCP performans tarafında rwnd, cwnd, Slow Start, ssthresh, Congestion Avoidance, BDP ve Window Scaling konuları tamamlanmış; teorik bilgiler ss -ti ve tcpdump ile gerçek Linux TCP bağlantıları üzerinde incelenmeye başlanacak noktaya gelmişti. Ayrıca yüksek RTT bağlantıları için TCP Accelerator, PEP/Split-TCP yaklaşımı ele alınmış ve sonraki çalışma başlığı olarak Netfilter Firewall ile MAC Filtering belirlenmişti.

Bugünkü çalışmada önce gerçek Linux TCP bağlantıları üzerinde ss -tin ve tcpdump kullanılarak TCP'nin kernel içerisinde tuttuğu değerler incelendi. RTT, RTO, cwnd, ssthresh, snd_wnd, MSS, Window Scale, retransmission, loss ve backoff değerleri gerçek bağlantılar üzerinden yorumlandı. Daha sonra tc/netem kullanılarak RTT'nin yapay olarak artırılması ve iperf3 ile bandwidth/throughput ölçümü üzerinden BDP'nin deneysel olarak gözlemlenmesi için altyapı oluşturuldu. Günün ikinci bölümünde hocanın Netfilter firewall ödevine geçildi; Netfilter hook mimarisi, skb, PRE_ROUTING, NF_ACCEPT/NF_DROP ve Ethernet header üzerinden source MAC filtering incelendi. Son olarak ilk Netfilter kernel modülü oluşturularak MAC blacklist firewall implementasyonuna başlandı.

1. Gerçek TCP Connection Oluşturulması

Önce teoride incelenen TCP parametrelerini gerçek sistem üzerinde görebilmek için localhost üzerinde TCP bağlantısı oluşturuldu.

Server:

nc -l 5001

Client:

nc 127.0.0.1 5001

Burada ilk denemede server Ctrl+C ile kapatıldığı için ss -tin çıktısında port 5001 bağlantısı yerine sistemde bulunan diğer HTTPS bağlantıları görüldü.

Bu durum aynı zamanda önemli bir pratik noktayı gösterdi:

nc -l 5001
     ↓
LISTEN
     ↓
client connect()
     ↓
SYN
     ↓
SYN-ACK
     ↓
ACK
     ↓
ESTABLISHED

TCP connection incelenirken server ve client'ın aynı anda çalışmaya devam etmesi gerektiği görüldü.

2. ss -tin ile TCP Kernel State İncelemesi

Bağlantılar:

ss -tin

ile incelendi.

Daha sonra yalnız ilgili connection'ı görmek için filtreleme kullanıldı:

ss -tin '( sport = :5001 or dport = :5001 )'

Burada önemli bir ayrım tekrar pekiştirildi:

tcpdump
   ↓
Wire üzerindeki packet

ss -ti
   ↓
Linux kernel içerisindeki
TCP connection state

On yedinci gün teorik olarak yapılan bu ayrım bugün gerçek sistem üzerinde uygulandı. Önceki raporda tcpdump ile advertised Window/SEQ/ACK gibi packet alanlarının, ss -ti ile ise cwnd, RTT ve RTO gibi local TCP state bilgilerinin görülebileceği belirlenmişti.

3. Gerçek ss Çıktısındaki TCP Parametreleri

Sistemde gerçek bağlantılarda aşağıdaki gibi bilgiler görüldü:

cubic
wscale:6,10
rto:202
rtt:1.689/0.455
mss:1448
cwnd:10
ssthresh:...
snd_wnd:66560

Bunların anlamları ayrıntılı olarak incelendi.

Parametre	Anlamı
cubic	Kullanılan TCP congestion-control algoritması
wscale	TCP Window Scale değerleri
rtt	Ölçülen/tahmin edilen Round Trip Time
rto	Retransmission Timeout
mss	Maximum Segment Size
cwnd	Congestion Window
ssthresh	Slow Start Threshold
snd_wnd	Receiver'ın advertised window bilgisinden türeyen send-window sınırı
unacked	Gönderilmiş fakat henüz ACK alınmamış veri
lost	Kayıp kabul edilen segmentler
retrans	Retransmission bilgisi
backoff	RTO exponential-backoff seviyesi
bytes_retrans	Yeniden gönderilen byte miktarı
rcv_ooopack	Out-of-order alınan packet sayısı
delivery_rate	Tahmini delivery rate
pacing_rate	TCP pacing hızı
app_limited	Network yerine application'ın gönderimi sınırladığı durum
4. cwnd × MSS İlişkisinin Gerçek Sistem Üzerinde Görülmesi

Örneğin gerçek bağlantılardan birinde:

mss:1448
cwnd:10
snd_wnd:66560

görüldü.

Congestion window'un yaklaşık byte karşılığı:

10 × 1448
=
14,480 byte

olarak hesaplandı.

Dolayısıyla:

cwnd sınırı ≈ 14.5 KB

snd_wnd     ≈ 66.5 KB

olduğunda:

               TCP SENDER
                    │
         ┌──────────┴──────────┐
         │                     │
       cwnd                 snd_wnd
         │                     │
      14.5 KB                66.5 KB
         │                     │
         └──────────┬──────────┘
                    │
                  min()
                    │
                    ▼
              ≈ 14.5 KB

şeklinde congestion-control tarafının daha dar sınır oluşturabileceği görüldü.

Bu, önceki gün öğrendiğimiz:

effective send limit
≈ min(rwnd, cwnd)

modelinin gerçek Linux state'iyle birleştirilmesi oldu.

5. Gerçek Retransmission ve RTO Backoff Örneği

ss çıktısındaki bazı gerçek Internet bağlantılarında özellikle:

cwnd:1
ssthresh:7
lost:1
retrans:1/7
backoff:6
rto:12928

gibi değerler görüldü.

Böylece daha önce teorik olarak incelenen:

LOSS
 │
 ▼
Retransmission
 │
 ▼
Congestion response
 │
 ▼
cwnd küçülmesi
 │
 ▼
ACK ilerlemiyorsa
 │
 ▼
RTO
 │
 ▼
Exponential backoff

mekanizmasının gerçek Linux TCP connection state'inde nasıl iz bırakabileceği görüldü.

Özellikle normal durumda birkaç yüz milisaniye civarında görülebilen RTO'nun tekrarlanan timeout/backoff sonrasında saniyeler seviyesine çıkabileceği gözlemlendi.

6. tcpdump ile Gerçek TCP Paketlerinin İzlenmesi

Loopback üzerinde:

sudo tcpdump -i lo -nn -vvv tcp port 5001

kullanıldı.

Doğru deney sırasının:

tcpdump başlat
      ↓
server başlat
      ↓
client bağlan

olması gerektiği görüldü.

Aksi durumda capture, connection kurulduktan sonra başlatılırsa:

SYN
SYN-ACK
ACK

paketleri kaçırılabilir.

Amaç SYN/SYN-ACK üzerinde:

MSS
SACK Permitted
Window Scale
Window

gibi TCP özelliklerini gerçek packet üzerinde görmekti.

7. RTT'nin Yapay Olarak Artırılması

Yüksek RTT bağlantısını laboratuvar ortamında taklit etmek için Linux Traffic Control incelendi.

netem kullanılarak:

sudo tc qdisc add dev lo root netem delay 100ms

ile loopback interface'e yapay gecikme eklenmesi planlandı.

Mantık:

Client
   │
   │ DATA
   │
   │ delay
   ▼
Server
   │
   │ ACK
   │
   │ delay
   ▼
Client

Böylece düşük gecikmeli localhost bağlantısında yüksek RTT'li WAN/uydu bağlantısına benzer bir test ortamı oluşturulabileceği görüldü.

Test sonrasında qdisc'in:

sudo tc qdisc del dev lo root

ile kaldırılabileceği incelendi.

Sistemde özel root qdisc bulunmadığı bir durumda:

Error: Cannot delete qdisc with handle of zero.

hatasının alınabileceği de görüldü.

8. Bandwidth + RTT + BDP Deney Tasarımı

Bir sonraki adım olarak yalnız latency değil bandwidth'in de sınırlanması ele alındı.

Örnek test sistemi:

Bandwidth = 100 Mbit/s
RTT       ≈ 200 ms

için:

$$ BDP = Bandwidth \times RTT $$ $$ 100\,Mbit/s \times 0.2s =20\,Mbit $$ $$ 20/8=2.5\,MB $$

elde edilir.

Dolayısıyla yaklaşık:

BDP = 2.5 MB

in-flight data hattı doldurmak için gereken ölçeği gösterir.

Bu değer daha sonra:

BDP
cwnd
snd_wnd

üçlüsüyle karşılaştırılacaktır.

9. iperf3 ile Throughput Ölçümüne Giriş

nc TCP davranışını gözlemlemek için yeterli olsa da sürekli yüksek miktarda veri üretmek için iperf3'ün daha uygun olduğu görüldü.

Planlanan deney:

iperf3 server
      ↓
iperf3 client
      ↓
TCP sürekli data gönderir
      ↓
ss -ti
      ↓
cwnd / RTT / snd_wnd
      ↓
iperf3
      ↓
gerçek throughput

Bu deney TCP Accelerator ödevi için temel ölçüm altyapısı olarak belirlendi.

10. Netfilter Konusuna Geçiş

TCP performans deneylerinden sonra hocanın ikinci network ödevine geçildi:

NETFILTER
    ↓
FIREWALL
    ↓
PACKET FILTERING
    ↓
MAC ADDRESS FILTERING

Bu geçiş önceki çalışma planıyla doğrudan uyumludur; 17. gün sonunda accelerator çalışmalarından sonra Netfilter, Firewall, Packet Filtering ve MAC Address Filtering başlıklarına geçileceği belirlenmişti.

11. Netfilter Nedir?

Netfilter'ın Linux kernel içerisindeki packet filtering/manipulation framework'ü olduğu öğrenildi.

Temel model:

PACKET
   ↓
Linux Network Stack
   ↓
belirli hook noktaları
   ↓
NETFILTER
   ↓
bizim callback
   ↓
verdict

Kernel module, callback fonksiyonunu Netfilter'a register eder.

Packet ilgili noktadan geçtiğinde Netfilter bizim callback'imizi çağırır.

12. Beş Temel Netfilter Hook Noktası

IPv4 tarafındaki temel hook'lar incelendi:

                         PACKET IN
                             │
                             ▼
                       PRE_ROUTING
                             │
                          ROUTING
                         /       \
                        /         \
                       ▼           ▼
                LOCAL_IN        FORWARD
                    │               │
                    ▼               │
               Local socket         │
                                    ▼
                              POST_ROUTING
                                    │
                                    ▼
                                   NIC


Local application:

Application
     │
     ▼
LOCAL_OUT
     │
     ▼
POST_ROUTING
     │
     ▼
    NIC

Bunlar:

NF_INET_PRE_ROUTING
NF_INET_LOCAL_IN
NF_INET_FORWARD
NF_INET_LOCAL_OUT
NF_INET_POST_ROUTING

olarak öğrenildi.

13. Netfilter'ın NIC Driver ile Birleştirilmesi

Daha önce yazılan NIC driver çalışmalarıyla Netfilter'ın konumu birleştirildi:

WIRE
 │
 ▼
NIC
 │
 ▼
DMA
 │
 ▼
RX Descriptor
 │
 ▼
IRQ
 │
 ▼
NAPI
 │
 ▼
skb
 │
 ▼
Linux Network Stack
 │
 ▼
NETFILTER
 │
 ▼
IP / TCP / UDP
 │
 ▼
Socket
 │
 ▼
Userspace

Böylece Netfilter'ın NIC driver'ın içerisinde olmadığı; driver'ın packet'ı skb halinde network stack'e teslim etmesinden sonraki üst katmanlardan birinde çalıştığı netleştirildi.

14. struct sk_buff Tekrar Karşımıza Çıktı

Driver çalışmalarında kullanılan:

struct sk_buff

Netfilter tarafında tekrar kullanıldı.

Hook callback:

static unsigned int mac_firewall_hook(
    void *priv,
    struct sk_buff *skb,
    const struct nf_hook_state *state)

şeklinde oluşturuldu.

Packet bilgisine:

skb
 │
 ├── Ethernet
 ├── IP
 ├── TCP/UDP
 └── payload

üzerinden erişilebileceği görüldü.

15. NF_ACCEPT ve NF_DROP

Netfilter hook'un packet hakkında verdict döndürdüğü öğrenildi.

NF_ACCEPT
    ↓
packet network stack'te
ilerlemeye devam eder

ve:

NF_DROP
    ↓
packet burada düşürülür

şeklinde iki temel davranış incelendi.

Firewall'ın temel mantığı böylece:

packet
   ↓
rule kontrolü
  / \
 /   \
izin  yasak
 │      │
 ▼      ▼
ACCEPT DROP

haline geldi.

16. İlk Netfilter Kernel Module Projesi

Yeni çalışma dizini oluşturuldu:

~/tasks/network/netfilter/

İçerisine:

netfilter/
├── mac_firewall.c
└── Makefile

dosyaları oluşturuldu.

İlk make denemesinde Makefile boş olduğu için:

make: *** No targets. Stop.

hatası alındı.

Ardından kernel module Kbuild Makefile'ı oluşturuldu.

17. Kernel Module Build Sürecinin Tekrarı

Build zinciri tekrar görüldü:

mac_firewall.c
      │
      │ make
      ▼
mac_firewall.o
      │
      ▼
MODPOST
      │
      ▼
mac_firewall.ko
      │
      │ insmod
      ▼
Linux Kernel

İlk derlemede:

ERROR: modpost: missing MODULE_LICENSE()
WARNING: modpost: missing MODULE_DESCRIPTION()

hatası alındı.

Bunun nedeni .c dosyasının henüz gerekli module metadata'sını içermemesiydi.

Aşağıdaki yapıların önemi tekrar görüldü:

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmet");
MODULE_DESCRIPTION("Simple Netfilter MAC firewall");
18. insmod ve rmmod

Module development döngüsü tekrar edildi:

source değiştir
      ↓
make
      ↓
rmmod
      ↓
eski module kernel'den çıkar
      ↓
insmod
      ↓
yeni .ko kernel'e yüklenir
      ↓
test

Özellikle:

sudo rmmod mac_firewall 2>/dev/null

komutundaki:

2
↓
stderr

ve:

/dev/null
↓
çıktıyı çöpe at

mantığı incelendi.

Dolayısıyla:

2>/dev/null

hata çıktısını gizlemek için kullanılan shell redirection olarak öğrenildi.

19. Netfilter Hook Registration

İlk module'de:

struct nf_hook_ops

kullanıldı.

Bu yapı kavramsal olarak:

struct nf_hook_ops
       │
       ├── hook
       │     └── hangi callback?
       │
       ├── pf
       │     └── hangi protocol family?
       │
       ├── hooknum
       │     └── hangi Netfilter noktası?
       │
       └── priority
             └── çalışma sırası

bilgilerini tutar.

İlk yapılandırma:

hook
 ↓
mac_firewall_hook

pf
 ↓
PF_INET / IPv4

hooknum
 ↓
NF_INET_PRE_ROUTING

priority
 ↓
NF_IP_PRI_FIRST

olarak oluşturuldu.

20. Framework/Callback Pattern'inin Tekrar Görülmesi

Netfilter ile daha önce PCI driver'da görülen kernel framework mantığı arasında bağlantı kuruldu.

PCI DRIVER                         NETFILTER

pci_register_driver()             nf_register_net_hook()
        │                                  │
        ▼                                  ▼
PCI core                           Netfilter core
        │                                  │
device match                       packet gelir
        │                                  │
        ▼                                  ▼
probe()                         firewall_hook()

Ortak pattern:

Ben framework'e
callback kaydediyorum
        ↓
event gerçekleşiyor
        ↓
kernel benim callback'imi
çağırıyor

şeklinde netleştirildi.

21. Ethernet Header'ın skb İçinden Alınması

MAC filtering için:

eth_hdr(skb)

kullanılarak:

struct ethhdr

elde edildi.

Temel alanlar:

struct ethhdr
     │
     ├── h_source
     │      ↓
     │   Source MAC
     │
     ├── h_dest
     │      ↓
     │   Destination MAC
     │
     └── h_proto
            ↓
         EtherType

olarak incelendi.

Kernel loglarında MAC adreslerini yazdırmak için:

%pM

formatının kullanılabileceği öğrenildi.

22. MAC Filtering'in Layer-2 Sınırı

Önemli bir networking detayı olarak MAC adresinin uçtan uca Internet kimliği olmadığı tekrar görüldü.

Örneğin:

HOST A
MAC = AA
   │
   ▼
ROUTER
   │
   ▼
SERVER

ilk link:

SRC MAC = AA
DST MAC = Router

iken router packet'ı yeniden gönderdiğinde yeni Ethernet frame oluşturulur.

Dolayısıyla sonraki linkte source MAC artık ilk hostun MAC'i olmak zorunda değildir.

Sonuç:

MAC filtering
     ↓
Layer 2 / local-link
seviyesinde anlamlıdır

Bu nedenle MAC blacklist'in özellikle doğrudan görülebilen Layer-2 komşular için kullanılabileceği öğrenildi.

23. MAC Blacklist Mantığı

Firewall'ın hedef algoritması:

Packet
   ↓
skb
   ↓
eth_hdr(skb)
   ↓
Source MAC
   ↓
blocked_mac ile karşılaştır
   ↓
       eşleşiyor mu?
        /        \
      EVET       HAYIR
       │           │
       ▼           ▼
   NF_DROP     NF_ACCEPT

olarak oluşturuldu.

MAC adresinin:

6 byte

olduğu ve kernel tarafında:

ETH_ALEN

sabitinin kullanılabileceği görüldü.

MAC karşılaştırması için:

ether_addr_equal()

kullanılmaya başlandı.

24. VS Code IntelliSense ile Gerçek Kernel Build Ayrımı

Kernel header'ları kullanılırken VS Code:

#include errors detected.
Please update your includePath.

uyarısı verdi.

Burada iki farklı sistem olduğu netleştirildi:

                C source
                   │
        ┌──────────┴──────────┐
        │                     │
        ▼                     ▼
VS Code IntelliSense      Kernel Kbuild
        │                     │
editor analizi           gerçek compiler
        │                     │
includePath              kernel headers

Dolayısıyla editor'ın kırmızı çizgi göstermesinin tek başına kernel module'ün derlenmediği anlamına gelmediği öğrenildi.

Asıl doğrulamanın:

make clean
make

ile yapılması gerektiği görüldü.

25. Bugün Oluşan Büyük Sistem Modeli

Bugünün sonunda daha önce birbirinden ayrı görünen konular aynı packet path üzerinde birleştirildi:

                         USERSPACE
                            │
                    TCP / UDP SOCKET
                            │
                            ▼
                           TCP
                            │
           ┌────────────────┼────────────────┐
           │                │                │
          rwnd             cwnd             RTT
           │                │                │
      Flow Control     Congestion       RTO / feedback
                       Control
                            │
                            ▼
                           IP
                            │
                            ▼
                      NETFILTER
                            │
                    MAC FIREWALL
                            │
                   ACCEPT / DROP
                            │
                            ▼
                           skb
                            │
                            ▼
                     NETWORK DRIVER
                            │
                      Descriptor Ring
                            │
                           DMA
                            │
                           NIC
                            │
                           WIRE

RX yönünde ise:

WIRE
 ↓
NIC
 ↓
DMA
 ↓
RX Descriptor
 ↓
IRQ
 ↓
NAPI
 ↓
skb
 ↓
NETFILTER
 ↓
PRE_ROUTING
 ↓
MAC kontrolü
 ↓
NF_ACCEPT / NF_DROP
 ↓
IP
 ↓
TCP / UDP
 ↓
Socket
 ↓
Userspace

şeklinde uçtan uca model oluşturuldu.

26. 17. Gün → 18. Gün İlerlemesi

Önceki gün çalışma:

17. GÜN
────────────────────────────

rwnd ↔ cwnd
      ↓
Slow Start
      ↓
ssthresh
      ↓
Congestion Avoidance
      ↓
Loss / AIMD
      ↓
BDP
      ↓
Window Scaling
      ↓
TCP Accelerator

seviyesine ulaşmıştı.

Bugün:

18. GÜN
────────────────────────────────

Gerçek TCP connection
        ↓
ss -tin
        ↓
RTT / RTO / cwnd / ssthresh
        ↓
snd_wnd / MSS / wscale
        ↓
Loss / Retrans / Backoff
        ↓
tcpdump
        ↓
Gerçek TCP packet
        ↓
tc / netem
        ↓
Yapay RTT
        ↓
iperf3 / throughput
        ↓
BDP deney altyapısı
        ↓
NETFILTER
        ↓
Netfilter Hook Architecture
        ↓
PRE_ROUTING
        ↓
struct sk_buff
        ↓
Ethernet Header
        ↓
Source MAC
        ↓
MAC Blacklist
        ↓
NF_ACCEPT / NF_DROP
        ↓
İLK FIREWALL KERNEL MODULE

seviyesine gelindi.

27. Sonraki Çalışma Rotası

Bir sonraki çalışma gününde doğrudan mevcut mac_firewall üzerinden devam edilecek:

Mevcut tek blocked_mac
        ↓
MAC blacklist array
        ↓
birden fazla MAC desteği
        ↓
blacklist traversal
        ↓
interface ayrımı
        ↓
daha kontrollü logging
        ↓
TCP / UDP header parsing
        ↓
IP / port filtering
        ↓
state/rule mantığı
        ↓
gerçek firewall testi

Daha sonra firewall tarafı TCP bilgisiyle birleştirilebilir:

Ethernet
   ↓
MAC rule
   ↓
IPv4
   ↓
Source / Destination IP
   ↓
TCP / UDP
   ↓
Source / Destination Port
   ↓
Firewall Rule
   ↓
ACCEPT / DROP

Böylece yalnız "şu MAC'i engelle" seviyesindeki bir örnekten, Ethernet → IP → TCP/UDP katmanlarını okuyabilen küçük bir kernel packet-filtering firewall seviyesine geçilebilir.

Gün Sonu Değerlendirmesi

On sekizinci günün en önemli kazanımı, TCP performans teorisinin gerçek Linux state'iyle ve daha önce öğrenilen network-driver packet path'inin Netfilter ile birleştirilmesi oldu. ss -tin çıktısındaki cwnd, RTT, RTO, MSS, snd_wnd, retransmission, loss ve backoff gibi değerler artık yalnız isim olarak değil, TCP sender'ın gerçek çalışma state'inin parçaları olarak okunabilir hale geldi.

Aynı zamanda çalışma odağı TCP'nin kendi davranışından kernel'in packet'ın geçişine müdahale edebildiği noktaya taşındı. PCI/NIC → DMA → IRQ → NAPI → skb zincirinde daha önce oluşturulan packet artık Netfilter hook'una kadar takip edildi ve skb üzerinden Ethernet header'a erişilerek source MAC'e göre NF_ACCEPT/NF_DROP kararı verecek ilk firewall mimarisi oluşturuldu.

Günün sonunda ulaşılan seviye:

Network Driver
      +
TCP Internal State
      +
Traffic Measurement
      +
Netfilter
      +
Packet Parsing
      +
MAC Filtering
      │
      ▼
LINUX NETWORKING
UÇTAN UCA SİSTEM MODELİ

oldu.