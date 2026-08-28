# STAJ RAPORU – 22. GÜN

## Problem–Mekanizma Eşleştirmesi, Blocking/Non-Blocking I/O, TCP Loss Recovery, High-RTT Problemi, Netfilter Packet Path ve Accelerator–PEP Mimarisinin Derinlemesine İncelenmesi

Yirmi birinci gün sonunda TCP server mimarileri, `epoll`, TCP connection kimliği, ACK/retransmission mekanizmaları, `rwnd/cwnd`, Window Scaling, Bandwidth-Delay Product, Split-TCP/PEP, Netfilter firewall ve C stack yapısı ayrıntılı olarak tekrar edilmişti.

Bugünkü çalışmada yeni bir kod modülü geliştirmek yerine, daha önce öğrenilmiş kavramların **hangi gerçek problemlere çözüm olduğu** üzerinde duruldu. Bir kavramı tanımlayabilmek ile problem verildiğinde doğru mekanizmayı seçebilmenin farklı beceriler olduğu görüldü.

Özellikle:

```text
Problem
   ↓
Problemin sistemde oluşturduğu sonuç
   ↓
İlgili Linux/TCP mekanizması
   ↓
Mekanizmanın problemi nasıl çözdüğü
```

şeklinde bir düşünme yöntemi benimsendi.

Bunun yanında blocking/non-blocking I/O'nun gerçek çalışma modeli, duplicate ACK ve Fast Retransmit ilişkisi, yüksek RTT bağlantılarında window yetersizliği, Netfilter `PRE_ROUTING`, `INPUT`, `FORWARD`, `OUTPUT` ve `POST_ROUTING` packet path'i ile TCP Accelerator ve PEP arasındaki mimari farklar ayrıntılı olarak incelendi.

---

# 1. Kavram Bilgisinden Problem Çözme Bilgisine Geçiş

Bugünkü çalışmanın temel noktalarından biri, daha önce öğrenilmiş birçok mekanizmanın tanımlarının bilinmesine rağmen bu mekanizmaların hangi problemlerin çözümü olduğunun her zaman doğrudan fark edilemediğinin görülmesi oldu.

Örneğin:

```text
Bilinen kavram:

TCP connection
=
Source IP
+
Source Port
+
Destination IP
+
Destination Port
```

yani 4-tuple yapısı daha önce öğrenilmişti.

Ancak soru:

```text
TCP port alanı 16-bit ise
bir server nasıl 65.535'ten
fazla connection yönetebilir?
```

şeklinde sorulduğunda bunun aslında doğrudan **4-tuple bilgisini test eden bir problem** olduğu fark edildi.

Benzer şekilde:

```text
Bilinen:

O_NONBLOCK
EAGAIN
epoll
```

olmasına rağmen:

```text
Bir client data göndermiyorsa
tek thread neden diğer client'ları
işlemeye devam edebilir?
```

sorusunun bu mekanizmalarla çözülmesi gerektiğini tanımak ayrıca değerlendirilmesi gereken bir beceridir.

Bu nedenle çalışma yöntemi:

```text
KAVRAM
   ↓
NE YAPAR?
   ↓
HANGİ PROBLEMİ ÇÖZER?
   ↓
HANGİ ŞARTLARDA KULLANILIR?
   ↓
ALTERNATİFİ NEDİR?
   ↓
TRADE-OFF NEDİR?
```

şeklinde genişletildi.

---

# 2. Blocking I/O'nun Gerçek Çalışma Modelinin Tekrarı

Blocking socket üzerinde:

```c
recv(fd, buffer, sizeof(buffer), 0);
```

çağrısı yapıldığında okunabilecek data bulunmuyorsa thread'in bu I/O işlemini bekleyebileceği tekrar edildi.

Kavramsal olarak:

```text
THREAD
   │
   ▼
recv(socket)
   │
   ▼
Data var mı?
   │
   ├── EVET
   │     ↓
   │   recv() döner
   │
   └── HAYIR
         ↓
      THREAD BLOCKED
         ↓
      data beklenir
```

şeklinde çalışır.

Burada önemli nokta CPU'nun sürekli aynı `recv()` instruction'ını çalıştırarak dönmesi değildir.

Kernel thread'i bekleme durumuna alabilir ve CPU başka runnable task'larla ilgilenebilir.

Problem tek thread'in belirli bir socket'i beklemesidir.

Örneğin:

```text
Client 1 → data yok
Client 2 → data var
Client 3 → data var
```

ve tek thread:

```text
recv(Client 1)
```

içerisinde blocking durumdaysa aynı thread Client 2 ve Client 3 ile ilgilenemez.

---

# 3. Thread, Socket ve CPU Kavramlarının Gerçek Hayat Benzetmesiyle Ayrılması

Bugün thread ve socket kavramlarının rolleri tekrar ayrıştırıldı.

Gerçek hayat benzetmesi olarak:

```text
Client  = müşteri
Socket  = müşteriyle iletişim kanalı / masa
Thread  = işi gerçekleştiren çalışan
CPU     = instruction'ların gerçekten yürütüldüğü işlem kaynağı
epoll   = hangi müşterinin hazır olduğunu bildiren çağrı sistemi
```

şeklinde düşünülebileceği görüldü.

Thread bir “iş” veya socket değildir.

Socket:

```text
network connection endpoint
```

iken thread:

```text
execution flow
```

olarak değerlendirilmelidir.

Ayrıca çok sayıda thread bulunması aynı sayıda CPU core bulunduğu anlamına gelmez.

```text
Thread 1 ─┐
Thread 2 ─┤
Thread 3 ─┤
...       ├──► Linux Scheduler ──► CPU cores
Thread N ─┘
```

Scheduler runnable thread'lerin CPU üzerinde çalışmasını yönetmektedir.

---

# 4. Connection Başına Thread Yaklaşımının İncelenmesi

Blocking server'ın basit çözümlerinden biri her client için ayrı thread oluşturmaktır.

```text
Client 1 ──► Thread 1
Client 2 ──► Thread 2
Client 3 ──► Thread 3
Client 4 ──► Thread 4
```

Böylece Thread 1 Client 1'i beklerken Thread 2 başka bir client ile çalışabilir.

Bu yaklaşım düşük veya orta connection sayılarında basit ve kullanılabilir olabilir.

Ancak çok yüksek connection sayılarında:

```text
çok fazla thread
      ↓
thread stack memory
      +
scheduler bookkeeping
      +
context switching
      +
CPU/cache maliyeti
```

oluşabilir.

Önceki gün çalışılan C stack konusu burada network server mimarisiyle doğrudan ilişkilendirildi.

Her thread'in kendi stack'i bulunduğundan çok yüksek thread sayısı yalnız scheduler problemi değil aynı zamanda memory problemidir.

---

# 5. Non-Blocking I/O ve EAGAIN

Socket:

```c
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

ile non-blocking hale getirildiğinde `recv()` için okunacak data bulunmaması durumunda thread'in ilgili socket'i beklemek zorunda olmadığı tekrar edildi.

```text
recv()
   ↓
data yok
   ↓
-1
   ↓
errno = EAGAIN / EWOULDBLOCK
```

Bu durum:

> “Şu anda işlem tamamlanamıyor; burada bekleme, daha sonra tekrar dene.”

şeklinde yorumlanabilir.

Dolayısıyla:

```text
BLOCKING

recv()
 ↓
data yok
 ↓
thread bekleyebilir
```

ile:

```text
NON-BLOCKING

recv()
 ↓
data yok
 ↓
EAGAIN
 ↓
thread devam eder
```

arasındaki temel fark tekrar edildi.

---

# 6. Sadece Non-Blocking Kullanmanın Yeterli Olmaması

Bir socket'in non-blocking yapılmasının tek başına ölçeklenebilir event-driven mimari oluşturmadığı görüldü.

Örneğin:

```c
while (1) {
    n = recv(fd, buffer, sizeof(buffer), 0);

    if (n < 0 && errno == EAGAIN)
        continue;
}
```

şeklindeki yapı blocking değildir.

Ancak:

```text
recv
 ↓
EAGAIN
 ↓
recv
 ↓
EAGAIN
 ↓
recv
 ↓
EAGAIN
 ↓
...
```

şeklinde CPU'nun sürekli hazır olmayan socket'i kontrol etmesine neden olabilir.

Çok sayıda socket olduğunda:

```text
Socket 1 hazır mı?
Socket 2 hazır mı?
Socket 3 hazır mı?
...
Socket 100000 hazır mı?

tekrar...
```

şeklinde sürekli kontrol yapılması verimsizdir.

Bu nedenle:

```text
Non-blocking
     +
Readiness notification
```

birlikte düşünülmelidir.

---

# 7. epoll'un Çözdüğü Problemin Netleştirilmesi

`epoll` mekanizmasının amacı yalnız socket'i non-blocking yapmak değildir.

Temel problem:

> Çok sayıda socket içerisinden hangisinin şu anda I/O için hazır olduğunu verimli şekilde belirlemek.

şeklinde tanımlandı.

```text
Socket 1 ───┐
Socket 2 ───┤
Socket 3 ───┤
...         ├──► epoll
Socket N ───┘
                │
                ▼
          hazır socket'ler
                │
                ▼
             Thread
```

Örneğin:

```text
Socket 1     hazır değil
Socket 2     hazır değil
Socket 8500  hazır
Socket 9000  hazır değil
```

ise event loop yalnız hazır endpoint'lerle ilgilenebilir.

```text
epoll_wait()
     ↓
Socket 8500 → EPOLLIN
     ↓
recv(Socket 8500)
```

Bu yapı çok sayıda connection'ın tek veya az sayıda thread ile yönetilebilmesini sağlar.

---

# 8. epoll_wait İçerisinde Thread'in Uyuması

Bugün önemli bir kavramsal ayrım daha incelendi.

“Non-blocking sistemde thread hiçbir zaman uyumaz” ifadesinin doğru olmadığı görüldü.

Thread:

```c
epoll_wait(epfd, events, MAX_EVENTS, -1);
```

içerisinde bekleyebilir.

Ancak blocking `recv()` ile temel fark şudur:

```text
Blocking recv:

"BU socket hazır olana kadar bekle."
```

Buna karşılık:

```text
epoll_wait:

"Takip ettiğim socket'lerden
HERHANGİ BİRİ hazır olduğunda
beni uyandır."
```

Dolayısıyla event-driven modelde thread'in uyuması problem değildir.

Problem:

```text
tek bir connection'ın I/O'suna
bağımlı şekilde uyumasıdır.
```

---

# 9. Connection State'in Neden Gerekli Olduğunun İncelenmesi

Non-blocking/event-driven programlamada bir connection üzerinde işlem geçici olarak bırakıldığında connection kaybolmaz.

State memory içerisinde tutulmaktadır.

TCP Accelerator projesindeki:

```c
struct connection
```

yapısının önemi bu açıdan tekrar değerlendirildi.

Kavramsal olarak:

```text
struct connection
    │
    ├── client_fd
    ├── server_fd
    ├── c2s buffer
    ├── s2c buffer
    ├── client read state
    ├── server read state
    └── write/half-close state
```

şeklinde her connection'ın durumu saklanmaktadır.

Bir socket hazır değilse CPU başka connection ile ilgilenebilir.

Daha sonra:

```text
socket ready
    ↓
epoll event
    ↓
endpoint
    ↓
connection pointer
    ↓
eski state üzerinden devam
```

edilebilir.

Bu yapı event-driven programming'in temel prensiplerinden biri olarak değerlendirildi.

---

# 10. TCP Duplicate ACK Mekanizmasının Problem Üzerinden Tekrarı

TCP'de packet loss senaryosu tekrar incelendi.

Örneğin:

```text
1000-1999  → geldi
2000-2999  → kayıp
3000-3999  → geldi
4000-4999  → geldi
5000-5999  → geldi
```

Receiver'ın cumulative ACK değeri:

```text
ACK = next expected byte
```

olduğu için ilk eksik byte 2000'dir.

Bu nedenle:

```text
1000-1999 geldi
     ↓
ACK 2000

3000-3999 geldi
     ↓
ACK 2000

4000-4999 geldi
     ↓
ACK 2000

5000-5999 geldi
     ↓
ACK 2000
```

davranışı oluşabilir.

Burada aynı ACK numarasının tekrar tekrar gönderilmesi:

```text
Duplicate ACK
```

olarak değerlendirildi.

---

# 11. Duplicate ACK ile Fast Retransmit İlişkisinin Kurulması

Duplicate ACK'in yalnız “aynı ACK tekrar geldi” tanımından ibaret olmadığı, sender açısından packet loss hakkında bilgi taşıdığı tekrar edildi.

```text
Sonraki data receiver'a ulaşıyor
             +
ACK ilerlemiyor
             ↓
Arada eksik data olabilir
```

Sender yeterli duplicate ACK pattern'i gördüğünde kayıp data için RTO'nun dolmasını beklemeden retransmission yapabilir.

```text
Duplicate ACK
      ↓
Loss indication
      ↓
Fast Retransmit
      ↓
Eksik segment tekrar gönderilir
```

Eksik:

```text
2000-2999
```

aralığı receiver'a ulaştığında receiver'ın elindeki contiguous byte stream:

```text
1000-1999 ✅
2000-2999 ✅
3000-3999 ✅
4000-4999 ✅
5000-5999 ✅
```

haline gelir.

Böylece cumulative ACK:

```text
ACK 6000
```

seviyesine ilerleyebilir.

---

# 12. High-RTT Network'te Küçük Window'un Neden Yetersiz Olduğu

Bugün “yüksek RTT network için küçük TCP window yetersizdir” ifadesindeki **yetersizliğin ne anlama geldiği** ayrıntılı olarak incelendi.

Buradaki yetersizlik:

```text
connection çalışmaz
```

anlamında değildir.

Asıl problem:

```text
link bandwidth'inin
tam kullanılamaması
```

yani throughput problemidir.

Örneğin:

```text
Bandwidth = 1 Gbit/s
RTT       = 500 ms
```

ve kullanılabilecek window yaklaşık:

```text
64 KB
```

ise sender ACK beklemeden yalnız sınırlı miktarda data'yı in-flight tutabilir.

Kabaca:

```text
Throughput ≈ Window / RTT
```

ilişkisi üzerinden:

```text
64 KB / 0.5 s
≈
128 KB/s
≈
1 Mbit/s
```

seviyesinde ciddi bir sınırlama oluşabileceği görüldü.

Fiziksel link:

```text
1000 Mbit/s
```

kapasiteli olsa bile küçük window nedeniyle kapasitenin büyük kısmı kullanılamayabilir.

---

# 13. Bandwidth-Delay Product'ın Fiziksel Anlamının Pekiştirilmesi

Aynı örnekte:

```text
Bandwidth = 1 Gbit/s
RTT       = 0.5 s
```

için:

```text
BDP
=
1 Gbit/s × 0.5 s
=
500 Mbit
=
62.5 MB
```

bulunur.

Bu değer kabaca hattı doldurabilmek için ACK beklenirken network üzerinde bulunması gereken data miktarını ifade etmektedir.

```text
High bandwidth
      +
High RTT
      ↓
High BDP
      ↓
Daha fazla in-flight data gerekir
      ↓
Küçük window throughput'u sınırlar
```

Bu nedenle high-RTT/high-bandwidth network'lerde büyük efektif TCP window ihtiyacı oluşmaktadır.

---

# 14. Window Scaling Mekanizmasının Çözdüğü Problemin Netleştirilmesi

TCP header içerisindeki klasik Window alanının 16-bit olması nedeniyle ham değer yaklaşık 64 KB ile sınırlıdır.

```text
16 bit
 ↓
65535
 ↓
