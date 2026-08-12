# STAJ RAPORU – 7. GÜN

## Linux PCI/PCIe Driver Mimarisi, BAR-MMIO, Interrupt Context, Kernel Sanal Dosya Sistemleri ve Genel Driver Tekrarının İncelenmesi

Altıncı gün sonunda Linux Kernel bellek yönetimi, virtual ve physical memory ilişkisi, CPU cache yapısı, SLUB/Buddy allocator mekanizmaları, object lifetime ve hata yönetimi konuları incelenmişti. Günün ikinci bölümünde DMA mimarisine geçilmiş; coherent ve streaming DMA, DMA address kavramı, DMA transfer yönleri ve descriptor ring yapısı NIC örneği üzerinden değerlendirilmişti.

Yedinci gün çalışmalarında ise DMA mekanizmasının gerçek bir donanım sürücüsü içerisinde nasıl konumlandığını daha iyi anlayabilmek amacıyla PCI/PCIe driver mimarisine geçildi. PCI device-driver eşleşmesi, PCI adresleme, BAR yapıları ve MMIO mekanizması incelendi. Ardından kernel execution context yapısı, interrupt context, spinlock ve IRQ ilişkisi tekrar ele alındı. Günün ilerleyen bölümünde sysfs, procfs ve debugfs yapıları karşılaştırıldı ve daha önce öğrenilen kernel memory, synchronization, poll, timer ve FIFO konuları genel tekrar kapsamında değerlendirildi.

## 1. PCI ve PCIe Mimarilerinin İncelenmesi

İlk olarak PCI ve PCI Express yapılarının Linux device driver geliştirme açısından temel çalışma mantığı incelendi.

PCIe cihazlarının işletim sistemi tarafından keşfedilebildiği ve Linux Kernel'ın cihazları PCI subsystem üzerinden yönettiği görüldü.

Bir PCIe cihazının;

* Vendor ID
* Device ID
* Bus
* Device
* Function

gibi bilgiler ile tanımlanabildiği incelendi.

PCIe'nin network kartları, NVMe cihazları, GPU'lar ve çeşitli yüksek hızlı donanımların CPU ve sistem belleği ile haberleşmesinde kullanılan temel bağlantı teknolojilerinden biri olduğu değerlendirildi.

## 2. PCI Bus:Device.Function Adresleme Yapısının İncelenmesi

PCI cihazlarının sistem içerisinde Bus:Device.Function biçiminde adreslenebildiği incelendi.

Temel gösterim;

```text
Bus : Device . Function
```

şeklinde değerlendirildi.

Örneğin;

```text
05:01.1
```

ifadesinde farklı alanların PCI topology içerisindeki bus, device ve function bilgilerini temsil ettiği görüldü.

Bu adreslemenin character device major/minor numaralarıyla aynı kavram olmadığı özellikle ayrıştırıldı.

PCI Bus:Device.Function bilgisinin fiziksel/logical PCI cihazını tanımladığı, character device major/minor bilgilerinin ise Linux character device subsystem'i tarafından kullanılan farklı bir adresleme yapısı olduğu öğrenildi.

## 3. PCI Device ve Driver Eşleşmesinin İncelenmesi

Linux Kernel içerisinde PCI cihazlarının uygun driver ile eşleştirilme mekanizması incelendi.

Driver tarafından desteklenen Vendor ID ve Device ID bilgilerinin bir PCI device ID tablosunda tanımlanabildiği görüldü.

Kernel'ın sistemde bulunan PCI cihazının kimlik bilgilerini driver'ın desteklediği cihaz listesiyle karşılaştırarak uygun driver'ı belirleyebildiği değerlendirildi.

Eşleşme gerçekleştiğinde driver'ın;

```text
probe()
```

fonksiyonunun çalıştırılabildiği, cihaz sistemden ayrıldığında veya driver kaldırıldığında ise;

```text
remove()
```

mekanizmasının devreye girdiği incelendi.

Bu yapı;

