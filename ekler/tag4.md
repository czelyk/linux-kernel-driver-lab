STAJ RAPORU – 4. GÜN
Linux Character Device Driver'da FIFO Mimarisi, Blocking I/O, Kernel Timer, Atomic İşlemler ve Workqueue Mekanizmaları

Üçüncü gün sonunda geliştirilen karakter sürücüsü; çoklu cihaz desteği, wait queue, non-blocking I/O, poll(), asenkron bildirim, mmap(), llseek() ve offset tabanlı okuma/yazma gibi gelişmiş özelliklere sahip hale getirilmişti. Dördüncü günün amacı ise sürücünün veri yönetim yapısını gerçek bir akış mantığına yaklaştırmak, FIFO tabanlı veri aktarımını öğrenmek ve kernel içerisinde zamanlanmış/asenkron işlemlerin nasıl gerçekleştirildiğini incelemek oldu.

1. Düz Buffer Yapısından FIFO Mimarisine Geçiş

İlk olarak sürücüde kullanılan klasik düz buffer yapısının çalışma mantığı tekrar değerlendirildi.

Düz buffer yapısında;

buffer
buffer_size
buffer_capacity
file->f_pos

değerleri kullanılarak veri yönetilmekteydi.

Ancak sürekli veri üreten yapılarda dosya konumunun ilerlemesi ve yeni verilerin aynı buffer konumlarına yazılması bazı problemlere neden olmaktaydı.

Bu nedenle Linux Kernel'in sağladığı;

struct kfifo

yapısı incelendi.

FIFO'nun "First In First Out" mantığıyla çalıştığı ve ilk eklenen verinin ilk okunan veri olduğu öğrenildi.

Bu yapı sayesinde karakter sürücüsünün veri yönetimi bir dosya mantığından çok gerçek bir veri akışı mantığına dönüştürülmeye başlandı.

2. Kernel FIFO Fonksiyonlarının Kullanılması

FIFO oluşturmak ve yönetmek amacıyla;

kfifo_alloc()
kfifo_free()
kfifo_in()
kfifo_to_user()
kfifo_is_empty()
kfifo_is_full()
kfifo_len()
kfifo_avail()

fonksiyonlarının çalışma mantıkları incelendi.

kfifo_in() ile FIFO'ya veri eklenirken, kfifo_to_user() ile FIFO içerisindeki verinin doğrudan kullanıcı alanına aktarılabileceği görüldü.

Ayrıca FIFO'nun doluluk ve boşluk durumları kontrol edilerek okuma ve yazma işlemlerinin buna göre yönetilmesi sağlandı.

3. FIFO Tabanlı Read İşleminin Geliştirilmesi

Sürücünün read() fonksiyonu FIFO mantığına uygun olacak şekilde yeniden düzenlendi.

FIFO boş olduğunda bloklayıcı olarak açılan süreçlerin;

wait_event_interruptible()

ile uyutulması sağlandı.

FIFO'ya veri geldiğinde ise;

wake_up_interruptible()

kullanılarak bekleyen okuyucular tekrar çalıştırıldı.

Dosya O_NONBLOCK ile açılmışsa FIFO boş olduğunda süreç uyutulmadan;

-EAGAIN

döndürülmesi sağlandı.

Böylece üçüncü gün öğrenilen wait queue ve non-blocking I/O mekanizmaları FIFO altyapısıyla birleştirilmiş oldu.

4. FIFO Tabanlı Blocking Write Mekanizması

FIFO'nun kapasitesi sınırlı olduğu için FIFO tamamen dolduğunda write() işleminin nasıl davranması gerektiği incelendi.

FIFO dolu olduğunda bloklayıcı yazma yapan proses;

write_queue

üzerinde bekletildi.

FIFO'dan veri okunduğunda boş alan oluştuğu için;

wake_up_interruptible(&dev->write_queue)

kullanılarak bekleyen yazıcı proseslerin uyandırılması sağlandı.

Bu mekanizma ile şu akış gerçekleştirildi:

FIFO dolar → write() bekler → başka proses FIFO'dan veri okur → FIFO'da boş alan oluşur → bekleyen write() devam eder.

Hazırlanan kullanıcı alanı test programları ile blocking write davranışı gözlemlendi.

5. Kernel Timer Mekanizmasının İncelenmesi

Linux Kernel içerisinde belirli zaman aralıklarında işlem gerçekleştirmek amacıyla kullanılan timer mekanizması incelendi.

Bu amaçla cihaz yapısına;

struct timer_list timer

alanı eklendi.

Timer oluşturmak ve yeniden planlamak amacıyla;

timer_setup()
mod_timer()
msecs_to_jiffies()
jiffies

kavramları incelendi.

Timer'ın her bir saniyede bir callback fonksiyonunu çağırması sağlandı.

Bu çalışma sırasında jiffies değerinin kernel içerisindeki zaman ölçüm mekanizmalarından biri olduğu ve milisaniye değerlerinin msecs_to_jiffies() ile kernel zaman birimine dönüştürülebileceği öğrenildi.

