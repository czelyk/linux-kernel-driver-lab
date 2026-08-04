03.08.2026/1

Bence doğru karar. alloc_chrdev_region() + cdev yöntemi bugün Linux kernel'de karakter sürücüsü yazarken öğrenmen gereken yaklaşımdır. Mentorun özellikle eski yöntemi istemediyse bunu kullanmanı öneririm.

Bunu da bir eğitim gibi ilerletelim. Kodu tek seferde kopyalamak yerine her adımın mantığını anlayalım.

Öğreneceğimiz yapı

Sürücünün iskeleti yaklaşık olarak şöyle olacak:

ahmet.c
│
├── module_init()
│     ├── alloc_chrdev_region()
│     ├── cdev_init()
│     ├── cdev_add()
│     ├── class_create()
│     └── device_create()   ---> /dev/ahmet
│
├── open()
├── read()
├── write()
├── release()
│
└── module_exit()
      ├── device_destroy()
      ├── class_destroy()
      ├── cdev_del()
      └── unregister_chrdev_region()

Yani amaç sadece /dev/ahmet oluşturmak değil, Linux Device Model'i de öğrenmek.

Kullanacağımız temel kavramlar

Bunlar kernel geliştirmede sürekli karşına çıkacak. İsimlerini şimdiden öğrenmeni tavsiye ederim:

Character Device Driver (Karakter aygıt sürücüsü)
Major Number (Ana aygıt numarası)
Minor Number (Alt aygıt numarası)
Device Number (dev_t) (aygıt numarası tipi)
struct cdev (karakter aygıt yapısı)
struct file_operations (dosya işlemleri tablosu)
Kernel Module (çekirdek modülü)
User Space (kullanıcı alanı)
Kernel Space (çekirdek alanı)
İlk hedefimiz

İlk sürümde sadece şunları yapacağız:

Modül yüklenecek.
/dev/ahmet oluşacak.
open() çağrılacak.
release() çağrılacak.
Henüz read() ve write() gerçek veri işlemi yapmayacak.

Yani önce iskeleti kuracağız. Sonra adım adım write() ile yazma, read() ile okuma ve kullanıcı belleğiyle veri alışverişi (copy_from_user, copy_to_user) ekleyeceğiz.

Bu şekilde ilerlersen staj sonunda sadece çalışan bir driver'ın değil, neden çalıştığını bildiğin bir driver'ın olur.


04.08.2026/1

Bugün Linux kernel üzerinde bir character device driver geliştirmeye devam ettim. İlk olarak sürücünün temel yapısını gözden geçirerek open, release, read ve write fonksiyonlarını tamamladım. Kullanıcı alanı ile kernel alanı arasında veri aktarımı için copy_from_user() ve copy_to_user() fonksiyonlarını kullanarak 1024 byte boyutunda bir kernel tamponu (buffer) oluşturdum. read fonksiyonunda dosya ofseti (offset) ve okunacak veri miktarını (bytes_to_read) dikkate alarak güvenli veri okuma işlemini gerçekleştirdim.

Daha sonra sürücüyü derleyerek (make) oluşan modülü (ahmet.ko) sisteme yükledim (insmod) ve dmesg çıktıları üzerinden modülün doğru şekilde yüklendiğini doğruladım. Sürücü için ayrılan major ve minor numaralarını inceleyerek cihaz dosyasının oluşturulmasını test ettim. echo komutu ile sürücüye veri yazıp cat komutu ile aynı veriyi okuyarak read ve write fonksiyonlarının beklendiği gibi çalıştığını doğruladım.

Sonraki aşamada kullanıcı tarafından manuel olarak oluşturulan /dev/ahmet cihaz dosyasını otomatik oluşturacak yapıyı ekledim. Bunun için class_create() fonksiyonu ile bir device class oluşturdum ve device_create() fonksiyonu ile ilgili cihazı sisteme kaydettim. Ayrıca hata durumları için IS_ERR() ve PTR_ERR() kullanarak gerekli kontrolleri ekledim. Bu sayede sürücü, yüklendiğinde cihaz düğümünü otomatik oluşturabilecek altyapıya kavuşmuş oldu.

Çalışma sırasında Linux kernel API'sini daha etkin kullanabilmek amacıyla kernel header dosyalarını ve fonksiyon prototiplerini inceleme yöntemlerini araştırdım. device_create() gibi fonksiyonların parametrelerinin nasıl belirlendiğini ve bu bilgilere kernel kaynak kodu ile resmi dokümantasyon üzerinden nasıl ulaşılabileceğini öğrendim. Gün sonunda temel kullanıcı-kernel veri aktarımını gerçekleştirebilen ve cihaz oluşturma mekanizmasını destekleyen işlevsel bir character device driver elde edilmiş oldu.