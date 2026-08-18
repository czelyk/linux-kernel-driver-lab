STAJ RAPORU – 11. GÜN
TCP Socket Programlama, TCP Packet Analizi, Raw Socket ile Packet Capture, PACKET_MMAP ve eBPF/XDP Çalışmaları

Onuncu gün sonunda UDP client-server uygulaması üzerinden Linux kernel socket yönetimi ayrıntılı olarak incelenmiş; ephemeral port, autobind, 5-tuple, socket lookup, socket receive queue, blocking I/O, NAT, conntrack ve packet capture kavramlarına giriş yapılmıştı.

On birinci gün çalışmalarında networking konusu daha alt seviyeye taşınarak TCP iletişimi, gerçek packet capture, Ethernet/IPv4/TCP/UDP header parsing, Linux AF_PACKET raw socket mekanizması, yüksek performanslı packet capture için PACKET_MMAP/TPACKET_V3 ve son olarak eBPF/XDP mimarisi üzerinde çalışıldı.

1. TCP Client-Server İletişiminin İncelenmesi

TCP tarafında client-server iletişimi çalıştırılarak TCP socket'in local ve remote endpoint bilgileri gözlemlendi.

Gerçek client çalıştırmasında;

Connected to 127.0.0.1:5001
Client local endpoint: 127.0.0.1:45872
Sent 18 bytes: merhaba tcp server
Received 17 bytes from server: zdravo tcp client

çıktısı elde edildi.

Böylece TCP connection için;

CLIENT


Local
127.0.0.1:45872


        ↓ TCP connection ↓


Remote
127.0.0.1:5001

ilişkisi gözlemlendi.

Client tarafındaki 45872 portunun client socket'in local/ephemeral portu, 5001 portunun ise server'ın dinlediği port olduğu değerlendirildi.

2. TCP Trafiğinin tcpdump ile Gözlemlenmesi

Loopback interface üzerindeki TCP trafiğini gözlemlemek amacıyla;

sudo tcpdump -i lo tcp port 5001

kullanıldı.

Capture sonucunda TCP bağlantısının başlangıcındaki;

[S]
[S.]
[.]

flag kombinasyonları gözlemlendi.

Bunların;

CLIENT                     SERVER


SYN
  ------------------------>


             SYN + ACK
  <------------------------


ACK
  ------------------------>


        ESTABLISHED

şeklindeki TCP three-way handshake mekanizmasını temsil ettiği incelendi.

Daha sonra payload taşıyan packet'larda PSH + ACK, bağlantının kapatılması sırasında ise FIN + ACK flag'leri gözlemlendi.

Böylece Socket API seviyesinde kullanılan;

connect()
send()
recv()
close()

işlemlerinin network üzerinde gerçek TCP segmentlerine karşılık geldiği görüldü.

3. Loopback Interface Kavramının İncelenmesi

TCP client ve server aynı sistem üzerinde çalıştırıldığı için trafik;

127.0.0.1

üzerinden gerçekleşti.

Bu adresin Linux loopback interface'i olan;

lo

ile ilişkili olduğu incelendi.

Temel model;

Application A
     ↓
TCP/IP stack
     ↓
    lo
     ↓
TCP/IP stack
     ↓
Application B

şeklinde oluşturuldu.

Bu iletişimde fiziksel Ethernet veya Wi-Fi ağına çıkılması gerekmediği görüldü.

4. Raw Socket ile Packet Capture Programının Oluşturulması

tcpdump ile yapılan gözlemden sonra packet capture mekanizmasının C ile doğrudan gerçekleştirilmesi üzerinde çalışıldı.

Bunun için;

socket(
    AF_PACKET,
    SOCK_RAW,
    htons(ETH_P_ALL)
);

kullanıldı.

Burada;

AF_PACKET
    ↓
link-layer packet erişimi


SOCK_RAW
    ↓
raw packet erişimi


ETH_P_ALL
    ↓
tüm Ethernet protocol tipleri

olarak değerlendirildi.

Packet verisinin tutulması için;

unsigned char buffer[65536];

oluşturuldu ve;

recv(
    sockfd,
    buffer,
    sizeof(buffer),
    0
);

ile kernel'den bir packet userspace buffer'a alındı.

