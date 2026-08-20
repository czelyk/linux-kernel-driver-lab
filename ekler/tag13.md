STAJ RAPORU – 13. GÜN
Linux Network Driver – RX/TX Descriptor Ring, Streaming DMA, NAPI ve TX Completion Çalışmaları

On ikinci gün sonunda Linux network driver mimarisinde struct sk_buff, struct net_device, ndo_start_xmit, MTU/Jumbo Frame, checksum, IRQ ve NAPI mekanizmaları incelenmişti. Özellikle NAPI'nin interrupt ve polling yaklaşımını birleştirdiği, budget kullanarak bir poll turunda işlenecek packet sayısını sınırlandırdığı görülmüştü.

On üçüncü gün çalışmalarında bu yapı gerçek network driver mimarisine yaklaştırılarak RX/TX descriptor ring, DMA mapping, DMA ownership, RX buffer yönetimi, TX completion ve network queue kontrolü üzerinde çalışıldı.

1. RX Descriptor Kavramının Oluşturulması

Önce RX tarafında NIC ile driver arasındaki veri aktarımını temsil etmek amacıyla bir RX descriptor yapısı oluşturuldu.

struct my_rx_desc
{
    void *buf;
    dma_addr_t dma_addr;
    unsigned int len;
    bool done;
};

Alanların görevleri:

my_rx_desc
│
├── buf
│    └── CPU'nun eriştiği RX buffer
│
├── dma_addr
│    └── NIC'in DMA için kullanacağı adres
│
├── len
│    └── alınan packet uzunluğu
│
└── done
     └── NIC packet'ı tamamladı mı?

Böylece packet verisinin kendisi ile packet'ın durumunu tanımlayan descriptor'ın farklı kavramlar olduğu görüldü.

2. RX Descriptor Ring

Tek bir descriptor yerine birden fazla descriptor kullanılarak RX ring oluşturuldu.

#define RX_RING_SIZE 64
#define RX_BUFFER_SIZE 2048

Driver private data içerisine:

struct my_rx_desc rx_ring[RX_RING_SIZE];
unsigned int rx_head;

eklendi.

Temel yapı:

RX RING


        rx_head
           ↓
[0][1][2][3][4] ........ [63]
 │  │  │  │               │
 ▼  ▼  ▼  ▼               ▼
buf buf buf buf           buf

Ring yapısının son descriptor'dan sonra tekrar ilk descriptor'a dönmesi için modulo işlemi kullanıldı:

priv->rx_head =
    (priv->rx_head + 1) % RX_RING_SIZE;
3. RX Buffer Allocation

Her RX descriptor için NIC'in packet yazabileceği bir buffer ayrılması üzerinde çalışıldı.

priv->rx_ring[i].buf =
    kmalloc(
        RX_BUFFER_SIZE,
        GFP_KERNEL
    );

Burada kmalloc() kullanılmasının nedeni buffer'ın kernel virtual memory içerisinde fiziksel olarak uygun bir alanda oluşturulması ve daha sonra DMA API ile DMA mapping yapılabilmesidir.

RX buffer:

CPU
 │
 │ kmalloc()
 ▼
RAM
+----------------------+
| 2048 byte RX buffer  |
+----------------------+

olarak oluşturuldu.

vmalloc() belleğinin fiziksel olarak contiguous olması garanti edilmediğinden basit dma_map_single() modeli için doğrudan tercih edilmediği tekrar değerlendirildi.

4. RX Streaming DMA Mapping

Oluşturulan RX buffer:

dma_map_single(
    dev->dev.parent,
    priv->rx_ring[i].buf,
    RX_BUFFER_SIZE,
    DMA_FROM_DEVICE
);

ile DMA için map edildi.

RX yönü:

NETWORK
   ↓
  NIC
   ↓
DMA WRITE
   ↓
  RAM

olduğundan:

DMA_FROM_DEVICE

kullanıldı.

