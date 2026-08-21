STAJ RAPORU – 14. GÜN
Linux PCIe Network Driver – PCI Lifecycle, MMIO, Hardware Descriptor Ring, DMA Ordering, IRQ/NAPI ve TCP/UDP Packet Akışı

On üçüncü gün sonunda Linux network driver çalışmaları RX/TX descriptor ring, streaming DMA, DMA ownership, TX completion ve network queue kontrolü seviyesine getirilmişti. RX tarafında uzun ömürlü DMA mapping modeli; TX tarafında ise skb yaşam süresine bağlı DMA mapping modeli incelenmiş, tx_head ve tx_clean üzerinden producer-consumer yapısı kurulmuştu.

On dördüncü gün çalışmalarında bu yapı daha gerçekçi bir PCIe Ethernet driver mimarisine dönüştürüldü. Önceki gün ayrı ayrı ele alınan descriptor, DMA, IRQ ve NAPI kavramlarının gerçek bir PCIe NIC içerisinde nasıl birbirine bağlandığı incelendi. Ayrıca PCI cihazının keşfedilmesinden başlayarak TCP/UDP userspace uygulamasına kadar uzanan uçtan uca sistem üzerinde çalışıldı.

1. Önceki Gün Oluşturulan Modelden Gerçek NIC Modeline Geçiş

Önceki gün sonunda genel yapı şu seviyeye ulaşmıştı:

Network Stack
     ↓
    skb
     ↓
ndo_start_xmit()
     ↓
TX descriptor
     ↓
DMA mapping
     ↓
    NIC


    NIC
     ↓
RX descriptor
     ↓
DMA WRITE
     ↓
    IRQ
     ↓
   NAPI
     ↓
    skb
     ↓
Network Stack

Özellikle RX tarafında dma_sync_single_for_cpu() / dma_sync_single_for_device() ownership geçişleri, TX tarafında ise completion sonrasında dma_unmap_single() ve skb free işlemleri üzerinde çalışılmıştı.

Bugün bu modelin altında gerçekte bulunan PCIe hardware katmanı eklendi:

                Linux Network Stack
                        │
                  struct net_device
                        │
                  Network Driver
                        │
        ┌───────────────┼───────────────┐
        │               │               │
       MMIO            DMA             IRQ
        │               │               │
        └───────────────┼───────────────┘
                        │
                     PCIe NIC

Böylece network driver'ın yalnızca packet işleyen bir yazılım olmadığı; PCIe cihazını yöneten bir hardware driver olduğu daha net hale getirildi.

2. PCIe Cihazının Sisteme Eklenmesi

Bir PCIe Ethernet cihazı sisteme bağlandığında ilk işlem network stack tarafından yapılmaz.

Öncelikle PCI subsystem cihazı keşfeder.

Temel süreç:

PCIe NIC
   ↓
PCI enumeration
   ↓
PCI Configuration Space okunur
   ↓
Vendor ID
Device ID
Class Code
BAR
Capabilities
   ↓
struct pci_dev

Linux, fiziksel PCI cihazını kernel içerisinde bir:

struct pci_dev

nesnesiyle temsil eder.

Bu aşamada cihaz henüz network interface değildir.

3. Vendor ID ve Device ID

PCI cihazlarının hangi driver tarafından yönetileceğinin belirlenmesinde temel bilgilerden ikisi:

Vendor ID
Device ID

olarak incelendi.

Bunların görevleri:

Vendor ID
    ↓
Cihazın üreticisini tanımlar


Device ID
    ↓
Üreticinin hangi cihaz/modeli olduğunu tanımlar

PCI driver ise desteklediği cihazları bir ID tablosu içerisinde bildirir.

Mantıksal olarak:

PCI DEVICE


Vendor = X
Device = Y
      │
      ▼
PCI CORE
      │
      │ ID tablolarını karşılaştırır
      ▼
PCI DRIVER
      │
      │ MATCH
      ▼
    probe()

Böylece probe() fonksiyonunun cihaz takıldığı için doğrudan çalışan rastgele bir callback olmadığı; PCI core tarafından başarılı device-driver matching sonrasında çağrıldığı görüldü.

