STAJ RAPORU – 16. GÜN

Linux Networking – Epoll TCP Server, TCP ACK/Sequence Number, Retransmission, SACK, FIN ve TCP Receive Window

On beşinci gün sonunda blocking/non-blocking TCP server mimarileri, select/poll/epoll, TCP 4-tuple, yüksek connection sayıları, SYN/accept queue ve file descriptor limitleri incelenmiş; non-blocking epoll server implementasyonuna başlanmıştı.

On altıncı gün çalışmalarında önce epoll tabanlı TCP server tamamlandı. Ardından TCP'nin güvenilir veri aktarım mekanizmasına geçilerek Sequence Number, ACK, cumulative ACK, duplicate ACK, RTT, RTO, retransmission, Fast Retransmit ve SACK kavramları incelendi. TCP connection'ın FIN ile kapatılması ve half-close davranışı ele alındı. Günün son bölümünde TCP Window Size, Sliding Window ve Receive Window (rwnd) mekanizmasına giriş yapılarak Zero Window ve window update davranışı incelendi.

1. Epoll TCP Server'ın Tamamlanması

Önceki gün oluşturulan:

socket()
   ↓
bind()
   ↓
listen()
   ↓
O_NONBLOCK
   ↓
epoll_create1()
   ↓
epoll_ctl()
   ↓
epoll_wait()

yapısı tamamlandı.

Server'ın temel çalışma modeli:

                        EPOLL SERVER
                             │
                        listen_fd
                             │
                       epoll_wait()
                             │
                 ┌───────────┴───────────┐
                 │                       │
          listen_fd ready          client_fd ready
                 │                       │
              accept()                  read()
                 │                       │
          new client_fd                 data
                 │                       │
          O_NONBLOCK                    write()
                 │
          epoll_ctl(ADD)
                 │
                 └───────────┐
                             ↓
                       epoll_wait()

şeklinde oluşturuldu.

Listening socket üzerinde EPOLLIN oluşmasının application data geldiği anlamına gelmediği; kabul edilmeye hazır yeni bir TCP connection bulunduğunu gösterdiği incelendi.

accept() sonrasında her connection için yeni bir client_fd elde edildi ve bu descriptor da non-blocking hale getirilerek epoll instance'ına eklendi.

2. Listening Socket ile Client Socket Ayrımı

Server tarafındaki yapı:

listen_fd
    │
    ├── client_fd 5
    ├── client_fd 6
    ├── client_fd 7
    └── ...

şeklinde ele alındı.

listen_fd yeni connection'ları kabul etmek için kullanılırken her client_fd belirli bir established TCP connection'ı temsil eder.

Böylece tek bir server portunun çok sayıda eşzamanlı connection yönetebilmesinin uygulama tarafındaki karşılığı görüldü.

3. Epoll Event Loop

Server'ın sürekli bütün socket'leri taraması yerine:

100.000 socket
      │
      ▼
 epoll_wait()
      │
      ▼
READY EVENTS
      │
  ┌───┴───┐
  ▼       ▼
fd 125   fd 8200

şeklinde yalnız hazır socket'lerle ilgilenebileceği incelendi.

Bu yaklaşımın yüksek connection concurrency için thread-per-connection modeline göre neden önemli olduğu pekiştirildi.

4. Non-Blocking read() Davranışı

Client socket üzerinde read() sonucunun üç temel anlamı incelendi:

read() > 0
    ↓
DATA ALINDI


read() == 0
    ↓
Peer orderly shutdown
    ↓
FIN / EOF


read() < 0
errno = EAGAIN/EWOULDBLOCK
    ↓
Şu anda okunacak data yok

Non-blocking socket'te EAGAIN oluşmasının connection hatası olmadığı, uygulamanın bloklanmadan event loop'a devam etmesini sağladığı görüldü.

5. Non-Blocking write() ve Partial Write

Non-blocking socket üzerinde:

write(fd, buffer, 4096)

çağrısının her zaman 4096 byte'ın tamamını gönderemeyebileceği incelendi.

Örneğin:

Gönderilecek = 4096 byte

write()
   ↓
1200 byte yazıldı

Kalan
   ↓
2896 byte

Gerçek yüksek performanslı server tasarımında kalan verinin connection'a ait output buffer içerisinde tutulması ve socket tekrar writable olduğunda EPOLLOUT üzerinden gönderime devam edilmesi gerektiği ele alındı.