Buradaki önemli ayrım:

kmalloc()
   ↓
Belleği oluşturur


dma_map_single()
   ↓
Mevcut belleği DMA kullanımına map eder

şeklinde yapıldı.

Dolayısıyla dma_map_single() tek başına buffer allocation gerçekleştirmemektedir.

5. DMA Ownership ve Synchronization

RX buffer'ın NIC ve CPU tarafından farklı zamanlarda kullanılması nedeniyle DMA ownership/synchronization konusu incelendi.

NIC packet'ı yazdıktan sonra CPU'nun buffer'ı işlemesinden önce:

dma_sync_single_for_cpu(
    dev->dev.parent,
    desc->dma_addr,
    desc->len,
    DMA_FROM_DEVICE
);

kullanıldı.

Akış:

NIC ownership
     ↓
NIC DMA WRITE
     ↓
packet hazır
     ↓
dma_sync_single_for_cpu()
     ↓
CPU ownership

CPU packet processing'i tamamladıktan sonra ise:

dma_sync_single_for_device(
    dev->dev.parent,
    desc->dma_addr,
    RX_BUFFER_SIZE,
    DMA_FROM_DEVICE
);

ile buffer tekrar device kullanımına hazırlandı.

CPU ownership
     ↓
packet processing
     ↓
dma_sync_single_for_device()
     ↓
NIC ownership
6. RX Descriptor Ring + NAPI Birleştirilmesi

Bir önceki gün öğrenilen NAPI poll mekanizması RX descriptor ring ile birleştirildi.

Temel yapı:

while(work_done < budget)
{
    desc = &priv->rx_ring[priv->rx_head];


    if(!desc->done)
        break;


    ...
}

Burada:

budget
   ↓
bir poll turunda maksimum
işlenecek packet sayısı


work_done
   ↓
gerçekte işlenen
packet sayısı

olarak kullanıldı.

Descriptor'ın:

desc->done

alanı NIC'in packet'ı tamamlayıp tamamlamadığını temsil etti.

7. RX DMA Buffer'dan sk_buff Oluşturulması

NIC'in DMA ile doldurduğu RX buffer doğrudan Linux network stack'e verilmek yerine bir sk_buff oluşturuldu.

skb = netdev_alloc_skb(
    dev,
    desc->len
);

Ardından:

memcpy(
    skb_put(skb, desc->len),
    desc->buf,
    desc->len
);

ile packet verisi DMA buffer'dan skb'ye kopyalandı.

Akış:

NIC
 ↓
DMA
 ↓
RX buffer
 ↓
memcpy
 ↓
sk_buff
 ↓
Linux Network Stack

şeklinde oluşturuldu.

8. RX Packet'ın Network Stack'e Teslim Edilmesi

Oluşturulan skb için:

skb->dev = dev;


skb->protocol =
    eth_type_trans(skb, dev);


skb->ip_summed =
    CHECKSUM_NONE;

metadata bilgileri hazırlandı.

Ardından:

netif_receive_skb(skb);

ile packet Linux network stack'e teslim edildi.

Tam RX yolu:

Network
   ↓
NIC
   ↓
DMA WRITE
   ↓
RX descriptor
   ↓
NAPI poll
   ↓
DMA sync for CPU
   ↓
RX buffer
   ↓
sk_buff
   ↓
eth_type_trans()
   ↓
netif_receive_skb()
   ↓
Linux Network Stack

haline getirildi.

9. RX Descriptor'ın Yeniden Kullanılması

Packet processing tamamlandıktan sonra:

desc->done = false;
desc->len = 0;

ile descriptor temizlendi.

Buffer tekrar NIC'e verilerek aynı DMA mapping'in sonraki packet'larda yeniden kullanılabileceği görüldü.

Bu nedenle RX tarafında temel lifecycle:

DMA MAP
   ↓
NIC kullanır
   ↓
sync for CPU
   ↓