5. Ethernet Header Parsing

Capture edilen byte dizisinin başlangıcının Ethernet header olarak yorumlanması için;

struct ethhdr *eth;


eth = (struct ethhdr *)buffer;

kullanıldı.

Böylece buffer;

buffer
  ↓
+---------------------+
| Ethernet Header     |
+---------------------+
| Network-layer data  |
+---------------------+

şeklinde yorumlandı.

Ethernet header içerisinden destination ve source MAC adresleri;

eth->h_dest
eth->h_source

üzerinden okundu.

Ayrıca;

eth->h_proto

ile EtherType alanı incelendi.

Gerçek capture sırasında;

EtherType: 0x0800

değeri gözlemlendi ve bunun IPv4 packet'i ifade ettiği görüldü.

6. Packet Boyutu ve Güvenli Header Erişimi

Packet buffer içerisindeki bir header'a erişmeden önce packet'ın yeterli büyüklükte olup olmadığının kontrol edilmesi gerektiği incelendi.

Ethernet için;

if(received_bytes < (ssize_t)sizeof(struct ethhdr))
{
    ...
}

kontrolü oluşturuldu.

IPv4 header okunmadan önce ise;

if(received_bytes <
   (ssize_t)(sizeof(struct ethhdr) +
             sizeof(struct iphdr)))
{
    ...
}

kontrolü gerçekleştirildi.

Böylece packet parsing sırasında buffer sınırlarının dışına çıkılmasının önlenmesi gerektiği görüldü.

7. IPv4 Header Parsing

Ethernet header'dan sonraki byte'ların IPv4 header olarak yorumlanması için;

ip = (struct iphdr *)
     (buffer + sizeof(struct ethhdr));

kullanıldı.

Packet memory layout;

buffer
  ↓
+----------------------+ 0
| Ethernet Header      |
+----------------------+ 14
| IPv4 Header          |
+----------------------+
| TCP / UDP / ICMP     |
+----------------------+
| Payload              |
+----------------------+

şeklinde değerlendirildi.

IPv4 source ve destination adreslerinin okunabilir string biçimine çevrilmesi için;

inet_ntop()

kullanıldı.

Bunun için;

#include <arpa/inet.h>

header'ının gerekli olduğu görüldü.

8. IPv4 Header Uzunluğunun Hesaplanması

IPv4 header'ın her zaman yalnızca sizeof(struct iphdr) kadar kabul edilmemesi gerektiği incelendi.

Header uzunluğu;

ip_header_len = ip->ihl * 4;

ile hesaplandı.

Bunun nedeni ihl alanının uzunluğu 32-bit word biriminde tutmasıdır.

Örneğin;

ihl = 5


5 × 4 byte
   =
20 byte

şeklinde standart IPv4 header uzunluğu elde edilmektedir.

Bu bilgi transport-layer header'ın gerçek başlangıç adresinin hesaplanmasında kullanıldı.

9. TCP ve UDP Header Parsing

IPv4 header içerisindeki;

ip->protocol

alanına göre transport protocol ayrımı gerçekleştirildi.

TCP için;

if(ip->protocol == IPPROTO_TCP)

UDP için;

else if(ip->protocol == IPPROTO_UDP)

ve ICMP için;

else if(ip->protocol == IPPROTO_ICMP)

kontrolleri kullanıldı.

TCP header'ın adresi;

tcp = (struct tcphdr *)
      (buffer +
       sizeof(struct ethhdr) +
       ip_header_len);

şeklinde hesaplandı.

Böylece;

Ethernet
   ↓
IPv4
   ↓
TCP

header zinciri gerçek packet memory layout üzerinde takip edildi.

TCP source ve destination portları;

ntohs(tcp->source)
ntohs(tcp->dest)

ile host byte order'a dönüştürülerek incelendi.

UDP için de benzer şekilde;

udp = (struct udphdr *)
      (buffer +
       sizeof(struct ethhdr) +
       ip_header_len);

kullanıldı.

10. TCP Flag Alanlarının İncelenmesi

TCP header içerisindeki;

SYN
ACK
FIN
RST
PSH
URG

flag'lerinin packet davranışını belirleyen önemli alanlar olduğu incelendi.

