STAJ RAPORU – 3. GÜN
Linux Character Device Driver'ın Geliştirilmesi, Çoklu Cihaz Desteği, Bekleme Mekanizmaları ve Gelişmiş Dosya İşlemleri

İkinci gün sonunda karakter sürücüsünün temel mimarisi profesyonel hale getirilmiş, dinamik bellek yönetimi ve mutex mekanizması eklenmişti. Üçüncü günün amacı sürücüyü gerçek Linux karakter sürücülerine daha fazla yaklaştırmak, birden fazla cihazı destekleyen, bekleme mekanizmalarına sahip ve gelişmiş dosya işlemlerini destekleyen bir yapıya dönüştürmek oldu.

1. Çoklu Karakter Cihazı Desteğinin Eklenmesi

İlk olarak sürücünün yalnızca tek bir karakter cihazı yerine birden fazla cihaz oluşturabilecek şekilde yeniden tasarlanması amaçlandı.

Bu amaçla;

DEVICE_COUNT
alloc_chrdev_region()
cdev
device_create()

fonksiyonlarının çoklu cihaz oluşturma mantığı incelendi.

Her cihaz için;

ayrı buffer,
ayrı mutex,
ayrı buffer_size,
ayrı buffer_capacity

oluşturularak her cihazın birbirinden tamamen bağımsız çalışması sağlandı.

Bunun sonucunda;

/dev/ahmet0
/dev/ahmet1
/dev/ahmet2
/dev/ahmet3

isimli dört farklı karakter cihazı oluşturuldu.

Yapılan testlerde her cihazın kendi verisini sakladığı ve cihazlar arasında veri paylaşımı olmadığı doğrulandı.

2. Device Yapısının Genişletilmesi

İkinci gün oluşturulan struct ahmet_device yapısı yeniden genişletildi.

Yapıya;

wait_queue_head_t read_queue
struct fasync_struct *async_queue

alanları eklendi.

Böylece her cihaz kendi bekleme kuyruğunu ve kendi asenkron bildirim listesini yönetebilir hale getirildi.

3. Wait Queue Mekanizmasının İncelenmesi

Linux Kernel içerisinde süreçlerin CPU'yu sürekli meşgul etmeden veri bekleyebilmesi amacıyla kullanılan Wait Queue mekanizması araştırıldı.

Driver içerisinde;

init_waitqueue_head()

ile bekleme kuyruğu oluşturuldu.

Read işlemi sırasında veri bulunmadığında;

wait_event_interruptible()

fonksiyonu kullanılarak proses uyku durumuna geçirildi.

Yeni veri yazıldığında ise;

wake_up_interruptible()

çağrısı ile bekleyen proses tekrar çalıştırıldı.

Bu sayede sürekli döngü içerisinde veri kontrolü yapan (Busy Waiting) yapı yerine Linux Kernel'in önerdiği bekleme mekanizması kullanılmış oldu.

4. Non-Blocking I/O Desteği

Karakter sürücüsünün bloklayıcı ve bloklamayan çalışma davranışları incelendi.

Dosya;

O_NONBLOCK

bayrağı ile açıldığında veri bulunmuyorsa süreç bekletilmeden;

-EAGAIN

hata kodu döndürülmektedir.

Bu yapı özellikle gerçek zamanlı uygulamalar ve olay tabanlı programlama açısından önem taşımaktadır.

5. poll() Fonksiyonunun Eklenmesi

Sürücüye;

poll()

desteği eklendi.

Bu amaçla;

poll_wait()

ve

EPOLLIN
EPOLLRDNORM

bayraklarının çalışma mantığı incelendi.

Buffer içerisinde okunabilecek veri bulunduğunda uygun olay bayrakları döndürülerek kullanıcı alanındaki poll() çağrısının veri hazır bilgisini alması sağlandı.

Hazırlanan kullanıcı alanı test programı ile poll() fonksiyonunun doğru çalıştığı doğrulandı.

6. Asenkron Bildirim (SIGIO) Mekanizmasının Eklenmesi

Linux Kernel içerisinde kullanılan asenkron I/O mekanizması araştırıldı.

Driver içerisine;

fasync()

desteği eklendi.

Yeni veri yazıldığında;

kill_fasync()

fonksiyonu çağrılarak ilgili sürece;

SIGIO

sinyali gönderilmektedir.

Kullanıcı alanında geliştirilen test uygulaması;

signal()

fcntl()

O_ASYNC

kullanarak bu sinyali başarıyla aldı.

Bu sayede kullanıcı uygulaması sürekli veri kontrolü yapmak yerine yalnızca veri geldiğinde bilgilendirilen olay tabanlı bir yapıya kavuştu.

7. mmap() Desteğinin Eklenmesi