CPU kullanır
   ↓
sync for device
   ↓
NIC tekrar kullanır
   ↓
...

şeklinde oluşturuldu.

10. TX Descriptor Yapısına Geçilmesi

RX ring'in ardından TX tarafı gerçek NIC mimarisine yaklaştırıldı.

struct my_tx_desc
{
    struct sk_buff *skb;
    dma_addr_t dma_addr;
    unsigned int len;
    bool done;
};

TX descriptor:

TX descriptor
│
├── skb
│
├── dma_addr
│
├── len
└── done

alanlarından oluşturuldu.

Private data içerisine:

struct my_tx_desc tx_ring[TX_RING_SIZE];
unsigned int tx_head;

eklendi.

11. TX Tarafında skb ve DMA İlişkisi

RX'ten farklı olarak TX tarafında packet zaten kernel tarafından:

struct sk_buff *skb

olarak driver'a gelmektedir.

Dolayısıyla yeni bir packet buffer allocation yapmak yerine mevcut:

skb->data

DMA için map edildi.

desc->dma_addr = dma_map_single(
    dev->dev.parent,
    skb->data,
    skb->len,
    DMA_TO_DEVICE
);

TX yönü:

RAM
 ↓
NIC
 ↓
NETWORK

olduğundan:

DMA_TO_DEVICE

kullanıldı.

12. TX Descriptor'ın Doldurulması

Mapping başarılı olduktan sonra:

desc->skb = skb;
desc->len = skb->len;
desc->done = false;

ile descriptor hazırlandı.

Temel model:

TX descriptor


+-----------------------+
| skb      → packet     |
| dma_addr → DMA addr   |
| len      → packet len |
| done     → false      |
+-----------------------+

done = false durumu NIC'in henüz transmit işlemini tamamlamadığını temsil etmektedir.

13. TX Ring ve tx_head

Yeni packet'ların farklı descriptor'lara yerleştirilmesi için:

priv->tx_head =
    (priv->tx_head + 1)
    % TX_RING_SIZE;

kullanımı incelendi.

Örneğin:

Packet 1 → desc0
Packet 2 → desc1
Packet 3 → desc2
...
Packet 64 → desc63
Packet 65 → desc0

şeklinde ring yapısının döndüğü görüldü.

Ancak desc0 hâlâ NIC tarafından kullanılmaktaysa üzerine yeni packet yazılmaması gerektiği üzerinde duruldu.

14. TX Ring Full ve Network Queue Kontrolü

CPU'nun NIC'ten daha hızlı descriptor doldurması durumunda TX ring'in dolabileceği incelendi.

CPU hızlı
   ↓
TX descriptor'lar dolar
   ↓
NIC henüz tamamlamadı
   ↓
TX RING FULL

Bu durumda:

netif_stop_queue(dev);

ile kernel'in driver'a yeni TX packet vermesi geçici olarak durdurulabilir.

NIC descriptor'ları tamamlayıp ring'de tekrar yer açıldığında:

netif_wake_queue(dev);

kullanılarak TX tekrar başlatılabilir.

Akış:

TX ring full
     ↓
netif_stop_queue()
     ↓
NIC packet gönderir
     ↓
descriptor boşalır
     ↓
netif_wake_queue()
15. TX Completion Kavramı

TX tarafında en önemli konulardan biri packet'ın NIC'e teslim edilmesi ile transmit işleminin tamamlanmasının aynı şey olmadığıdır.

Temel lifecycle:

ndo_start_xmit()
      ↓
skb
      ↓
DMA MAP
      ↓
TX descriptor
      ↓
NIC ownership
      ↓
DMA READ
      ↓
packet transmit
      ↓
TX COMPLETE

NIC transmit işlemini tamamladığında descriptor'ın status bilgisinin hardware tarafından güncellenebileceği incelendi.

Basitleştirilmiş model:

done = false
     ↓
NIC packet gönderiyor
     ↓