4. PCI Driver Registration

Driver module yüklendiğinde:

module_init()
      ↓
pci_register_driver()
      ↓
PCI Core

üzerinden PCI subsystem'e kayıt olur.

Driver temel olarak PCI core'a:

Ben şu PCI cihazlarını destekliyorum.

bilgisini verir.

Ardından PCI core mevcut cihazları driver'ın ID tablosuyla karşılaştırır.

PCI device
     │
     ├── Vendor ID
     └── Device ID
            │
            ▼
      PCI ID matching
            │
       match bulundu
            │
            ▼
          probe()

Bu aşamadan itibaren cihazın gerçek driver initialization işlemleri başlar.

5. probe() Fonksiyonunun Sistemdeki Yeri

probe() cihaz ile driver'ın birbirine bağlandığı temel initialization noktası olarak ele alındı.

Genel süreç:

PCI enumeration
      ↓
struct pci_dev
      ↓
Vendor/Device match
      ↓
probe()
      ↓
pci_enable_device()
      ↓
DMA capability
      ↓
BAR resources
      ↓
MMIO mapping
      ↓
descriptor rings
      ↓
net_device
      ↓
NAPI
      ↓
register_netdev()

Böylece daha önce ayrı ayrı öğrenilmiş olan PCI, DMA ve network driver kavramları aynı initialization zincirine bağlandı.

6. BAR Kavramı

PCI cihazının register'larına CPU'nun erişebilmesi için BAR – Base Address Register yapısı incelendi.

NIC içerisinde kontrol register'ları bulunur.

Örneğin kavramsal olarak:

NIC


+-----------------------+
| CONTROL               |
| STATUS                |
| INTERRUPT MASK        |
| INTERRUPT CAUSE       |
| RX RING BASE          |
| RX HEAD               |
| RX TAIL               |
| TX RING BASE          |
| TX HEAD               |
| TX TAIL               |
+-----------------------+

CPU'nun bunlara doğrudan normal RAM pointer'ı gibi erişmesi yerine PCI BAR üzerinden bir memory region sunulur.

PCI NIC
   │
  BAR
   │
   ▼
Physical MMIO Region
   │
   ▼
ioremap / pci_iomap
   │
   ▼
void __iomem *

Driver böylece cihaz register'larına erişebilir.

7. MMIO – Memory Mapped I/O

MMIO sayesinde NIC register'ları CPU'nun address space'ine map edilir.

Ancak bu alan normal RAM değildir.

Normal RAM


CPU
 ↓
load/store
 ↓
RAM




MMIO


CPU
 ↓
readl()/writel()
 ↓
PCIe
 ↓
NIC register

Bu nedenle kernel driver içerisinde MMIO register erişimleri için kavramsal olarak:

readl()
writel()

gibi operasyonlar kullanılır.

Böylece driver hardware'e komut verebilir ve hardware durumunu okuyabilir.

8. Descriptor'ın Hardware ile İlişkisi

Önceki gün descriptor kavramı yazılımsal bir yapı olarak incelenmişti. Bugün descriptor'ın gerçek NIC tarafından nasıl kullanıldığı üzerinde duruldu.

Descriptor temel olarak NIC'e:

Packet bellekte nerede?
Packet ne kadar uzun?
Buffer kullanılabilir mi?
İşlem tamamlandı mı?

gibi bilgiler verir.

Temel yapı:

Descriptor
│
├── DMA address
├── length
├── command/control
└── status

Burada önemli ayrım:

DESCRIPTOR
    ↓
Packet'ın kendisi değildir
    ↓
Packet'ın RAM'deki yerini ve durumunu tarif eder




BUFFER
    ↓
Gerçek packet byte'larını içerir

olarak netleştirildi.

9. Descriptor Ring'in NIC Tarafından Görülmesi

Descriptor ring yalnızca driver'ın tuttuğu bir C array'i değildir.

NIC'in descriptor'lara erişebilmesi için ring'in DMA erişilebilir bellekte bulunması gerekir.

Bu nedenle gerçekçi model:

CPU
 │
 │ dma_alloc_coherent()
 ▼
RAM
+------+------+------+------+------+
|desc0 |desc1 |desc2 |desc3 | ...  |
+------+------+------+------+------+
             ▲
             │
          PCIe DMA
             │
             ▼
            NIC

haline getirildi.

Descriptor ring için coherent DMA kullanımı ile packet buffer için streaming DMA kullanımı arasındaki fark incelendi.

10. Coherent DMA ve Streaming DMA Ayrımı

Bugünkü önemli ayrımlardan biri descriptor ve packet buffer'ın farklı DMA kullanım karakteristiğine sahip olmasıydı.

DMA
│
├── Coherent DMA
│      ↓
│   descriptor ring gibi
│   CPU ve device tarafından
│   sık paylaşılan yapılar
│
└── Streaming DMA
       ↓
    packet buffer gibi
    belirli bir DMA işlemi için
    map edilen veri

Önceki gün RX/TX packet buffer'larında streaming DMA kullanılmıştı. RX streaming DMA lifecycle'ı önceki raporda map → NIC → sync CPU → CPU → sync device → NIC şeklinde oluşturulmuştu.

Bugün buna descriptor ring'in coherent DMA modeli eklendi.

11. Software Metadata ve Hardware Descriptor Ayrımı

Gerçek bir driver'da hardware'in görmesi gereken bilgiler ile yalnızca driver'ın ihtiyaç duyduğu bilgiler birbirinden ayrılabilir.

Örneğin:

Hardware descriptor
│
├── DMA address
├── length
├── command
└── status




Software metadata
│
├── struct sk_buff *
├── buffer pointer
└── driver bookkeeping

NIC'in:

struct sk_buff *

gibi kernel pointer'larını bilmesine gerek olmadığı görüldü.

NIC yalnızca DMA adresleri ve hardware descriptor formatıyla ilgilenir.

12. TX Packet'ın Descriptor'a Yerleştirilmesi

Network stack driver'a bir packet gönderdiğinde:

Application
    ↓
TCP / UDP
    ↓
IP
    ↓
Ethernet
    ↓
skb
    ↓
ndo_start_xmit()

zinciri oluşur.

Driver:

skb->data

alanını DMA için map eder.

Ardından descriptor'a:

DMA address
packet length
control bits

yazar.

Sonuç:

skb
 │
 ▼
RAM packet buffer
 │
 │ dma_map
 ▼
DMA address
 │
 ▼
TX descriptor

haline gelir.

13. Doorbell / Tail Register

Descriptor'ın RAM'e yazılması NIC'in otomatik olarak yeni packet olduğunu anlaması anlamına gelmez.

Driver NIC'e:

Yeni descriptor hazırladım.

bilgisini vermelidir.

Bu işlem genellikle bir tail/doorbell register'ına MMIO write ile yapılır.

CPU
 │
 ├── descriptor hazırla
 │
 ├── DMA address yaz
 │
 ├── length yaz
 │
 └── tail/doorbell register
             │
             ▼
            NIC

NIC yeni tail değerini gördüğünde yeni descriptor'ları tüketmeye başlayabilir.

14. DMA Memory Ordering

Descriptor alanlarının hardware'e doğru sırada görünmesi gerektiği üzerinde duruldu.

İstenen mantık:

ÖNCE


descriptor->addr
descriptor->len
descriptor->cmd


SONRA


doorbell

olmalıdır.

CPU/compiler ordering nedeniyle driver'ın hardware'e:

Descriptor tamamen hazır olmadan doorbell'i görme.

garantisi vermesi gerekebilir.

Bu bağlamda DMA memory ordering ve:

dma_wmb()
dma_rmb()

kavramları ele alındı.

TX tarafında kavramsal model:

descriptor hazırla
      ↓
dma_wmb()
      ↓
doorbell / tail
      ↓
NIC descriptor'ı görür

olarak oluşturuldu.

15. NIC'in TX DMA İşlemi

NIC descriptor'ı aldıktan sonra descriptor içerisindeki DMA adresini kullanır.

