STAJ RAPORU – 15. GÜN
Linux Networking – DMA/Descriptor Tekrarı, MMIO Register Erişimi, Packet Capture, TCP Server Mimarileri ve Yüksek Bağlantı Sayıları

On dördüncü gün sonunda PCIe network driver yapısı; PCI enumeration, Vendor/Device ID matching, probe(), BAR/MMIO, hardware descriptor ring, coherent/streaming DMA, interrupt mekanizması, NAPI ve TCP/UDP userspace uygulamalarına kadar uçtan uca incelenmişti.

On beşinci gün çalışmalarında önceki gün öğrenilen bazı düşük seviye driver kavramları derinleştirildi. Özellikle DMA mapping yaşam döngüsü, descriptor-buffer ilişkisi, NIC register erişimi ve volatile kullanımının sınırları üzerinde duruldu. Ardından Linux'taki packet capture yöntemleri karşılaştırıldı. Günün ikinci bölümünde TCP tarafına geçilerek senkron ve asenkron server mimarileri, blocking/non-blocking socket yapısı, select/poll/epoll, TCP port ve connection ilişkisi ile çok yüksek sayıda eşzamanlı bağlantının Linux üzerinde nasıl yönetilebileceği incelendi.

1. DMA Mapping ve Cache İlişkisinin Tekrarı

DMA kullanıldığında CPU'nun cache sisteminin ortadan kalkmadığı üzerinde duruldu.

Temel yapı:

CPU
 │
 ├── Cache
 │
 ▼
RAM
 ▲
 │
DMA
 │
NIC

NIC, DMA üzerinden RAM'e erişirken CPU aynı memory alanının cache içerisinde bulunan bir kopyasını kullanabilir.

Bu nedenle özellikle non-coherent sistemlerde CPU ve device'ın aynı buffer hakkında farklı veri görmemesi için DMA API'nin doğru kullanılması gerekir.

Streaming DMA mapping sırasında yön bilgisi önemlidir:

DMA_TO_DEVICE
    ↓
CPU hazırladı
    ↓
Device okuyacak


DMA_FROM_DEVICE
    ↓
Device yazacak
    ↓
CPU okuyacak

TX packet buffer'ı tipik olarak:

dma_map_single(..., DMA_TO_DEVICE)

RX packet buffer'ı ise:

dma_map_single(..., DMA_FROM_DEVICE)

mantığıyla ele alınır.

2. dma_unmap_single() ve DMA Sync Farkı

Streaming DMA mapping'in tamamen sonlandırılması ile aynı mapping'in korunarak ownership değiştirilmesi arasındaki fark tekrar incelendi.

Bir TX packet için:

skb
 ↓
dma_map_single()
 ↓
NIC DMA READ
 ↓
TX complete
 ↓
dma_unmap_single()
 ↓
skb free

şeklinde bir yaşam döngüsü kullanılabilir.

Burada:

dma_unmap_single()

mapping'in artık kullanılmayacağını ifade eder.

RX gibi uzun süre kullanılan bir buffer'da ise her packet için:

unmap
 ↓
map
 ↓
unmap
 ↓
map

yapmak yerine mapping korunabilir.

Bu durumda:

NIC ownership
      ↓
dma_sync_single_for_cpu()
      ↓
CPU ownership
      ↓
packet processing
      ↓
dma_sync_single_for_device()
      ↓
NIC ownership

modeli kullanılabilir.

Böylece DMA mapping'in yaşam süresi ile buffer ownership kavramları birbirinden ayrılmış oldu.

3. Packet Buffer Belleğinin Ayrılması

Streaming DMA kullanılan packet buffer'ın belleğinin nasıl oluşturulduğu üzerinde duruldu.

Basitleştirilmiş RX modeli:

kmalloc()
   ↓
CPU virtual address
   ↓
dma_map_single()
   ↓
DMA address

Burada:

kmalloc()

buffer için RAM ayırırken:

dma_map_single()

yeni packet belleği oluşturmaz.

Mevcut buffer'ı device'ın DMA yapabileceği şekilde DMA API'ye tanıtır ve gerekli DMA address bilgisini sağlar.

Bu ayrım:

memory allocation
        ≠
DMA mapping

