STAJ RAPORU – 17. GÜN

Linux Networking – TCP Congestion Window, Congestion Control, Slow Start, ssthresh, Congestion Avoidance, BDP, Window Scaling ve TCP Accelerator'a Giriş

On altıncı gün sonunda non-blocking epoll TCP server tamamlanmış; TCP'nin güvenilir iletim mekanizması Sequence Number, ACK, retransmission, SACK ve FIN üzerinden incelenmişti. Günün sonunda TCP Window konusuna geçilmiş; rwnd, Sliding Window, Zero Window, Window Update ve Zero Window Probe/Persist mekanizmalarına kadar gelinmişti. Bir sonraki çalışma konusu olarak cwnd, Congestion Control, Window Scaling, BDP ve yüksek RTT bağlantıları belirlenmişti.

Bugünkü çalışmada doğrudan bu noktadan devam edilerek TCP'nin yalnız receiver kapasitesini değil, network kapasitesini de nasıl dikkate aldığı incelendi. cwnd ile rwnd arasındaki fark netleştirildi; Slow Start, ssthresh, Congestion Avoidance ve kayıp durumunda congestion-control tepkisi ele alındı. Daha sonra Bandwidth-Delay Product hesaplanarak yüksek bandwidth/yüksek RTT bağlantılarında büyük TCP window ihtiyacının nedeni incelendi. TCP Window Scaling ile 16-bit Window alanının nasıl daha büyük receive window'ları temsil edebildiği görüldü. Son bölümde bütün kavramlar hocanın uydu haberleşmesi / TCP Window Accelerator ödeviyle ilişkilendirildi ve Linux üzerinde ss/tcpdump ile gerçek connection'larda hangi değerlerin gözlemlenebileceği ele alındı.

1. rwnd ve cwnd Ayrımının Tamamlanması

Önceki gün sender'ın gönderimini etkileyen iki pencere olduğu görülmüştü:

                    TCP SENDER
                        │
              ┌─────────┴─────────┐
              │                   │
             rwnd                cwnd
              │                   │
       Receive Window      Congestion Window
              │                   │
        Receiver sınırı      Network sınırı
              │                   │
        Flow Control      Congestion Control

Bugün özellikle cwnd tarafına geçildi.

rwnd

Receiver tarafından belirlenir.

Temel amacı:

Sender'ın receiver'ı
aşırı veriyle doldurmasını önlemek

Receiver'ın socket receive buffer durumuyla ilişkilidir ve advertised Window üzerinden karşı tarafa bildirilir.

cwnd

Sender tarafından tutulur.

Temel amacı:

Sender'ın network'e
aşırı miktarda in-flight data
çıkarmasını önlemek

cwnd, TCP header içerisinde karşı tarafa gönderilen bir alan değildir.

Bu nedenle önemli ayrım:

TCP packet içerisindeki Window
        ↓
advertised receive window / rwnd tarafı


Sender kernel içerisindeki cwnd
        ↓
congestion-control state

olarak oluşturuldu.

2. Effective Send Window

Sender'ın receiver açısından çok fazla veri göndermemesi:

rwnd

ile;

network açısından fazla agresif davranmaması:

cwnd

ile kontrol edilir.

Kavramsal sınır:

min(rwnd, cwnd)

olarak ele alındı.

Örneğin:

rwnd = 100 KB
cwnd = 10 KB

ise receiver 100 KB kabul edebilecek durumda olsa bile congestion-control sınırı nedeniyle sender yaklaşık 10 KB'lık sınır tarafından kısıtlanır.

Tersi:

rwnd = 10 KB
cwnd = 100 KB

olduğunda network daha fazlasına izin verebilecek durumda olsa bile receiver flow-control sınırı belirleyici olur.

Mevcut in-flight data da dikkate alındığında zihinsel model:

              min(rwnd, cwnd)
                     │
                     ▼
             gönderim sınırı
                     │
               - in-flight
                     │
                     ▼
          yeni gönderilebilecek veri

şeklinde oluşturuldu.

3. Congestion Window ve In-Flight Data

cwnd'nin gönderilmiş fakat henüz ACK alınmamış veri miktarıyla ilişkisi incelendi.

Örneğin:

cwnd = 6000 byte

ve:

SEQ 0-1499
SEQ 1500-2999
SEQ 3000-4499
SEQ 4500-5999

gönderilmiş fakat henüz ACK alınmamışsa:

in-flight = 6000 byte
cwnd      = 6000 byte