```text
PCI Device keşfedilir
        ↓
Vendor ID / Device ID
        ↓
Driver ID table ile eşleşme
        ↓
probe()
        ↓
Device initialization
```

şeklinde değerlendirildi.

## 4. Kernel Driver ile Donanım Üreticisi Arasındaki İlişkinin İncelenmesi

Bir cihazın üreticisi ile Linux Kernel içerisinde çalışan driver'ın farklı kavramlar olduğu incelendi.

Örneğin NVIDIA, Intel, Realtek veya başka bir üretici fiziksel donanımı üretebilirken bu cihazı yöneten driver'ın Linux Kernel içerisinde çalıştığı görüldü.

Driver'ın temel görevinin işletim sistemi ile donanım arasında bir yazılım katmanı oluşturmak olduğu değerlendirildi.

Temel yapı;

```text
User Space
     ↓
Linux Kernel
     ↓
Device Driver
     ↓
PCI/PCIe Device
```

şeklinde ele alındı.

## 5. PCI BAR Yapısının İncelenmesi

PCI/PCIe cihazlarının CPU tarafından erişilebilen register veya memory bölgelerini tanımlamak amacıyla BAR (Base Address Register) yapılarının kullanılabildiği incelendi.

Bir PCI cihazında birden fazla BAR bulunabileceği görüldü.

Kavramsal olarak;

```text
PCI Device
   │
   ├── BAR0 → Register / memory region
   ├── BAR1 → Başka bir region
   ├── BAR2 → Başka bir region
   └── ...
```

şeklinde değerlendirildi.

BAR0 ve BAR1'in aynı içeriğin iki kopyası olmadığı, her BAR'ın cihaz tarafından farklı bir address region tanımlamak amacıyla kullanılabileceği öğrenildi.

Bir BAR'ın kontrol register'larını, başka bir BAR'ın daha büyük device memory alanını temsil edebileceği değerlendirildi.

## 6. MMIO Mekanizmasının İncelenmesi

MMIO (Memory-Mapped I/O), cihaz register'larının CPU address space içerisinde memory benzeri adresler üzerinden erişilebilir hale getirilmesi yaklaşımı olarak incelendi.

PCI BAR tarafından tanımlanan device memory/register region'ının driver tarafından map edilerek kernel virtual address space üzerinden erişilebildiği görüldü.

Temel ilişki;

```text
PCI Device
    ↓
BAR
    ↓
Device register region
    ↓
MMIO mapping
    ↓
Kernel virtual address
    ↓
Driver
```

şeklinde değerlendirildi.

MMIO bölgesine normal RAM gibi davranılmaması gerektiği ve kernel içerisinde device register erişimleri için uygun I/O accessor mekanizmalarının kullanılması gerektiği öğrenildi.

Bu kapsamda;

```text
readl()
writel()
```

gibi fonksiyonların temel kullanım amacı incelendi.

## 7. BAR ile DMA Arasındaki Farkın İncelenmesi

Bir önceki gün öğrenilen DMA mekanizması ile BAR/MMIO yapısının aynı amaç için kullanılmadığı ayrıştırıldı.

MMIO'nun temel olarak CPU'nun cihaz register'larına erişmesini sağladığı, DMA'nın ise cihaz ile RAM arasında büyük veri transferlerinin gerçekleştirilmesinde kullanıldığı görüldü.

Temel yapı;

```text
CPU / Driver
     │
     │ MMIO
     ▼
Device Registers
     │
     │ DMA başlatılır/yönetilir
     ▼
DEVICE ═══════════ RAM
          DMA
```

şeklinde değerlendirildi.

Böylece MMIO'nun çoğunlukla cihazın kontrol yolu, DMA'nın ise yüksek miktarlı verinin taşındığı veri yolu olarak düşünülebileceği görüldü.

## 8. Character Device ile PCI Device Ayrımının İncelenmesi

Daha önce geliştirilen character driver yapısı ile PCI device yapısı arasındaki ilişki tekrar ele alındı.