~64 KB
```

Modern yüksek BDP bağlantılarda bu değer yetersiz kalabileceği için Window Scaling kullanılmaktadır.

```text
16-bit Window
      +
Window Scale
      ↓
çok daha büyük
effective receive window
```

Böylece sender'ın daha fazla data'yı ACK beklerken in-flight tutabilmesine flow-control açısından imkan sağlanabilir.

Ancak bunun congestion control'den bağımsız olmadığı tekrar vurgulandı.

```text
rwnd
=
receiver flow-control limiti

cwnd
=
network congestion-control limiti

effective sending limit
≈
min(rwnd, cwnd)
```

Dolayısıyla büyük `rwnd`, tek başına yüksek throughput garantisi değildir.

---

# 15. Netfilter Packet Path'in Baştan İncelenmesi

Bugünkü ikinci büyük konu Netfilter packet path oldu.

Dışarıdan gelen packet için temel akış:

```text
NETWORK
   ↓
NIC
   ↓
Network Driver
   ↓
struct sk_buff
   ↓
PRE_ROUTING
   ↓
ROUTING DECISION
   │
   ├── destination local
   │        ↓
   │      INPUT
   │        ↓
   │   Local Process
   │
   └── destination başka host
            ↓
         FORWARD
            ↓
       POST_ROUTING
            ↓
           NIC
            ↓
         NETWORK
```

Local process'in oluşturduğu packet ise:

```text
Local Process
     ↓
   socket
     ↓
   OUTPUT
     ↓
POST_ROUTING
     ↓
    NIC
     ↓
 NETWORK
```

yolunu izlemektedir.

---

# 16. PRE_ROUTING'in Gerçek Görevinin Netleştirilmesi

`PRE_ROUTING`'in routing işlemini kendisinin yapmadığı özellikle ayrıştırıldı.

`PRE_ROUTING`:

> Dışarıdan gelen packet'ın routing decision verilmeden önce geçtiği Netfilter hook noktasıdır.

```text
NIC
 ↓
Driver
 ↓
skb
 ↓
PRE_ROUTING       ← müdahale noktası
 ↓
ROUTING           ← routing kararı
```

Netfilter hook'u üzerinden bu aşamada:

```text
packet inspect
packet filter
NF_DROP
NF_ACCEPT
DNAT gibi değişiklikler
```

yapılabilir.

Bizim firewall implementation'ında:

```text
PRE_ROUTING
     ↓
firewall callback
     ↓
Source MAC kontrolü
     ↓
Source IP kontrolü
     ↓
TCP/UDP kontrolü
     ↓
NF_DROP / NF_ACCEPT
```

mantığı kullanılmıştı.

Bu sayede yasak packet routing işlemine ilerlemeden erken aşamada drop edilebilmektedir.

---

# 17. PRE_ROUTING ile Routing Decision Arasındaki Fark

Bugün özellikle şu ayrım netleştirildi:

```text
PRE_ROUTING
=
routing'den önce müdahale noktası
```

ve:

```text
ROUTING DECISION
=
packet'ın nereye gönderileceğinin
belirlendiği aşama
```

aynı şey değildir.

Routing decision sırasında destination IP ve routing table değerlendirilerek packet'ın:

```text
bu Linux host'a mı?
```

yoksa:

```text
başka bir host'a mı?
```

gideceğine karar verilir.

Kavramsal olarak:

```text
PRE_ROUTING
     ↓
ROUTING DECISION
    /          \
   /            \
  ▼              ▼
LOCAL           REMOTE
  │               │
  ▼               ▼
INPUT           FORWARD
```

şeklinde ilerler.

---

# 18. INPUT Hook'unun İncelenmesi

Routing sonucunda destination'ın local Linux host olduğu belirlenirse packet:

```text
INPUT
```

path'ine girer.

Örneğin:

```text
Linux IP = 192.168.1.10

Packet:
SRC = 192.168.1.20
DST = 192.168.1.10
```

ise:

```text
PRE_ROUTING
     ↓
ROUTING
     ↓
"192.168.1.10 local address"
     ↓
INPUT
     ↓
TCP/UDP
     ↓
socket
     ↓
application
```

şeklinde ilerleyebilir.

Burada routing kararının destination port üzerinden değil öncelikle IP/routing bilgisi üzerinden verildiği tekrar edildi.

Daha sonra transport layer'da destination port ilgili socket'in bulunmasında kullanılmaktadır.

---

# 19. FORWARD Hook'unun İncelenmesi

Packet'ın destination adresi Linux router'ın kendisi değilse ve IP forwarding yapılacaksa:

```text
PRE_ROUTING
     ↓
ROUTING
     ↓
FORWARD
     ↓
POST_ROUTING
     ↓
Outgoing NIC
```

path'i kullanılmaktadır.

Örneğin:

```text
PC
192.168.1.20
     │
     ▼
Linux Router
     │
     ▼
Internet
```

senaryosunda PC'nin Internet'e gönderdiği packet router üzerinde local application'a teslim edilmez.

Router açısından:

```text
incoming packet
     ↓
PRE_ROUTING
     ↓
destination başka network
     ↓
FORWARD
     ↓
POST_ROUTING
     ↓
outgoing interface
```

şeklinde ilerler.

---

# 20. OUTPUT Hook'unun İncelenmesi

Packet dışarıdan gelmeyip Linux host'un kendi process'i tarafından oluşturulmuşsa:

```text
Local Process
     ↓