olduğundan sender'ın yeni veri gönderebileceği congestion-window alanı kalmamıştır.

ACK geldikçe eski veriler in-flight durumundan çıkar:

ACK geldi
    ↓
in-flight azalır
    ↓
window içerisinde alan açılır
    ↓
yeni data gönderilebilir

Böylece ACK'in yalnız güvenilirlik mekanizmasının değil, sender'ın gönderim hızının ilerlemesinin de önemli bir parçası olduğu görüldü.

4. Slow Start

Yeni bir TCP connection'ın network kapasitesini başlangıçta kesin olarak bilemeyeceği ele alındı.

Sender doğrudan çok büyük miktarda veriyi network'e basmak yerine congestion window'u kontrollü şekilde büyütür.

Mekanizmayı görmek için basitleştirilmiş olarak:

MSS = 1000 byte
Initial cwnd = 1 MSS

örneği kullanıldı.

Başarılı ACK'lerle:

1 MSS
 ↓
2 MSS
 ↓
4 MSS
 ↓
8 MSS

şeklinde hızlı büyüme incelendi.

Slow Start ismine rağmen büyümenin yaklaşık RTT bazında exponential karakter gösterebildiği görüldü.

Burada ayrıca gerçek modern TCP implementasyonlarının başlangıç cwnd değerinin eğitim örneğindeki 1 MSS olmak zorunda olmadığı not edildi.

5. ssthresh

Slow Start'ın sonsuza kadar exponential şekilde devam edemeyeceği görüldü.

Bu nedenle:

ssthresh
=
Slow Start Threshold

kavramı ele alındı.

Örneğin:

ssthresh = 8 MSS

ise:

1
↓
2
↓
4
↓
8       ← ssthresh

bölgesi Slow Start olarak düşünülür.

Eşiğe ulaşıldıktan sonra daha kontrollü büyüme davranışına geçilir.

6. Congestion Avoidance

ssthresh sonrasında Congestion Avoidance davranışı incelendi.

Klasik zihinsel model:

Slow Start:

1 → 2 → 4 → 8


Congestion Avoidance:

8 → 9 → 10 → 11 → 12 ...

şeklinde ele alındı.

Böylece iki farklı büyüme karakteri ayrıldı:

Slow Start
    ↓
yaklaşık exponential büyüme


Congestion Avoidance
    ↓
daha kontrollü / yaklaşık linear büyüme

Bu davranış klasik TCP congestion-control yaklaşımındaki Additive Increase fikriyle ilişkilendirildi.

7. Packet Loss'un Congestion Control Açısından Anlamı

Önceki gün packet loss:

Duplicate ACK
Fast Retransmit
RTO
Retransmission

açısından incelenmişti.

Bugün aynı olayın congestion-control açısından da anlam taşıdığı görüldü.

PACKET LOSS
     │
     ├── kayıp verinin yeniden gönderilmesi
     │
     └── congestion sinyali olarak değerlendirilmesi
                       │
                       ▼
                  cwnd azaltımı

Dolayısıyla retransmission yalnız:

"Kayıp segmenti tekrar gönder."

değildir.

Aynı zamanda sender:

"Network'e fazla agresif davranıyor
olabilirim."

sonucunu çıkarabilir.

8. Fast Retransmit ile RTO'nun Congestion Açısından Farkı

Duplicate ACK'lerle tespit edilen kayıpla RTO oluşması karşılaştırıldı.

Duplicate ACK / Fast Retransmit

Sonraki segmentlerin receiver'a ulaşması network'ün tamamen durmuş olmadığını gösterir:

segment kayıp
     ↓
sonraki segmentler ulaşıyor
     ↓
Duplicate ACK
     ↓
Fast Retransmit
RTO

ACK ilerlemesinin beklenen süre içerisinde gerçekleşmemesi daha ciddi bir sinyal olabilir:

DATA
 ↓
ACK yok
 ↓
RTO
 ↓
Retransmission
 ↓
daha temkinli congestion davranışı

Klasik Reno modeli üzerinden kayıp halinde ssthresh ve cwnd azaltımının temel mantığı incelendi; gerçek davranışın kullanılan congestion-control algoritmasına göre değişebileceği vurgulandı.

9. AIMD Mantığı

Congestion Avoidance davranışı:

AIMD

kavramıyla ilişkilendirildi.

AI
=
Additive Increase

Başarılı ilerleme oldukça
cwnd kontrollü artırılır.


MD
=
Multiplicative Decrease

Congestion sinyalinde
gönderim agresifliği azaltılır.