Daha önce tcpdump çıktısında görülen;

S
S.
.
P.
F.

ifadeleri ile TCP header içerisindeki gerçek flag bitleri arasında bağlantı kuruldu.

Böylece TCP handshake, data transfer ve connection termination mekanizmalarının header seviyesindeki karşılıkları daha ayrıntılı hale getirildi.

11. recv() Tabanlı Packet Capture'ın Çalışma Modeli

İlk raw socket uygulamasında her;

recv()

çağrısında kernel'den userspace'e packet verisi alınmaktadır.

Temel model;

NIC
 ↓
driver
 ↓
kernel networking
 ↓
AF_PACKET socket
 ↓
recv()
 ↓
userspace buffer
 ↓
packet parsing

şeklinde değerlendirildi.

Yüksek packet hızlarında her packet için syscall/copy maliyetlerinin performans açısından önemli hale gelebileceği görüldü.

12. PACKET_MMAP Mekanizmasına Geçiş

Daha yüksek performanslı packet capture mekanizmasını incelemek amacıyla Linux PACKET_MMAP yapısına geçildi.

Bu yöntemde;

recv()
recv()
recv()
recv()

şeklinde her packet için klasik çağrı yapmak yerine kernel ile userspace arasında paylaşılabilen bir ring yapısı oluşturulması incelendi.

Programda;

version = TPACKET_V3;

kullanılarak TPACKET_V3 seçildi.

Ardından;

setsockopt(
    sockfd,
    SOL_PACKET,
    PACKET_VERSION,
    &version,
    sizeof(version)
);

ile packet socket için istenen TPACKET sürümü kernel'e bildirildi.

13. TPACKET_V3 Ring Yapısının Oluşturulması

Ring parametreleri;

#define BLOCK_SIZE (1 << 20)
#define BLOCK_NR 4
#define FRAME_SIZE 2048

şeklinde belirlendi.

Burada;

BLOCK_SIZE = 1 MiB
BLOCK_NR   = 4
FRAME_SIZE = 2048 byte

olarak kullanıldı.

Toplam ring büyüklüğü;

1 MiB × 4
=
4 MiB

olarak oluşturuldu.

Frame sayısı ise;

req.tp_frame_nr =
    (req.tp_block_size *
     req.tp_block_nr) /
     req.tp_frame_size;

ile hesaplandı.

14. mmap() ile Ring Buffer'ın Userspace'e Map Edilmesi

Kernel tarafından packet socket için oluşturulan ring;

ring = mmap(
    NULL,
    ring_size,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    sockfd,
    0
);

ile process'in virtual address space'ine map edildi.

Bu yapı;

                 PHYSICAL RAM
                     │
            packet ring memory
                     │
             ┌───────┴───────┐
             │               │
          KERNEL          USERSPACE
             │               │
          driver /         mmap()
          packet           virtual
          socket           address

şeklinde değerlendirildi.

mmap() işleminin veriyi CPU içerisine taşımadığı; process virtual address space ile ilgili memory pages arasında mapping oluşturduğu üzerinde duruldu.

Virtual address → physical page ilişkisinin MMU ve page table mekanizmaları üzerinden gerçekleştirildiği değerlendirildi.

15. TPACKET Block Ownership Mekanizması

TPACKET_V3 yapısında block'ların kernel ve userspace arasında ownership/state bilgisi üzerinden paylaşıldığı incelendi.

Programda;

if(!(block->hdr.bh1.block_status & TP_STATUS_USER))
{
    usleep(1000);
    continue;
}

ile block'un userspace tarafından okunmaya hazır olup olmadığı kontrol edildi.

Hazır block işlendiğinde;

block->hdr.bh1.block_status = TP_STATUS_KERNEL;

ile block tekrar kernel'e bırakıldı.

Temel döngü;

KERNEL
  ↓
block'a packet'ları yazar
  ↓
TP_STATUS_USER
  ↓
USERSPACE
  ↓
block'u parse eder
  ↓
TP_STATUS_KERNEL
  ↓
KERNEL
  ↓
block tekrar kullanılabilir

şeklinde oluşturuldu.

16. Packet Parsing Kavramının Netleştirilmesi