şeklinde netleştirildi.

4. Descriptor Ring ile Packet Buffer Ayrımı

Descriptor ring ile packet buffer havuzunun aynı yapı olmadığı tekrar incelendi.

Örneğin:

RX DESCRIPTOR RING

+-------+-------+-------+-------+
| desc0 | desc1 | desc2 | desc3 |
+---|---+---|---+---|---+---|---+
    |       |       |       |
    ▼       ▼       ▼       ▼
 buffer0 buffer1 buffer2 buffer3

Descriptor:

DMA address
length
status
control

gibi metadata tutarken packet'ın gerçek byte'ları ayrı buffer içerisinde bulunur.

Dolayısıyla:

descriptor ring
      ≠
packet data

ancak descriptor'ların işaret ettiği buffer'lar birlikte bir RX buffer havuzu/ring çalışma düzeni oluşturur.

5. Descriptor ile Buffer İlişkisi ve Ring'in Dönmesi

Bir descriptor'ın her zaman aynı fiziksel buffer'a bağlı olmak zorunda olup olmadığı tartışıldı.

Basit tasarımda:

desc0 → buffer0
desc1 → buffer1
desc2 → buffer2
desc3 → buffer3

şeklinde sabit bir eşleme kullanılabilir.

Ring döndüğünde:

desc0
 ↓
buffer0
 ↓
packet alınır
 ↓
CPU işler
 ↓
buffer tekrar hazırlanır
 ↓
desc0 tekrar NIC'e verilir

olur.

Ancak gelişmiş driver'larda descriptor yeniden doldurulurken farklı bir buffer da atanabilir.

Temel güvenlik kuralı:

NIC'e yeniden verilen descriptor'ın gösterdiği buffer, CPU tarafından hâlâ kullanılıyor olmamalıdır.

Bu nedenle descriptor ownership ve buffer lifetime birbirinden ayrı takip edilmelidir.

6. Descriptor'sız Kalan Buffer

Bir buffer'ın descriptor tarafından işaret edilmemesi buffer'ın RAM'den kaybolduğu anlamına gelmez.

Örneğin:

buffer0
   ↑
CPU pointer

mevcut olduğu sürece CPU buffer'a erişebilir.

Fakat:

desc → DMA address → buffer

bağlantısı kaldırılmışsa NIC artık o buffer'ı mevcut RX descriptor üzerinden kullanmaz.

Dolayısıyla:

CPU'nun buffer'a erişebilmesi
           ≠
NIC'in buffer'ı DMA hedefi olarak kullanabilmesi

ayrımı yapıldı.

7. volatile, Atomic, Lock ve Barrier Ayrımı

Kernel ve hardware programming açısından önemli bir kavram ayrımı yapıldı:

volatile
    ≠
atomic
    ≠
lock
    ≠
memory barrier

volatile, temel olarak compiler optimizasyonlarıyla ilişkili bir C özelliğidir.

Tek başına:

race condition çözmez
mutual exclusion sağlamaz
CPU memory ordering garantisi sağlamaz
MMIO erişimini doğru hale getirmez

Bu nedenle Linux driver içerisinde MMIO register erişimi normal bir volatile pointer üzerinden yapılmak yerine:

void __iomem *

ile temsil edilen MMIO alanında:

readl()
writel()

gibi kernel API'leriyle gerçekleştirilir.

8. NIC Register'larında Ne Tutulduğu

NIC register'larının packet'ın kendisini tutmadığı üzerinde duruldu.

Register'lar daha çok NIC'in kontrol ve durum bilgisini içerir.

Kavramsal register alanı:

NIC REGISTERS
│
├── CONTROL
├── STATUS
├── MAC ADDRESS
│
├── RX CONTROL
├── RX RING BASE
├── RX HEAD
├── RX TAIL
│
├── TX CONTROL
├── TX RING BASE
├── TX HEAD
├── TX TAIL
│
├── INTERRUPT MASK
└── INTERRUPT CAUSE

Driver bunlara:

CPU
 ↓
readl()/writel()
 ↓
MMIO
 ↓
PCIe
 ↓
NIC register

yoluyla erişebilir.

Bu bilgiler descriptor ring ile birlikte NIC'in nasıl çalışacağını belirler.