Böylece TCP sender'ın network kapasitesini statik olarak bilmediği, sürekli geri bildirim üzerinden kapasiteyi araştırdığı görüldü:

küçük cwnd
    ↓
ACK
    ↓
cwnd büyüt
    ↓
ACK
    ↓
cwnd büyüt
    ↓
LOSS
    ↓
cwnd azalt
    ↓
tekrar kapasiteyi araştır
10. Bandwidth-Delay Product – BDP

Daha sonra yüksek RTT bağlantılarındaki temel performans problemine geçildi.

Formül:

$$ BDP = Bandwidth \times RTT $$

BDP'nin:

Hattın kapasitesini doldurabilmek için yaklaşık ne kadar verinin aynı anda uçuşta olması gerektiğini

gösterdiği incelendi.

Örnek:

Bandwidth = 1 Gbit/s
RTT       = 500 ms

Önce:

$$ 1\,Gbit/s \times 0.5s = 500\,Mbit $$

Bit → Byte dönüşümü:

1 Byte = 8 bit

olduğundan:

$$ 500\,Mbit / 8 = 62.5\,MB $$

elde edildi.

Dolayısıyla:

1 Gbit/s
+
500 ms RTT
        ↓
BDP ≈ 62.5 MB

olarak hesaplandı.

11. Bit ve Byte Ayrımı

BDP hesabı sırasında ağ hızlarının genellikle bit/s cinsinden verilmesi nedeniyle bit/Byte dönüşümü ayrıca netleştirildi.

1 Byte = 8 bit

500 Mbit / 8
=
62.5 MB

Aynı şekilde:

1 Gbit/s
=
125 MB/s

ve:

125 MB/s × 0.5 s
=
62.5 MB

sonucuna ulaşıldı.

b ile B ayrımının network hesaplarında kritik olduğu görüldü:

Mb  → Megabit
MB  → Megabyte

Mbps → Megabit/s
MB/s → Megabyte/s
12. Küçük Window + Yüksek RTT Problemi

Örneğin:

Bandwidth = 1 Gbit/s
RTT       = 500 ms
Window    = 64 KiB

olduğunda yaklaşık throughput sınırı:

$$ Throughput \lesssim \frac{Window}{RTT} $$

ile ilişkilendirildi.

Yaklaşık:

65536 / 0.5
=
131072 byte/s

≈ 1.05 Mbit/s

elde edilir.

Böylece fiziksel hat:

1 Gbit/s

olmasına rağmen çok küçük bir effective TCP window nedeniyle hattın kapasitesinin çok küçük bir kısmının kullanılabileceği görüldü.

Problem:

küçük window
      ↓
yetersiz in-flight data
      ↓
window dolar
      ↓
sender ACK bekler
      ↓
hat zaman zaman boş kalır
      ↓
throughput düşer

şeklinde açıklandı.

13. RTT'nin BDP Üzerindeki Etkisi

Aynı 1 Gbit/s bağlantı için farklı RTT değerleri karşılaştırıldı:

Bandwidth	RTT	Yaklaşık BDP
1 Gbit/s	1 ms	125 KB
1 Gbit/s	10 ms	1.25 MB
1 Gbit/s	100 ms	12.5 MB
1 Gbit/s	500 ms	62.5 MB
1 Gbit/s	1 s	125 MB

Böylece:

Bandwidth yüksek
       +
RTT yüksek
       ↓
BDP çok büyük
       ↓
çok miktarda in-flight data gerekli

sonucuna ulaşıldı.

Bu durumun uydu haberleşmesinde TCP performansı açısından neden kritik olduğu görüldü.

14. BDP ile rwnd / cwnd İlişkisi

BDP'nin bir TCP window türü olmadığı özellikle ayrıldı.

BDP
 ↓
Hattı doldurmak için gereken
in-flight data hakkında bilgi


rwnd
 ↓
Receiver'ın izin verdiği sınır


cwnd
 ↓
Congestion control'ün izin verdiği sınır

Örneğin:

BDP  = 62.5 MB
rwnd = 100 MB
cwnd = 1 MB

ise büyük rwnd tek başına yeterli değildir.

cwnd darboğaz oluşturabilir.

Tersi:

BDP  = 62.5 MB
rwnd = 1 MB
cwnd = 100 MB

ise receiver flow-control sınırı darboğaz olabilir.

Bu nedenle yüksek BDP'li bağlantıda yalnız TCP header'daki Window alanını büyütmenin bütün performans problemini çözmeyeceği görüldü.

