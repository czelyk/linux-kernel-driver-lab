STAJ RAPORU – 19. GÜN
Linux Networking – Netfilter Firewall Geliştirme, MAC/IP/Port Filtering ve TCP Accelerator Tasarımı

On sekizinci gün sonunda gerçek Linux TCP bağlantıları ss -tin ve tcpdump ile incelenmiş; RTT, RTO, cwnd, ssthresh, snd_wnd, retransmission ve backoff gibi TCP state değerleri gerçek bağlantılar üzerinde yorumlanmıştı. Aynı gün Netfilter framework'üne geçilerek PRE_ROUTING, struct sk_buff, Ethernet header, source MAC ve NF_ACCEPT/NF_DROP yapıları kullanılmış ve ilk MAC filtering kernel modülü oluşturulmuştu. Sonraki çalışma rotası MAC blacklist'in genişletilmesi, TCP/UDP header parsing, IP/port filtering ve firewall rule mantığının geliştirilmesi olarak belirlenmişti.

Bugünkü çalışmada mevcut Netfilter firewall modülü geliştirilerek birden fazla MAC adresinin blacklist içerisinde tutulması ve gelen packet'ın source MAC adresinin bu liste içerisinde aranması sağlandı. Ardından firewall yalnız Layer 2 seviyesinde bırakılmayarak IPv4 header parsing, source IP filtering, TCP/UDP protocol ayrımı ve TCP destination-port filtering eklendi. Böylece skb → Ethernet → IPv4 → TCP/UDP zinciri kernel içerisinde uygulamalı olarak takip edildi. Günün sonunda Netfilter ödevi temel hedefleriyle tamamlandı ve sonraki büyük çalışma olan yüksek RTT bağlantıları için TCP Accelerator / Split-TCP mimarisinin tasarımına geçildi.

1. MAC Blacklist'in Birden Fazla MAC İçin Genişletilmesi

Önceki gün oluşturulan MAC firewall yapısı bugün birden fazla MAC adresini destekleyecek hale getirildi.

Blacklist:

static unsigned char blocked_macs[][ETH_ALEN] = {
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
    {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}
};

Eleman sayısı:

#define BLOCKED_MAC_COUNT \
    (sizeof(blocked_macs) / sizeof(blocked_macs[0]))

şeklinde hesaplandı.

Böylece firewall'ın mantığı:

Tek blocked MAC
        ↓
MAC array
        ↓
blocked_macs[0]
blocked_macs[1]
blocked_macs[2]
        ↓
liste içerisinde arama

haline getirildi.

2. MAC Karşılaştırma Fonksiyonunun Ayrılması

MAC kontrolü hook callback içerisinde doğrudan yapılmak yerine ayrı bir fonksiyona taşındı:

static bool is_mac_blocked(
    const unsigned char *mac)
{
    int i;

    for(i = 0; i < BLOCKED_MAC_COUNT; i++)
    {
        if(ether_addr_equal(
                mac,
                blocked_macs[i]))
        {
            return true;
        }
    }

    return false;
}

Buradaki algoritma:

Source MAC
    ↓
blocked_macs[0]
    ↓
eşleşmedi
    ↓
blocked_macs[1]
    ↓
eşleşmedi
    ↓
blocked_macs[2]
    ↓
eşleşti
    ↓
true

şeklindedir.

Hook tarafında:

if(is_mac_blocked(eth->h_source))
    return NF_DROP;

kullanılarak packet'ın düşürülmesi sağlandı.

Bu çalışma ile rule kontrolü ile packet-processing kodunun birbirinden ayrılması konusunda ilk adım atıldı.

3. Gerçek Layer-2 Komşuların İncelenmesi

Test için:

ip neigh

kullanıldı.

Sistemde örnek olarak:

10.10.2.1 dev wlo1 lladdr 00:0d:48:30:f7:c9
10.10.3.60 dev wlo1 lladdr cc:47:40:02:b2:da

gibi gerçek Layer-2 komşular görüldü.

Burada önceki gün öğrenilen önemli bilgi tekrar uygulamaya bağlandı:

IP
 ↓
ARP / Neighbor table
 ↓
MAC
 ↓
Ethernet frame

MAC filtering'in Internet üzerinde uçtan uca değil, firewall'ın doğrudan görebildiği local Layer-2 segment üzerinde anlamlı olduğu pekiştirildi. Önceki gün de router'ın sonraki link için yeni Ethernet frame oluşturması nedeniyle MAC adresinin uçtan uca kimlik olmadığı incelenmişti.

4. Firewall'ın Layer 3'e Çıkarılması

MAC filtering tamamlandıktan sonra skb içerisinden IPv4 header okunmaya başlandı.

Header:

#include <linux/ip.h>

ve:

struct iphdr *iph;

iph = ip_hdr(skb);

kullanıldı.

Artık packet:

struct sk_buff
       │
       ├── Ethernet Header
       │      ├── Source MAC
       │      └── Destination MAC
       │
       └── IPv4 Header
              ├── Source IP
              ├── Destination IP
              └── Protocol

şeklinde incelenebilir hale geldi.

Kernel logunda IPv4 adreslerinin gösterilmesi için:

pr_info(
    "SRC_IP=%pI4 DST_IP=%pI4\n",
    &iph->saddr,
    &iph->daddr
);

yapısı kullanıldı.

5. Source IP Filtering

Firewall'a ikinci bir filtre katmanı eklendi.

Engellenecek IP:

static __be32 blocked_ip;

olarak tanımlandı.

Module init sırasında:

blocked_ip = in_aton("10.10.3.60");

ile network byte order'daki IPv4 değerine dönüştürüldü.

Packet geldiğinde:

if(iph->saddr == blocked_ip)
{
    return NF_DROP;
}

kontrolü gerçekleştirildi.

Böylece:

PACKET
   ↓
Source MAC
   ↓
MAC blacklist?
   │
   ├── YES → DROP
   │
   ▼
Source IP
   ↓
IP blacklist?
   │
   ├── YES → DROP
   │
   ▼
devam

mimarisi oluşturuldu.

6. Layer 2 ve Layer 3 Filtering Farkı

Bugünkü implementasyon iki filtering türünün farkını da netleştirdi:

Filtering	Katman	İncelenen bilgi	Kullanım alanı
MAC filtering	Layer 2	Ethernet address	Local link
IP filtering	Layer 3	IPv4 address	Routed IP network

Bu noktada firewall artık yalnız Ethernet seviyesinde çalışan bir MAC blacklist olmaktan çıktı.

7. IPv4 Protocol Alanının İncelenmesi

Sonraki aşamada:

iph->protocol

alanı kullanıldı.

Bu alan üzerinden:

IPv4
 │
 └── protocol
       │
       ├── IPPROTO_TCP
       │
       ├── IPPROTO_UDP
       │
       └── IPPROTO_ICMP

ayrımı yapılabileceği görüldü.

Kod:

if(iph->protocol == IPPROTO_TCP)
{
    ...
}

else if(iph->protocol == IPPROTO_UDP)
{
    ...
}

şeklinde geliştirildi.

8. TCP Header Parsing

TCP packet tespit edildiğinde:

#include <linux/tcp.h>

ve:

struct tcphdr *tcph;

tcph = tcp_hdr(skb);

kullanıldı.

Böylece:

TCP HEADER
   │
   ├── source port
   └── destination port

bilgileri okunabilir hale geldi.

Portlar network byte order'da bulunduğu için:

ntohs(tcph->source)
ntohs(tcph->dest)

kullanıldı.

Kernel log:

pr_info(
    "TCP SRC_PORT=%u DST_PORT=%u\n",
    ntohs(tcph->source),
    ntohs(tcph->dest)
);

şeklinde oluşturuldu.

9. TCP Destination Port Filtering

TCP header parsing sonrasında destination port 5001 için firewall rule eklendi:

if(ntohs(tcph->dest) == 5001)
{
    pr_info(
        "BLOCKED TCP DST PORT 5001\n"
    );

    return NF_DROP;
}