TX descriptor
      ↓
DMA address
      ↓
NIC PCIe DMA READ
      ↓
RAM'deki packet
      ↓
NIC TX engine
      ↓
Ethernet frame
      ↓
Network

Burada CPU packet byte'larını NIC'e tek tek kopyalamaz.

DMA engine RAM'deki packet'ı doğrudan okur.

16. TX Completion

Packet'ın descriptor'a verilmesi ile packet'ın gönderilmesinin aynı olay olmadığı tekrar gerçek hardware modeli üzerinden ele alındı.

CPU descriptor hazırlar
        ↓
NIC descriptor'ı alır
        ↓
DMA READ
        ↓
packet transmit
        ↓
descriptor status update
        ↓
TX completion

NIC işlem tamamlandığında descriptor'ın status bilgisini günceller.

Böylece driver:

NIC artık bu buffer'ı kullanmıyor.

sonucuna ulaşabilir.

Ancak bundan sonra:

dma_unmap_single()
dev_kfree_skb()

işlemlerinin güvenli olduğu önceki gün oluşturulan TX lifetime modeliyle ilişkilendirildi.

17. Interrupt'ın Descriptor'dan Farkı

Bugün özellikle interrupt'ın ne olduğu üzerinde duruldu.

Interrupt:

Descriptor içerisine yeni packet'ın nerede olduğunu yazmak değildir.

Descriptor zaten RAM içerisinde packet durumunu tutar.

Interrupt ise NIC'in CPU'ya:

İlgilenmen gereken yeni bir olay meydana geldi.

demesidir.

Descriptor
    ↓
RAM'deki durum/veri bilgisi




IRQ
    ↓
CPU'ya event notification

Bu ayrım network driver mimarisinin anlaşılması açısından önemli hale geldi.

18. Interrupt Cause

NIC farklı nedenlerle interrupt oluşturabilir.

Örneğin:

Interrupt Cause
│
├── RX complete
├── TX complete
├── Link change
└── Error

Driver interrupt handler içerisinde NIC'in interrupt cause/status register'ını okuyarak:

Bu interrupt neden geldi?

sorusuna cevap verir.

19. INTx, MSI ve MSI-X

PCI interrupt mekanizmaları genel olarak incelendi:

PCI Interrupts
│
├── Legacy INTx
│
├── MSI
│
└── MSI-X

Legacy INTx klasik line-based interrupt modelidir.

MSI ile cihaz interrupt controller'a bir message write gerçekleştirerek interrupt oluşturabilir.

MSI-X ise daha fazla interrupt vector kullanılmasına olanak sağlar.

Özellikle modern multi-queue NIC yapısı:

RX/TX Queue 0 → MSI-X vector 0
RX/TX Queue 1 → MSI-X vector 1
RX/TX Queue 2 → MSI-X vector 2
...

şeklinde ölçeklenebilir.

Bu yapı farklı queue'ların farklı CPU core'ları tarafından işlenebilmesinin temelini oluşturur.

20. Hard IRQ Handler

NIC interrupt oluşturduğunda CPU kernel içerisindeki interrupt handler'a geçer.

Temel yapı:

NIC
 ↓
IRQ
 ↓
Interrupt Controller
 ↓
CPU
 ↓
Hard IRQ Handler

Hard IRQ context içerisinde uzun packet processing yapılmaması gerektiği incelendi.

Handler'ın temel işi:

interrupt cause oku
        ↓
interrupt'ı acknowledge/mask et
        ↓
NAPI schedule et
        ↓
çık

şeklindedir.

21. IRQ ile NAPI Arasındaki İlişki

Önceki gün öğrenilen NAPI mekanizması bugün gerçek interrupt sistemiyle birleştirildi.

NIC
 │
 │ RX packet geldi
 ▼
IRQ
 │
 ▼
Hard IRQ Handler
 │
 ├── interrupt cause oku
 │
 ├── RX interrupt mask
 │
 └── napi_schedule()
          │
          ▼
       NAPI poll

Böylece IRQ'nun packet processing yapmak yerine packet processing mekanizmasını tetiklediği görüldü.