Packet parsing kavramının, packet'ın raw byte dizisini protocol header yapılarına göre yorumlamak anlamına geldiği incelendi.

Örneğin;

RAW BYTES


↓ parse


Ethernet Header
    ↓
EtherType


↓ parse


IPv4 Header
    ↓
protocol


↓ parse


TCP Header
    ↓
source/destination port


↓ parse


Payload

şeklinde ilerlenmektedir.

Dolayısıyla packet capture ve packet parsing kavramlarının aynı olmadığı;

CAPTURE
→ packet'ı elde et


PARSE
→ elde edilen byte'ların ne anlama geldiğini çöz

ayrımı yapıldı.

17. eBPF Kavramının Ayrıntılı İncelenmesi

Günün devamında eBPF'nin Linux kernel içerisinde belirli hook noktalarında kontrollü programların çalıştırılmasını sağlayan mekanizma olduğu incelendi.

Temel yaşam döngüsü;

eBPF C programı
      ↓
compile
      ↓
eBPF object
      ↓
kernel'e load
      ↓
verifier
      ↓
hook'a attach
      ↓
event / packet
      ↓
eBPF programı çalışır

şeklinde oluşturuldu.

Kernel'e yüklenen programların doğrudan sınırsız şekilde çalıştırılmadığı; önce eBPF verifier tarafından güvenlik ve erişim kuralları açısından kontrol edildiği incelendi.

18. eBPF Verifier ve Packet Boundary Kontrolleri

eBPF/XDP programlarının packet memory'sine erişirken packet sınırlarının kontrol edilmesi gerektiği görüldü.

XDP context üzerinden;

ctx->data
ctx->data_end

bilgileri elde edilmektedir.

Kavramsal yapı;

data                            data_end
 ↓                                ↓
+----------------------------------+
|            PACKET                |
+----------------------------------+

şeklindedir.

Bu yapı daha önce raw socket programında kullanılan;

if(received_bytes < sizeof(struct ethhdr))

kontrolüyle ilişkilendirildi.

Her iki durumda da temel amaç;

packet sınırının dışına çıkma

hatasını önlemektir.

19. XDP Hook Mekanizmasının İncelenmesi

XDP'nin network RX path'in erken aşamalarında eBPF programı çalıştırılmasına olanak sağlayan bir mekanizma olduğu incelendi.

Temel akış;

NIC
 ↓
Network Driver
 ↓
XDP Hook
 ↓
eBPF Program
 ↓
XDP_PASS / XDP_DROP / ...
 ↓
Linux Network Stack

şeklinde oluşturuldu.

XDP_PASS sonucunda packet'ın normal network stack'e devam ettiği, XDP_DROP sonucunda ise packet'ın erken aşamada düşürülebildiği görüldü.

20. İlk XDP/eBPF Programının Yazılması

İlk XDP programı;

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


SEC("xdp")
int xdp_pass_all(struct xdp_md *ctx)
{
    void *data;
    void *data_end;


    data = (void *)(long)ctx->data;
    data_end = (void *)(long)ctx->data_end;


    if(data >= data_end)
        return XDP_ABORTED;


    return XDP_PASS;
}


char LICENSE[] SEC("license") = "GPL";

şeklinde oluşturuldu.

Program;

packet gelir
    ↓
XDP programı
    ↓
data / data_end alınır
    ↓
boundary kontrolü
    ↓
XDP_PASS
    ↓
normal network stack

davranışına sahiptir.

21. eBPF Geliştirme Ortamının Hazırlanması

Ubuntu üzerinde eBPF programının derlenebilmesi için Clang/LLVM araçları kuruldu.

Program;

clang -O2 -g \
    -target bpf \
    -I/usr/include/x86_64-linux-gnu \
    -c xdp_pass.c \
    -o xdp_pass.o

ile derlendi.

İlk denemede;

fatal error: 'asm/types.h' file not found

hatası alındı.

asm/types.h dosyasının;

/usr/include/x86_64-linux-gnu/asm/types.h

altında bulunduğu tespit edildi.

Bu nedenle;

-I/usr/include/x86_64-linux-gnu

include path'i Clang'a açıkça verilerek problem giderildi.

22. eBPF Object Dosyasının Doğrulanması