6. Timer Callback Mantığının İncelenmesi

Timer süresi dolduğunda çalışan callback fonksiyonunun normal kullanıcı süreçlerinden farklı bir execution context içerisinde çalıştığı incelendi.

Callback içerisinde uzun süren veya uyuyabilecek işlemlerin yapılmasının uygun olmadığı görüldü.

Bu nedenle timer callback fonksiyonunun mümkün olduğunca kısa tutulması gerektiği öğrenildi.

Callback içerisinde temel olarak;

mod_timer()

ile bir sonraki timer olayı planlandı.

Timer'ın bir defalık çalışmasının yanında callback içerisinden yeniden kurulması sayesinde periyodik olarak çalıştırılabileceği görüldü.

7. Atomic İşlemlerin İncelenmesi

Kernel içerisinde bir değerin birden fazla execution context tarafından güvenli biçimde değiştirilmesi gerektiğinde kullanılabilen atomic işlemler incelendi.

Bu kapsamda;

atomic_t
atomic_set()
atomic_read()
atomic_inc()

gibi yapı ve fonksiyonların çalışma mantıkları ele alındı.

Atomic işlemlerin basit sayaç işlemlerini bölünemez şekilde gerçekleştirebildiği ve bazı durumlarda mutex kullanmadan güvenli sayaç yönetimi sağlayabildiği öğrenildi.

Özellikle timer callback gibi farklı execution context'lerden erişilebilecek sayaçların yönetiminde atomic işlemlerin kullanım amacı incelendi.

8. Timer ve Atomic Sayaç İlişkisinin İncelenmesi

Timer'ın her saniye çalıştığı fakat belirli işlemlerin örneğin her üçüncü timer olayında gerçekleştirilmesi senaryosu üzerinde çalışıldı.

Mantık olarak;

saniye → sayaç artırılır
saniye → sayaç artırılır
saniye → sayaç koşulu sağlanır ve olay gerçekleştirilir

şeklinde bir yapı incelendi.

Bu çalışma ile timer'ın zamanlamayı sağladığı, atomic değişkenin ise kaç timer olayı gerçekleştiğinin güvenli biçimde takip edilmesi amacıyla kullanılabileceği öğrenildi.

Böylece timer ve atomic kavramlarının birbirinden farklı görevleri olduğu daha net şekilde görüldü.

9. Workqueue Mekanizmasının İncelenmesi

Timer callback içerisinde doğrudan ağır işlemler gerçekleştirmek yerine Linux Kernel'in workqueue mekanizmasının kullanılması ele alındı.

Cihaz yapısına;

struct work_struct timer_work

alanı eklendi.

Worker fonksiyonu;

INIT_WORK()

ile hazırlanırken, timer callback içerisinden;

schedule_work()

kullanılarak işin kernel worker thread'ine aktarılması sağlandı.

Bu yapı sayesinde timer callback yalnızca yapılacak işi planlayan kısa bir fonksiyon haline getirildi.

10. Timer ve Workqueue Yapılarının Birlikte Kullanılması

Timer ve workqueue birlikte kullanılarak aşağıdaki çalışma modeli oluşturuldu:

Timer süresi dolar → timer callback çalışır → schedule_work() çağrılır → worker thread çalışır → veri işlemi gerçekleştirilir → timer bir sonraki çalışması için tekrar planlanır.

Bu yapı sayesinde zamanlama ile gerçek iş birbirinden ayrılmış oldu.

Timer callback'in zaman açısından hassas ve kısa tutulması, FIFO üzerinde gerçekleştirilecek işlemlerin ise worker context içerisinde yapılması sağlandı.

11. Workqueue İçerisinden FIFO'ya Veri Eklenmesi

Worker fonksiyonu içerisinde;

container_of()

makrosu kullanılarak work_struct üzerinden ilgili ahmet_device yapısına ulaşıldı.

Ardından;

mutex_lock()
kfifo_avail()
kfifo_in()
mutex_unlock()

kullanılarak FIFO'ya güvenli biçimde;

"Timer Event\n"

mesajı eklendi.

Bu aşamada container_of() makrosunun bir yapı içerisindeki üyenin adresinden ana yapının adresini bulmak amacıyla kullanıldığı öğrenildi.

FIFO'da mesajın tamamı için yeterli alan bulunmadığı durumda parçalı mesaj eklenmemesi için kfifo_avail() ile kullanılabilir kapasite kontrol edildi.

12. Timer Tarafından Üretilen Verinin poll() ile Algılanması

Worker tarafından FIFO'ya yeni veri eklendikten sonra;

wake_up_interruptible()

ile read_queue üzerinde bekleyen prosesler uyandırıldı.

Ayrıca mevcut asenkron I/O altyapısıyla;

kill_fasync()

kullanılarak gerektiğinde SIGIO bildirimi gönderilebilecek yapı korundu.

Kullanıcı alanında hazırlanan poll() tabanlı test uygulaması ile timer tarafından otomatik olarak oluşturulan verinin kullanıcı alanından algılanabildiği doğrulandı.