TCP GÜVENİLİR İLETİM
6. TCP'nin Byte Stream Olması

TCP'nin message/packet tabanlı değil byte-stream protokolü olduğu incelendi.

Örneğin application:

write(..., 1500)

yapsa bile receiver:

read() → 500
read() → 700
read() → 300

şeklinde okuyabilir.

Dolayısıyla:

bir write()
     ≠
bir read()
     ≠
bir TCP segment

sonucuna ulaşıldı.

TCP application mesajlarının nerede başlayıp bittiğini korumaz.

Mesaj sınırları gerekiyorsa application protocol tarafından:

Length Prefix
Delimiter
Fixed Length
Connection Close

gibi yöntemlerle framing yapılması gerekir.

7. TCP Sequence Number

TCP'nin byte stream içerisindeki verileri Sequence Number ile takip ettiği incelendi.

Örneğin:

SEQ = 0
LEN = 1000

taşınan byte'lar:

0 ... 999

Receiver bu veriyi eksiksiz aldığında:

ACK = 1000

gönderir.

Bunun anlamı:

0–999 arasını aldım; sıradaki beklediğim sequence position 1000.

şeklindedir.

8. Cumulative ACK

TCP ACK'in temel olarak cumulative çalıştığı incelendi.

Örneğin:

SEQ 1000 → GELDİ
SEQ 2000 → KAYIP
SEQ 3000 → GELDİ
SEQ 4000 → GELDİ

receiver hâlâ:

ACK = 2000

gönderebilir.

Çünkü kesintisiz byte stream 2000 noktasında bozulmuştur.

Dolayısıyla:

ACK=2000

şu anlama gelir:

Sıradaki kesintisiz beklediğim byte 2000.

9. Duplicate ACK

Eksik segmentten sonraki segmentler receiver'a ulaştığında aynı ACK numarası tekrar gönderilebilir:

ACK=2000
ACK=2000
ACK=2000
ACK=2000

Bunlar Duplicate ACK olarak ele alındı.

Sender, sonraki segmentlerin ulaştığı fakat arada bir eksiklik bulunduğu yönünde bilgi çıkarabilir.

10. RTT

RTT – Round Trip Time, bir verinin gönderilmesi ile buna ilişkin geri dönüşün alınması arasındaki gidiş-dönüş süresi olarak incelendi.

Sender                       Receiver

DATA ----------------------->
     <----------------------- ACK

|<---------- RTT ---------->|

RTT özellikle yüksek gecikmeli ağlarda TCP performansı açısından kritik hale gelir.

11. RTO – Retransmission Timeout

TCP'nin ACK için sabit bir timeout kullanmadığı; RTT ölçümlerinden yararlanarak retransmission timeout hesapladığı incelendi.

Temel kavramlar:

RTT
 ↓
SRTT
 ↓
RTTVAR
 ↓
RTO

Kavramsal ilişki:

RTO ≈ SRTT + 4 × RTTVAR

olarak ele alındı.

RTO dolduğu halde gerekli ACK alınmamışsa TCP retransmission gerçekleştirebilir.

12. Exponential Backoff

Tekrarlanan timeout durumlarında TCP'nin sürekli aynı hızda retransmission yapmadığı incelendi.

Kavramsal olarak:

RTO
 ↓
timeout
 ↓
2 × RTO
 ↓
timeout
 ↓
4 × RTO
 ↓
...

şeklinde bekleme süresinin artırılması network congestion'ın daha da kötüleşmesini önlemeye yardımcı olur.

13. Fast Retransmit

TCP'nin her kayıp durumunda RTO'nun dolmasını beklemek zorunda olmadığı incelendi.

Yeterli duplicate ACK kayıp olduğuna dair güçlü bir sinyal oluşturabilir:

Duplicate ACK
     ↓
Duplicate ACK
     ↓
Duplicate ACK
     ↓
Kayıp şüphesi
     ↓
FAST RETRANSMIT
     ↓
RTO beklenmeden retransmission

Önemli ayrım:

3 duplicate ACK
        ≠
3 packet retransmit

Duplicate ACK'ler kayıp tespiti için sinyaldir.

14. SACK – Selective Acknowledgment

