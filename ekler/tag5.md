STAJ RAPORU – 5. GÜN
Kernel Thread, Execution Context, Spinlock, Bottom Half ve Çoklu Producer Yapılarının İncelenmesi

Dördüncü gün sonunda karakter sürücüsünün veri yönetimi FIFO tabanlı hale getirilmiş; kernel timer, workqueue, wait queue ve poll() mekanizmaları bir araya getirilerek kernel içerisinde periyodik olarak üretilen olayların kullanıcı alanına aktarılması sağlanmıştı. Özellikle timer callback içerisindeki işlemlerin workqueue'ya devredilmesiyle zamanlama ve gerçek veri işleme görevleri birbirinden ayrılmıştı.

Beşinci günün amacı ise kernel içerisinde bağımsız çalışan thread mekanizmasını öğrenmek, farklı execution context'lerin özelliklerini incelemek ve birden fazla kernel bileşeninin aynı FIFO üzerinde güvenli şekilde çalışmasını sağlamaktı.

1. Kernel Thread (kthread) Mekanizmasının İncelenmesi

İlk olarak Linux Kernel içerisinde bağımsız kernel thread'lerinin nasıl oluşturulduğu incelendi.

Bu kapsamda;

struct task_struct
kthread_run()
kthread_should_stop()
kthread_stop()

yapı ve fonksiyonlarının çalışma mantıkları ele alındı.

Her karakter cihazı için ayrı bir kernel thread oluşturularak thread'lerin cihazlardan bağımsız şekilde çalışabilmesi sağlandı.

Sistemde oluşturulan thread'ler;

ahmet_fifo_thread_0
ahmet_fifo_thread_1
ahmet_fifo_thread_2
ahmet_fifo_thread_3

şeklinde gözlemlendi.

ps üzerinden yapılan kontrollerde kernel thread'lerinin modül yüklendiği sürece aktif olduğu doğrulandı.

2. Kthread İçerisinde Periyodik Olay Üretilmesi

Oluşturulan kernel thread'lerin belirli aralıklarla çalışarak FIFO'ya veri üretmesi sağlandı.

Kthread tarafından;

Thread Event\n

mesajı oluşturuldu ve ilgili cihazın FIFO'suna eklendi.

Thread'in sürekli CPU kullanmasını önlemek amacıyla uyuma/zamanlama mekanizması kullanılarak işlemin belirli aralıklarla gerçekleştirilmesi sağlandı.

Böylece daha önce timer tarafından gerçekleştirilen periyodik olay üretiminin kernel thread kullanılarak da gerçekleştirilebileceği görüldü.

3. Kthread-FIFO Kullanıcı Alanı Testinin Hazırlanması

Kthread tarafından üretilen olayların kullanıcı alanına doğru şekilde ulaştığını doğrulamak amacıyla thread_fifo_test.c isimli test uygulaması hazırlandı.

Test programında;

open()
poll()
read()

fonksiyonları kullanıldı.

Uygulama /dev/ahmet_fifo0 cihazını açarak poll() içerisinde bekledi.

Kernel thread FIFO'ya;

Thread Event\n

mesajını eklediğinde wait queue üzerinden kullanıcı uygulaması uyandırıldı ve mesaj read() ile okundu.

Beş farklı olay başarıyla alınarak;

kthread → FIFO → wake_up → poll → read

zincirinin doğru çalıştığı doğrulandı.

4. Kernel Thread'lerin Güvenli Şekilde Durdurulması

Kernel modülü kaldırılırken oluşturulan thread'lerin çalışmaya devam etmemesi gerektiği incelendi.

Bu nedenle thread döngüsünde;

kthread_should_stop()

kontrolü gerçekleştirildi.

Cleanup sırasında ise;

kthread_stop()

kullanılarak thread'lerin sonlandırılması beklendi.

Modül kaldırıldıktan sonra yapılan kontrollerde kernel thread'lerinin sistemde kalmadığı doğrulandı.

Bu çalışma, asenkron çalışan kernel kaynaklarının ilgili veri yapıları serbest bırakılmadan önce mutlaka durdurulması gerektiğini tekrar gösterdi.

5. Execution Context Kavramının Derinleştirilmesi

Kthread, timer ve workqueue mekanizmalarının karşılaştırılması sırasında Linux Kernel içerisindeki execution context kavramı ayrıntılı olarak ele alındı.

Temel olarak;

Process Context
Interrupt Context

ayrımı incelendi.

Kthread ve workqueue'nun process context içerisinde çalıştığı ve gerektiğinde uyuyabildiği görüldü.