Böylece daha önce ayrı ayrı öğrenilen;

timer, workqueue, FIFO, wait queue ve poll()

mekanizmaları tek bir veri akışı içerisinde birleştirildi.

13. Timer ve Workqueue Kaynaklarının Güvenli Şekilde Temizlenmesi

Kernel modülü kaldırılırken aktif timer veya worker'ın serbest bırakılmış belleğe erişmemesi gerektiği incelendi.

Bu nedenle cleanup işlemleri sırasında;

timer_shutdown_sync()
cancel_work_sync()

mekanizmaları kullanıldı.

Öncelikle timer'ın yeni iş oluşturması engellendi, ardından sırada bulunan veya çalışmakta olan worker işlemlerinin tamamlanması beklendi.

Daha sonra FIFO ve diğer cihaz kaynakları güvenli biçimde serbest bırakıldı.

Bu aşamada yanlış cleanup sırasının kernel oops veya kernel panic gibi ciddi problemlere yol açabileceği öğrenildi.

14. Kernel Modülü Yükleme ve Sysfs Çakışmalarının İncelenmesi

Geliştirme sırasında bazı başarısız modül yükleme ve kaldırma işlemlerinden sonra;

File exists

hatası ile karşılaşıldı.

dmesg çıktıları incelendiğinde sysfs içerisinde önce;

ahmet_class

daha sonra ise major/minor cihaz numarasıyla ilişkili;

/dev/char/510:0

kayıtlarında çakışmalar oluştuğu görüldü.

Bu çalışma sırasında;

class adı, cihaz adı, major numarası ve minor numarasının birbirinden farklı kavramlar olduğu öğrenildi.

FIFO sürümünün diğer sürücüden ayrılması amacıyla;

ahmet_fifo_class

class adı ve;

/dev/ahmet_fifo0
/dev/ahmet_fifo1
/dev/ahmet_fifo2
/dev/ahmet_fifo3

cihaz isimleri kullanıldı.

Sistem yeniden başlatıldıktan sonra eski kernel/sysfs kayıtları temizlendi ve modül başarıyla yüklendi.

15. Timer-FIFO Test Uygulamasının Hazırlanması

Timer tarafından oluşturulan olayların FIFO üzerinden doğru şekilde kullanıcı alanına ulaşıp ulaşmadığını doğrulamak amacıyla yeni bir test programı hazırlandı.

Test uygulamasında;

open()
poll()
read()

fonksiyonları kullanıldı.

Her timer olayı için poll() çağrısının beklemesi ve veri geldiğinde 12 byte uzunluğundaki;

Timer Event\n

mesajını okuması sağlandı.

İlk testlerde read() çağrısının 127 byte istemesi nedeniyle FIFO içerisinde birikmiş birden fazla timer mesajının tek seferde okunduğu ve mesajların okuma sınırında parçalanabildiği gözlemlendi.

Bu durum FIFO'nun mesaj tabanlı değil, byte akışı tabanlı çalıştığının anlaşılması açısından önemli bir test oldu.

Okuma boyutu mesaj uzunluğu olan 12 byte ile sınırlandırıldıktan sonra her timer olayının ayrı ayrı doğru şekilde okunabildiği doğrulandı.

Gün Sonu Değerlendirmesi

Dördüncü günün sonunda karakter sürücüsünün veri yönetim altyapısı düz buffer yapısından FIFO tabanlı bir akış mimarisine taşınmış ve blocking read/write mekanizmaları FIFO ile bütünleştirilmiştir. Linux Kernel timer mekanizması incelenerek periyodik olay üretimi gerçekleştirilmiş, atomic işlemler kullanılarak farklı execution context'lerde güvenli sayaç yönetiminin mantığı öğrenilmiştir.

Timer callback içerisinde ağır işlemler yapılmasının uygun olmaması nedeniyle workqueue mekanizması incelenmiş ve timer tarafından planlanan işlemlerin kernel worker thread içerisinde gerçekleştirilmesi sağlanmıştır. Worker tarafından FIFO'ya veri eklenmiş, wait queue üzerinden bekleyen prosesler uyandırılmış ve poll() aracılığıyla kullanıcı alanındaki uygulamanın yeni veriyi olay tabanlı olarak algılaması sağlanmıştır.

Ayrıca timer ve workqueue kaynaklarının modül kaldırılırken güvenli biçimde sonlandırılması, sysfs class yapısı, major/minor cihaz numaraları ve kernel modülü yükleme sırasında oluşabilecek kaynak çakışmaları uygulamalı olarak incelenmiştir.

Gün sonunda oluşturulan sistemde timer → callback → workqueue → FIFO → wait queue → poll → read zinciri başarıyla çalıştırılmıştır. Böylece karakter sürücüsü yalnızca kullanıcı tarafından veri yazılan pasif bir yapı olmaktan çıkarılarak kernel içerisinde kendi olaylarını üretebilen, bu olayları güvenli şekilde kuyruklayabilen ve kullanıcı alanına asenkron olarak aktarabilen daha gelişmiş bir sürücü mimarisine dönüştürülmüştür.