PCI device'ın fiziksel veya sanal donanımı temsil ettiği, character device'ın ise driver'ın kullanıcı alanına sunduğu arayüzlerden biri olduğu görüldü.

Bir PCI driver'ın aynı zamanda;

```text
/dev/device0
```

gibi bir character device oluşturarak userspace uygulamalarına;

```text
open()
read()
write()
ioctl()
poll()
mmap()
```

gibi sistem çağrıları üzerinden erişim sağlayabileceği değerlendirildi.

Bu nedenle PCI device ile `/dev` altında oluşturulan character device'ın aynı kavram olmadığı, ancak aynı driver mimarisi içerisinde birbirine bağlanabileceği öğrenildi.

## 9. Kernel Execution Context Yapısının Tekrar İncelenmesi

Kernel kodunun çalışabileceği farklı execution context türleri tekrar ele alındı.

Temel ayrım;

```text
Kernel Execution
       │
       ├── Process Context
       │
       └── Atomic Context
```

şeklinde değerlendirildi.

Process context içerisinde çalışan kodun uygun şartlarda sleep edebildiği, atomic context içerisinde ise sleep edilmemesi gerektiği tekrarlandı.

Process context tarafında syscall, kernel thread ve workqueue gibi yapılar; atomic context tarafında ise hard IRQ ve softIRQ gibi yapılar değerlendirildi.

## 10. Hard IRQ ve SoftIRQ Context Yapılarının İncelenmesi

Hard IRQ context'in doğrudan donanım interrupt'ı sonucunda çalışan interrupt handler ile ilişkili olduğu incelendi.

Hard IRQ handler'ın mümkün olduğunca kısa tutulması ve sleep edebilecek işlemlerin burada gerçekleştirilmemesi gerektiği görüldü.

SoftIRQ ise bazı interrupt sonrası işlemlerin ertelenmiş şekilde gerçekleştirilmesine yardımcı olan kernel mekanizması olarak incelendi.

Network receive ve transmit işlemleri gibi yüksek performans gerektiren kernel işlerinin SoftIRQ mekanizmasıyla ilişkili olabileceği değerlendirildi.

Temel ayrım;

```text
Hard IRQ
→ Donanım interrupt'ına doğrudan cevap

SoftIRQ
→ Ertelenmiş atomic-context işlemleri
```

şeklinde ele alındı.

Her iki context içerisinde de sleep edilmemesi gerektiği görüldü.

## 11. Workqueue ile SoftIRQ Arasındaki Farkın İncelenmesi

Workqueue'nun deferred work gerçekleştirmek için kullanılmasına rağmen SoftIRQ'dan farklı olarak process context içerisinde çalıştığı tekrarlandı.

Bu nedenle;

```text
SoftIRQ
→ Atomic context
→ Sleep yok

Workqueue
→ Process context
→ Sleep mümkün
```

ayrımı yapıldı.

Daha önce geliştirilen timer/FIFO yapısında timer callback içerisinde uzun veya sleep edebilecek işlemleri gerçekleştirmek yerine işin workqueue'ya aktarılmasının bu nedenle önemli olduğu tekrar değerlendirildi.

## 12. Spinlock, IRQ ve IRQSave Mekanizmalarının İncelenmesi

Spinlock mekanizması interrupt context ile ilişkili olarak tekrar ele alındı.

Bu kapsamda;

```text
spin_lock()
spin_lock_irq()
spin_lock_irqsave()
```

mekanizmalarının farkları incelendi.

`spin_lock()` fonksiyonunun temel spinlock işlemini gerçekleştirdiği görüldü.

`spin_lock_irq()` kullanımında local interrupt'ların disable edilerek aynı CPU üzerindeki interrupt'ın korunan critical section'ı kesmesinin engellenebildiği incelendi.

`spin_lock_irqsave()` kullanımında ise interrupt durumunun önce kaydedildiği, ardından interrupt'ların disable edildiği ve işlem sonunda eski interrupt durumunun restore edildiği görüldü.