15. TCP Window Scaling

TCP header içerisindeki Window alanının:

16 bit

olduğu hatırlandı.

Ham maksimum değer:

$$ 2^{16}-1=65535 $$

yaklaşık 64 KiB'dir.

Ancak yüksek BDP bağlantılarında onlarca MB receive window gerekebilir.

Bu problem:

TCP Window Scale Option

ile çözülür.

Örneğin:

Window = 65535
Window Scale = 10

ise etkin advertised window yaklaşık:

$$ 65535 \times 2^{10} $$

olarak yorumlanabilir.

Bu da yaklaşık 67 MB büyüklüğünde bir pencereyi temsil edebilir.

16. Window Scale Negotiation

Window Scale bilgisinin normal her TCP paketinde tekrar tekrar gönderilmediği; connection kurulurken SYN aşamasında negotiate edildiği incelendi.

CLIENT                               SERVER

SYN
wscale = X ------------------------->

                    <--------------- SYN-ACK
                                      wscale = Y

ACK -------------------------------->

İki yönün scale değerlerinin bağımsız olabileceği görüldü.

Çünkü:

A'nın receive kapasitesi
        ≠
B'nin receive kapasitesi

olabilir.

17. Linux'ta Gerçek TCP State'inin İncelenmesi

Teorik kavramların Linux üzerinde nerede görülebileceğine geçildi.

Bir established TCP connection için:

ss -tin

komutuyla benzer bilgiler görülebileceği incelendi:

cubic
wscale:...
rto:...
rtt:...
cwnd:...

Bunlar öğrenilen kavramlarla ilişkilendirildi:

rtt
 ↓
Round Trip Time

rto
 ↓
Retransmission Timeout

cwnd
 ↓
Congestion Window

wscale
 ↓
Window Scaling

cubic
 ↓
Congestion-control algorithm
18. CUBIC ile Eğitimde Kullanılan Reno Modelinin Ayrımı

Bugüne kadar congestion-control davranışı anlaşılır olması için büyük ölçüde klasik Reno tarzı model üzerinden incelendi.

Ancak Linux'ta:

sysctl net.ipv4.tcp_congestion_control

ile gerçek kullanılan congestion-control algoritmasının kontrol edilebileceği görüldü.

Dolayısıyla:

Teorik eğitim modeli
        ↓
Reno / AIMD mantığı

Gerçek Linux sistemi
        ↓
örneğin CUBIC

ayrımı yapıldı.

cwnd'nin gerçek büyüme ve kayıp tepkisinin kullanılan congestion-control algoritmasına göre değişebileceği not edildi.

19. tcpdump ile TCP Options

Gerçek connection'ın SYN paketleri:

sudo tcpdump -i <interface> -nn -vvv tcp port 5001

ile incelenebilecek şekilde ele alındı.

SYN içerisinde örneğin:

MSS
SACK Permitted
Window Scale

gibi TCP option'larının görülebileceği açıklandı.

Böylece daha önce teorik olarak öğrenilen:

MSS
SACK
Window Scaling

mekanizmalarının gerçek packet üzerinde nerede bulunduğu ilişkilendirildi.

20. tcpdump ile ss Arasındaki Kritik Fark

Önemli bir ayrım oluşturuldu:

tcpdump
   ↓
wire üzerindeki TCP packet'larını görür
   ↓
SEQ
ACK
advertised Window
TCP Options
...

Buna karşılık:

ss -ti
   ↓
local Linux TCP state'inden
bilgiler gösterebilir
   ↓
cwnd
RTT
RTO
...

Dolayısıyla packet capture içerisinde görülen:

win ...

değeri:

cwnd

değildir.

cwnd sender'ın local congestion-control state'idir.

21. ACK Number ile Window Alanının Yönsel Anlamı

Tek TCP segment içerisinde:

SEQ
ACK
Window

alanlarının farklı görevleri tekrar birleştirildi.

Örneğin B → A segmentinde:

ACK = 5000
Window = 10000

varsa:

ACK=5000
 ↓
B'nin, A'nın gönderdiği stream hakkında
verdiği bilgi


Window=10000
 ↓
B'nin kendi receive kapasitesi hakkında
A'ya verdiği bilgi

olarak yorumlandı.

Bu ayrım TCP'nin full-duplex yapısıyla ilişkilendirildi.

22. TCP Window Accelerator Problemine Giriş

Günün sonunda hocanın uydu haberleşmesi için verdiği TCP Window Accelerator problemi tekrar ele alındı.