Cumulative ACK'in hangi sonraki byte bloklarının gerçekten ulaştığını tek başına söylemediği görüldü.

Örneğin:

1000-1999    GELDİ
2000-2999    KAYIP
3000-3999    GELDİ
4000-4999    GELDİ
5000-5999    GELDİ

Cumulative ACK:

ACK = 2000

iken SACK ile ek olarak:

SACK = 3000-6000

bilgisi taşınabilir.

Böylece sender:

2000-2999 → eksik

3000-5999 → receiver'da mevcut

bilgisini daha açık şekilde elde edebilir.

TCP CONNECTION KAPATMA
15. FIN Mekanizması

TCP'nin full-duplex olduğu ve FIN'in connection'ın iki yönünü aynı anda kapatmadığı incelendi.

A tarafı FIN gönderirse:

A                                      B

FIN ---------------------------------->
                    <----------------- ACK

sonrasında:

A → B application DATA    KAPALI
B → A application DATA    AÇIK

olabilir.

Bu durum half-close olarak ele alındı.

16. FIN Sonrasında ACK Trafiği

FIN gönderen tarafın artık yeni application data gönderememesine rağmen karşı taraftan gelen verilere TCP ACK göndermeye devam edebileceği incelendi.

A                                      B

FIN ---------------------------------->
                    <----------------- ACK

                    <----------------- DATA
ACK --------------------------------->

                    <----------------- DATA
ACK --------------------------------->

Dolayısıyla:

FIN
=
"Bu yöndeki application byte stream'im bitti."

anlamına gelir.

TCP kontrol trafiğinin tamamen durması anlamına gelmez.

17. FIN Retransmission

FIN'in de TCP sequence number uzayında yer aldığı ve güvenilir şekilde iletilmesi gerektiği incelendi.

Örneğin:

FIN SEQ=1500 ------------------------>

                    <---------------- ACK=1501
                              X
                         ACK kayboldu

RTO
 ↓

FIN SEQ=1500 ------------------------>
                    <---------------- ACK=1501

FIN'in ACK'i kaybolursa FIN yeniden iletilebilir.

Receiver aynı sequence number'a sahip FIN'i tekrar gördüğünde gerekli ACK'i yeniden gönderebilir.

TCP WINDOW
18. TCP Window Size Kavramına Giriş

TCP'nin her segmentten sonra ACK bekleyerek çalışmasının yüksek RTT bağlantılarında verimsiz olacağı incelendi.

Stop-and-wait benzeri bir yapı:

DATA -------->
     <-------- ACK

DATA -------->
     <-------- ACK

yerine TCP:

DATA -------->
DATA -------->
DATA -------->
DATA -------->
     <-------- ACK

şeklinde birden fazla veriyi ACK gelmeden uçuşta tutabilir.

Gönderilmiş fakat henüz ACK'lenmemiş veriye:

in-flight data

denildi.

19. Sliding Window

ACK geldikçe gönderilebilir sequence-number aralığının ileri hareket etmesi incelendi.

Örneğin:

Başlangıç:

[ 0 ---------------- 5999 ]


ACK=1500


Sonra:

       [ 1500 ---------------- 7499 ]
                →

Pencerenin bu şekilde ilerlemesi nedeniyle yapı Sliding Window olarak adlandırılır.

20. rwnd ve cwnd Ayrımı

TCP sender'ın gönderimini sınırlayan iki farklı mekanizma olduğu görüldü:

                 TCP SENDER
                     │
          ┌──────────┴──────────┐
          │                     │
         rwnd                  cwnd
          │                     │
 Receive Window         Congestion Window
          │                     │
 Receiver sınırı         Network sınırı
          │                     │
   Flow Control       Congestion Control

Kavramsal gönderim sınırı:

min(rwnd, cwnd)

olarak ele alındı.

Bu gün özellikle rwnd üzerinde duruldu; cwnd sonraki çalışmaya bırakıldı.

21. Receive Window – rwnd

Receiver'ın TCP receive buffer kapasitesinin sender'ı kontrol etmek için kullanılabileceği incelendi.

Temel veri yolu:

NIC
 ↓
Driver / NAPI
 ↓
skb
 ↓
IP
 ↓
TCP
 ↓
Socket Receive Buffer
 ↓
read()
 ↓
Userspace

