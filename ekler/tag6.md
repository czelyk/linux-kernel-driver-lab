STAJ RAPORU – 6. GÜN
Linux Kernel Bellek Yönetimi, Cache Yapısı, Object Lifetime, Hata Yönetimi ve DMA Mekanizmalarının İncelenmesi

Beşinci gün sonunda kernel thread, timer ve workqueue yapılarının aynı FIFO üzerinde güvenli biçimde çalışması sağlanmış; synchronization, producer-consumer ve backpressure kavramları incelenmişti. Birden fazla execution context'in ortak veri yapıları üzerinde güvenli biçimde çalışmasının önemi uygulamalı olarak görülmüştü.

Altıncı gün çalışmalarında ise Linux Kernel bellek yönetimi daha ayrıntılı olarak incelenmiş; kernel içerisinde kullanılan bellek ayırma yöntemleri, fiziksel ve sanal bellek ilişkisi, CPU cache yapısı, object lifetime ve hata yönetimi konuları ele alınmıştır. Günün ilerleyen bölümünde DMA mekanizmasına geçilerek cihazların ana bellek ile CPU'nun doğrudan veri kopyalamasına gerek kalmadan nasıl veri transferi gerçekleştirebildiği incelenmiştir.

1. Kernel Bellek Ayırma Mekanizmalarının İncelenmesi

İlk olarak kernel içerisinde dinamik bellek ayırmak amacıyla kullanılan temel fonksiyonlar incelendi.

Bu kapsamda;

kmalloc()
kzalloc()
kcalloc()
kfree()
vmalloc()
vfree()

fonksiyonlarının kullanım amaçları karşılaştırıldı.

kmalloc() ile kernel içerisinde genel amaçlı dinamik bellek ayrılabildiği, kzalloc() fonksiyonunun ise ayrılan belleği sıfırlayarak teslim ettiği görüldü. kcalloc() fonksiyonunun dizi biçimindeki allocation işlemlerinde kullanılabileceği incelendi.

vmalloc() ile alınan alanın sanal adres uzayında ardışık olmasına rağmen fiziksel bellekte ardışık olmak zorunda olmadığı öğrenildi.

Böylece kmalloc() ve vmalloc() arasındaki temel fiziksel bellek yerleşimi farkı incelendi.

2. GFP_KERNEL ve GFP_ATOMIC Bayraklarının İncelenmesi

Kernel memory allocation işlemlerinde kullanılan allocation flag'leri ele alındı.

Özellikle;

GFP_KERNEL
GFP_ATOMIC

arasındaki fark incelendi.

GFP_KERNEL kullanılan allocation işlemlerinin gerektiğinde bekleyebileceği ve bu nedenle uyumanın mümkün olduğu process context'lerde kullanılmasının uygun olduğu görüldü.

GFP_ATOMIC ise uyumanın mümkün olmadığı atomic context'lerde gerçekleştirilecek allocation işlemleri için incelendi.

Bu konu daha önce öğrenilen execution context kavramıyla ilişkilendirilerek kullanılacak memory allocation yönteminin kodun çalıştığı context'e göre seçilmesi gerektiği görüldü.

3. Fiziksel ve Sanal Bellek Yapısının İncelenmesi

CPU'nun programlar tarafından kullanılan sanal adresleri doğrudan fiziksel RAM adresi olarak kullanmadığı incelendi.

Temel yapı;

Virtual Address
      ↓
     MMU
      ↓
Page Tables
      ↓
Physical Address
      ↓
     RAM

şeklinde değerlendirildi.

Kernel içerisindeki pointer'ların genellikle CPU tarafından kullanılan sanal adresler olduğu ve MMU/page table mekanizmaları aracılığıyla fiziksel belleğe çevrildiği öğrenildi.

Page kavramı ve fiziksel belleğin sayfalar halinde yönetilmesi üzerinde duruldu.

4. CPU Cache ve Cache-Friendly Bellek Erişiminin İncelenmesi

CPU'nun her memory erişiminde doğrudan RAM'e gitmesinin yüksek gecikmeye neden olacağı için L1, L2 ve L3 cache seviyelerinden yararlandığı incelendi.

Bellekten verilerin tek tek byte olarak değil, cache line adı verilen bloklar halinde cache'e getirilebildiği öğrenildi.

