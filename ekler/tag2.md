STAJ RAPORU – 2. GÜN
Linux Character Device Driver Geliştirilmesi ve Kodun Profesyonel Hale Getirilmesi

İlk gün geliştirilen Linux Character Device Driver temel seviyede çalışır hale getirilmişti. Sürücü, open(), release(), read(), write() ve ioctl() fonksiyonlarını destekliyor; kullanıcı alanı (User Space) ile çekirdek alanı (Kernel Space) arasında veri alışverişi gerçekleştirilebiliyordu.

İkinci günün amacı çalışan sürücünün mimarisini geliştirmek, kod kalitesini artırmak ve Linux çekirdeğinde kullanılan sürücü geliştirme prensiplerine daha uygun hale getirmek oldu.

1. Karakter sürücüsü mimarisinin incelenmesi

Çalışmaya başlamadan önce Linux Character Driver yapısı tekrar incelendi.

Karakter sürücülerinin çalışma mantığında kullanıcı tarafından yapılan;

open()
read()
write()
ioctl()
close()

çağrılarının doğrudan kernel içerisindeki file_operations yapısındaki fonksiyonlara yönlendirildiği tekrar analiz edildi.

Bu yapı sayesinde kullanıcı alanındaki uygulamalar ile kernel arasında güvenli bir haberleşme gerçekleştirildiği görüldü.

Bu inceleme ilerleyen geliştirmelerin daha bilinçli yapılmasını sağladı.

2. Bellek yönetiminin incelenmesi

Driver içerisinde kullanılan buffer'ın başlangıçta sabit boyutlu olmasının ilerleyen aşamalarda yetersiz kalabileceği değerlendirildi.

Linux Kernel içerisinde kullanılan dinamik bellek yönetim fonksiyonları araştırıldı.

Özellikle;

kmalloc()
kzalloc()
krealloc()
kfree()

fonksiyonlarının görevleri incelendi.

kmalloc() fonksiyonunun yalnızca bellek ayırdığı,

kzalloc() fonksiyonunun ise ayrılan belleği otomatik olarak sıfırladığı öğrenildi.

Ayrıca çalışma sırasında buffer boyutunun artırılması gerektiğinde krealloc() fonksiyonunun kullanılmasının daha uygun olduğu görüldü.

3. Dinamik Buffer Yönetiminin Geliştirilmesi

İlk sürümde buffer sabit boyutlu olarak kullanılmasına rağmen ikinci gün buffer yönetimi geliştirildi.

Driver içerisine;

buffer
buffer_size
buffer_capacity

alanları eklendi.

Bu yapı sayesinde;

buffer'ın gerçek doluluk miktarı ile ayrılan toplam bellek miktarı birbirinden ayrılmış oldu.

Böylece;

gereksiz veri okunmasının,
kullanılmayan alanların kullanıcıya gönderilmesinin,
buffer taşmalarının

önüne geçilmiş oldu.

4. Dinamik Bellek Genişletme

Buffer dolduğu zaman belleğin yeniden ayrılması amacıyla Linux Kernel'in krealloc() fonksiyonu kullanıldı.

Yeni yazılan veri mevcut kapasiteden büyük olduğunda;

eski buffer korunmakta,
yeni boyutta bellek ayrılmakta,
mevcut veriler otomatik olarak yeni alana taşınmaktadır.

Bu sayede driver sabit boyutlu bir karakter sürücüsü olmaktan çıkarılarak dinamik bellek yönetebilen bir yapıya dönüştürüldü.

5. Mutex Kullanımı

Karakter sürücüsü aynı anda birden fazla kullanıcı tarafından erişilebileceği için veri bütünlüğünün korunması gerektiği değerlendirildi.

Bu amaçla Linux Kernel mutex mekanizması incelendi.

Driver içerisinde;

mutex_lock()

mutex_unlock()

fonksiyonları kullanılmaya başlandı.

Özellikle;

read()
write()
ioctl()

fonksiyonlarında ortak kullanılan buffer'a erişmeden önce mutex kilidi alınmakta, işlem tamamlandıktan sonra kilit serbest bırakılmaktadır.

Bu sayede aynı anda iki farklı prosesin buffer üzerinde değişiklik yapmasının önüne geçildi.

Bu durum Race Condition (yarış durumu) oluşmasını engellemektedir.

6. Kullanıcı Alanı ile Kernel Alanı Arasındaki Veri Aktarımı

Kernel içerisinde kullanıcı belleğine doğrudan erişilemediği tekrar incelendi.

Bu nedenle;

copy_to_user()

copy_from_user()

fonksiyonlarının çalışma mantığı ayrıntılı olarak araştırıldı.

Read işlemlerinde;