22. Neden NAPI Sırasında Interrupt Maskelenir?

Yüksek packet trafiğinde her packet için ayrı interrupt oluşturulması ciddi CPU maliyetine neden olabilir.

packet
 ↓
IRQ


packet
 ↓
IRQ


packet
 ↓
IRQ


packet
 ↓
IRQ

yerine NAPI:

packet
 ↓
IRQ
 ↓
NAPI başlat
 ↓
IRQ mask
 ↓
packet
packet
packet
packet
 ↓
poll ile toplu işle

modelini kullanır.

Bu nedenle NAPI interrupt ve polling'in birleşimi olarak ele alındı.

23. NAPI Budget

NAPI'nin bir poll çağrısında sonsuz sayıda packet işlemesine izin verilmez.

poll(budget)

ile bir üst sınır verilir.

Örneğin:

budget = 64

ise:

NAPI
 │
 ├── packet 1
 ├── packet 2
 ├── ...
 └── packet 64

seviyesine kadar RX işi yapılabilir.

Bu, önceki gün kullanılan budget/work_done yapısının gerçek interrupt mimarisindeki yerini netleştirdi. Önceki raporda budget'ın bir poll turundaki maksimum packet sayısını, work_done'ın ise gerçekten işlenen packet sayısını temsil ettiği belirlenmişti.

24. NAPI Completion ve Interrupt'ın Yeniden Açılması

NAPI bütün mevcut işi budget dolmadan bitirirse:

RX ring boş
     ↓
napi_complete_done()
     ↓
RX interrupt unmask
     ↓
tekrar interrupt-driven mode

modeline dönülür.

Fakat budget tamamen tüketilmişse daha fazla iş olabileceğinden NAPI devam eder.

work_done == budget
        ↓
muhtemelen daha iş var
        ↓
interrupt açma
        ↓
NAPI tekrar poll

Böylece yoğun trafik altında interrupt sayısı azaltılmış olur.

25. RX Hardware Akışı

RX tarafındaki bütün parçalar birleştirildiğinde:

NETWORK
   ↓
Ethernet frame
   ↓
NIC
   ↓
RX descriptor
   ↓
descriptor'daki DMA address
   ↓
PCIe DMA WRITE
   ↓
RX buffer / RAM
   ↓
descriptor status = DONE
   ↓
IRQ
   ↓
Hard IRQ Handler
   ↓
napi_schedule()
   ↓
NAPI poll
   ↓
descriptor kontrol
   ↓
DMA ownership CPU
   ↓
skb oluştur
   ↓
Network Stack

akışı ortaya çıktı.

Bu, önceki gün kurulan RX descriptor + NAPI modelinin hardware seviyesine genişletilmiş hali oldu.

26. RX Descriptor'ın Yeniden NIC'e Verilmesi

CPU packet'ı aldıktan sonra RX descriptor sonsuza kadar kullanılmış olarak bırakılmaz.

NIC
 ↓
packet DMA
 ↓
DONE
 ↓
CPU packet'ı işler
 ↓
buffer hazırlanır
 ↓
descriptor yeniden arm edilir
 ↓
tail güncellenir
 ↓
NIC tekrar kullanabilir

Böylece RX ring sürekli dönen bir buffer havuzu olarak düşünüldü.

27. TX ve RX Producer–Consumer Yapısı

Önceki gün öğrenilen producer-consumer mantığı hardware modeliyle tekrar ilişkilendirildi. Önceki raporda TX için CPU'nun producer, NIC'in consumer; RX için ise NIC'in producer, CPU'nun consumer olduğu belirlenmişti.

TX:

CPU
 │
 │ descriptor üretir
 ▼
TX Ring
 │
 │ descriptor tüketir
 ▼
NIC

RX:

NIC
 │
 │ packet üretir
 ▼
RX Ring
 │
 │ packet tüketir
 ▼
CPU

olarak düşünüldü.

28. net_device ile PCI Cihazının Birleşmesi

PCI subsystem cihazın hardware tarafını temsil ederken Linux network stack:

struct net_device

üzerinden Ethernet interface'i görür.

