STAJ RAPORU – 8. GÜN
Linux Platform Device/Driver, Device Model, sysfs-debugfs Arayüzleri ve Driver Uygulamasının Geliştirilmesi

Yedinci gün sonunda Linux PCI/PCIe driver mimarisi, BAR-MMIO yapısı, DMA ile MMIO arasındaki ilişki, kernel execution context, interrupt mekanizmaları ve procfs/sysfs/debugfs gibi sanal dosya sistemleri incelenmişti. Ayrıca daha önce öğrenilen synchronization, memory allocation, poll, wait queue, timer ve FIFO mekanizmaları genel bir Linux device driver mimarisi içerisinde ilişkilendirilmişti.

Sekizinci gün çalışmalarında Linux device model konusu uygulama ağırlıklı olarak genişletildi. Daha önce geliştirilen kernel modülü üzerinden device, class ve sysfs attribute ilişkileri incelendi. Ardından platform device ve platform driver mimarisinin temel çalışma prensipleri ele alınarak PCI gibi otomatik keşfedilebilen bus yapıları ile platform cihazlarının farkları değerlendirildi.

1. Linux Device Model Yapısının İncelenmesi

Linux Kernel içerisinde cihazların yalnızca /dev altında oluşturulan dosyalardan ibaret olmadığı, kernel içerisinde daha genel bir device model yapısının bulunduğu incelendi.

Temel olarak;

Kernel
  │
  ├── Bus
  │
  ├── Device
  │
  ├── Driver
  │
  └── Class

yapıları arasındaki ilişkiler değerlendirildi.

Character device'ın userspace'e open(), read(), write(), ioctl() ve poll() gibi sistem çağrıları üzerinden bir arayüz sunduğu; struct device yapısının ise cihazın Linux device model içerisindeki temsilinde kullanıldığı görüldü.

2. Class ve Device Yapılarının Uygulama Üzerinden İncelenmesi

Daha önce geliştirilen character driver uygulamasında class ve device oluşturma mekanizmaları tekrar incelendi.

Driver yüklendiğinde oluşturulan cihazın sysfs içerisinde;

/sys/class/ahmet_fifo_class/ahmet_fifo0/

şeklinde temsil edilebildiği gözlemlendi.

Böylece /dev altında bulunan character device ile /sys/class altında bulunan device model temsilinin farklı amaçlara hizmet ettiği görüldü.

Temel ayrım;

/dev/ahmet_fifo
      ↓
read / write / ioctl / poll


/sys/class/.../ahmet_fifo0
      ↓
device information / attributes

şeklinde değerlendirildi.

3. sysfs Attribute Mekanizmasının İncelenmesi

Driver'ın çalışma durumuna ilişkin bilgilerin userspace'e sysfs attribute'ları üzerinden sunulması incelendi.

Bu kapsamda FIFO ve driver çalışma durumuyla ilişkili çeşitli bilgiler sysfs üzerinden expose edildi.

Örneğin;

fifo_len
fifo_capacity
produced_events
read_events
dropped_events
timer_callbacks

gibi attribute'lar üzerinden driver içerisindeki runtime durumunun userspace tarafından okunabilmesi sağlandı.

Gerçek sistem üzerinde;

cat /sys/class/ahmet_fifo_class/ahmet_fifo0/fifo_len
cat /sys/class/ahmet_fifo_class/ahmet_fifo0/fifo_capacity
cat /sys/class/ahmet_fifo_class/ahmet_fifo0/produced_events

gibi komutlarla kernel içerisindeki değerlerin userspace'ten gözlemlenebildiği görüldü.

4. sysfs Attribute Group Yapısının İncelenmesi

Birden fazla sysfs attribute'un tek tek yönetilmesi yerine attribute_group yapısı kullanılarak gruplanabileceği incelendi.

Temel yapı;

device attributes
      │
      ├── attr 1
      ├── attr 2
      ├── attr 3
      └── ...
           ↓
    attribute_group
           ↓
        device

şeklinde değerlendirildi.

Attribute pointer dizilerinin NULL ile sonlandırılması gerektiği ve attribute group'un bu liste üzerinden ilgili dosyaları sysfs içerisinde oluşturabildiği görüldü.