Kernel Buffer

↓

copy_to_user()

↓

User Space

akışı kullanılmaktadır.

Write işlemlerinde ise;

User Space

↓

copy_from_user()

↓

Kernel Buffer

şeklinde veri aktarımı gerçekleştirilmektedir.

Her iki fonksiyonun hata döndürme durumları da kontrol edilerek güvenli veri aktarımı sağlandı.

7. IOCTL Yapısının İncelenmesi

Standart read() ve write() fonksiyonlarının dışında sürücüye özel komut gönderebilmek amacıyla ioctl mekanizması tekrar incelendi.

Driver içerisinde;

Buffer temizleme
Buffer boyutunu öğrenme

işlemleri ioctl komutları olarak tanımlandı.

Bu amaçla;

_IO()

_IOR()

makrolarının kullanım mantığı araştırıldı.

Kernel tarafında switch-case yapısı kullanılarak gelen komutlar ayrıştırıldı.

8. Kodun Yeniden Düzenlenmesi

İlk sürümde driver bilgileri;

buffer
buffer_size
buffer_capacity
mutex

şeklinde birbirinden bağımsız global değişkenler olarak tutuluyordu.

İkinci gün bu yapı yeniden tasarlandı.

Driver'a ait bütün bilgiler;

struct ahmet_device

isimli yapı içerisine taşındı.

Bu yapı içerisinde;

buffer
buffer_size
buffer_capacity
mutex

tek bir nesne altında toplandı.

Böylece driver'ın bütün durumu tek noktadan yönetilebilir hale geldi.

Bu yaklaşım Linux Kernel içerisindeki gerçek driver mimarisinde yaygın olarak kullanılmaktadır.

9. Global Değişkenlerin Yapı Üzerinden Yönetilmesi

Daha önce kullanılan;

buffer

buffer_size

buffer_capacity

ahmet_mutex

ifadeleri yerine;

ahmet_dev->buffer

ahmet_dev->buffer_size

ahmet_dev->buffer_capacity

ahmet_dev->ahmet_mutex

kullanılmaya başlandı.

Bu değişiklik sayesinde driver kodu daha okunabilir hale geldi.

Ayrıca ilerleyen aşamalarda aynı sürücüden birden fazla cihaz oluşturulabilmesi için uygun altyapı hazırlanmış oldu.

10. kzalloc Kullanımı

Driver yapısının oluşturulmasında;

kzalloc()

kullanılmaya başlandı.

Bu fonksiyon hem bellek ayırmakta hem de ayrılan belleği otomatik olarak sıfırlamaktadır.

Bu sayede;

NULL pointer
sıfır uzunluk
başlangıç değerleri

otomatik olarak güvenli hale getirildi.

Bu yöntem Linux Kernel geliştirmede yaygın olarak tercih edilmektedir.

11. Belleğin Güvenli Şekilde Serbest Bırakılması

Driver kaldırılırken;

önce buffer,

daha sonra driver yapısı,

en son ise pointer NULL yapılacak şekilde cleanup fonksiyonu yeniden düzenlendi.

Böylece;

bellek sızıntıları (Memory Leak)
geçersiz pointer erişimleri (Dangling Pointer)
çift serbest bırakma (Double Free)

gibi problemlerin önüne geçildi.

12. Hata Yönetiminin İncelenmesi

Kernel modüllerinde hata oluştuğunda sistemin kararlı kalabilmesi için;

goto

etiketleri kullanılarak kaynakların doğru sırayla serbest bırakılması tekrar incelendi.

Her başarısız adım sonrasında;

ayrılan bellek
oluşturulan cdev
class
device

kaynakları ters sırayla temizlenmektedir.

Bu yaklaşım Linux Kernel Coding Style içerisinde önerilen yöntemlerden biridir.

Gün Sonu Değerlendirmesi

İkinci günün sonunda sürücü kullanıcı açısından aynı işlevleri sunmaya devam etmesine rağmen iç mimarisi önemli ölçüde geliştirilmiştir. Driver'a ait tüm bilgiler struct ahmet_device yapısı altında toplanmış, dinamik bellek yönetimi iyileştirilmiş, kzalloc() ve krealloc() kullanılarak daha güvenli bellek tahsisi sağlanmış, mutex mekanizması ile eşzamanlı erişimler güvence altına alınmış ve kaynak yönetimi Linux Kernel geliştirme prensiplerine uygun şekilde yeniden düzenlenmiştir. Yapılan bu değişiklikler sayesinde sürücü daha okunabilir, bakım yapılabilir, ölçeklenebilir ve gelecekte birden fazla karakter cihazını destekleyebilecek profesyonel bir yapıya kavuşturulmuştur.