Driver bu iki dünyayı birbirine bağlar:

        Linux Network Stack
               │
        struct net_device
               │
             DRIVER
          /     |      \
         /      |       \
       DMA     MMIO      IRQ
         \      |       /
          \     |      /
             PCI NIC

Böylece örneğin eth0 benzeri bir interface'in arkasında PCIe cihaz, DMA engine, descriptor ring ve interrupt mekanizması bulunduğu görüldü.

29. Interface UP İşlemi

Userspace:

ip link set <interface> up

gibi bir işlem yaptığında network stack driver'ın:

ndo_open()

callback'ini çağırır.

Gerçekçi initialization sırası:

ndo_open()
    ↓
NAPI enable
    ↓
IRQ setup/request
    ↓
hardware start
    ↓
RX/TX engine
    ↓
interrupt enable
    ↓
netif_start_queue()

şeklinde ele alındı.

Burada önemli nokta, hardware interrupt üretmeye başlamadan önce IRQ handler ve NAPI mekanizmasının hazır olmasıdır.

30. Interface DOWN İşlemi

Interface kapatılırken işlem sırasının tersine güvenli cleanup yapılması gerektiği incelendi:

ndo_stop()
    ↓
netif_stop_queue()
    ↓
hardware stop
    ↓
interrupt mask
    ↓
IRQ free
    ↓
NAPI disable

Amaç, driver kaynakları kaldırılırken NIC'in hâlâ DMA veya interrupt üretmesini engellemektir.

31. UDP Userspace Tarafının Driver'a Bağlanması

Bugün userspace UDP client/server kodunun driver ile ilişkisi de incelendi.

UDP client:

socket()
   ↓
sendto()
   ↓
UDP
   ↓
IP
   ↓
Routing
   ↓
Neighbour / ARP
   ↓
Ethernet
   ↓
skb
   ↓
qdisc
   ↓
ndo_start_xmit()
   ↓
TX descriptor
   ↓
DMA
   ↓
NIC
   ↓
NETWORK

şeklinde driver'a kadar iner.

Burada userspace uygulaması descriptor veya DMA hakkında hiçbir şey bilmez.

32. UDP RX Yolunun Tamamı

UDP server'a gelen bir packet'ın tam yolu:

NETWORK
   ↓
NIC
   ↓
RX DMA
   ↓
RX descriptor DONE
   ↓
IRQ
   ↓
NAPI
   ↓
skb
   ↓
Ethernet
   ↓
IP
   ↓
UDP
   ↓
socket lookup
   ↓
socket receive queue
   ↓
recvfrom()
   ↓
APPLICATION

olarak oluşturuldu.

Böylece recvfrom() çağrısının arkasında PCIe DMA'ya kadar uzanan büyük bir kernel/hardware zinciri olduğu görüldü.

33. TCP'nin Aynı Driver Üzerinden Çalışması

TCP'nin farklı bir Ethernet driver kullanmadığı üzerinde duruldu.

Driver açısından:

UDP packet
TCP packet
ICMP packet
ARP packet

temelde Ethernet frame'leridir.

TCP'ye özgü:

sequence number
ACK
retransmission
congestion control
connection state

gibi mekanizmalar daha üst katmandaki TCP implementation tarafından yönetilir.

34. TCP Three-Way Handshake'in Driver Seviyesindeki Görünümü

TCP client connect() yaptığında:

APPLICATION
    ↓
connect()
    ↓
TCP SYN
    ↓
IP
    ↓
Ethernet
    ↓
skb
    ↓
ndo_start_xmit()
    ↓
TX DMA
    ↓
NIC
    ↓
NETWORK

karşı taraftan:

SYN-ACK
   ↓
NIC
   ↓
RX DMA
   ↓
IRQ
   ↓
NAPI
   ↓
skb
   ↓
IP
   ↓
TCP

gelir.

Son olarak:

ACK
 ↓
TX path

oluşur.

Yani klasik:

Client                  Server


 SYN -------------------->
     <---------------- SYN-ACK
 ACK -------------------->