send()
     ↓
TCP/UDP/IP
     ↓
OUTPUT
     ↓
POST_ROUTING
     ↓
NIC
```

path'i kullanılmaktadır.

Dolayısıyla local olarak oluşturulan packet normal incoming `PRE_ROUTING` path'inden geçmez.

Temel ayrım:

```text
Incoming packet
→ PRE_ROUTING

Local-generated packet
→ OUTPUT
```

şeklindedir.

---

# 21. POST_ROUTING Hook'unun İncelenmesi

`POST_ROUTING`, routing kararı verildikten sonra packet'ın outgoing network interface üzerinden gönderilmesinden önceki Netfilter hook noktasıdır.

```text
Routing tamamlandı
       ↓
Outgoing interface belli
       ↓
POST_ROUTING
       ↓
qdisc / networking output path
       ↓
network driver
       ↓
ndo_start_xmit()
       ↓
TX descriptors
       ↓
DMA
       ↓
NIC
       ↓
WIRE
```

Bu nokta daha önce çalışılan Linux network driver konularıyla Netfilter konularını birbirine bağladı.

`POST_ROUTING` aşamasında packet'ın hangi interface üzerinden çıkacağı belirlenmiş durumdadır.

---

# 22. PRE_ROUTING ve POST_ROUTING'in NAT Üzerinden Karşılaştırılması

Hook sıralamasının neden önemli olduğu DNAT ve SNAT üzerinden incelendi.

Destination NAT için:

```text
Packet geldi

DST = Public IP
       ↓
PRE_ROUTING
       ↓
DNAT
       ↓
DST = Internal Server IP
       ↓
ROUTING
       ↓
yeni destination'a göre karar
```

mantıklıdır.

Çünkü routing decision yeni destination adresini görmelidir.

Buna karşılık source NAT için:

```text
Routing tamamlandı
       ↓
packet'ın çıkış yönü belli
       ↓
POST_ROUTING
       ↓
SNAT / MASQUERADE
       ↓
outgoing NIC
```

mantığı kullanılabilir.

Böylece hook isimlerinin yalnız ezberlenmesi yerine neden bu sırada bulundukları anlaşılmış oldu.

---

# 23. Netfilter Hook'larının Tek Ağaçta Birleştirilmesi

Gün sonunda Netfilter hook'ları üç temel packet yolu halinde özetlendi.

### Dışarıdan gelip local host'a giden packet

```text
NIC
 ↓
PRE_ROUTING
 ↓
ROUTING
 ↓
INPUT
 ↓
LOCAL PROCESS
```

### Dışarıdan gelip başka host'a forward edilen packet

```text
NIC
 ↓
PRE_ROUTING
 ↓
ROUTING
 ↓
FORWARD
 ↓
POST_ROUTING
 ↓
NIC
```

### Local host tarafından oluşturulan packet

```text
LOCAL PROCESS
 ↓
OUTPUT
 ↓
POST_ROUTING
 ↓
NIC
```

Bu üç yol Netfilter packet path sorularının çözümünde temel referans olarak belirlendi.

---

# 24. Firewall Implementation'ında Yapılan Varsayımların İncelenmesi

Daha önce geliştirilen MAC/IP/port firewall implementation'ı bu kez yalnız kod açısından değil, **hangi varsayımlar altında çalıştığı** açısından değerlendirildi.

Temel yapı:

```text
PF_INET
   ↓
NF_INET_PRE_ROUTING
   ↓
struct sk_buff
   ↓
Ethernet / IPv4 / TCP-UDP parsing
   ↓
rule matching
   ↓
NF_DROP / NF_ACCEPT
```

şeklindeydi.

MAC filtering için:

```c
eth_hdr(skb)
```

üzerinden Ethernet header'a erişilerek source MAC blacklist ile karşılaştırılmıştı.

Burada temel varsayımlar:

```text
Ethernet/L2 header erişilebilir
IPv4 packet path kullanılıyor
MAC local L2 segment açısından anlamlı
Blacklist statik
Packet header'ları güvenli şekilde parse edilmeli
MAC spoofing mümkün
```

olarak değerlendirildi.

Özellikle source MAC adresinin Internet boyunca end-to-end taşınan bir kimlik olmadığı tekrar edildi.

Router geçildiğinde Layer-2 frame yeniden oluşturulabileceğinden uzak host'un gerçek Ethernet MAC adresi local segmentte doğrudan görülemez.

---

# 25. TCP Accelerator Mimarisinin Tekrarı

Daha önce geliştirilen accelerator'ın temel mimarisi tekrar değerlendirildi:

```text
CLIENT                ACCELERATOR                SERVER
   │                       │                        │
   │       TCP #1          │        TCP #2          │
   │◄─────────────────────►│◄──────────────────────►│
```

Accelerator client connection'ını kabul eder ve server'a ayrı bir outgoing TCP connection açar.

Bu nedenle her client flow için accelerator tarafında:

```text
client_fd
    +
server_fd
```

olmak üzere iki connected socket bulunmaktadır.

İki TCP connection bağımsız:

```text
SEQ
ACK
rwnd
cwnd
RTT
RTO
retransmission state
```

taşımaktadır.

---

# 26. Accelerator'ın Mevcut İşlevlerinin Tekrarı

Mevcut accelerator implementation'ında:

```text
Split TCP
   +