9. Gerçek Network Driver İnceleme Yaklaşımı

Teorik olarak oluşturulan driver modelinin gerçek Linux Ethernet driver'larında nasıl bulunduğuna bakılması gerektiği üzerinde duruldu.

Gerçek driver incelerken kodu satır satır rastgele okumak yerine aşağıdaki rota kullanılabilir:

PCI ID table
      ↓
pci_driver
      ↓
probe()
      ↓
pci_enable_device()
      ↓
BAR/MMIO
      ↓
DMA setup
      ↓
net_device allocation
      ↓
RX/TX ring setup
      ↓
NAPI
      ↓
IRQ/MSI-X
      ↓
ndo_open()
      ↓
ndo_start_xmit()
      ↓
TX descriptors
      ↓
IRQ
      ↓
NAPI poll
      ↓
RX descriptors
      ↓
skb

Böylece daha önce teorik olarak öğrenilen kavramların gerçek kernel source içerisindeki karşılıklarının bulunması amaçlandı.

10. Linux Packet Capture Yöntemleri

Linux'ta network verisini yakalamanın farklı seviyelerde yapılabileceği incelendi.

Genel yapı:

                     PACKET
                        │
                       NIC
                        │
                      DRIVER
                        │
             ┌──────────┼───────────┐
             │          │           │
            XDP       skb        AF_PACKET
             │          │           │
             │      Network Stack   │
             │          │           │
             └──────────┴───────────┘
                        │
                    USERSPACE

Temel yaklaşımlar:

AF_PACKET + recv()
PACKET_MMAP
XDP
AF_XDP
libpcap/tcpdump

olarak ele alındı.

11. Copy-Based Packet Capture

Klasik capture modelinde:

NIC
 ↓ DMA
kernel memory
 ↓
skb
 ↓
copy
 ↓
userspace buffer

şeklinde kopyalama gerçekleşebilir.

Basitliği önemli avantajıdır ancak yüksek packet rate durumlarında memory copy ve syscall maliyetleri önemli hale gelebilir.

12. Memory-Mapped Capture

PACKET_MMAP yaklaşımında kernel tarafından oluşturulan packet ring userspace'e mmap() ile açılabilir.

Kernel packet ring
        ║
        ║ mmap
        ║
Userspace

Amaç klasik recv() tabanlı yaklaşımdaki bazı syscall ve copy maliyetlerini azaltmaktır.

13. Zero-Copy Yaklaşımı

Zero-copy kavramı da incelendi.

Amaç packet'ın yol boyunca:

buffer A
 ↓ COPY
buffer B
 ↓ COPY
buffer C

şeklinde sürekli kopyalanmasını azaltmaktır.

Daha gelişmiş yüksek performanslı network yapılarında XDP/AF_XDP gibi mekanizmalar bu amaç doğrultusunda kullanılabilir.

Önemli ayrım:

DMA
≠
zero-copy

DMA yalnız NIC ile RAM arasındaki veri transfer yöntemidir.

DMA sonrasında driver veya userspace yolunda başka kopyalamalar yapılabilir.

14. TCP Senkron/Blocking Server

Günün ikinci bölümünde TCP server mimarileri incelendi.

Basit blocking server:

socket()
 ↓
bind()
 ↓
listen()
 ↓
accept()
 ↓
read()
 ↓
process
 ↓
write()

modeline sahiptir.

accept() veya read() üzerinde gerekli olay henüz oluşmamışsa thread bloklanabilir.

Örneğin:

Client A
   ↓
read(A)
   ↓
veri yok
   ↓
thread bekliyor

Bu sırada başka client'ların uygulama tarafından işlenmesi tek thread'li basit tasarımda gecikebilir.

15. Thread-per-Connection Yaklaşımı

Blocking server'ın bir geliştirmesi olarak her connection için ayrı thread kullanılması ele alındı:

                SERVER
                  │
               accept()
                  │
       ┌──────────┼──────────┐
       ▼          ▼          ▼
    Thread A   Thread B   Thread C
       │          │          │
    Client A   Client B   Client C

Bu model küçük ve orta sayıda connection için anlaşılması kolay olabilir.

Ancak connection sayısı çok yükseldiğinde:

thread stack
scheduler overhead
context switching
cache pressure

gibi maliyetler ortaya çıkar.

16. Non-Blocking Socket

Socket'in:

O_NONBLOCK

modunda çalışması incelendi.

Blocking durumda:

read()
 ↓
data yok
 ↓
thread bekler

iken non-blocking durumda:

read()
 ↓
data yok
 ↓
EAGAIN / EWOULDBLOCK
 ↓
program devam eder

şeklinde çalışır.

Ancak bütün socket'leri sürekli tek tek kontrol etmek CPU açısından verimsiz olacağından event notification mekanizmalarına ihtiyaç duyulur.

17. select, poll ve epoll

Çok sayıda socket'in hazır olup olmadığını yönetmek için:

select
poll
epoll

mekanizmaları karşılaştırıldı.

Temel amaç:

100.000 socket var

       ↓

hangilerinde şu anda event var?

       ↓

sadece hazır socket'leri işle

şeklindedir.

Özellikle Linux epoll yapısı yüksek connection concurrency için önemli hale gelir.

18. epoll Çalışma Mantığı

Genel model:

                   epoll
                     │
      ┌──────────────┼──────────────┐
      │              │              │
   socket A       socket B       socket C
      │              │              │
      └──────────────┼──────────────┘
                     │
                epoll_wait()
                     │
                     ▼
               READY EVENTS
                     │
            ┌────────┴────────┐
            ▼                 ▼
         socket A          socket C

Uygulama yalnız hazır socket'lerle ilgilenebilir.

Bu yapı:

çok connection
+
az sayıda aktif connection

senaryosunda özellikle değerlidir.

19. Concurrency ve Parallelism Ayrımı

Event-driven server'ın aynı anda çok connection yönetebilmesinin hepsini fiziksel olarak aynı anda çalıştırdığı anlamına gelmediği incelendi.

CONCURRENCY

A → B → C → A → D → ...

tek thread üzerinde yapılabilir.

Parallelism ise:

CPU0 → workload A
CPU1 → workload B
CPU2 → workload C
CPU3 → workload D

şeklinde gerçekten birden fazla CPU core üzerinde eşzamanlı execution anlamına gelir.

Modern server tasarımlarında event-driven yapı ile multi-core parallelism birlikte kullanılabilir.

20. 65.535 Port ile Connection Sayısının Aynı Olmadığı

Hocanın verdiği sorulardan biri:

TCP'de yaklaşık 65 bin port varsa 1 milyon connection nasıl olabilir?

şeklinde ele alındı.

TCP port alanı 16 bittir:

0 – 65535

Ancak bir TCP connection yalnız destination port ile tanımlanmaz.

Temel connection kimliği:

source IP
source port
destination IP
destination port

olarak düşünülebilir.

Örneğin:

10.0.0.1:51001 ──→ 192.168.1.10:443
10.0.0.2:51001 ──→ 192.168.1.10:443
10.0.0.3:42000 ──→ 192.168.1.10:443

aynı server portuna gelen farklı TCP connection'lardır.

Dolayısıyla:

65535 port
≠
65535 maksimum TCP connection

sonucuna ulaşıldı.

21. Listening Socket ve Connected Socket Ayrımı

Server tarafında:

listen_fd
    │
    ├── connection A → fd
    ├── connection B → fd
    ├── connection C → fd
    └── ...

şeklinde bir yapı bulunur.

Bir adet listening socket çok sayıda established connection oluşturabilir.

accept() her başarılı connection için userspace'e yeni bir file descriptor verir.

22. File Descriptor Limiti

Çok yüksek connection sayısında gerçek limitlerden birinin file descriptor sayısı olduğu incelendi.

Linux üzerinde:

ulimit -n

ile process'in açık file descriptor limiti görülebilir.

Socket'ler de file descriptor üzerinden userspace'e sunulduğundan:

1.000.000 connection
       ↓
yaklaşık 1.000.000 socket fd
       +
listen fd
epoll fd
files
logs
...

gibi kaynak gereksinimleri ortaya çıkabilir.

23. Ephemeral Port Kavramı

65 bin port sınırının özellikle client tarafında önemli olabileceği incelendi.