işleminin her oku altta bir NIC TX/RX işlemi oluşturur.

35. TCP Server Tarafı

TCP server:

socket()
 ↓
bind()
 ↓
listen()
 ↓
accept()

yapısına sahiptir.

listen() sonrasında SYN packet'ları userspace tarafından değil kernel TCP state machine tarafından işlenir.

NIC RX
 ↓
NAPI
 ↓
IP
 ↓
TCP
 ↓
SYN processing
 ↓
SYN-ACK TX
 ↓
ACK RX
 ↓
connection established
 ↓
accept queue
 ↓
accept()

Böylece accept() döndüğünde three-way handshake'in büyük kısmının kernel tarafından zaten tamamlanmış olduğu incelendi.

36. Driver'ın TCP ve UDP'den Bağımsızlığı

Bugünün önemli sonuçlarından biri network katmanlarının ayrılması oldu.

                 APPLICATION
                     │
             ┌───────┴───────┐
             │               │
            TCP             UDP
             │               │
             └───────┬───────┘
                     │
                     IP
                     │
                  Ethernet
                     │
                    skb
                     │
              Network Driver
                     │
           descriptor + DMA
                     │
                    NIC

Driver TCP bağlantı durumunu veya UDP socket'lerini yönetmez.

Driver'ın temel görevi:

TX:
skb → hardware


RX:
hardware → skb

dönüşümünü sağlamaktır.

37. Bugün Oluşturulan Tam TX Modeli

Günün sonunda TX yolu şu seviyeye getirildi:

APPLICATION
     ↓
send()/write()/sendto()
     ↓
TCP / UDP
     ↓
IP
     ↓
Routing
     ↓
Neighbour / ARP
     ↓
Ethernet
     ↓
skb
     ↓
qdisc
     ↓
ndo_start_xmit()
     ↓
DMA MAP
     ↓
TX descriptor hazırlanır
     ↓
dma_wmb()
     ↓
MMIO tail / doorbell
     ↓
PCIe NIC descriptor'ı okur
     ↓
DMA READ packet buffer
     ↓
packet transmit
     ↓
descriptor DONE
     ↓
IRQ
     ↓
NAPI / TX cleanup
     ↓
dma_unmap_single()
     ↓
skb free
     ↓
tx_clean ilerler
     ↓
gerekirse netif_wake_queue()
38. Bugün Oluşturulan Tam RX Modeli

RX tarafı ise:

NETWORK
     ↓
PCIe NIC
     ↓
RX descriptor seçilir
     ↓
DMA address alınır
     ↓
PCIe DMA WRITE
     ↓
RAM RX buffer
     ↓
descriptor length/status güncellenir
     ↓
IRQ
     ↓
Interrupt Controller
     ↓
Hard IRQ Handler
     ↓
interrupt cause
     ↓
RX IRQ mask
     ↓
napi_schedule()
     ↓
NAPI poll
     ↓
descriptor DONE kontrol
     ↓
DMA ownership CPU
     ↓
RX buffer
     ↓
skb
     ↓
Ethernet
     ↓
IP
     ↓
TCP / UDP
     ↓
socket receive queue
     ↓
recv()/recvfrom()
     ↓
APPLICATION

seviyesine getirildi.

39. Sistemin PCI'dan Userspace'e Tam Görünümü

Bugün öğrenilen yapıların tamamı tek diyagramda:

                     PCIe NIC TAKILIR
                           │
                           ▼
                    PCI ENUMERATION
                           │
                           ▼
                    Configuration Space
                           │
              ┌────────────┼────────────┐
              │            │            │
          Vendor ID    Device ID       BAR
              │            │            │
              └──────┬─────┘            │
                     ▼                  │
                Driver Match            │
                     │                  │
                     ▼                  │
                   probe()              │
                     │                  │
           ┌─────────┼─────────┐        │
           │         │         │        │
          DMA       IRQ     net_device  │
           │         │         │        │
           │         │         │       MMIO
           │         │         │        │
           └─────────┴─────────┴────────┘
                     │
                     ▼
                  PCIe NIC
                     │
              ┌──────┴──────┐
              │             │
             RX             TX
              │             │
              ▼             ▼
         descriptor     descriptor
              │             │
             DMA           DMA
              │             │
              ▼             ▼
             RAM           NIC
              │             │
             IRQ         NETWORK
              │
             NAPI
              │
             skb
              │
           Ethernet
              │
              IP
              │
       ┌──────┴──────┐
       │             │
      TCP           UDP
       │             │
       └──────┬──────┘
              │
            SOCKET
              │
              ▼
         APPLICATION