5. Read-Only ve Read-Write sysfs Attribute'larının İncelenmesi

sysfs attribute'larının yalnızca bilgi okumak için değil, kontrollü biçimde driver parametrelerini değiştirmek için de kullanılabileceği incelendi.

Read-only attribute'larda userspace'in driver durumunu gözlemlediği;

Kernel value
    ↓
show()
    ↓
sysfs
    ↓
cat

akışı değerlendirildi.

Yazılabilir attribute'larda ise;

echo value
    ↓
sysfs
    ↓
store()
    ↓
parse
    ↓
driver state değişikliği

mantığı incelendi.

Bu yapı üzerinden timer gibi driver içerisindeki çalışma mekanizmalarının userspace tarafından kontrol edilebilmesinin nasıl tasarlanabileceği değerlendirildi.

6. sysfs ile debugfs Arasındaki Farkın Uygulama Üzerinden İncelenmesi

Daha önce teorik olarak karşılaştırılan sysfs ve debugfs yapıları uygulama açısından tekrar değerlendirildi.

sysfs'in;

device
driver
bus
attributes
configuration/state

gibi device model ile ilişkili bilgilerin userspace'e sunulmasında kullanılabildiği görüldü.

debugfs'in ise daha çok geliştiriciye yönelik;

internal counters
debug state
driver statistics
diagnostic information

gibi bilgilerin incelenmesinde kullanılabileceği değerlendirildi.

Bu nedenle debugfs'in stabil bir userspace ABI olarak düşünülmemesi gerektiği tekrarlandı.

7. Driver Runtime İstatistiklerinin İzlenmesi

Driver içerisinde gerçekleşen olayların sayaçlarla takip edilmesi uygulandı.

Bu kapsamda;

produced events
read events
dropped events
timer callbacks
FIFO occupancy

gibi bilgiler üzerinden driver'ın çalışma davranışının gözlemlenebileceği görüldü.

Özellikle producer'ın consumer'dan daha hızlı çalışması durumunda FIFO'nun dolabileceği ve yeni event'lerin kaybedilebileceği değerlendirildi.

Temel yapı;

Producer
   ↓
FIFO
   ↓
Consumer


Producer > Consumer
        ↓
FIFO occupancy artar
        ↓
FIFO full
        ↓
event/drop durumu

şeklinde incelendi.

8. Platform Device Kavramının İncelenmesi

Linux Kernel içerisindeki platform device kavramına giriş yapıldı.

PCI/PCIe gibi cihazların bus üzerinden belirli discovery mekanizmalarına sahip olabildiği, bazı SoC üzerindeki donanımların ise sistem içerisinde önceden bilinen veya firmware tarafından tarif edilen kaynaklara sahip olduğu görüldü.

Platform device yapısının özellikle;

SoC peripherals
UART
I2C controller
SPI controller
GPIO controller
timer
embedded Ethernet controller

gibi cihazlarla sıklıkla ilişkili olduğu değerlendirildi.

9. Platform Device ve Platform Driver İlişkisinin İncelenmesi

Platform device ile onu yöneten platform driver'ın ayrı yapılar olduğu incelendi.

Temel ilişki;

Platform Device
      │
      │ match
      ▼
Platform Driver
      │
      ▼
    probe()

şeklinde değerlendirildi.

Driver ile device arasında uygun eşleşme gerçekleştiğinde probe() fonksiyonunun çağrıldığı, cihaz veya driver kaldırıldığında ise remove mekanizmasının kullanılabildiği görüldü.

Bu yapı daha önce PCI driver tarafında öğrenilen;

Device
  ↓
Driver matching
  ↓
probe()

mantığıyla ilişkilendirildi.

10. Platform Device Resource Kavramının İncelenmesi

Bir platform cihazının yalnızca isimden ibaret olmadığı; gerçek donanım sürücülerinde çeşitli hardware resource bilgilerinin driver'a aktarılması gerektiği incelendi.

Örneğin;

MMIO region
IRQ
DMA-related information
clock
reset
GPIO

gibi kaynakların bir cihazın çalışması için gerekli olabileceği değerlendirildi.

Bu yapı yedinci gün öğrenilen MMIO ve interrupt bilgileriyle ilişkilendirildi.

