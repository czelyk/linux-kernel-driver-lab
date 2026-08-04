# ahmet — Linux Character Device Driver

Gömülü sistem stajı kapsamında sıfırdan geliştirilen basit bir Linux karakter
aygıt sürücüsü (character device driver). Bu belge, projenin şu ana kadarki
durumunu ve arkasındaki mantığı özetler.

## Ne yapıyor?

```
echo "Merhaba Kernel" > /dev/ahmet    # user space -> kernel'e veri yazar
cat /dev/ahmet                        # kernel'den veriyi geri okur
```

`ahmet.ko` yüklendiğinde `/dev/ahmet` cihazı otomatik oluşur (elle `mknod`
yapmaya gerek yoktur). Kernel tarafında 1024 byte'lık bir tampon (buffer)
tutulur; `write()` bu tamponu doldurur, `read()` içinden okur.

## Şu ana kadar tamamlananlar

- [x] Dinamik major/minor numarası alma (`alloc_chrdev_region`)
- [x] `cdev` ile karakter aygıtın kernel'e tanıtılması
- [x] `open` / `release` / `read` / `write` operasyonları
- [x] `copy_to_user` / `copy_from_user` ile güvenli user↔kernel veri transferi
- [x] Linux Device Model (`class_create` + `device_create`) ile `/dev/ahmet`
      dosyasının **otomatik** oluşturulması (udev üzerinden)
- [x] Hata yollarında `IS_ERR`/`PTR_ERR` kontrolü ve temiz geri alma (rollback)
- [x] `module_exit` içinde tüm kaynakların sırayla serbest bırakılması

Sırada (henüz yapılmadı):

- [ ] `ioctl()` desteği
- [ ] Eşzamanlılık koruması (mutex/spinlock) — şu an iki process aynı anda
      yazarsa/okursa `buffer` üzerinde yarış durumu (race condition) oluşur
- [ ] `write()` içinde `*offset` parametresinin dikkate alınması (şu an her
      `write` çağrısı tamponun tamamını baştan değiştiriyor, önceki `read`
      pozisyonunu değil)
- [ ] wait queue / blocking I/O
- [ ] sysfs arayüzü

Daha ayrıntılı, adım adım teknik anlatım için: [`04082026r1`](./04082026r1)
(mentor ile yapılan konuşmanın günlük notları).

## Mimari: neden bu kadar çok fonksiyon var?

```
User Application  (örn: echo, cat)
        |
        v
/dev/ahmet   <-- sadece bir "dosya" gibi görünür, ama aslında kernel'e
                 giden bir kapı
        |
        v
ahmet.ko (bizim kodumuz)
        |
        v
kernel içindeki 1024 byte'lık static buffer
```

Kullanıcı programları (`cat`, `echo`, senin kendi C kodun...) donanıma veya
kernel belleğine **doğrudan** erişemez — bu, güvenlik ve kararlılık için
bilinçli bir sınırdır. Bunun yerine, kernel bir "dosya" arayüzü sunar:
`open`, `read`, `write`, `close` gibi standart sistem çağrılarını kullanırsın
ve kernel bunları senin `struct file_operations`'ında tanımladığın
fonksiyonlara yönlendirir. `ahmet.c` bu köprüyü kuruyor.

## Kod nasıl çalışıyor? (`ahmet.c`)

### 1. Cihaz numarası alma — `ahmet_init()`

```c
alloc_chrdev_region(&dev_num, 0, 1, "ahmet");
```

Her karakter aygıtın bir **major** (hangi driver) ve **minor** (o driver
altındaki hangi cihaz) numarası vardır. Bunu elle sabit bir sayı olarak
seçmek yerine (`register_chrdev_region` ile olurdu) kernel'den dinamik
olarak istiyoruz — bu, çakışma riskini ortadan kaldırdığı için tercih edilen
yöntem (mentörün de istediği buydu).

### 2. cdev'i bağlama