Gün Sonu Değerlendirmesi

On dördüncü gün sonunda önceki gün oluşturulan RX/TX descriptor ve streaming DMA modeli daha gerçekçi bir PCIe Ethernet driver mimarisine bağlandı. Önceki gün RX/TX ring, DMA ownership, TX completion ve tx_head/tx_clean mekanizmaları öğrenilmişti. Bugün ise bu yapıların PCIe cihaz tarafından nasıl görüldüğü, descriptor ring'in hardware ile nasıl paylaşıldığı ve MMIO register'larının driver-hardware iletişimindeki rolü üzerinde duruldu.

PCI enumeration, Vendor ID/Device ID matching ve probe() ilişkisi incelenerek driver'ın cihazla hangi aşamada eşleştiği ele alındı. BAR ve MMIO üzerinden NIC register'larına erişim; descriptor ring, DMA address ve tail/doorbell mekanizması üzerinden driver'ın NIC'e nasıl iş verdiği incelendi.

Descriptor'ın packet'ın kendisi olmadığı, RAM'deki packet buffer'ı ve işlemin durumunu tarif eden hardware-visible metadata olduğu netleştirildi. Descriptor ring için coherent DMA ile packet buffer'larında kullanılan streaming DMA arasındaki mimari fark üzerinde duruldu. DMA memory ordering kapsamında descriptor bilgilerinin NIC'e doğru sırada görünmesi için barrier kavramları ele alındı.

IRQ'nun descriptor içerisine packet bilgisi yazılması olmadığı; NIC'in CPU'ya bir olay meydana geldiğini bildirmesi olduğu netleştirildi. Interrupt cause, interrupt masking, hard IRQ context ve NAPI arasındaki ilişki incelendi. Yoğun trafik altında interrupt'ların maskelenerek NAPI polling'e geçilmesi ve iş tamamlandığında interrupt-driven çalışma biçimine dönülmesi bütün RX/TX lifecycle ile ilişkilendirildi.

Son olarak driver'ın üstündeki network stack de sisteme bağlandı. UDP sendto()/recvfrom() ve TCP connect()/accept()/read()/write() işlemlerinin en alt seviyede aynı net_device, skb, descriptor, DMA, IRQ ve NAPI mekanizmalarını kullandığı görüldü. TCP three-way handshake'in her SYN/SYN-ACK/ACK adımının altta normal TX veya RX packet yolu oluşturduğu değerlendirildi.

Böylece iki günlük ilerleme:

13. GÜN
────────────────────────


NAPI
 ↓
RX descriptor ring
 ↓
Streaming DMA
 ↓
DMA ownership
 ↓
RX buffer → skb
 ↓
TX descriptor ring
 ↓
TX DMA
 ↓
TX completion
 ↓
tx_head / tx_clean
 ↓
stop/wake queue




             │
             ▼




14. GÜN
────────────────────────


PCIe enumeration
 ↓
Vendor / Device ID
 ↓
Driver matching
 ↓
probe()
 ↓
BAR
 ↓
MMIO
 ↓
Hardware descriptor ring
 ↓
Coherent DMA + Streaming DMA
 ↓
Memory ordering
 ↓
Head / Tail / Doorbell
 ↓
NIC DMA engine
 ↓
Interrupt Cause
 ↓
INTx / MSI / MSI-X
 ↓
Hard IRQ
 ↓
NAPI
 ↓
RX/TX completion
 ↓
skb
 ↓
Ethernet
 ↓
IP
 ↓
TCP / UDP
 ↓
Socket
 ↓
Userspace Application

seviyesine taşınmış oldu.