Temel ilişki;

```text
spin_lock()
→ lock

spin_lock_irq()
→ IRQ disable + lock

spin_lock_irqsave()
→ IRQ state save
→ IRQ disable
→ lock
→ unlock
→ IRQ state restore
```

şeklinde değerlendirildi.

## 13. Atomic Context ile Atomic Operation Ayrımının İncelenmesi

Atomic context ve atomic operation kavramlarının aynı olmadığı özellikle tekrarlandı.

Atomic context'in sleep edilemeyen execution context'i ifade ettiği;

```text
atomic_t
atomic_inc()
atomic_dec()
```

gibi yapıların ise belirli işlemlerin bölünemez şekilde gerçekleştirilmesine yönelik atomic operation mekanizmaları olduğu görüldü.

Bu nedenle;

```text
Atomic Context ≠ Atomic Operation
```

ayrımı yapıldı.

## 14. Mutex, Spinlock ve Binary Semaphore Yapılarının Tekrarı

Synchronization mekanizmaları genel tekrar kapsamında karşılaştırıldı.

Mutex'in lock alınamadığında task'in sleep edebilmesine izin verdiği ve mutual exclusion amacıyla kullanıldığı görüldü.

Spinlock'ta ise lock alınamadığında execution context'in sleep etmek yerine spin ederek beklediği tekrarlandı.

Binary semaphore'un 0/1 değer mantığıyla synchronization veya mutual exclusion amacıyla kullanılabildiği ancak mutex ile aynı ownership semantiğine sahip olmadığı değerlendirildi.

Temel ayrım;

```text
Mutex
→ Sleep mümkün

Spinlock
→ Sleep yok
→ Spin ederek bekleme

Binary Semaphore
→ 0/1 synchronization mekanizması
→ Bekleyen task sleep edebilir
```

şeklinde tekrarlandı.

## 15. procfs, sysfs ve debugfs Yapılarının İncelenmesi

Linux Kernel'ın userspace'e bilgi sunmak için kullandığı sanal filesystem yapıları karşılaştırıldı.

procfs'in process ve kernel runtime/system bilgilerini userspace'e sunabildiği görüldü.

sysfs'in kernel device modelini, device-driver-bus ilişkilerini ve expose edilen attribute'ları userspace'e sunduğu incelendi.

debugfs'in ise kernel ve driver geliştiricilerinin internal debugging bilgilerini userspace üzerinden inceleyebilmesi amacıyla kullanılabildiği görüldü.

Temel ayrım;

```text
procfs
→ Process + runtime/system information

sysfs
→ Device / driver / bus / attributes

debugfs
→ Internal debugging information
```

şeklinde değerlendirildi.

Debugfs'in stabil userspace ABI sağlamak amacıyla tasarlanmadığı özellikle incelendi.

## 16. Gerçek Sistem Üzerinde Sanal Filesystem Yapılarının İncelenmesi

procfs, sysfs ve debugfs yapılarının yalnızca teorik olarak değil, Linux sistemindeki gerçek driver ve kernel bilgilerinin incelenmesi amacıyla kullanılabileceği değerlendirildi.

PCI cihazlarının sysfs üzerinden device, vendor, driver ve resource bilgilerinin görülebileceği; procfs üzerinden memory, CPU ve interrupt gibi sistem bilgilerinin incelenebileceği; debugfs üzerinden ise destekleyen driver'ların daha ayrıntılı internal debug bilgilerini expose edebileceği görüldü.

Bu yapıların driver geliştirme ve hata ayıklama sırasında farklı amaçlara hizmet ettiği tekrarlandı.

## 17. poll, Wait Queue ve Timer Mekanizmasının Tekrarı

Daha önce geliştirilen FIFO tabanlı driver üzerinden `poll()` mekanizması tekrar değerlendirildi.

`poll()` mekanizmasının userspace uygulamasının bir file descriptor üzerinde event/readiness beklemesini sağladığı görüldü.