Bidirectional forwarding
   +
Userspace c2s/s2c buffers
   +
SO_RCVBUF / SO_SNDBUF
   +
O_NONBLOCK
   +
Partial send handling
   +
Backpressure
   +
Half-close
   +
epoll
   +
Multi-client connection state
```

mekanizmaları bulunmaktadır.

Data akışı:

```text
Client
  ↓
client_fd
  ↓
c2s buffer
  ↓
server_fd
  ↓
Server
```

ve ters yönde:

```text
Server
  ↓
server_fd
  ↓
s2c buffer
  ↓
client_fd
  ↓
Client
```

şeklindedir.

---

# 27. TCP Accelerator ile PEP Arasındaki Farkın İncelenmesi

Bugün accelerator ve PEP kavramlarının aynı şey olarak düşünülmemesi gerektiği ayrıntılı olarak ele alındı.

Mevcut accelerator temel olarak:

> Split-TCP kullanan, buffering ve event-driven forwarding yapan bir TCP proxy/accelerator altyapısıdır.

PEP ise:

```text
PEP
=
Performance Enhancing Proxy
```

olarak özellikle belirli network koşullarındaki performans problemlerini iyileştirmeyi amaçlamaktadır.

Örneğin:

```text
Client
   ↓
PEP-A
   ║
   ║ Satellite / High RTT / High BDP Link
   ║
PEP-B
   ↓
Server
```

mimarisi kullanılabilir.

Ancak iki PEP cihazının zorunlu olmadığı, PEP implementation'larının farklı mimariler kullanabileceği özellikle belirtildi.

---

# 28. “Optimized Link” Kavramının Açıklanması

PEP-A ile PEP-B arasındaki bağlantının “optimized” olması:

```text
burada kesinlikle özel bir
standart protokol çalışır
```

anlamına gelmemektedir.

Daha doğru ifade:

> High-RTT/high-BDP veya loss karakteristiğine sahip segment için taşıma davranışının o bağlantının özelliklerine göre optimize edilmesi.

şeklindedir.

PEP implementation'ına bağlı olarak:

```text
larger buffers/windows
local recovery/retransmission
connection splitting
ACK-related optimizations
different transport behavior
compression
tunneling
multiplexing
```

gibi tekniklerden bazıları uygulanabilir.

Her PEP'in bunların tamamını yapmak zorunda olmadığı vurgulandı.

---

# 29. Accelerator ile PEP'in Seviyelendirilmesi

Kavramsal olarak üç aşamalı bir model oluşturuldu:

```text
LEVEL 1
TCP PROXY

Client ↔ Proxy ↔ Server
```

↓

```text
LEVEL 2
SPLIT-TCP ACCELERATOR

Client ↔ Accelerator ↔ Server

+ independent TCP connections
+ buffering
+ non-blocking
+ epoll
+ backpressure
+ multi-client
```

↓

```text
LEVEL 3
PERFORMANCE ENHANCING PROXY

problemli network segmentini tanı
       ↓
RTT / BDP / loss / congestion
       ↓
performance policy
       ↓
link'e özel optimization
```

Bu sınıflandırmanın resmi protokol sınıflandırması değil, mevcut projenin gelişim seviyelerini anlamak amacıyla kullanılan kavramsal bir model olduğu değerlendirildi.

---

# 30. Split-TCP'nin ACK Semantiğine Etkisinin İncelenmesi

Split-TCP mimarisinde önemli bir trade-off tekrar değerlendirildi.

Normal end-to-end TCP'de:

```text
Client ───────── DATA ────────► Server
Client ◄──────── ACK ───────── Server
```

bulunurken Split-TCP'de:

```text
Client          Accelerator          Server
   │                 │                  │
   │── DATA ────────►│                  │
   │◄── ACK ─────────│                  │
   │                 │── DATA ─────────►│
   │                 │◄── ACK ──────────│
```

şeklinde iki bağımsız ACK domain'i oluşmaktadır.

Dolayısıyla client'ın accelerator'dan aldığı ACK:

> Accelerator'ın ilgili TCP connection üzerinde datayı kabul ettiğini

ifade eder.

Bu anda final server'ın aynı datayı henüz almış olması zorunlu değildir.

Bu durum Split-TCP/PEP tasarımının performans avantajlarının yanında end-to-end semantics açısından değerlendirilmesi gereken önemli bir trade-off'tur.

---

# 31. TCP_INFO'nun Sonraki Accelerator Adımıyla İlişkilendirilmesi

Mevcut accelerator henüz network koşullarını gözlemleyip buna göre dinamik optimization yapan bir PEP seviyesinde değildir.

Bu nedenle sonraki teknik aşama:

```text
TCP_INFO
```

ile connection state'ini gözlemlemektir.

Planlanan gözlem zinciri:

```text
getsockopt()
     ↓
TCP_INFO
     ↓
struct tcp_info
     │
     ├── RTT
     ├── RTO
     ├── cwnd
     ├── ssthresh
     ├── MSS
     ├── unacked
     ├── retrans
     └── loss-related state
```

şeklindedir.

Böylece accelerator'ın:

```text
"Network nasıl davranıyor?"
```

sorusuna ölçülebilir verilerle cevap verebilmesi hedeflenmektedir.

Daha sonraki tuning aşamasında:

```text
OBSERVE
   ↓