Packet path:

TCP SYN
   │
   │ destination = 5001
   ▼
NIC
   ↓
Driver
   ↓
skb
   ↓
PRE_ROUTING
   ↓
IPv4
   ↓
TCP
   ↓
Destination Port = 5001
   ↓
NF_DROP

şeklinde oluşturuldu.

Böylece belirli bir TCP servisine gelen connection daha socket seviyesine ulaşmadan kernel packet path üzerinde engellenebilir hale geldi.

10. UDP Header Parsing

UDP için:

#include <linux/udp.h>

eklendi.

Packet:

struct udphdr *udph;

udph = udp_hdr(skb);

ile parse edildi.

Ardından:

ntohs(udph->source)
ntohs(udph->dest)

ile UDP source ve destination port bilgileri okunmaya başlandı.

Bu aşamada UDP için DROP rule eklenmedi; header parsing ve logging altyapısı oluşturuldu.

11. C Scope Hatasının Görülmesi

Geliştirme sırasında önemli bir C programlama problemiyle karşılaşıldı.

Başlangıçta:

if(iph->protocol == IPPROTO_TCP)
{
    struct tcphdr *tcph;

    ...
}

if(ntohs(tcph->dest) == 5001)
{
    ...
}

şeklinde bir yapı oluşturulmuştu.

Buradaki problem:

{
    struct tcphdr *tcph;
}
        ↓
scope biter
        ↓
tcph artık kullanılamaz

olmasıdır.

Bu nedenle port kontrolü TCP bloğunun içerisine taşındı:

if(iph->protocol == IPPROTO_TCP)
{
    struct tcphdr *tcph;

    ...

    if(ntohs(tcph->dest) == 5001)
        return NF_DROP;
}

Bu çalışma kernel kodu üzerinden C'deki block scope kavramının tekrar görülmesini sağladı.

12. Firewall'ın Gün Sonundaki Yapısı

Bugün sonunda firewall'ın packet decision tree'si:

                     PACKET
                        │
                        ▼
                  PRE_ROUTING
                        │
                        ▼
                       skb
                        │
                        ▼
                Ethernet Header
                        │
                        ▼
                  Source MAC
                        │
                 blacklist?
                   /        \
                 YES        NO
                  │          │
                  ▼          ▼
               NF_DROP     IPv4
                              │
                              ▼
                         Source IP
                              │
                         blacklist?
                          /       \
                        YES       NO
                         │         │
                         ▼         ▼
                      NF_DROP   Protocol
                                  │
                       ┌──────────┴─────────┐
                       │                    │
                      TCP                  UDP
                       │                    │
                       ▼                    ▼
                  TCP Header           UDP Header
                       │                    │
                       ▼                    ▼
                 Destination Port       Port Log
                       │
                    == 5001?
                     /    \
                   YES    NO
                    │      │
                    ▼      ▼
                 NF_DROP NF_ACCEPT

haline geldi.

13. Netfilter Ödevinin Tamamlanan Kapsamı

Hocanın:

Netfilter ile firewall uygulaması
        +
belirli MAC adreslerini banlama

hedefi tamamlandı.

Bunun üzerine çalışma genişletilerek:

MAC Filtering
     +
IP Filtering
     +
Protocol Parsing
     +
TCP Port Filtering
     +
UDP Parsing

seviyesine ulaşıldı.

Önceki gün Netfilter'ın skb üzerinden Ethernet header'a erişip source MAC'e göre verdict vermesiyle başlanan çalışma bugün:

skb
 ↓
Ethernet
 ↓
IPv4
 ↓
TCP / UDP
 ↓
Firewall Decision

seviyesine getirildi.

14. TCP Accelerator Konusuna Dönüş

Firewall çalışmasının ardından hocanın diğer büyük konusu olan TCP Accelerator tekrar ele alındı.

Problem:

HIGH BANDWIDTH
       +
HIGH RTT
       ↓
HIGH BDP
       ↓
çok miktarda data'nın
in-flight olması gerekir
       ↓
TCP window / congestion
state kritik hale gelir