Driver tarafında wait queue kullanılarak henüz veri bulunmadığında process'in bekletilebildiği, FIFO'ya veri geldiğinde;

```text
wake_up_interruptible()
```

ile bekleyen process'in uyandırılabildiği tekrarlandı.

Timer ve workqueue tarafından FIFO'ya veri eklenmesiyle poll bekleyen userspace uygulamasının tekrar çalışabilir hale geldiği incelendi.

Temel akış;

```text
Userspace poll()
      ↓
Driver .poll()
      ↓
Wait Queue
      ↓
FIFO'ya event gelir
      ↓
wake_up_interruptible()
      ↓
poll() readiness kontrolü
      ↓
Userspace read()
```

şeklinde değerlendirildi.

## 18. Kernel Memory Allocation Mekanizmalarının Genel Tekrarı

Daha önce öğrenilen;

```text
kmalloc()
vmalloc()
dma_alloc_coherent()
```

mekanizmaları tekrar karşılaştırıldı.

`kmalloc()` fonksiyonunun normal kernel allocation işlemleri için kullanıldığı ve fiziksel olarak contiguous allocation sağladığı tekrarlandı.

`vmalloc()` ile sanal olarak contiguous fakat fiziksel olarak dağınık olabilen memory alanlarının oluşturulabildiği görüldü.

`dma_alloc_coherent()` fonksiyonunun ise CPU ile DMA-capable device arasında paylaşılacak coherent DMA memory oluşturmak amacıyla kullanıldığı tekrarlandı.

DMA allocation'ın normal kernel memory allocation ile aynı amaç için kullanılmadığı özellikle vurgulandı.

## 19. Userspace malloc ve calloc Mekanizmalarının Tekrarı

Kernel memory allocation yöntemleriyle userspace allocation yöntemlerinin karıştırılmaması amacıyla `malloc()` ve `calloc()` fonksiyonları tekrar değerlendirildi.

Her iki fonksiyonun da userspace dinamik bellek tahsisi için kullanılan C standart kütüphane fonksiyonları olduğu görüldü.

`malloc()` fonksiyonunun istenilen byte miktarında allocation yaptığı ancak alanı sıfırlama garantisi vermediği, `calloc()` fonksiyonunun ise eleman sayısı ve eleman boyutuna göre allocation gerçekleştirerek tahsis edilen belleğin tüm bitlerini sıfırladığı tekrarlandı.

## 20. C Program Memory Layout Yapısının İncelenmesi

Bir userspace C programının virtual address space'i içerisindeki temel memory bölgeleri ele alındı.

Bu kapsamda;

```text
Text
RODATA
Data
BSS
Heap
Stack
```

bölümleri incelendi.

Text bölümünün executable program kodunu, RODATA'nın read-only verileri, Data bölümünün başlangıç değeri verilmiş global/static değişkenleri, BSS bölümünün sıfır başlangıçlı veya açıkça initialize edilmemiş global/static değişkenleri içerdiği görüldü.

Heap'in dinamik allocation işlemleriyle, stack'in ise fonksiyon çağrıları ve otomatik ömürlü local değişkenlerle ilişkili olduğu incelendi.

Bir pointer değişkeninin stack üzerinde bulunurken `malloc()` ile aldığı dinamik alanın farklı bir memory bölgesinde bulunabileceği üzerinden pointer ile pointer'ın gösterdiği object arasındaki fark tekrar değerlendirildi.

## 21. Virtual Memory, Paging ve Swap Mekanizmalarının Tekrarı

Virtual memory mekanizmasının process isolation, memory protection ve fiziksel belleğin soyutlanmasını sağladığı tekrarlandı.

Virtual memory'nin page adı verilen sabit boyutlu birimler üzerinden yönetilebildiği ve virtual page'lerin page table mekanizması aracılığıyla fiziksel page frame'lere map edildiği incelendi.

Page fault kavramı tekrar ele alınarak bir memory erişiminin mevcut mapping durumu nedeniyle kernel müdahalesi gerektirmesi durumunda page fault oluşabileceği değerlendirildi.