Client bir TCP connection açarken local ephemeral port kullanabilir:

CLIENT

source IP
source ephemeral port
        ↓
SERVER IP
server port

Tek source IP'den aynı destination IP ve destination port'a çok yüksek sayıda connection açılırken available ephemeral port sayısı sınırlayıcı faktörlerden biri olabilir.

Bu nedenle server'ın toplam connection kapasitesi ile tek bir client IP'nin aynı hedefe oluşturabileceği connection sayısı birbirinden ayrıldı.

24. SYN Queue ve Accept Queue

TCP server'a connection gelirken iki önemli kuyruk kavramı ele alındı:

SYN
 ↓
SYN processing
 ↓
SYN queue
 ↓
SYN-ACK
 ↓
ACK
 ↓
handshake complete
 ↓
accept queue
 ↓
accept()
 ↓
application

SYN queue henüz handshake'i tamamlanmamış bağlantılarla, accept queue ise handshake tamamlanmış fakat application tarafından henüz accept() edilmemiş bağlantılarla ilişkilidir.

Bu nedenle çok yüksek connection arrival rate durumunda yalnız application kodu değil kernel TCP queue yapıları da önem kazanır.

25. Bir Milyon Connection Geldiğinde Gerçek Sınırlamalar

“Bir milyon connection gelirse ne olur?” sorusunun tek cevabının port sayısı olmadığı görüldü.

Gerçek sistem:

             1.000.000 CONNECTION
                      │
        ┌─────────────┼──────────────┐
        │             │              │
        ▼             ▼              ▼
       CPU           RAM        File descriptors
        │             │              │
        ├─────────────┼──────────────┤
        │             │              │
        ▼             ▼              ▼
   SYN backlog   accept queue   socket memory
        │
        ▼
   application
   accept rate

gibi birçok kaynağa bağlıdır.

Bunun yanında:

network bandwidth
TCP timers
send/receive buffers
scheduler overhead
kernel socket structures

da ölçeklenebilirliği etkiler.

26. Epoll Server Implementasyonuna Başlanması

Teorik incelemenin ardından konuyu gerçek Linux üzerinde görmek amacıyla yeni bir uygulama çalışmasına başlandı.

Proje yapısı:

network/
│
├── tcp/
├── udp/
│
└── epoll_server/
      │
      ├── epoll_server.c
      └── Makefile

olarak planlandı.

İlk olarak gerekli userspace Linux header'ları belirlendi:

socket API
     +
fcntl / O_NONBLOCK
     +
errno
     +
epoll API

Ardından uygulamanın hedef mimarisi oluşturuldu:

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
   ↓
ready socket
   ↓
accept/read/write

Implementasyon bir sonraki çalışma gününde adım adım devam ettirilecek şekilde hazırlandı.

27. Önceki Driver Çalışmalarıyla Yeni Server Konusunun İlişkisi

Bugünkü TCP server konusu daha önce incelenen driver yapısından bağımsız değildir.

Tam sistem:

                   APPLICATION
                        │
                 epoll TCP server
                        │
                socket / TCP state
                        │
                       TCP
                        │
                       IP
                        │
                    Ethernet
                        │
                       skb
                        │
                  Network Driver
                        │
              ┌─────────┼─────────┐
              │         │         │
             DMA       MMIO      IRQ
              │         │         │
              └─────────┼─────────┘
                        │
                       NIC
                        │
                     NETWORK

şeklinde düşünüldü.

Böylece daha önce:

NIC → driver → skb → TCP → socket

yönünde incelenen yapı bugün:

socket → server architecture → concurrency

seviyesine taşınmaya başlandı.

28. Hocayla Görüşme Sonrasında Belirlenen Yeni Çalışma Konuları

Görüşme sonucunda ilerleyen günlerde çalışılacak networking konuları belirlendi:

TCP
│
├── synchronous / asynchronous server
├── high connection counts
├── ACK
├── retransmission
├── TCP Window Size
├── Sliding Window
├── Receive Window (rwnd)
├── Congestion Window (cwnd)
└── Window Scaling


Linux Networking
│
├── Netfilter
├── firewall
├── packet filtering
└── MAC address filtering