Ardışık bellek erişimlerinin spatial locality nedeniyle cache kullanımını daha verimli hale getirebildiği görüldü.

Bu kapsamda ardışık array erişimleri ile dağınık pointer erişimleri karşılaştırılarak cache-friendly programlama kavramı incelendi.

5. SLAB/SLUB ve Buddy Allocator Mekanizmalarının İncelenmesi

Linux Kernel'ın fiziksel bellek yönetiminde kullandığı allocator yapıları ele alındı.

Buddy allocator'ın fiziksel page bloklarının yönetiminde kullanıldığı, SLAB/SLUB allocator yapılarının ise küçük kernel object allocation işlemlerini daha verimli gerçekleştirmek amacıyla kullanıldığı incelendi.

Temel ilişki;

kmalloc()
    ↓
SLUB
    ↓
Page Allocator / Buddy
    ↓
Physical Memory

şeklinde değerlendirildi.

Ayrıca SLUB içerisindeki cache kavramının CPU'nun L1/L2/L3 cache yapılarıyla aynı şey olmadığı özellikle ayrıştırıldı.

6. Fragmentation, Alignment ve Padding Kavramlarının İncelenmesi

Bellek yönetiminde internal ve external fragmentation kavramları incelendi.

Ardından CPU'nun verilere uygun adreslerden erişebilmesi açısından alignment kavramı ele alındı.

C yapılarında compiler tarafından alanlar arasına padding eklenebileceği ve bunun sizeof(struct) sonucunu etkileyebileceği görüldü.

Struct alanlarının sıralamasının hem bellek kullanımı hem de cache davranışı üzerinde etkili olabileceği değerlendirildi.

7. Stack ve Dinamik Kernel Belleğinin Karşılaştırılması

Kernel stack ile kmalloc() gibi yöntemlerle elde edilen dinamik kernel belleği arasındaki fark incelendi.

Stack'in bir bellek bölgesi olduğu, stack verilerinin CPU tarafından kullanılırken cache içerisinde geçici olarak bulunabileceği ancak stack ile CPU cache kavramlarının aynı olmadığı öğrenildi.

Ayrıca compiler optimizasyonları nedeniyle bazı local değişkenlerin doğrudan register içerisinde tutulabileceği değerlendirildi.

8. Pointer Dereference ve Bellek Hatalarının İncelenmesi

Pointer dereference kavramı ayrıntılı olarak ele alındı.

Bir pointer'ın tuttuğu adresteki nesneye;

*p

ve struct pointer'larında;

ptr->member

ifadeleriyle erişilebildiği incelendi.

Bu kapsamda;

NULL pointer dereference
memory leak
use-after-free
double-free
out-of-bounds access

gibi temel memory bug türleri değerlendirildi.

9. Object Lifetime ve Ownership Kavramlarının İncelenmesi

Dinamik olarak oluşturulan kernel object'lerinin yalnızca allocate ve free edilmesinin yeterli olmadığı, object'in yaşam süresinin de doğru yönetilmesi gerektiği görüldü.

Bir object için;

allocate
   ↓
object kullanılabilir
   ↓
kullanan bütün tarafların işi biter
   ↓
free

sırasının korunması gerektiği incelendi.

Ownership kavramı üzerinden bir allocation'ın hangi kernel bileşeni tarafından serbest bırakılacağının açık şekilde belirlenmesinin memory leak ve double-free hatalarını önlemedeki önemi değerlendirildi.

10. Reference Counting Mekanizmasının İncelenmesi

Bir kernel object'inin birden fazla execution context tarafından kullanılabileceği durumlarda object lifetime yönetimi için reference counting yaklaşımı incelendi.

Bu kapsamda;

refcount_t
kref

mekanizmalarının temel çalışma mantıkları ele alındı.

Reference sayısının sıfıra düşmesinin object'i artık kullanan taraf kalmadığını gösterebileceği ve object'in ancak uygun lifetime koşulları sağlandıktan sonra serbest bırakılması gerektiği öğrenildi.

Reference counting'in mutex veya spinlock ile aynı amaç için kullanılmadığı; lock mekanizmalarının eşzamanlı erişimi, reference counting'in ise object lifetime'ını yönetmeye yardımcı olduğu görüldü.

11. Error-Path Cleanup ve Kernel goto Yapısının İncelenmesi