```c
cdev_init(&ahmet_cdev, &fops);
cdev_add(&ahmet_cdev, dev_num, 1);
```

`cdev_init` bizim `fops` (file operations) tablomuzu `ahmet_cdev` yapısına
bağlar. `cdev_add` ise bu yapıyı, aldığımız `dev_num` ile birlikte kernel'e
resmen kaydeder — bu satırdan sonra kernel "bu numaraya gelen istekleri şu
fonksiyonlara yönlendir" demiş olur.

### 3. `/dev/ahmet` dosyasının otomatik oluşması

```c
ahmet_class  = class_create("ahmet_class");
ahmet_device = device_create(ahmet_class, NULL, dev_num, NULL, "ahmet");
```

`class_create` sistemde bir cihaz sınıfı tanımlar (`/sys/class/ahmet_class`
altında görünür). `device_create` bu sınıf altında somut bir cihaz kaydı
oluşturur; bunu gören **udev** servisi otomatik olarak `/dev/ahmet` dosyasını
yaratır. Bu adım olmasaydı, her `insmod`'dan sonra elle
`mknod /dev/ahmet c <major> <minor>` çalıştırman gerekirdi — bu eski ve
önerilmeyen bir yöntem.

Her adımdan sonra `IS_ERR(...)` ile hata kontrolü yapılıyor ve bir önceki
adımlar geri alınıyor (rollback). Bu, kernel kodunda çok önemli bir alışkanlık:
kernel'de exception yoktur, her hata elle temizlenmelidir.

### 4. `open` / `release`

```c
static int ahmet_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened\n");
    return 0;
}
```

`cat /dev/ahmet` veya `echo ... > /dev/ahmet` çalıştığında dosya önce
"açılır" — kernel bunu görünce `ahmet_open`'ı çağırır. Şu an burada özel bir
iş yapılmıyor, sadece `dmesg`'e log basılıyor (`sudo dmesg` ile görülebilir).
`release`, dosya kapanınca (`close()`) çağrılır, aynı şekilde sadece log
basıyor.

### 5. `write` — user space'ten kernel'e veri

```c
static ssize_t ahmet_write(struct file *file, const char __user *buf,
                            size_t len, loff_t *offset)
{
    if (len > BUFFER_SIZE)
        len = BUFFER_SIZE;

    if (copy_from_user(buffer, buf, len))
        return -EFAULT;

    buffer_size = len;
    return len;
}
```

`buf` işaretçisi **user space** belleğini gösterir — kernel bu adrese
doğrudan `memcpy` ile erişemez, çünkü bu adres user space'in sanal bellek
haritasına ait ve güvenlik/geçerlilik garantisi yoktur. `copy_from_user`
bu kopyalamayı güvenli şekilde yapar (adresin gerçekten geçerli ve
erişilebilir olduğunu kontrol eder). Başarısız olursa `-EFAULT` (kötü adres
hatası) döner.

> Not: Bu fonksiyon `*offset`'i hiç kullanmıyor — yani her `write` çağrısı
> tamponun tamamını en baştan değiştiriyor. `echo "a" > /dev/ahmet` sonra
> `echo "b" >> /dev/ahmet` yaparsan ikinci yazma birinciyi tamamen ez er.
> Bu, "sırada ne var" listesindeki maddelerden biri.

### 6. `read` — kernel'den user space'e veri

```c
static ssize_t ahmet_read(struct file *file, char __user *buf,
                           size_t len, loff_t *offset)
{
    if (*offset >= buffer_size)
        return 0;

    bytes_to_read = min(len, buffer_size - *offset);
    copy_to_user(buf, buffer + *offset, bytes_to_read);
    *offset += bytes_to_read;
    return bytes_to_read;
}
```

