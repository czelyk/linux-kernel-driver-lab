#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define BUFFER_SIZE 1024

static dev_t dev_num;
static struct cdev ahmet_cdev;
static struct class *ahmet_class;
static struct device *ahmet_device;

static char buffer[BUFFER_SIZE];
static size_t buffer_size = 0;

static int ahmet_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened\n");
    return 0;
}

static int ahmet_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}

static ssize_t ahmet_read(struct file *file,
                          char __user *buf,
                          size_t len,
                          loff_t *offset)
{
    size_t bytes_to_read;

    if (*offset >= buffer_size)
        return 0;

    bytes_to_read = min(len, buffer_size - *offset);

    if (copy_to_user(buf,
                     buffer + *offset,
                     bytes_to_read))
    {
        return -EFAULT;
    }

    *offset += bytes_to_read;

    pr_info("Device read: %zu bytes\n", bytes_to_read);

    return bytes_to_read;
}

static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
    if (len > BUFFER_SIZE)
        len = BUFFER_SIZE;

    if (copy_from_user(buffer, buf, len))
    {
        return -EFAULT;
    }

    buffer_size = len;

    pr_info("Device write: %zu bytes\n", len);

    return len;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = ahmet_open,
    .release = ahmet_release,
    .read = ahmet_read,
    .write = ahmet_write,
};

static int __init ahmet_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, "ahmet");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        return ret;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));

    cdev_init(&ahmet_cdev, &fops);

    ret = cdev_add(&ahmet_cdev, dev_num, 1);
    if (ret < 0)
    {
        pr_err("Failed to add cdev\n");
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    ahmet_class = class_create("ahmet_class");
    if (IS_ERR(ahmet_class))
    {
        cdev_del(&ahmet_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(ahmet_class);
    }

    ahmet_device = device_create(
        ahmet_class,
        NULL,
        dev_num,
        NULL,
        "ahmet"
    );

    if(IS_ERR(ahmet_device))
    {
        class_destroy(ahmet_class);
        cdev_del(&ahmet_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(ahmet_device);
    }

    pr_info("Ahmet character device added\n");

    return 0;
}

static void __exit ahmet_exit(void)
{
    device_destroy(ahmet_class, dev_num);
    class_destroy(ahmet_class);
    
    cdev_del(&ahmet_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("Ahmet module unloaded!\n");
}

module_init(ahmet_init);
module_exit(ahmet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmet Hasan Celik");
MODULE_DESCRIPTION("My first Linux character driver");


Bu kodu satır satır değil, Linux kernel mimarisi içinde bir cihaz sürücüsü nasıl doğar ve kullanıcıya nasıl sunulur mantığıyla anlatalım. Çünkü gerçek hayatta driver yazarken ezberlenen fonksiyonlardan çok, hangi katmanda neyi temsil ettiğini anlaman gerekiyor.

Senin yazdığın şey aslında küçük bir Character Device Driver (Karakter Cihaz Sürücüsü). Bu driver'ın görevi:

Kernel ile kullanıcı programı arasında bir köprü oluşturmak.

Mimari olarak şöyle düşün:

                 USER SPACE
────────────────────────────────

       Kullanıcı programı

        echo "test" > /dev/ahmet
                 |
                 |
              system call
              (write)
                 |
────────────────────────────────
                 |
                 ↓

              KERNEL SPACE

        VFS (Virtual File System)
                 |
                 |
        file_operations tablosu
                 |
                 |
        Senin driver'ın
        ahmet_write()
        ahmet_read()

                 |
                 |
          Donanım / Buffer / RAM

────────────────────────────────

Sen şu anda gerçek bir donanım sürücüsü değil, RAM üzerinde çalışan sanal bir cihaz yaptın. Ama mimari aynı.

1. Include'lar: Driver'ın hangi kernel servislerine ihtiyacı var?
#include <linux/module.h>

Bu sana kernel module altyapısını verir.

Çünkü senin programın normal C programı değil.

Normal program:

main()
 ↓
exit()

Kernel module:

module_init()
 ↓
kernel içine yüklenir

module_exit()
 ↓
kernel'den çıkarılır

Bu header olmadan:

module_init(ahmet_init);
module_exit(ahmet_exit);

çalışmaz.

#include <linux/fs.h>

Bu çok önemli.

Linux'ta her şey dosya mantığıyla çalışır.

Mesela:

/dev/ahmet
/dev/ttyUSB0
/dev/sda

bunların hepsi dosya gibi görünür.

Ama arka planda:

open()
read()
write()
close()

çağrıları vardır.

fs.h sana bu dosya sistemi arayüzünü verir.

Buradan:

struct file_operations

gelir.

#include <linux/cdev.h>

Bu senin driver'ın kernel tarafındaki kimliğidir.

Yani:

Senin driver'ın
       |
       |
      cdev
       |
       |
Kernel'e kayıt

Bunu eklemeden kernel:

"Ben bu cihazı tanımıyorum."

der.

#include <linux/uaccess.h>

Bu güvenlik sınırıdır.

Çünkü:

USER SPACE

char data[];


KERNEL SPACE

buffer[]

bunlar farklı dünyalar.

Direkt:

buffer = user_buffer;

yapamazsın.

Bunun yerine:

copy_from_user()

USER
 |
 |
 ↓

KERNEL

ve

copy_to_user()

KERNEL
 |
 |
 ↓

USER

kullanılır.

#include <linux/device.h>

Bu son eklediğimiz mimari katman.

Bu Linux Device Model.

Görevi:

Kernel'deki cihazı:

kernel object

       ↓

device model

       ↓

/dev/ahmet

haline getirmek.

2. Global değişkenler: Driver'ın sahip olduğu kaynaklar
Device Number
static dev_t dev_num;

Linux cihazları numara ile tanır.

Örneğin:

Major: 510
Minor: 0

gibi.

Major:

Hangi driver?

Minor:

O driver'ın hangi cihazı?

Örneğin:

/dev/sda

major 8
minor 0

demek:

"Bu disk driver 8 tarafından yönetiliyor, bu ilk disk."

Sen:

major 510
minor 0

aldın.

cdev
static struct cdev ahmet_cdev;

Bu kernel tarafındaki gerçek cihaz nesnesidir.

Şu bağlantıyı oluşturur:

dev_num

510:0

  |
  |
  ↓

cdev

  |
  |
  ↓

file_operations


Yani:

"510 numaralı cihaz açılırsa hangi fonksiyonlar çalışacak?"

sorusunun cevabı burada.

class
static struct class *ahmet_class;

Linux Device Model'in parçası.

Mantık:

Driver

 |
 |
 class

 |
 |
 device

 |
 |
 /dev/ahmet
device
static struct device *ahmet_device;

Bu artık kullanıcıya görünen cihazdır.

Yani:

Kernel nesnesi

        ↓

/sys/class/ahmet_class/

        ↓

udev

        ↓

/dev/ahmet
3. Buffer
static char buffer[BUFFER_SIZE];

Şu anda donanım yerine RAM kullanıyorsun.

Gerçek hayatta:

write()

   ↓

driver

   ↓

UART register
SPI register
GPIO


olurdu.

Sen:

write()

 ↓

kernel buffer


yapıyorsun.

4. File Operations mimarisi

Buradaki en önemli nokta:

static const struct file_operations fops

Bu bir callback tablosu.

Linux diyor ki:

"Ben kullanıcıdan bir dosya işlemi gelirse, sen bana hangi fonksiyonu çağıracağımı söyle."

Sen diyorsun:

.open = ahmet_open,
.read = ahmet_read,
.write = ahmet_write,
.release = ahmet_release,

Yani:

Kullanıcı

open("/dev/ahmet")

        |

        ↓

VFS

        |

        ↓

ahmet_open()


5. init fonksiyonu: Driver'ın doğuşu

Burası en önemli bölüm.

A) Numara alıyorsun
alloc_chrdev_region()

Kernel'e diyorsun:

"Ben yeni bir cihaz oluşturacağım."

Kernel:

Tamam.

Major 510
Minor 0

senin.
B) cdev oluşturuyorsun
cdev_init()

Bağlantı:

cdev

+

file_operations


oluşuyor.

C) Kernel'e ekliyorsun
cdev_add()

Artık:

Kernel biliyor:

510:0 cihazı var.

Dosya operasyonları:
read
write
open
close

burada.
D) Device Model
class
class_create()