Driver initialization sırasında birden fazla resource alınması durumunda ara aşamalardan birinin başarısız olabileceği incelendi.

Örneğin;

Memory       ✓
FIFO         ✓
Thread       ✓
Device       ✗

durumunda daha önce başarıyla alınmış kaynakların serbest bırakılması gerektiği görüldü.

Cleanup işlemlerinin genellikle resource acquisition sırasının tersine gerçekleştirildiği incelendi.

Kernel kodunda sık kullanılan;

goto err_fifo;
goto err_buffer;

gibi error-path yapılarının tekrar eden cleanup kodlarını azaltmak ve doğru resource unwinding gerçekleştirmek amacıyla kullanıldığı öğrenildi.

12. Kernel Error Pointer Mekanizmasının İncelenmesi

Pointer döndüren bazı kernel API'lerinin hata durumunda NULL yerine error pointer döndürebildiği incelendi.

Bu kapsamda;

ERR_PTR()
IS_ERR()
PTR_ERR()
IS_ERR_OR_NULL()

mekanizmalarının çalışma mantıkları ele alındı.

Özellikle daha önce kullanılan;

kthread_run()

fonksiyonunun hata kontrolünün IS_ERR() ile yapılması gerektiği tekrar değerlendirildi.

Her kernel API'sinin aynı hata döndürme yöntemini kullanmadığı ve API'nin hata sözleşmesinin bilinmesi gerektiği görüldü.

13. Kernel Memory Debugging Mekanizmalarının İncelenmesi

Kernel memory hatalarının tespitinde kullanılan debugging mekanizmaları teorik olarak incelendi.

Bu kapsamda;

KASAN
kmemleak
SLUB debugging
memory poisoning
dmesg / call trace

konuları ele alındı.

KASAN'ın use-after-free ve out-of-bounds gibi geçersiz memory erişimlerini yakalamaya yardımcı olduğu, kmemleak mekanizmasının ise olası kernel memory leak'lerini tespit etmek amacıyla kullanılabildiği öğrenildi.

14. DMA (Direct Memory Access) Mekanizmasına Giriş

Günün ilerleyen bölümünde DMA mekanizması incelendi.

DMA'nın temel amacı, cihaz ile ana bellek arasındaki büyük veri transferlerinde CPU'nun her veri parçasını doğrudan kopyalamak zorunda kalmamasını sağlamaktır.

Temel yapı;

CPU / Driver
     │
     │ transferi hazırlar
     ▼

DEVICE ═══════════ RAM
          DMA

şeklinde değerlendirildi.

Network Interface Card (NIC) ve NVMe cihazları üzerinden DMA kullanım örnekleri incelendi.

15. CPU Address, Physical Address ve DMA Address Ayrımı

DMA işlemlerinde CPU'nun kullandığı virtual address ile device'ın kullandığı DMA address'in aynı kavram olmadığı öğrenildi.

Temel olarak;

CPU:
Virtual Address
      ↓
     MMU
      ↓
Physical Memory


Device:
DMA Address
      ↓
    IOMMU
      ↓
Physical Memory

ilişkisi incelendi.

Bu nedenle DMA işlemlerinde fiziksel adresin doğrudan tahmin edilmesi yerine Linux DMA API'nin kullanılması gerektiği görüldü.

DMA adreslerinin;

dma_addr_t

tipiyle temsil edilebildiği öğrenildi.

16. Coherent DMA ve Streaming DMA'nın Karşılaştırılması

DMA memory kullanımında iki temel yaklaşım incelendi.

Coherent DMA için;

dma_alloc_coherent()
dma_free_coherent()

mekanizmaları ele alındı.

Streaming DMA tarafında ise;

dma_map_single()
dma_unmap_single()
dma_mapping_error()

fonksiyonlarının temel çalışma mantığı incelendi.

Coherent DMA'nın descriptor/ring gibi daha uzun süre CPU ve device tarafından paylaşılan yapılar için kullanılabileceği, streaming DMA'nın ise mevcut buffer'ların belirli transferler için DMA'ya map edilmesinde kullanılabileceği görüldü.

17. DMA Transfer Yönlerinin İncelenmesi

Streaming DMA işlemlerinde transfer yönünün DMA API'ye bildirilmesi gerektiği incelendi.

Bu kapsamda;