Timer callback'in ise softirq context içerisinde çalışması nedeniyle uyuyamayacağı öğrenildi.

Bu ayrımın kernel içerisinde kullanılabilecek synchronization ve memory allocation mekanizmalarını doğrudan etkilediği görüldü.

6. Top Half ve Bottom Half Kavramlarının İncelenmesi

Interrupt işlemlerinde yapılacak işlerin mümkün olduğunca kısa tutulması gerektiği incelendi.

Bu kapsamda;

Top Half
Bottom Half

kavramları ele alındı.

Top half'in olayın oluştuğu anda hızlı şekilde gerçekleştirilmesi gereken işlemleri, bottom half'in ise daha sonra gerçekleştirilebilecek işleri ifade ettiği öğrenildi.

Timer/softirq ve workqueue yapıları üzerinden deferred work mantığı değerlendirildi.

7. Mutex ve Spinlock Arasındaki Farkların İncelenmesi

Daha önce FIFO'yu korumak amacıyla kullanılan mutex mekanizması tekrar değerlendirildi.

Mutex'in kilit alınamadığında ilgili task'i uyutabileceği için yalnızca uyumanın mümkün olduğu context'lerde kullanılmasının uygun olduğu görüldü.

Softirq veya ileride kullanılacak hard IRQ gibi uyumanın yasak olduğu context'lerde ise mutex kullanılamayacağı öğrenildi.

Bu nedenle spinlock mekanizması incelendi.

Spinlock'un bekleyen execution context'i uyutmak yerine kilit serbest bırakılana kadar kısa süreli olarak beklettiği ve atomic/interrupt context'lerde kullanılabilecek synchronization mekanizmalarından biri olduğu öğrenildi.

8. spin_lock_bh() Mekanizmasının İncelenmesi

Aynı veriye process context ile bottom half/softirq tarafından erişilebilmesi durumunda yalnızca normal spinlock kullanımının her durumda yeterli olmayabileceği incelendi.

Bu amaçla;

spin_lock_bh()
spin_unlock_bh()

fonksiyonlarının çalışma mantığı ele alındı.

_bh kullanımının kilit alınırken mevcut CPU üzerindeki bottom half işlemlerini devre dışı bırakarak aynı CPU üzerinde softirq kaynaklı yeniden giriş problemlerinin önüne geçmeye yardımcı olduğu öğrenildi.

9. spin_lock_irqsave() Kavramının İncelenmesi

İleride hard IRQ işlemlerinde kullanılacak;

spin_lock_irqsave()
spin_unlock_irqrestore()

mekanizmalarının amacı incelendi.

Bu yapının spinlock alınırken local interrupt durumunu kaydettiği ve interrupt'ları geçici olarak devre dışı bıraktığı öğrenildi.

Her durumda irqsave kullanmanın gerekli olmadığı; synchronization yönteminin paylaşılan veriye hangi execution context'lerin eriştiğine göre seçilmesi gerektiği görüldü.

10. Çoklu Producer – Tek FIFO Mimarisinin Oluşturulması

Driver içerisinde birden fazla kernel bileşeninin aynı FIFO'ya veri üretmesi senaryosu oluşturuldu.

Bu yapıda;

Kthread ───────────┐
                   ├──→ FIFO → User Space
Timer → Workqueue ─┘

mimarisi kullanıldı.

Kthread;

Thread Event\n

mesajını üretirken timer tarafından planlanan workqueue;

Timer Event\n

mesajını aynı FIFO'ya ekledi.

Böylece FIFO'nun birden fazla producer tarafından ortak kullanıldığı daha gerçekçi bir veri akışı oluşturuldu.

11. Çoklu Producer Yapısında Senkronizasyon

Kthread ve workqueue'nun aynı anda FIFO üzerinde değişiklik yapabilme ihtimali nedeniyle ortak veri yapısının synchronization mekanizmasıyla korunması gerektiği incelendi.

Critical section korunarak iki producer'ın FIFO iç durumunu eşzamanlı olarak değiştirmesi engellendi.

Bu çalışma üzerinden race condition kavramının pratikte neden önemli olduğu tekrar görüldü.

Özellikle synchronization olmaması durumunda birden fazla execution context'in aynı FIFO veya sayaç üzerinde eşzamanlı değişiklik yapmasının veri tutarsızlığı oluşturabileceği değerlendirildi.

12. Producer–Consumer Mantığının İncelenmesi

Oluşturulan sistem producer-consumer modeli açısından değerlendirildi.

Kernel tarafında;

kthread
timer/workqueue