Önce temel soru ortaya çıkarıldı:

"Window'u maksimum tutacağız" derken hangi window?

Çünkü:

                    WINDOW
                      │
            ┌─────────┴─────────┐
            │                   │
           rwnd                cwnd
            │                   │
     Flow Control       Congestion Control
            │                   │
    TCP header'da       Sender'ın local
      advertised          TCP state'i

Bir cihaz yalnız TCP header'daki Window değerini değiştiriyorsa doğrudan cwnd'yi değiştirmiş olmaz.

Ayrıca receiver gerçekte küçük bir buffer alanına sahipken advertised window'u yapay olarak büyütmenin flow-control mekanizmasını bozabileceği görüldü.

23. Accelerator İçin PEP / Split-TCP Fikri

Uydu bağlantısındaki yüksek RTT probleminin yalnız Window header değerini değiştirerek çözülmesinin yetersiz olabileceği değerlendirildi.

Alternatif olarak bağlantının accelerator/proxy tarafından bölündüğü mimariye giriş yapıldı:

Sender
   │
   │ TCP
   ▼
Accelerator
   │
   │
   │ High RTT / Satellite
   │
   ▼
Accelerator
   │
   │ TCP
   ▼
Receiver

Bu yaklaşım Performance Enhancing Proxy (PEP) / split-TCP fikriyle ilişkilendirildi.

Böyle bir cihazın yalnız packet header değiştirmek yerine:

buffering
ACK davranışı
window yönetimi
connection state
congestion control
yüksek RTT link optimizasyonu

gibi daha geniş sorumluluklara sahip olabileceği görüldü.

Bu konu henüz implementasyon aşamasına geçirilmedi; bugün mimari problem tanımlandı.

24. Önceki Driver Çalışmalarıyla Bugünkü Konuların Birleştirilmesi

On altıncı gün raporunda driver'dan application'a kadar kurulan NIC → DMA/IRQ/NAPI → TCP → socket → userspace zinciri bulunuyordu.

Bugün sender tarafındaki TCP state daha ayrıntılı hale getirildi:

                       APPLICATION
                            │
                          socket
                            │
                            ▼
                           TCP
                            │
             ┌──────────────┼──────────────┐
             │              │              │
            rwnd           cwnd           RTT
             │              │              │
       Flow Control    Congestion      RTO hesabı /
                       Control         feedback
             │              │              │
             └──────────────┼──────────────┘
                            │
                         in-flight
                            │
                            ▼
                            IP
                            │
                         Ethernet
                            │
                           skb
                            │
                     Network Driver
                            │
                     TX Descriptor
                            │
                           DMA
                            │
                           NIC
                            │
                           WIRE

Böylece daha önce driver seviyesinde öğrenilen TX/RX mekanizmalarıyla TCP congestion-control mekanizmaları aynı uçtan uca sistem içerisinde birleştirildi.

25. Bugün Oluşan Büyük TCP Modeli

Bugünkü çalışma sonunda TCP performans tarafı şu bütünlüğe ulaştı:

                         TCP
                          │
        ┌─────────────────┼──────────────────┐
        │                 │                  │
        ▼                 ▼                  ▼
  RELIABILITY        FLOW CONTROL      CONGESTION CONTROL
        │                 │                  │
       SEQ               rwnd               cwnd
        │                 │                  │
       ACK          Receive Buffer      Slow Start
        │                 │                  │
      SACK          Window Update       ssthresh
        │                 │                  │
Fast Retransmit      Zero Window    Congestion Avoidance
        │                 │                  │
       RTO             Persist             Loss
        │                                    │
 Retransmission                           cwnd ↓
                                             │
                                             ▼
                                       Network Capacity
                                             │
                                             ▼
                                             BDP
                                             │
                                  Bandwidth × RTT
                                             │
                                             ▼
                                      High RTT Link
                                             │
                                             ▼
                                      TCP ACCELERATOR
26. Sonraki Çalışma Rotası

Bugün teorik olarak accelerator'a kadar ulaşıldı. Bir sonraki aşamada önce mevcut Linux TCP bağlantısı üzerinde gerçek değerlerin gözlemlenmesi planlandı:

TCP SERVER / CLIENT
        ↓
tcpdump
        ↓
SYN / SYN-ACK
        ↓
MSS / SACK / Window Scale
        ↓
SEQ / ACK / advertised Window
        ↓
ss -ti
        ↓
RTT / RTO / cwnd
        ↓
iperf3
        ↓