Bir kategori oluşturuyorsun:

/sys/class/ahmet_class
device
device_create()

Asıl olay burada.

Diyorsun ki:

"Bu device number'a sahip gerçek bir cihaz var."

Linux:

udev çalışır

/dev/ahmet oluşturulur

Yani artık:

Eskiden:

mknod

yapıyordun.

Şimdi:

kernel
 |
 |
device_create()
 |
 |
udev
 |
 |
/dev/ahmet

oluyor.

Mentörünün istediği şey buydu.

6. exit fonksiyonu

Bir driver yüklenirken kaynak alır.

Örneğin:

alloc_chrdev_region
        |
        |
      kaynak aldı


cdev_add
        |
        |
      kernel kaydı yaptı


device_create
        |
        |
      /dev oluşturdu


Çıkarken tersini yaparsın:

device_destroy()

önce kullanıcıya görünen şeyi kaldır.

class_destroy()

device model temizle.

cdev_del()

kernel kaydını sil.

unregister_chrdev_region()

numarayı geri ver.

Yani:

CREATE

number
 ↓
cdev
 ↓
class
 ↓
device
 ↓
/dev


DESTROY

/dev
 ↓
device
 ↓
class
 ↓
cdev
 ↓
number

Büyük resim

Sen aslında şu mimariyi kurdun:

                 USER SPACE

        cat /dev/ahmet
        echo test > /dev/ahmet

                 |
                 |
              VFS

                 |
                 |

        file_operations

                 |
        ┌──────────────┐
        │ ahmet driver │
        └──────────────┘

          open()
          read()
          write()

                 |

            kernel buffer


                 |
                 
              cdev

                 |

          Linux Device Model

                 |

             udev

                 |

          /dev/ahmet


Şu anda öğrendiğin şey aslında Linux driver dünyasının temel taşı:

Bir cihazı kernel'e tanıtmak → dosya arayüzü vermek → kullanıcıyla haberleştirmek → yaşam döngüsünü yönetmek.

Bundan sonraki doğal adım ioctl değil önce error handling değil; önce bu mimarinin eksik parçası olan "driver state ve concurrency" tarafına geçmek. Çünkü gerçek bir driver'da aynı anda iki program /dev/ahmet açabilir. Şu anki kodun orada bozulur. Bu bizi Race Condition (yarış durumu) ve Mutex (kilit mekanizması) konusuna götürür.