producer görevini üstlenirken kullanıcı alanında read() gerçekleştiren uygulama consumer görevini üstlendi.

FIFO'nun producer ile consumer arasındaki geçici veri alanı olduğu görüldü.

Producer'ın consumer'dan daha hızlı çalışması durumunda FIFO içerisinde verilerin birikeceği ve sonunda FIFO'nun tamamen dolabileceği incelendi.

13. FIFO Doluluk Durumu ve Veri Kaybı Politikası

Kthread tarafından FIFO'ya mesaj eklenmeden önce kullanılabilir alan;

kfifo_avail()

ile kontrol edildi.

FIFO içerisinde mesajın tamamını saklayacak alan bulunması durumunda veri eklendi.

Yeterli alan bulunmadığında ise parçalı mesaj eklemek yerine olayın düşürülmesi tercih edildi.

Bu amaçla üretilen, okunan ve düşürülen olayları takip eden istatistik sayaçları kullanıldı.

Test sırasında consumer çalıştırılmadan kernel thread'lerinin veri üretmesine izin verilerek FIFO'nun davranışı gözlemlendi.

14. Driver İstatistiklerinin İncelenmesi

Modül kaldırılırken her cihaz için istatistik bilgilerinin kernel log'una yazdırılması sağlandı.

Bu kapsamda;

produced
read
dropped
timer

gibi değerler takip edildi.

Yapılan testlerde her cihazın bağımsız şekilde olay ürettiği ve modül kaldırılırken thread'lerin güvenli şekilde sonlandırıldığı dmesg çıktıları üzerinden doğrulandı.

15. Backpressure Kavramının İncelenmesi

Producer'ın consumer'dan daha hızlı çalışması durumunda uygulanabilecek yöntemler incelendi.

User-space write() tarafında FIFO dolu olduğunda prosesin write_queue üzerinde bekletilmesi gerçek bir backpressure mekanizması olarak değerlendirildi.

Kernel içerisinde sürekli olay üreten yapılarda ise;

üreticiyi bekletme
olayı düşürme
buffer kapasitesini artırma
işi daha sonra tekrar planlama

gibi farklı politikaların uygulanabileceği görüldü.

Her yöntemin sistemin gereksinimlerine göre avantaj ve dezavantajlarının bulunduğu öğrenildi.

Gün Sonu Değerlendirmesi

Beşinci günün sonunda Linux Kernel içerisinde bağımsız kernel thread oluşturma, çalıştırma ve güvenli şekilde sonlandırma mekanizmaları uygulamalı olarak incelenmiştir. Kernel thread tarafından periyodik olarak oluşturulan olaylar FIFO'ya aktarılmış ve kullanıcı alanındaki poll() tabanlı test uygulamasıyla başarıyla okunmuştur.

Timer, workqueue ve kthread mekanizmalarının çalışma context'leri karşılaştırılarak process context, interrupt context, softirq ve bottom half kavramları üzerinde durulmuştur. Uyuyabilen ve uyuyamayan context'ler arasındaki farkın synchronization mekanizması seçimine etkisi incelenmiş; mutex, spinlock, spin_lock_bh() ve spin_lock_irqsave() mekanizmalarının kullanım amaçları değerlendirilmiştir.

Ayrıca kthread ile timer/workqueue aynı FIFO üzerinde producer olarak çalıştırılarak çoklu producer–tek consumer mimarisi oluşturulmuştur. FIFO doluluk durumu, veri düşürme politikası, olay sayaçları, producer-consumer ve backpressure kavramları uygulamalı olarak incelenmiştir.

Böylece dördüncü gün oluşturulan:

timer → workqueue → FIFO → poll → read

mimarisi beşinci günde:

Kthread ───────────────┐
                       ├→ synchronized FIFO
Timer → Workqueue ─────┘
                              ↓
                         wait queue
                              ↓
                            poll()
                              ↓
                            read()

şeklinde birden fazla kernel execution context'inin aynı veri akışı üzerinde güvenli biçimde çalıştığı daha gelişmiş bir mimariye dönüştürülmüştür.

Not: Bugün konuştuğumuz malloc/calloc/kmalloc/kcalloc kısmını rapora yeni geliştirme olarak koymadım; o bölüm önceki günlerde yaptığımız bellek yönetiminin kısa tekrarıydı. Binary semaphore/race condition teorisini de henüz uygulamalı olarak işlemediğimiz için 5. günün yapılmış işi gibi yazmadım. Bu şekilde rapor gerçekten bugün yaptıklarımızı yansıtıyor.