DMA_TO_DEVICE
DMA_FROM_DEVICE
DMA_BIDIRECTIONAL

değerleri ele alındı.

Network kartı üzerinden;

TX:
RAM → NIC
DMA_TO_DEVICE

RX:
NIC → RAM
DMA_FROM_DEVICE

ilişkisi kuruldu.

DMA yönünün doğru belirtilmesinin özellikle cache coherency ve platforma özgü DMA işlemleri açısından önemli olduğu görüldü.

18. DMA Mapping Kavramının İncelenmesi

dma_map_single() işleminin yeni bir buffer oluşturmak veya veriyi başka bir yere kopyalamak anlamına gelmediği incelendi.

Mevcut buffer için cihazın kullanabileceği DMA mapping ilişkisinin oluşturulduğu görüldü.

Temel yaşam döngüsü;

normal buffer
      ↓
dma_map_single()
      ↓
DMA address
      ↓
device transferi
      ↓
dma_unmap_single()
      ↓
buffer kullanılmaya devam edilebilir

şeklinde değerlendirildi.

dma_unmap_single() işleminin memory'yi serbest bırakmadığı, yalnızca DMA mapping yaşam döngüsünü sonlandırdığı özellikle ayrıştırıldı.

19. DMA Descriptor ve Descriptor Ring Yapısının İncelenmesi

Son olarak cihazların DMA buffer'larının adresini, uzunluğunu ve işlem durumunu nasıl takip edebildiğini anlamak amacıyla descriptor kavramı incelendi.

Kavramsal bir descriptor;

struct descriptor {
    dma_addr_t address;
    u32 length;
    u32 flags;
};

şeklinde değerlendirildi.

Descriptor'ın gerçek veriyi değil, verinin bulunduğu DMA adresi ve transfer hakkında metadata içerdiği öğrenildi.

Birden fazla transferin verimli şekilde yönetilebilmesi amacıyla descriptor'ların circular bir yapı içerisinde kullanılabileceği ve bunun descriptor ring olarak adlandırıldığı görüldü.

Network kartı açısından;

Descriptor Ring
      │
      ├── Descriptor 0 → Packet Buffer 0
      ├── Descriptor 1 → Packet Buffer 1
      ├── Descriptor 2 → Packet Buffer 2
      └── Descriptor 3 → Packet Buffer 3

yapısı incelendi.

Descriptor ring'in coherent DMA ile, paket verilerinin ise streaming DMA ile yönetilebildiği tipik kullanım modeli değerlendirildi.

Gün Sonu Değerlendirmesi

Altıncı günün sonunda Linux Kernel bellek yönetimi kapsamlı şekilde ele alınmıştır. kmalloc, vmalloc, GFP flag'leri, virtual/physical memory, MMU, cache line, SLUB, Buddy allocator, fragmentation, alignment ve stack gibi temel bellek kavramları birbirleriyle ilişkilendirilmiştir.

Memory allocation'ın yalnızca bellek ayırmaktan ibaret olmadığı; ownership, object lifetime, reference counting ve doğru cleanup sırasının güvenli driver geliştirmede önemli olduğu görülmüştür. Memory leak, use-after-free, double-free, NULL dereference ve out-of-bounds gibi hatalar ile bunların KASAN, kmemleak ve SLUB debugging gibi mekanizmalarla nasıl tespit edilebileceği incelenmiştir.

Günün ikinci bölümünde DMA mimarisine geçilmiş; CPU virtual address, physical address ve DMA address ayrımı, IOMMU, coherent ve streaming DMA, DMA mapping ve transfer yönleri üzerinde durulmuştur. NIC örneği üzerinden TX ve RX veri akışları incelenmiş ve son olarak DMA descriptor ile descriptor ring yapılarının cihaz ile driver arasındaki veri transferlerinin yönetimindeki rolü ele alınmıştır.

Gün sonunda ulaşılan genel yapı şu şekilde özetlenebilir:

                     CPU / DRIVER
                          │
                  Descriptor Ring
                   (DMA metadata)
                          │
                          ▼
                         NIC
                       ↙     ↘
                    TX         RX
                    │           │
              DMA_TO_DEVICE  DMA_FROM_DEVICE
                    │           │
                    └─────┬─────┘
                          │
                     DMA Buffers
                          │
                          ▼
                         RAM