done = true

olarak ele alındı.

16. TX DMA Unmap ve skb Lifetime

TX tamamlanmadan skb'nin free edilmemesi gerektiği özellikle incelendi.

Yanlış akış:

dma_map_single()
      ↓
skb free
      ↓
NIC DMA READ       ← HATALI

Doğru akış:

skb
 ↓
dma_map_single()
 ↓
NIC DMA READ
 ↓
TX COMPLETE
 ↓
dma_unmap_single()
 ↓
dev_kfree_skb()

şeklinde oluşturuldu.

Burada:

dma_unmap_single()

DMA mapping'in yaşam süresini bitirirken:

dev_kfree_skb()

skb'nin yaşam süresini bitirmektedir.

Bu iki işlemin aynı işlem olmadığı netleştirildi.

17. tx_head ve tx_clean Ayrımı

TX ring'in yönetilmesi için iki farklı index kavramı incelendi:

tx_head
   ↓
Yeni packet'ın konulacağı descriptor




tx_clean
   ↓
Tamamlanmış descriptor'ların
temizlenmeye başlanacağı nokta

Örneğin:

TX RING


[0][1][2][3][4][5][6]
       ↑       ↑
   tx_clean  tx_head

Bu yapı producer-consumer mantığıyla ilişkilendirildi.

TX tarafında:

CPU = Producer
NIC = Consumer

RX tarafında ise:

NIC = Producer
CPU = Consumer

olduğu görüldü.

18. TX Descriptor Cleanup

NIC tarafından tamamlanmış descriptor için:

dma_unmap_single(...);
dev_kfree_skb(desc->skb);

işlemleri uygulanması üzerinde çalışıldı.

Sonrasında descriptor:

desc->skb = NULL;
desc->len = 0;
desc->done = false;

ile tekrar kullanılabilir duruma getirildi.

Ardından:

tx_clean++;

mantığıyla bir sonraki tamamlanmış descriptor'a geçildi.

19. RX ve TX DMA Lifecycle Karşılaştırması

Günün sonunda iki yön arasındaki önemli fark şu şekilde oluşturuldu:

RX
────────────────────────


kmalloc RX buffer
      ↓
dma_map_single(FROM_DEVICE)
      ↓
NIC DMA WRITE
      ↓
sync_for_cpu
      ↓
CPU packet işler
      ↓
sync_for_device
      ↓
aynı buffer tekrar kullanılır




TX
────────────────────────


skb gelir
      ↓
dma_map_single(TO_DEVICE)
      ↓
NIC DMA READ
      ↓
packet gönderilir
      ↓
TX completion
      ↓
dma_unmap_single
      ↓
skb free

Böylece RX tarafında uzun ömürlü ve tekrar kullanılan DMA mapping modeli ile TX tarafında packet ömrüne bağlı mapping modeli arasındaki fark görüldü.

20. IRQ, NAPI, RX ve TX'in Birleştirilmesi

Network driver'ın genel interrupt yapısı gün sonunda şu modele ulaştırıldı:

                         NIC
                          │
                         IRQ
                          │
                  Interrupt Handler
                          │
                 IRQ status kontrol
                          │
              ┌───────────┴───────────┐
              │                       │
          RX EVENT                 TX EVENT
              │                       │
              ▼                       ▼
       napi_schedule()          TX completion
              │                       │
              ▼                       ▼
          my_poll()             tx_clean
              │                       │
              ▼                       ▼
       RX descriptor          DMA unmap
              │                       │
              ▼                       ▼
       DMA sync CPU             skb free
              │                       │
              ▼                       ▼
            skb                descriptor free
              │                       │
              ▼                       ▼
       Network Stack         netif_wake_queue()

Burada RX packet processing'in NAPI üzerinden yapılması, TX tarafında ise tamamlanmış descriptor'ların temizlenmesi gerektiği görüldü.