Burada `*offset` doğru kullanılıyor: `cat` gibi araçlar `read`'i veri
bitene kadar tekrar tekrar çağırır; her çağrıda `offset` bir önceki
kaldığı yerden devam eder. `*offset >= buffer_size` olduğunda `0` dönmek,
"dosyanın sonuna geldik" (EOF) anlamına gelir — `cat` bunu görünce durur.
`min(len, buffer_size - *offset)` ile de kullanıcının istediğinden fazla
veri göndermemiş oluyoruz (buffer taşmasını önlüyor).

### 7. Temizlik — `ahmet_exit()`

```c
device_destroy(ahmet_class, dev_num);
class_destroy(ahmet_class);
cdev_del(&ahmet_cdev);
unregister_chrdev_region(dev_num, 1);
```

`sudo rmmod ahmet` çalıştığında bu sıra ile her şey **oluşturulma sırasının
tersinde** temizleniyor. Bu ters sıra kuralı kernel programlamada genel bir
prensiptir (init'te A→B→C yapıldıysa, exit'te C→B→A ile geri alınır).

## Kavramlar sözlüğü (staja yeni başlayan biri için)

| Terim | Ne demek |
|---|---|
| **User space / Kernel space** | Kullanıcı programlarının çalıştığı korumalı alan / işletim çekirdeğinin çalıştığı, donanıma tam erişimi olan alan. Aralarında serbest geçiş yok, `copy_to/from_user` gibi kontrollü kapılar var. |
| **Character device** | Veriyi akış/karakter olarak işleyen cihaz türü (klavye, seri port, bizim `ahmet`). Blok cihazın (disk) tersine, rastgele erişim/blok yapısı yok. |
| **Kernel module (.ko)** | Kernel'i yeniden derlemeden, çalışırken (`insmod`) yüklenebilen kod parçası. `.ko` = "Kernel Object". |
| **Major / Minor number** | Major: hangi driver sorumlu. Minor: o driver altındaki hangi cihaz. İkisi birlikte `dev_t` tipini oluşturur. |
| **`struct cdev`** | Kernel içinde bir karakter aygıtı temsil eden yapı; `file_operations`'ı bir `dev_t` numarasına bağlar. |
| **`struct file_operations`** | Hangi sistem çağrısının (`open`, `read`, `write`, `release`...) hangi fonksiyona gideceğini tanımlayan tablo. `cat`/`echo` aslında bu tabloyu tetikliyor. |
| **`copy_to_user` / `copy_from_user`** | Kernel ile user space arasında güvenli, doğrulanmış veri kopyalama. Doğrudan pointer erişimi yerine bunlar kullanılmalı. |
| **Linux Device Model (`class_create`/`device_create`)** | `/dev` altındaki dosyanın udev tarafından otomatik oluşturulmasını sağlayan modern yaklaşım; elle `mknod` yapmanın yerini alır. |
| **`IS_ERR` / `PTR_ERR`** | Kernel'de fonksiyonlar hata durumunda `NULL` yerine genelde geçersiz bir pointer (hata kodunu içine gömülü) döner; `IS_ERR` bunu tespit eder, `PTR_ERR` içindeki hata kodunu çıkarır. |

## Build & test

```sh
make                    # ahmet.ko üretir
sudo insmod ahmet.ko    # modülü yükle
dmesg | tail            # major/minor numarasını ve logları gör
ls -l /dev/ahmet        # cihaz dosyasının otomatik oluştuğunu doğrula
echo "Merhaba Kernel" > /dev/ahmet
cat /dev/ahmet
sudo rmmod ahmet        # modülü kaldır
```

## Kullanılan araçlar

- **GCC / Make** — kernel modülünü derlemek için (`obj-m += ahmet.o`, kernel
  build sistemi `/lib/modules/$(uname -r)/build` üzerinden çağrılıyor)
- **VS Code** + kernel header include path'leri (`/usr/src/linux-headers-...`)
- **Bear** — `compile_commands.json` üretip VS Code IntelliSense'in kernel
  build sistemini anlamasını sağlamak için