Application yeterince hızlı read() yapmazsa receive buffer dolabilir.

Receive Buffer

BOŞ
 ↓
DATA
 ↓
DATA
 ↓
DATA
 ↓
DOLU

Bu durumda receiver'ın ilan ettiği receive window küçülebilir.

22. ACK ile Window Alanının Farkı

TCP header içerisinde ACK Number ile Window alanının farklı bilgiler taşıdığı incelendi.

Örneğin:

ACK = 1500
Window = 500

şu şekilde yorumlandı:

ACK=1500
   ↓
"Kesintisiz olarak sıradaki beklediğim
sequence position 1500."


Window=500
   ↓
"İlan ettiğim receive window
şu anda 500 byte."

Dolayısıyla:

ACK Number
     ≠
Window Size

ayrımı netleştirildi.

23. Zero Window

Receiver'ın receive tarafında yeni veri için alan ilan edemediği durumda:

ACK=1500
Window=0

gönderebileceği incelendi.

Bu:

Connection'ı kapat.

anlamına gelmez.

Şu anlama gelir:

Normal yeni veri gönderimini şimdilik durdur; receive window'um kapalı.

Dolayısıyla:

Window=0
   ≠
FIN
24. Window Update

Receiver application daha sonra:

read()

yapıp buffer'dan veri tüketirse tekrar alan oluşabilir.

Örneğin:

ÖNCE

ACK=1500
Window=0


Application read()
        ↓
500 byte alan açıldı


SONRA

ACK=1500
Window=500

Burada ACK numarası değişmemiştir çünkü yeni TCP data alınmamıştır.

Ancak advertised window değişmiştir:

Window
0 → 500

Receiver yeni window bilgisini sender'a bildirebilir ve veri akışı yeniden başlayabilir.

25. Zero Window Probe ve Persist Mekanizması

Window update segmentinin kaybolması halinde oluşabilecek problem incelendi.

Receiver:

Window 0 → 500
      ↓
Window Update
      ↓
      X
   KAYBOLDU

Sender'ın elindeki son bilgi:

rwnd = 0

olarak kalabilir.

Sender hiçbir şey yapmasa iki taraf uzun süre bekleyebilirdi.

Bu nedenle TCP'deki persist mechanism / zero-window probe kavramına giriş yapıldı.

Sender
  │
Window=0
  │
normal gönderim durdu
  │
Persist mechanism
  │
Zero Window Probe
  │
  └────────────────────> Receiver
                          │
                    güncel window
                          │
       <──────────────────┘

Böylece sender receiver'ın window durumunu yeniden öğrenebilir.

26. Bugün Öğrenilen TCP Mekanizmalarının Birbirine Bağlanması

Günün sonunda TCP tarafında şu bütünlük oluşturuldu:

                     TCP
                      │
        ┌─────────────┴──────────────┐
        │                            │
   RELIABILITY                  FLOW CONTROL
        │                            │
        ▼                            ▼
 Sequence Number                   rwnd
        │                            │
       ACK                     Receive Buffer
        │                            │
 Duplicate ACK                 Window Update
        │                            │
 Fast Retransmit               Zero Window
        │                            │
       SACK                    Persist Timer
        │                            │
       RTO                  Zero Window Probe
        │
 Retransmission
        │
       FIN
        │
 FIN Retransmission
27. Önceki Driver Çalışmalarıyla Bağlantı

Bugünkü TCP mekanizmalarının daha önce oluşturulan NIC/driver modelinin üzerinde çalıştığı tekrar ilişkilendirildi.

Önceki raporda kurulan uçtan uca yapı NIC → driver → skb → TCP → socket → userspace seviyesine kadar getirilmişti.

Bugün bunun TCP içerisindeki kısmı açıldı:

                       APPLICATION
                            │
                      epoll server
                            │
                         socket
                            │
                 ┌──────────┴──────────┐
                 │                     │
              send side            receive side
                 │                     │
                TCP                   TCP
                 │                     │
          SEQ / retransmit       ACK / rwnd
                 │                     │
                 └──────────┬──────────┘
                            │
                           IP
                            │
                         Ethernet
                            │
                           skb
                            │
                      Network Driver
                            │
                  DMA / IRQ / NAPI
                            │
                           NIC