Kernel ile kullanıcı alanı arasında veri paylaşımının daha verimli yapılabilmesi amacıyla bellek eşleme (Memory Mapping) mekanizması incelendi.

Bu kapsamda;

vmalloc_user()

ile kullanıcı alanına eşlenebilir bellek tahsis edildi.

Driver içerisinde;

remap_vmalloc_range()

kullanılarak buffer'ın kullanıcı alanına eşlenmesi sağlandı.

Kullanıcı alanında hazırlanan test programı ile mmap() çağrıları test edildi ve sürücünün bellek eşleme altyapısı oluşturuldu.

8. vmalloc Kullanımının İncelenmesi

Dinamik bellek yönetimi tekrar ele alınarak;

kmalloc()
kzalloc()
vmalloc()
vmalloc_user()
kfree()
vfree()

fonksiyonlarının kullanım alanları incelendi.

Özellikle büyük bellek bloklarının fiziksel olarak ardışık olmasının gerekmediği durumlarda vmalloc() kullanımının daha uygun olduğu öğrenildi.

Belleğin serbest bırakılması sırasında ise;

vfree()

fonksiyonunun kullanılması gerektiği görüldü.

9. llseek() Fonksiyonunun Eklenmesi

Driver içerisine;

llseek()

fonksiyonu eklendi.

Bu sayede kullanıcı alanındaki;

lseek()

çağrıları kernel tarafında karşılanabilir hale geldi.

Fonksiyon içerisinde;

SEEK_SET
SEEK_CUR
SEEK_END

işlemleri desteklendi.

Dosya konumunu temsil eden;

file->f_pos

değeri güncellenerek okuma ve yazma işlemlerinin istenilen konumdan başlatılması sağlandı.

10. Offset Destekli Read ve Write İşlemleri

Read ve Write fonksiyonları dosya konumunu dikkate alacak şekilde yeniden düzenlendi.

Write işlemi sırasında;

copy_from_user(dev->buffer + *offset, ...)

kullanılarak yalnızca buffer başlangıcına değil, istenilen konuma yazma yapılması sağlandı.

Yazma sonrasında;

dosya konumu (offset)
gerçek veri uzunluğu (buffer_size)

güncellendi.

Hazırlanan test programları ile farklı konumlardan okuma ve yazma işlemlerinin doğru çalıştığı doğrulandı.

11. O_APPEND Desteğinin Eklenmesi

Driver içerisinde;

O_APPEND

bayrağı kontrol edilmeye başlandı.

Dosya append modunda açıldığında;

file->f_flags

incelenerek yazma işlemi başlamadan önce dosya konumu mevcut verinin sonuna taşındı.

Böylece her yeni yazma işlemi mevcut verinin üzerine yazmak yerine sonuna eklenebilecek hale getirildi.

12. Kullanıcı Alanı Test Programlarının Hazırlanması

Driver'ın doğruluğunu test edebilmek amacıyla çeşitli kullanıcı alanı uygulamaları geliştirildi.

Hazırlanan test programları ile;

read()
ioctl()
poll()
mmap()
SIGIO
llseek()
offset destekli write()

işlemleri ayrı ayrı test edildi.

Ayrıca proje düzeni yeniden organize edilerek;

bin/
tests/

klasörleri oluşturuldu ve derlenen uygulamalar ile kaynak kodlar birbirinden ayrıldı.

Bu düzenleme proje yapısını daha okunabilir ve yönetilebilir hale getirdi.

Gün Sonu Değerlendirmesi

Üçüncü günün sonunda karakter sürücüsü önemli ölçüde geliştirilmiştir. Sürücü, birden fazla karakter cihazını bağımsız olarak yönetebilen, bekleme kuyrukları sayesinde bloklayıcı ve bloklamayan giriş/çıkış işlemlerini destekleyen, poll() ve SIGIO mekanizmaları ile olay tabanlı çalışabilen, mmap() aracılığıyla kullanıcı alanı ile bellek paylaşımı yapabilen ve llseek() desteği sayesinde dosya konumunu yönetebilen daha gelişmiş bir yapıya dönüştürülmüştür. Ayrıca O_APPEND desteği ile yazma işlemleri gerçek dosya davranışına yaklaştırılmış, hazırlanan kapsamlı kullanıcı alanı test uygulamaları ile gerçekleştirilen tüm geliştirmeler doğrulanmıştır. Yapılan bu çalışmalar sonucunda geliştirilen karakter sürücüsü, Linux Kernel sürücü geliştirme prensiplerine daha uygun, daha modüler, daha ölçeklenebilir ve gerçek sistemlerde kullanılan karakter sürücülerine oldukça yakın bir mimariye ulaştırılmıştır.