ANALYZE
   ↓
DECIDE
   ↓
TUNE
```

yaklaşımına geçilmesi planlanmaktadır.

---

# 32. Bugünkü Problem–Mekanizma Eşleştirmeleri

Gün boyunca öğrenilen konuların hangi problemlere karşılık geldiği aşağıdaki şekilde birleştirildi:

```text
Bir socket data göndermiyor,
thread diğer client'lara geçemiyor
        ↓
Blocking I/O problemi
        ↓
O_NONBLOCK
```

```text
100.000 non-blocking socket'i
tek tek kontrol etmek pahalı
        ↓
Readiness detection problemi
        ↓
epoll
```

```text
Bir connection'ın işlemi bırakılıp
daha sonra devam ettirilecek
        ↓
State preservation problemi
        ↓
struct connection
```

```text
Aradaki TCP data kayboldu
ama sonraki segmentler geliyor
        ↓
Duplicate ACK
        ↓
Fast Retransmit
```

```text
High bandwidth + high RTT
ama throughput düşük
        ↓
High BDP
        ↓
Daha fazla in-flight data ihtiyacı
        ↓
Window Scaling / uygun buffering
```

```text
Packet routing'den önce
kontrol/değişiklik gerekiyor
        ↓
PRE_ROUTING
```

```text
Packet başka host'a yönlendiriliyor
        ↓
FORWARD
```

```text
Packet dışarı çıkmak üzere,
routing tamamlandı
        ↓
POST_ROUTING
```

```text
High-RTT segment
end-to-end TCP performansını düşürüyor
        ↓
Split TCP / PEP yaklaşımı
```

Bu eşleştirmeler bugünkü çalışmanın en önemli kazanımlarından biri oldu.

---

# 33. 21. Gün → 22. Gün İlerlemesi

Önceki gün:

```text
21. GÜN
────────────────────────

TCP / SYSTEM FUNDAMENTALS

Blocking / Non-Blocking
        ↓
epoll
        ↓
4-Tuple
        ↓
ACK / Retransmission
        ↓
rwnd / cwnd
        ↓
Window Scaling
        ↓
BDP
        ↓
PEP
        ↓
Netfilter
        ↓
C Stack
```

başlıklarının kapsamlı teorik tekrarı yapılmıştı.

Bugün ise aynı bilgilerin **problem çözme bağlamı** oluşturuldu:

```text
22. GÜN
────────────────────────────────

BİLİNEN KAVRAM
      ↓
HANGİ PROBLEMİ ÇÖZÜYOR?
      ↓
PROBLEM → MEKANİZMA EŞLEŞTİRMESİ
      │
      ├── Blocking
      │      ↓
      │   Thread waiting
      │
      ├── Non-blocking
      │      ↓
      │   EAGAIN
      │
      ├── epoll
      │      ↓
      │   Readiness notification
      │
      ├── Connection State
      │      ↓
      │   Event-driven continuation
      │
      ├── Duplicate ACK
      │      ↓
      │   Loss indication
      │      ↓
      │   Fast Retransmit
      │
      ├── High RTT
      │      ↓
      │   High BDP
      │      ↓
      │   Window requirement
      │
      ├── Netfilter
      │      ↓
      │   PRE_ROUTING
      │   INPUT
      │   FORWARD
      │   OUTPUT
      │   POST_ROUTING
      │
      └── High-RTT TCP Performance
             ↓
          Split TCP
             ↓
          Accelerator
             ↓
             PEP
```

seviyesine geçildi.

---

# 34. Gün Sonunda Oluşturulan Bütünsel Network Akışı

Bugün önceki günlerde ayrı ayrı öğrenilmiş kernel, driver, Netfilter, TCP ve userspace server konuları tek packet/data akışı üzerinde birleştirildi.

Incoming packet açısından:

```text
PHYSICAL NETWORK
       ↓
      NIC
       ↓
NETWORK DRIVER
       ↓
struct sk_buff
       ↓
PRE_ROUTING
       ↓
ROUTING DECISION
      / \
     /   \
    ▼     ▼
 INPUT   FORWARD
   │        │
   ▼        ▼
LOCAL    POST_ROUTING
PROCESS      │
             ▼
            NIC
```

Userspace network server açısından:

```text
Socket
   ↓
Blocking / Non-Blocking
   ↓
EAGAIN
   ↓
epoll
   ↓
struct connection
   ↓
multi-client state management
```

TCP açısından:

```text
TCP
 │
 ├── SEQ
 ├── ACK
 ├── Duplicate ACK
 ├── Fast Retransmit
 ├── SACK
 ├── rwnd
 ├── cwnd
 └── Window Scaling
        │
        ▼
       BDP
        │
        ▼
     High RTT
        │
        ▼
     Split TCP
        │
        ▼
    Accelerator
        │
        ▼
       PEP