Böylece düşük seviyede öğrenilen driver mekanizmaları ile TCP'nin transport-layer mekanizmaları aynı sistem üzerinde birleştirildi.

28. Sonraki Çalışma Konuları

Bugünün sonunda rwnd ve Zero Window mekanizmasına kadar gelindi.

Sonraki çalışma sırası:

TCP WINDOW
│
├── rwnd                         ✓
├── Zero Window                  ✓
├── Window Update                ✓
├── Zero Window Probe            ✓
│
├── cwnd                         ← SIRADAKİ
│
├── Congestion Control
│   ├── Slow Start
│   ├── Congestion Avoidance
│   └── loss reaction
│
├── Effective Send Window
│   └── min(rwnd, cwnd)
│
├── Window Scaling
│
├── Bandwidth-Delay Product
│
└── Satellite / High RTT
        │
        ▼
   TCP WINDOW ACCELERATOR

Bunlardan sonra hocanın diğer ödev başlığına:

Netfilter
   ↓
Firewall
   ↓
Packet Filtering
   ↓
MAC Address Filtering

geçilecek. Bu başlıklar da önceki günün ileri çalışma planında belirlenmişti.

Gün Sonu Değerlendirmesi

On altıncı gün sonunda önceki gün başlatılan non-blocking epoll TCP server implementasyonu tamamlandı. Listening socket ile connected socket'lerin epoll içerisinde nasıl yönetildiği, EPOLLIN, non-blocking accept/read/write, connection kapanması ve partial write problemi incelendi.

Ardından TCP'nin güvenilir iletim mekanizması ayrıntılı olarak ele alındı. Sequence Number ve cumulative ACK ilişkisinden başlanarak Duplicate ACK, RTT, RTO, exponential backoff, Fast Retransmit ve SACK mekanizmaları birbirleriyle ilişkilendirildi. TCP'nin packet/message değil byte-stream protokolü olması nedeniyle application mesaj sınırlarının TCP tarafından korunmadığı görüldü.

TCP connection kapatma tarafında FIN'in sequence space içerisinde yer aldığı, FIN'in yalnız bir yöndeki application-data stream'ini kapattığı, karşı yöndeki veri ve ACK trafiğinin devam edebildiği ve FIN/ACK kayıplarında retransmission mekanizmasının kullanılabildiği incelendi.

Günün son bölümünde TCP Window Size ve Sliding Window mekanizmasına geçildi. rwnd'nin receiver tarafındaki flow control mekanizması olduğu, ACK Number ile advertised Window değerinin farklı bilgiler taşıdığı, receive buffer dolduğunda Zero Window ilan edilebildiği ve application buffer'ı boşalttığında Window Update gönderilebildiği öğrenildi. Window update'in kaybolması halinde persist/zero-window-probe mekanizmasının bağlantının kalıcı olarak beklemesini engellemedeki rolü incelendi.

Genel ilerleme:

15. GÜN
────────────────────────────────

TCP Server Architecture
        ↓
Blocking / Non-Blocking
        ↓
select / poll / epoll
        ↓
High Connection Concurrency
        ↓
Epoll Server İskeleti


              │
              ▼


16. GÜN
────────────────────────────────

Epoll Server Tamamlandı
        ↓
TCP Byte Stream
        ↓
Sequence Number
        ↓
Cumulative ACK
        ↓
Duplicate ACK
        ↓
RTT / RTO
        ↓
Retransmission
        ↓
Fast Retransmit
        ↓
SACK
        ↓
FIN / Half-Close
        ↓
FIN Retransmission
        ↓
TCP Window Size
        ↓
Sliding Window
        ↓
Receive Window (rwnd)
        ↓
Zero Window
        ↓
Window Update
        ↓
Zero Window Probe / Persist
        ↓
        ●
        │
        ▼
SIRADAKİ: cwnd / Congestion Control
        ↓
Window Scaling
        ↓
BDP
        ↓
Satellite / High RTT
        ↓
TCP WINDOW ACCELERATOR

Böylece on altıncı gün sonunda çalışma odağı “çok sayıda TCP connection'ı userspace'te nasıl yönetiriz?” seviyesinden “TCP bu connection'ların içerisinde verinin güvenilirliğini ve receiver'ın kaldırabileceği veri miktarını nasıl yönetir?” seviyesine taşınmış oldu.