şeklinde modellendi.

Örnek olarak:

Bandwidth = 100 Mbit/s
RTT       = 500 ms

için:

BDP = 100 Mbit/s × 0.5 s
    = 50 Mbit
    = 6.25 MB

hesaplandı.

Yani hattın kapasitesini tamamen kullanabilmek için yaklaşık MB seviyesinde verinin ACK beklerken network üzerinde bulunabilmesi gerekebilir.

15. Sadece TCP Window Alanını Değiştirmenin Yetersizliği

Önemli bir tasarım problemi incelendi.

Basit yaklaşım:

SERVER
   │
   │ Window = küçük
   ▼
ACCELERATOR
   │
   │ Window = büyük göster
   ▼
CLIENT

şeklinde düşünülebilir.

Ancak receiver'ın gerçekten yeterli buffer kapasitesi yoksa yalnız TCP header içerisindeki advertised-window bilgisini büyütmek problemi çözmez.

Çünkü:

Büyük window advertise et
          ↓
Sender daha fazla data gönderir
          ↓
Bu data'nın gerçekten
bir yerde tutulması gerekir

sonucuna ulaşılır.

16. Split-TCP / PEP Yaklaşımı

Bunun üzerine daha gerçekçi accelerator mimarisi ele alındı:

CLIENT              ACCELERATOR               SERVER
   │                     │                       │
   │ TCP CONNECTION #1   │ TCP CONNECTION #2     │
   │<───────────────────>│<─────────────────────>│

Tek connection:

Client ←────────────→ Server

yerine iki bağımsız TCP connection oluşturulur:

Client ←→ Accelerator

Accelerator ←→ Server

Bu durumda her TCP connection'ın bağımsız:

cwnd
rwnd
RTT
RTO
send buffer
receive buffer

state'i bulunur.

Bu yaklaşım Split TCP / Performance Enhancing Proxy (PEP) tasarımının temeli olarak incelendi.

17. Accelerator Buffer Mantığı

Accelerator'ın yalnız packet header değiştiren bir cihaz değil, gerektiğinde veriyi gerçekten buffer'layabilen bir ara nokta olması gerektiği görüldü:

CLIENT
   │
   ▼
┌──────────────────────────────┐
│         ACCELERATOR          │
│                              │
│       receive socket         │
│             ↓                │
│       ┌─────────────┐        │
│       │   BUFFER    │        │
│       │ DATA DATA   │        │
│       └─────────────┘        │
│             ↓                │
│        send socket           │
│                              │
└──────────────┬───────────────┘
               │
               ▼
             SERVER

Bu model daha önce öğrenilen socket buffering ve TCP receive-window kavramlarını doğrudan accelerator tasarımına bağladı.

18. Yazılacak TCP Accelerator Prototipinin Tasarımı

Bir sonraki implementasyon için userspace Split-TCP proxy tasarlandı:

CLIENT
   │
   │ TCP
   ▼
ACCELERATOR
   │
   │ TCP
   ▼
SERVER

Accelerator programı hem server hem client davranışı gösterecektir:

socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
   ↓
client_fd
   │
   ├──────── BUFFER ────────┐
   │                        │
   │                        ▼
   │                    server_fd
   │                        │
   │                     socket()
   │                        ↓
   │                     connect()
   │                        ↓
   └──────────────────── SERVER

Veri iki yönde aktarılacaktır:

Client → Accelerator → Server

Server → Accelerator → Client
19. Accelerator İçin Planlanan Geliştirme Aşamaları

Kodun aşamalı olarak:

tcp_accelerator.c
        │
        ├── Listening socket
        ├── accept()
        ├── Server socket
        ├── connect()
        ├── Client → Server forwarding
        ├── Server → Client forwarding
        ├── Buffering
        ├── SO_RCVBUF
        ├── SO_SNDBUF
        ├── O_NONBLOCK
        ├── epoll
        └── Multi-client

şeklinde geliştirilmesine karar verildi.

Burada daha önce çalışılan:

TCP socket
blocking / non-blocking
O_NONBLOCK
EAGAIN
epoll
TCP buffer
rwnd
cwnd

konuları tek bir projede birleştirilecektir.

20. 18. Gün → 19. Gün İlerlemesi

Önceki gün:

18. GÜN
────────────────────────────

Gerçek TCP state
      ↓
ss -tin / tcpdump
      ↓
RTT / RTO / cwnd
      ↓
BDP deney altyapısı
      ↓
Netfilter
      ↓
PRE_ROUTING
      ↓
skb
      ↓
Ethernet Header
      ↓
Source MAC
      ↓
MAC Blacklist
      ↓
İlk Firewall Kernel Module

seviyesine ulaşılmıştı.

Bugün:

19. GÜN
────────────────────────────────

MAC Blacklist Array
        ↓
Blacklist Traversal
        ↓
ether_addr_equal()
        ↓
Gerçek Layer-2 komşular
        ↓
IPv4 Header Parsing
        ↓
Source IP Filtering
        ↓
iph->protocol
        ↓
TCP / UDP ayrımı
        ↓
TCP Header Parsing
        ↓
Source / Destination Port
        ↓
TCP Port 5001 Filtering
        ↓
NF_DROP
        ↓
NETFILTER FIREWALL TAMAMLANDI
        ↓
TCP Accelerator problem analizi
        ↓
High RTT + High Bandwidth
        ↓
BDP
        ↓
Window / Buffer problemi
        ↓
Split TCP / PEP
        ↓
Userspace Accelerator Tasarımı

seviyesine gelindi.

21. Sonraki Çalışma Rotası

Bir sonraki gün doğrudan kod tarafına geçilecektir:

~/tasks/network/tcp_accelerator/
              │
              ▼
      tcp_accelerator.c
              │
              ▼
      Listening socket
              │
              ▼
           accept()
              │
              ▼
          client_fd
              │
              ▼
        Server socket
              │
              ▼
          connect()
              │
              ▼
          server_fd
              │
              ▼
CLIENT ←→ ACCELERATOR ←→ SERVER
              │
              ▼
        Data Forwarding
              │
              ▼
           Buffering
              │
              ▼
     SO_RCVBUF / SO_SNDBUF
              │
              ▼
         Non-blocking
              │
              ▼
            epoll

İlk hedef tek client destekleyen blocking Split-TCP proxy olacaktır. Mimari doğrulandıktan sonra non-blocking I/O ve epoll eklenerek daha gerçekçi accelerator mimarisine geçilecektir.

Gün Sonu Değerlendirmesi

On dokuzuncu günün en önemli kazanımı, önceki gün başlatılan Netfilter çalışmasının yalnız MAC blacklist seviyesinde bırakılmayıp Ethernet → IPv4 → TCP/UDP katmanlarını okuyabilen küçük bir kernel firewall'a dönüştürülmesi oldu. Böylece daha önce ayrı ayrı öğrenilen struct sk_buff, Ethernet header, IP header, transport protocol ve TCP/UDP port kavramları aynı packet-processing zinciri içerisinde uygulandı.

Aynı zamanda Netfilter çalışmasının tamamlanmasının ardından TCP performans konusuna geri dönülerek hocanın TCP Accelerator fikri sistem seviyesinde modellendi. Yalnız advertised window değerini değiştirmek yerine buffer kapasitesi ve bağımsız TCP state'lerinin neden önemli olduğu görüldü ve Split-TCP/PEP tabanlı userspace accelerator mimarisi tasarlandı.

Günün sonunda ulaşılan seviye:

NETWORK DRIVER
      +
skb / PACKET PATH
      +
NETFILTER
      +
MAC FILTERING
      +
IP FILTERING
      +
TCP/UDP PARSING
      +
PORT FILTERING
      │
      ▼
KERNEL FIREWALL
      │
      ▼
HIGH RTT / BDP
      +
TCP FLOW & CONGESTION CONTROL
      +
BUFFERING
      +
SPLIT TCP / PEP
      │
      ▼
TCP ACCELERATOR TASARIMI

oldu.