Başarılı derleme sonucunda;

xdp_pass.o

oluşturuldu.

Dosya;

file xdp_pass.o

ile kontrol edildi ve;

ELF 64-bit LSB relocatable, eBPF,
version 1 (SYSV), with debug_info, not stripped

çıktısı elde edildi.

Böylece;

xdp_pass.c
    ↓
Clang
    ↓
-target bpf
    ↓
xdp_pass.o
    ↓
ELF eBPF object

zinciri gerçek sistem üzerinde başarıyla tamamlandı.

23. Network Interface ve bpftool Ortamının Kontrolü

Sistemdeki network interface'ler;

ip link

ile incelendi.

Aktif wireless interface;

wlo1

olarak gözlemlendi.

Ayrıca;

bpftool version

ile;

bpftool v7.7.0
using libbpf v1.7

ortamının hazır olduğu doğrulandı.

Böylece bir sonraki aşamada oluşturulan xdp_pass.o programının kernel'e yüklenmesi ve XDP hook'a attach edilmesi için gerekli temel hazırlandı.

Gün Sonu Değerlendirmesi

On birinci gün sonunda networking çalışmaları Socket API seviyesinden packet ve kernel seviyesine doğru ilerletildi.

TCP client-server iletişimi gerçek tcpdump capture'ı üzerinden incelenmiş; SYN, SYN/ACK, ACK, PSH ve FIN flag'leri gözlemlenerek application seviyesindeki connect(), send(), recv() ve close() işlemleri gerçek TCP segmentleriyle ilişkilendirilmiştir.

Linux AF_PACKET raw socket kullanılarak C dilinde temel bir packet capture programı geliştirilmiştir. Capture edilen packet'ın Ethernet header'ı parse edilerek source/destination MAC ve EtherType alanları okunmuş; IPv4 packet'larında source/destination IP adresleri ve transport protocol bilgileri elde edilmiştir. TCP ve UDP header'larının packet buffer içerisindeki konumları hesaplanarak port bilgilerinin okunması üzerinde çalışılmıştır.

Daha sonra klasik recv() tabanlı packet capture yaklaşımından PACKET_MMAP ve TPACKET_V3 yapısına geçilmiş; block, frame, ring buffer, mmap(), virtual memory mapping ve kernel/userspace block ownership mekanizmaları incelenmiştir.

Günün ikinci bölümünde eBPF ve XDP mimarisi ayrıntılandırılmış; eBPF programlarının compile, load, verifier ve attach aşamaları incelenmiştir. XDP'nin network driver RX path'inde erken bir hook noktası sağladığı ve packet'ların XDP_PASS, XDP_DROP gibi sonuçlarla yönlendirilebildiği görülmüştür.

İlk XDP programı xdp_pass.c adıyla geliştirilmiş, Clang kullanılarak BPF target için başarıyla derlenmiş ve;

xdp_pass.o:
ELF 64-bit LSB relocatable, eBPF

sonucu elde edilmiştir.

Günün sonunda ulaşılan genel model;

                         NETWORK PACKET
                               ↓
                              NIC
                               ↓
                        Network Driver
                               ↓
                    ┌──────── XDP ────────┐
                    │                     │
                    │    eBPF program     │
                    │                     │
                    └─────────┬───────────┘
                              ↓
                          XDP_PASS
                              ↓
                       Linux Network Stack
                              ↓
                     Ethernet / IPv4
                              ↓
                         TCP / UDP
                              ↓
                           Socket
                              ↓
                         Application




ALTERNATİF GÖZLEM YOLU


Network packet
      ↓
   AF_PACKET
      ↓
PACKET_MMAP / TPACKET_V3
      ↓
 mmap ring buffer
      ↓
packet capture
      ↓
packet parsing
      ↓
Ethernet
      ↓
IPv4
      ↓
TCP / UDP

şeklinde oluşturulmuştur.

Bir sonraki çalışma aşaması olarak xdp_pass.o eBPF programının kernel'e yüklenmesi, verifier sonucunun gözlemlenmesi, wlo1 interface'ine XDP olarak attach edilmesi ve gerçek trafik üzerinde XDP_PASS davranışının test edilmesi planlanmıştır.