Temel olarak;

Platform Device
      │
      ├── MMIO resource
      ├── IRQ resource
      └── diğer resources
              ↓
            probe()
              ↓
            Driver

mantığı ele alındı.

11. Device Tree Kavramına Giriş

Platform device mimarisiyle bağlantılı olarak Device Tree'nin temel amacı incelendi.

Device Tree'nin driver kodunun kendisi olmadığı; işletim sistemine sistemde bulunan donanımın özelliklerini tarif eden bir hardware description mekanizması olduğu değerlendirildi.

Kavramsal olarak;

Device Tree
    ↓
"Bu donanım sistemde mevcut"
    ↓
register / MMIO bilgileri
interrupt bilgileri
compatible bilgisi
diğer hardware özellikleri
    ↓
Kernel
    ↓
uygun driver

ilişkisi kuruldu.

12. compatible ve Driver Matching Mantığının İncelenmesi

Device Tree içerisindeki compatible bilgisinin uygun driver'ın belirlenmesinde kullanılabileceği incelendi.

Temel yapı;

Device Tree Node
compatible = "vendor,device"
        │
        │ match
        ▼
Driver OF match table
        │
        ▼
      probe()

şeklinde değerlendirildi.

Böylece PCI tarafındaki Vendor ID / Device ID eşleşmesi ile Device Tree tarafındaki compatible tabanlı eşleşmenin aynı mekanizma olmadığı, ancak her ikisinin de uygun cihaz ile uygun driver'ı bir araya getirme problemine hizmet ettiği görüldü.

13. Platform Device ve Device Tree İlişkisinin Değerlendirilmesi

Platform device ile Device Tree'nin aynı kavram olmadığı özellikle ayrıştırıldı.

Device Tree'nin donanımı tarif edebildiği, platform bus/device modelinin ise kernel içerisinde cihaz ile driver'ın eşleştirilmesinde kullanılan yapılardan biri olduğu değerlendirildi.

Temel ilişki;

          DEVICE TREE
              │
              │ hardware description
              ▼
          Linux Kernel
              │
              ▼
        Platform Device
              │
              │ match
              ▼
        Platform Driver
              │
              ▼
            probe()

şeklinde ele alındı.

Gün Sonu Değerlendirmesi

Sekizinci gün sonunda daha önce geliştirilen character driver uygulaması Linux device model açısından genişletilmiş; /dev character device arayüzü ile sysfs device/attribute arayüzü arasındaki fark uygulama üzerinden incelenmiştir.

Driver içerisindeki FIFO uzunluğu, kapasite, üretilen ve okunan event sayıları, drop durumları ve timer callback sayıları gibi runtime bilgilerin sysfs üzerinden userspace'e aktarılması ele alınmıştır. Attribute ve attribute group mekanizmaları incelenerek driver'ın yalnızca veri transferi yapan bir yapı değil, aynı zamanda çalışma durumunun gözlemlenebildiği ve belirli parametrelerinin yönetilebildiği bir kernel bileşeni olduğu görülmüştür.

Günün ilerleyen bölümünde Linux platform device/platform driver mimarisine geçilmiş; platform cihazlarının resource yapısı, device-driver matching ve probe() yaşam döngüsü değerlendirilmiştir. Device Tree'nin donanımı tarif etmedeki rolüne giriş yapılarak compatible tabanlı eşleşmenin platform driver ile ilişkisi incelenmiştir.

Böylece önceki günlerde oluşturulan;

Userspace
   ↓
Character Device
   ↓
Driver
   ↓
FIFO / Timer / Workqueue

yapısı sekizinci gün sonunda;

                         USER SPACE
                        ↙          ↘
                /dev interface    sysfs
                     │              │
              read/write/poll    attributes
                     │              │
                     └──────┬───────┘
                            ▼
                     DEVICE / DRIVER
                            │
                   Platform Driver
                            │
                         probe()
                            │
               ┌────────────┴────────────┐
               │                         │
            MMIO/IRQ                Device Tree
               │                    description
               └────────────┬────────────┘
                            ▼
                         HARDWARE

şeklinde daha geniş bir Linux device model ve embedded driver mimarisi içerisinde değerlendirilmiştir.