İLERİ PROJE
│
└── TCP Window Accelerator
       │
       ├── TCP window monitoring
       ├── high RTT links
       ├── bandwidth-delay product
       ├── flow control
       ├── buffering
       └── satellite communication use case

Özellikle TCP Window Size konusunun ileride geliştirilecek accelerator projesinin temelini oluşturacağı belirlendi.

Gün Sonu Değerlendirmesi

On beşinci gün sonunda önceki gün oluşturulan PCIe NIC ve network driver modeli üzerinde bazı kritik kavramlar tekrar edilerek derinleştirildi. DMA mapping ile memory allocation'ın farklı işlemler olduğu, coherent ve streaming DMA kullanım modellerinin farklı amaçlara hizmet ettiği ve streaming DMA mapping içerisinde CPU/device ownership geçişlerinin nasıl yönetildiği incelendi.

Descriptor ring ile gerçek packet buffer'larının farklı yapılar olduğu; descriptor'ın packet'ın DMA adresini ve durumunu tarif ettiği, buffer'ın ise gerçek packet byte'larını içerdiği netleştirildi. NIC register'larının descriptor veya packet'ın kendisini değil cihazın kontrol/durum bilgilerini tuttuğu ve MMIO erişimlerinde volatile yerine Linux'un readl()/writel() gibi uygun I/O API'lerinin kullanılmasının gerekliliği ele alındı.

Linux packet capture tarafında klasik copy-based capture, memory-mapped capture ve zero-copy yaklaşımları karşılaştırıldı. DMA ile zero-copy kavramlarının aynı şey olmadığı özellikle ayrıştırıldı.

Günün ikinci yarısında çalışmalar TCP server mimarisine taşındı. Blocking/synchronous server, thread-per-connection ve non-blocking/event-driven server modelleri karşılaştırıldı. select, poll ve özellikle Linux epoll mekanizmasının çok sayıda eşzamanlı connection yönetimindeki yeri incelendi.

TCP'deki 16-bit port alanının maksimum connection sayısını doğrudan belirlemediği; connection'ların source/destination IP ve port kombinasyonlarıyla ayrıldığı öğrenildi. Bir milyon seviyesinde connection hedeflendiğinde file descriptor limitleri, socket memory, CPU/RAM, ephemeral port'lar, SYN backlog, accept queue ve application accept rate gibi gerçek sistem kaynaklarının belirleyici olduğu görüldü.

Son olarak bu teorik çalışmayı uygulamaya dönüştürmek amacıyla non-blocking epoll tabanlı TCP server projesinin dizin yapısı ve temel iskeleti oluşturulmaya başlandı.

Günün genel ilerlemesi şu şekilde özetlenebilir:

14. GÜN
────────────────────────────────

PCI Enumeration
      ↓
probe()
      ↓
BAR / MMIO
      ↓
Descriptor Rings
      ↓
DMA
      ↓
IRQ
      ↓
NAPI
      ↓
skb
      ↓
TCP / UDP
      ↓
Socket
      ↓
Userspace


               │
               ▼


15. GÜN
────────────────────────────────

DMA / Cache
      ↓
DMA Mapping Lifecycle
      ↓
Descriptor ↔ Buffer ilişkisi
      ↓
MMIO Register Access
      ↓
volatile / barrier ayrımı
      ↓
Packet Capture
      ↓
Copy / MMAP / Zero-Copy
      ↓
TCP Server
      ↓
Blocking
      ↓
Non-Blocking
      ↓
select / poll / epoll
      ↓
TCP 4-Tuple
      ↓
Listening / Connected Socket
      ↓
File Descriptor Limits
      ↓
Ephemeral Ports
      ↓
SYN Queue / Accept Queue
      ↓
High Connection Concurrency
      ↓
Epoll Server Implementasyonu
      ↓
TCP ACK / Window / Netfilter
      ↓
TCP WINDOW ACCELERATOR

Böylece çalışma odağı, önceki gün tamamlanan “bir packet fiziksel NIC'ten userspace socket'e nasıl ulaşır?” sorusundan, “userspace ve TCP stack çok yüksek sayıda bağlantıyı nasıl yönetir ve TCP'nin akış mekanizmaları nasıl çalışır?” sorusuna doğru ilerletilmiş oldu.