RTT yapay olarak artırılacak
        ↓
Throughput ölçülecek
        ↓
BDP hesaplanacak
        ↓
Window darboğazı gözlemlenecek
        ↓
TCP ACCELERATOR MİMARİSİ
        │
        ├── Window manipulation
        ├── Transparent Proxy / PEP
        └── Split-TCP

Bundan sonra accelerator tasarımı tamamlandığında hocanın diğer ödev başlığına geçilecek:

Netfilter
    ↓
Firewall
    ↓
Packet Filtering
    ↓
MAC Address Filtering

Bu başlıklar önceki günün ileri çalışma planında da accelerator sonrasındaki çalışma olarak belirlenmişti.

Gün Sonu Değerlendirmesi

On yedinci gün sonunda TCP Window konusu receiver tarafındaki rwnd seviyesinden sender ve network tarafındaki cwnd seviyesine taşındı. cwnd'nin TCP header'da gönderilen bir Window alanı olmadığı, sender'ın congestion-control state'i olduğu öğrenildi. rwnd ile cwnd'nin farklı problemleri çözdüğü ve sender'ın gönderiminin bu iki sınırın birlikte değerlendirilmesiyle kontrol edildiği pekiştirildi.

Congestion-control tarafında Slow Start, ssthresh, Congestion Avoidance ve loss reaction mekanizmaları incelendi. Duplicate ACK/Fast Retransmit ve RTO gibi daha önce güvenilirlik açısından öğrenilen mekanizmaların aynı zamanda congestion-control açısından network hakkında geri bildirim sağladığı görüldü.

Ardından Bandwidth-Delay Product ile bandwidth ve RTT'nin TCP throughput üzerindeki etkisi matematiksel olarak incelendi. 1 Gbit/s + 500 ms RTT örneğinde BDP'nin yaklaşık 62.5 MB olduğu hesaplandı. Böylece yüksek bandwidth fakat yüksek RTT'li bir bağlantıda onlarca MB verinin aynı anda in-flight tutulabilmesinin neden gerekli olabileceği anlaşıldı.

TCP Window alanının 16-bit olması nedeniyle ortaya çıkan sınır Window Scaling ile ilişkilendirildi. Window Scale'ın SYN sırasında negotiate edildiği ve büyük advertised receive window değerlerinin bu mekanizmayla temsil edilebildiği öğrenildi.

Son olarak teorik bilgiler Linux'taki gerçek TCP state'iyle birleştirildi. tcpdump ile packet üzerinde advertised Window, ACK, Sequence Number, MSS, SACK ve Window Scale gibi bilgilerin; ss -ti ile ise local TCP state'ine ait cwnd, RTT ve RTO gibi bilgilerin incelenebileceği görüldü.

Genel ilerleme:

16. GÜN
────────────────────────────────

Epoll TCP Server
        ↓
TCP Byte Stream
        ↓
SEQ / ACK
        ↓
RTT / RTO
        ↓
Retransmission
        ↓
SACK
        ↓
FIN / Half-Close
        ↓
Sliding Window
        ↓
rwnd
        ↓
Zero Window / Persist


                 │
                 ▼


17. GÜN
────────────────────────────────

rwnd ↔ cwnd ayrımı
        ↓
Congestion Window
        ↓
In-Flight Data
        ↓
Slow Start
        ↓
ssthresh
        ↓
Congestion Avoidance
        ↓
Loss Reaction
        ↓
AIMD
        ↓
Bandwidth-Delay Product
        ↓
bit ↔ Byte dönüşümü
        ↓
High Bandwidth + High RTT
        ↓
Window Scaling
        ↓
Linux TCP State
        ↓
tcpdump ↔ ss -ti
        ↓
Satellite / High RTT
        ↓
TCP WINDOW ACCELERATOR
        ●
        │
        ▼
SIRADAKİ:
Gerçek bağlantıda ölçüm
        ↓
RTT'yi yapay artırma
        ↓
BDP / Throughput deneyi
        ↓
PEP / Split-TCP tasarımı
        ↓
Accelerator implementasyonu
        ↓
Netfilter Firewall
        ↓
MAC Filtering

Böylece 17. gün sonunda çalışma odağı “TCP receiver ve sender ne kadar veri gönderilmesine izin veriyor?” seviyesinden, “bandwidth ve RTT nedeniyle hattı doldurmak için ne kadar verinin uçuşta olması gerekir ve bir TCP accelerator bu davranışa nereden müdahale edebilir?” seviyesine taşınmış oldu.