21. Gün Sonunda Oluşan Genel Network Driver Modeli

On üçüncü gün sonunda önceki gün oluşturulan network driver modeli descriptor ve DMA seviyesine indirilmiş oldu:

                           APPLICATION
                         ↑             ↓
                      recv()         send()
                         ↑             ↓
                      TCP / UDP     TCP / UDP
                         ↑             ↓
                         IP            IP
                         ↑             ↓
                    NETWORK STACK
                         ↑             ↓
                         │            skb
                         │             ↓
                         │      ndo_start_xmit()
                         │             ↓
                         │        TX descriptor
                         │             ↓
                         │      dma_map_single()
                         │             ↓
                         │          NIC DMA
                         │             ↓
                         │          NETWORK
                         │             │
                         │             ▼
                         │            NIC
                         │             ↓
                         │        RX descriptor
                         │             ↓
                         │         DMA WRITE
                         │             ↓
                         │            IRQ
                         │             ↓
                         │      napi_schedule()
                         │             ↓
                         │          my_poll()
                         │             ↓
                         │       RX descriptor
                         │             ↓
                         │     sync_for_cpu()
                         │             ↓
                         └──────────── skb
Gün Sonu Değerlendirmesi

On üçüncü gün sonunda Linux network driver çalışmaları NAPI seviyesinden DMA ve descriptor ring seviyesine taşındı. Bir önceki gün NAPI'nin budget ve work_done mekanizmaları incelenmişken, bugün NAPI poll içerisinde gerçek bir RX descriptor ring'in nasıl tüketilebileceği modellenmiş oldu. Önceki raporda bu aşama bir sonraki çalışma hedefi olarak belirlenmişti.

RX tarafında kmalloc() ile buffer oluşturma, dma_map_single(..., DMA_FROM_DEVICE) ile streaming DMA mapping, dma_sync_single_for_cpu() / dma_sync_single_for_device() ile buffer ownership geçişleri ve DMA buffer'dan sk_buff oluşturularak netif_receive_skb() üzerinden network stack'e teslim edilmesi incelendi.

TX tarafında ise ndo_start_xmit() ile gelen skb'nin dma_map_single(..., DMA_TO_DEVICE) kullanılarak NIC için map edilmesi, TX descriptor içerisinde skb/DMA adresi/uzunluk/status bilgisinin tutulması ve ring'in tx_head ile yönetilmesi üzerinde çalışıldı.

TX işleminin ndo_start_xmit() dönüşüyle tamamlanmadığı; skb'nin NIC DMA işlemi bitene kadar yaşamaya devam etmesi gerektiği görüldü. NIC transmit işlemini tamamladıktan sonra DMA mapping'in dma_unmap_single() ile kaldırılması ve skb'nin ancak bundan sonra serbest bırakılması gerektiği incelendi.

Ayrıca tx_head ve tx_clean ayrımı üzerinden TX ring producer-consumer mantığı oluşturuldu. Ring'in dolması halinde netif_stop_queue(), descriptor'lar temizlendikten sonra ise netif_wake_queue() kullanılarak Linux network stack ile driver arasında flow control sağlanabileceği değerlendirildi.

Böylece iki günlük ilerleme kabaca:

12. GÜN
Network Stack
   ↓
sk_buff
   ↓
net_device
   ↓
ndo_start_xmit
   ↓
MTU / Jumbo
   ↓
RX / TX
   ↓
IRQ
   ↓
NAPI
   ↓
budget / work_done


            │
            ▼


13. GÜN
NAPI
   ↓
RX descriptor ring
   ↓
RX buffer
   ↓
Streaming DMA
   ↓
DMA ownership / sync
   ↓
RX → skb → Network Stack
   ↓
TX descriptor ring
   ↓
TX DMA mapping
   ↓
TX completion
   ↓
DMA unmap
   ↓
skb lifetime
   ↓
tx_head / tx_clean
   ↓
stop/wake queue