```

şeklinde birbirine bağlı bir yapı elde edildi.

---

# Gün Sonu Değerlendirmesi

Yirmi ikinci gün, daha önce öğrenilmiş network ve system programming kavramlarının yalnız tanım seviyesinde bilinmesinden çıkarılarak **problem–çözüm ilişkisi içerisinde değerlendirilmesine** ayrıldı.

Blocking ve non-blocking I/O gerçek execution modeli üzerinden tekrar edildi. Thread'in bir socket üzerinde blocking durumda beklemesi ile `O_NONBLOCK` kullanıldığında `EAGAIN` alınması arasındaki fark incelendi. Sadece non-blocking socket kullanımının yeterli olmadığı, çok sayıda socket'in sürekli kontrol edilmesinin CPU açısından verimsiz olabileceği ve `epoll` mekanizmasının çok sayıda file descriptor arasından hazır olanları belirleme problemini çözdüğü görüldü.

Thread, socket ve CPU kavramları birbirinden ayrılarak connection başına thread yaklaşımının düşük connection sayılarında basit olmasına rağmen yüksek connection sayılarında stack memory, scheduler ve context-switch maliyetleri oluşturabileceği değerlendirildi. Event-driven modelde bir connection geçici olarak işlenmediğinde state'in `struct connection` gibi userspace veri yapılarında saklandığı ve daha sonra `epoll` event'i geldiğinde aynı state üzerinden devam edilebildiği pekiştirildi.

TCP reliability tarafında cumulative ACK, duplicate ACK ve Fast Retransmit ilişkisi problem üzerinden tekrar edildi. Aradaki segment kaybolurken sonraki segmentlerin ulaşması durumunda receiver'ın aynı next-expected-byte değerini tekrar tekrar ACK etmesinin duplicate ACK oluşturduğu ve bunun sender açısından kayıp göstergesi olarak kullanılabileceği değerlendirildi.

High-RTT network'lerde küçük TCP window'un “yetersiz” olmasının bağlantının çalışmaması değil, mevcut bandwidth'in tam kullanılamaması anlamına geldiği netleştirildi. Bandwidth-Delay Product üzerinden yüksek bandwidth ve yüksek RTT kombinasyonunda çok daha fazla datanın in-flight tutulması gerektiği ve Window Scaling'in hangi gerçek probleme cevap verdiği tekrar oluşturuldu.

Netfilter tarafında packet path baştan sona incelendi. `PRE_ROUTING`'in routing işlemini gerçekleştiren yer olmadığı, routing decision'dan önceki müdahale hook'u olduğu netleştirildi. Routing sonucunda local destination için `INPUT`, başka host'a yönlendirilecek packet için `FORWARD`, local process tarafından oluşturulan packet için `OUTPUT` ve dışarı çıkacak packet için `POST_ROUTING` aşamalarının kullanıldığı tekrar edildi. DNAT ve SNAT örnekleri üzerinden hook sıralamasının mantığı incelendi.

Daha önce geliştirilen Netfilter firewall bu packet path üzerine tekrar yerleştirilerek `PRE_ROUTING` aşamasında `struct sk_buff` üzerinden Ethernet/IP/TCP-UDP bilgilerinin incelenmesi, blacklist kontrolü ve `NF_DROP/NF_ACCEPT` verdict'lerinin verilmesi değerlendirildi. Ayrıca MAC filtering implementation'ının local Layer-2 görünürlüğü ve MAC spoofing gibi varsayımları tekrar ele alındı.

Günün son bölümünde mevcut TCP Accelerator ile Performance Enhancing Proxy arasındaki fark ayrıntılı olarak incelendi. Mevcut accelerator'ın iki bağımsız TCP connection, userspace buffering, non-blocking I/O, `epoll`, backpressure, half-close ve multi-client state management sağlayan bir Split-TCP proxy olduğu; PEP yaklaşımının ise özellikle high-RTT/high-BDP gibi problemli network segmentlerinin performansını iyileştirmeye yönelik ek optimizasyon politikaları içerebileceği görüldü.

Ayrıca Split-TCP mimarisinde client'ın accelerator'dan aldığı ACK'in final server'ın datayı aldığı anlamına gelmeyebileceği ve bunun end-to-end TCP semantics açısından önemli bir trade-off olduğu değerlendirildi.

Bugünün en önemli kazanımı, daha önce öğrenilmiş mekanizmaların yalnız isim ve tanımlarının değil, **hangi problem ortaya çıktığında hangi mekanizmanın düşünülmesi gerektiğinin** sistematik hale getirilmesi oldu.

Gün sonunda ulaşılan düşünme modeli:

```text
PROBLEMİ GÖR
     ↓
HANGİ KATMANDA?
     │
     ├── C / Memory
     ├── System Programming
     ├── Socket / I/O
     ├── TCP
     ├── Netfilter
     ├── Driver
     └── Network Architecture
     ↓
PROBLEMİN SEBEBİNİ BUL
     ↓
İLGİLİ MEKANİZMAYI SEÇ
     ↓
NASIL ÇÖZDÜĞÜNÜ AÇIKLA
     ↓
VARSAYIMLARI BELİRLE
     ↓
TRADE-OFF'LARI DEĞERLENDİR
```

şeklinde oluşturuldu.

Bir sonraki çalışma aşamasında TCP Accelerator'ın yalnız packet forwarding yapan bir sistem olmaktan çıkarılarak connection davranışını gözlemleyebilen bir yapıya dönüştürülmesi planlanmaktadır. Bunun için `getsockopt()` ve `TCP_INFO` kullanılarak RTT, RTO, congestion window, slow-start threshold, MSS, unacknowledged data ve retransmission bilgilerinin accelerator içerisinde gözlemlenmesi; daha sonraki aşamada bu metriklerin dynamic tuning kararlarında kullanılması hedeflenmektedir.