Swap mekanizmasının ise uygun anonymous memory page'lerinin memory pressure durumunda disk/SSD üzerindeki swap alanına taşınabilmesine yardımcı olduğu görüldü.

Virtual memory ile swap'ın aynı kavram olmadığı, swap'ın virtual memory sisteminin kullanabileceği mekanizmalardan yalnızca biri olduğu tekrarlandı.

## 22. SLAB/SLUB Object Allocation Mantığının Tekrarı

Küçük kernel object allocation işlemlerinde her küçük allocation için bütün bir page'in harcanmasının verimsiz olabileceği üzerinden SLAB/SLUB allocator mantığı tekrar ele alındı.

SLUB allocator'ın page'lerden daha küçük kernel object'leri için allocation sağlayabildiği görüldü.

Temel ilişki;

```text
Physical Memory
      ↓
Pages
      ↓
SLUB
      ↓
Small Kernel Objects
```

şeklinde tekrarlandı.

`kmalloc()` gibi genel amaçlı kernel allocation API'lerinin altında bu allocator mekanizmalarının kullanılabileceği değerlendirildi.

## Gün Sonu Değerlendirmesi

Yedinci günün sonunda Linux driver geliştirme sürecinde memory ve DMA bilgisinin gerçek donanım mimarisiyle nasıl birleştiği daha kapsamlı şekilde ele alınmıştır.

PCI/PCIe cihazlarının kernel tarafından nasıl tanımlandığı, Bus:Device.Function adresleme yapısı, Vendor ID ve Device ID üzerinden driver eşleşmesi, probe/remove yaşam döngüsü, BAR yapıları ve MMIO mekanizması incelenmiştir. BAR/MMIO ile DMA arasındaki fark ortaya konularak MMIO'nun cihazın kontrol/register erişiminde, DMA'nın ise cihaz ile ana bellek arasındaki veri transferlerinde kullanılabileceği değerlendirilmiştir.

Kernel execution context konusu genişletilerek process context, hard IRQ, softIRQ ve workqueue yapıları karşılaştırılmıştır. Atomic context içerisinde sleep edilememesinin mutex, spinlock, GFP_ATOMIC ve deferred-work tasarımı üzerindeki etkisi tekrar değerlendirilmiştir. Spinlock, spin_lock_irq ve spin_lock_irqsave mekanizmaları interrupt davranışı açısından karşılaştırılmıştır.

Ayrıca procfs, sysfs ve debugfs sanal filesystem yapılarının farklı kullanım amaçları incelenmiş; sysfs'in device model ve attribute bilgileri, procfs'in process ve runtime/system bilgileri, debugfs'in ise internal debugging bilgileri için kullanılabileceği görülmüştür.

Günün son bölümünde daha önce öğrenilen atomic operation, race condition, mutex-spinlock-binary semaphore, kmalloc-vmalloc-dma_alloc_coherent, malloc-calloc, poll/wait queue/timer, virtual memory, paging, swap, C program memory layout ve SLUB object allocation konuları genel tekrar kapsamında birbirleriyle ilişkilendirilmiştir.

Böylece önceki gün öğrenilen;

```text
Memory
   ↓
DMA
   ↓
Descriptor Ring
```

yapısı, yedinci gün sonunda;

```text
                         USER SPACE
                             │
                   read / ioctl / poll / mmap
                             │
                             ▼
                    Character Device API
                             │
──────────────────────── KERNEL ────────────────────────
                             │
                         PCI Driver
                       ↙           ↘
                    MMIO           DMA
                     │              │
                    BAR       Descriptor Ring
                     │              │
                     └──────┬───────┘
                            ▼
                       PCIe DEVICE
                            │
                       IRQ / Events
                            │
                 Hard IRQ / Deferred Work
                            │
                     Wait Queue / poll
```

şeklinde daha bütüncül bir Linux device driver mimarisi içerisinde değerlendirilmiştir.
