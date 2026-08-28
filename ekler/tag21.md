# STAJ RAPORU – 21. GÜN

## TCP Temellerinin Tekrarı, Server Mimarileri, ACK/Retransmission, Window Management, PEP ve C Stack Yapısının Derinlemesine İncelenmesi

Yirminci gün sonunda TCP Accelerator / Split-TCP proxy mimarisi userspace üzerinde geliştirilmiş; bidirectional forwarding, userspace buffering, non-blocking I/O, partial send, backpressure, half-close, `epoll` ve multi-client connection yönetimi ele alınmıştı. Bugünkü çalışma yeni bir modül geliştirmek yerine, hocanın daha önce sorduğu ve TCP Accelerator projesinin temelini oluşturan konuların ayrıntılı tekrarına ayrıldı.

Çalışma boyunca TCP server mimarileri, çok yüksek connection sayılarında sistemin davranışı, TCP acknowledgment sistemi, packet loss ve retransmission mekanizmaları, receive/congestion window ilişkisi, Netfilter firewall mantığı, PEP/accelerator yaklaşımı ve C dilinde stack yapısı tekrar edildi. Özellikle konular birbirlerinden bağımsız ezber başlıklar halinde değil, Linux system programming ve network programming içerisinde birbirleriyle ilişkili şekilde ele alındı.

---

## 1. Blocking ve Non-Blocking TCP Server Yapılarının Tekrarı

İlk olarak klasik blocking TCP server ile non-blocking/event-driven server yapılarının farkı tekrar edildi.

Blocking yapıda:

```text
accept()
   ↓
recv()
   ↓
data yok
   ↓
thread bekler
   ↓
data geldiğinde devam eder
```

şeklinde bir davranış bulunmaktadır.

Örneğin bir socket üzerinde:

```c
recv(fd, buffer, sizeof(buffer), 0);
```

çağrısı yapıldığında socket üzerinde okunabilecek data bulunmuyorsa thread kernel tarafından bekleme durumuna alınabilir.

Tek thread ile çok sayıda client yönetilmek istenirse bu durum diğer client'ların işlenmesini engelleyebilir.

Klasik çözümlerden biri:

```text
Client 1 → Thread 1
Client 2 → Thread 2
Client 3 → Thread 3
```

şeklinde connection başına thread oluşturmaktır.

Ancak connection sayısı çok büyüdüğünde thread stack memory, scheduler yükü ve context-switch maliyeti önemli hale gelir.

Non-blocking modelde socket:

```c
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

ile non-blocking hale getirilir.

Bu durumda data bulunmuyorsa:

```text
recv()
   ↓
-1
   ↓
EAGAIN / EWOULDBLOCK
```

sonucu alınabilir ve thread başka bir connection ile ilgilenmeye devam edebilir.

Çok sayıda socket'in tek tek kontrol edilmesi yerine:

```text
select
poll
epoll
```

gibi readiness-notification mekanizmalarının kullanılabileceği tekrar edildi.

Özellikle Linux üzerinde `epoll` ile:

```text
çok sayıda socket
        ↓
      kernel
        ↓
      epoll
        ↓
yalnız hazır socket'ler
        ↓
    application
```

şeklinde event-driven mimari kurulabileceği pekiştirildi.

Sonuç olarak:

```text
Az connection
      ↓
Blocking model
basit ve yeterli olabilir

Çok connection
      ↓
Non-blocking
+
epoll
      ↓
daha ölçeklenebilir mimari
```

sonucuna ulaşıldı.

---

## 2. TCP'de 65 Bin Port ile Connection Sayısının Aynı Şey Olmadığının Tekrarı

TCP port alanının 16-bit olduğu tekrar edildi.

```text
2^16 = 65536
```

Dolayısıyla port numarası:

```text
0 ... 65535
```

aralığındadır.

Ancak TCP connection yalnızca port numarasıyla tanımlanmaz.

Bir TCP connection'ın temel kimliği:

```text
Source IP
Source Port
Destination IP
Destination Port
```

alanlarının birleşimidir.

Bu yapı 4-tuple olarak adlandırılır.

Örneğin:

```text
10.0.0.1:40000 → 192.168.1.10:5001
10.0.0.2:40000 → 192.168.1.10:5001
10.0.0.3:40000 → 192.168.1.10:5001
```

connection'larının source port ve destination port değerleri aynı olsa bile source IP farklı olduğu için birbirlerinden bağımsız TCP connection'larıdır.

Bu nedenle:

```text
65535 port
≠
65535 maksimum connection
```

olduğu tekrar edildi.

---

## 3. Çok Yüksek TCP Connection Sayısında Gerçek Sınırların İncelenmesi

Bir server'a çok yüksek sayıda connection geldiğinde gerçek limitin port sayısından çok sistem kaynakları olduğu tekrar edildi.

Örneğin bir milyon connection için:

```text
1.000.000 TCP connection
          │
          ├── file descriptors
          ├── socket structures
          ├── TCP state
          ├── receive buffers
          ├── send buffers
          ├── userspace connection state
          ├── RAM
          ├── CPU
          ├── scheduler
          └── network bandwidth
```

kaynaklarının tüketileceği değerlendirildi.

Ayrıca Split-TCP accelerator mimarisinde bir client başına:

```text
client_fd
+
server_fd
```

olmak üzere iki connected socket bulunduğu için connection sayısının accelerator tarafında socket sayısını daha da büyüteceği görüldü.

Bu tekrar multi-client `epoll` mimarisinin neden gerekli olduğunu daha net hale getirdi.

---

## 4. TCP Sequence Number ve ACK Mekanizmasının Tekrarı

TCP'nin packet-oriented değil byte-stream oriented bir protokol olduğu tekrar edildi.

Örneğin:

```text
SEQ = 1000
LEN = 500
```

olan bir TCP segmenti:

```text
1000 ... 1499
```

sequence space'indeki byte'ları taşımaktadır.

Receiver bu veriyi düzgün aldıktan sonra:

```text
ACK = 1500
```

gönderebilir.

Bunun anlamı:

```text
1499'a kadar olan byte'ları aldım
ve
1500 numaralı byte'ı bekliyorum
```

şeklindedir.

Dolayısıyla ACK değerinin:

```text
alınan son byte
```

değil:

```text
next expected byte
```

olduğu tekrar vurgulandı.

---

## 5. Cumulative ACK Mantığının Tekrarı

TCP ACK mekanizmasının cumulative olduğu tekrar edildi.

Örneğin:

```text
1000-1999 ✅
2000-2999 ✅
3000-3999 ✅
```

byte aralıkları alınmışsa receiver:

```text
ACK = 4000
```

göndererek 3999'a kadar olan data'nın geldiğini tek ACK ile ifade edebilir.

Bu yapı sayesinde her segment için ayrı ayrı geçmiş ACK bilgilerinin taşınmasına gerek kalmaz.

---

## 6. Packet Loss ve Duplicate ACK Davranışının Tekrarı

Kayıp senaryosu tekrar incelendi:

```text
1000-1999 ✅
2000-2999 ❌
3000-3999 ✅
4000-4999 ✅
5000-5999 ✅
```

Receiver'ın halen beklediği ilk byte:

```text
2000
```

olduğu için gelen out-of-order segmentlere rağmen cumulative ACK:

```text
ACK 2000
ACK 2000
ACK 2000
```

olarak devam edebilir.

Bu repeated ACK'lerin duplicate ACK olduğu tekrar edildi.

---

## 7. Retransmission Mekanizmalarının Tekrarı

TCP'nin kayıp data'yı tekrar gönderebilmesi için başlıca mekanizmalar tekrar edildi:

```text
TCP Retransmission
      │
      ├── RTO
      │   └── Retransmission Timeout
      │
      ├── Duplicate ACK
      │
      ├── Fast Retransmit
      │
      └── SACK
```

RTO mekanizmasında sender, belirli süre içerisinde ACK alamazsa data'yı yeniden gönderir.

Fast Retransmit mekanizmasında ise sender duplicate ACK pattern'inden bir segmentin kaybolmuş olabileceğini çıkararak timeout beklemeden retransmission gerçekleştirebilir.

---

## 8. SACK Mekanizmasının Tekrarı

Selective Acknowledgment kullanıldığında receiver'ın yalnız cumulative ACK değerini değil, out-of-order olarak aldığı diğer byte aralıklarını da sender'a bildirebildiği tekrar edildi.

Örneğin:

```text
2000-2999 ❌
3000-3999 ✅
4000-4999 ✅
5000-5999 ✅
```

durumunda receiver:

```text
ACK = 2000
```

bilgisine ek olarak:

```text
3000-6000 aralığı bende
```

anlamındaki SACK bilgisini taşıyabilir.

Böylece sender'ın zaten ulaşmış olan datayı tekrar göndermesine gerek kalmaz.

---

## 9. TCP Receive Window Tekrarı

TCP receive window'un flow control amacıyla kullanıldığı tekrar edildi.

Receiver'ın receive buffer kapasitesi sınırlıdır.

Bu nedenle receiver sender'a:

```text
Benim şu kadar byte daha kabul edecek alanım var
```

bilgisini advertised receive window ile bildirir.

```text
Receiver Buffer

┌───────────────────────────────┐
│ USED │         FREE           │
└───────────────────────────────┘
               ↑
              rwnd
```

Buradaki `rwnd` receiver'ın sender'a uyguladığı flow-control limitidir.

---

## 10. Congestion Window Tekrarı

Receive window'dan farklı olarak congestion window:

```text
cwnd
```

receiver kapasitesinden değil network congestion durumundan etkilenmektedir.

Bu nedenle:

```text
rwnd
=
receiver flow control

cwnd
=
network congestion control
```

şeklindeki ayrım tekrar edildi.

Sender'ın kullanılabilir gönderim sınırı basitleştirilmiş olarak:

```text
Effective Window
≈
min(rwnd, cwnd)
```

şeklinde düşünülebilir.

Bu nedenle yalnız receive window değerinin çok büyük olması sender'ın sınırsız data göndereceği anlamına gelmez.

---

## 11. Window Scaling Tekrarı

TCP header içerisindeki klasik Window alanının 16-bit olduğu ve ham olarak yaklaşık 65 KB seviyesinde değer taşıyabildiği tekrar edildi.

Yüksek BDP network'lerde bu boyutun yetersiz olması nedeniyle TCP Window Scaling mekanizmasının SYN/SYN-ACK sırasında negotiate edilebildiği tekrar edildi.

Bu mekanizma ile TCP efektif receive window'unun 64 KB'nin çok üzerine çıkabildiği görüldü.

---

## 12. Bandwidth-Delay Product ve Uydu Bağlantısı Tekrarı

TCP accelerator ödevinin temel sebebi olan high-BDP problemi tekrar hesaplandı.

Örneğin:

```text
Bandwidth = 100 Mbit/s
RTT       = 500 ms
```

için:

```text
BDP
=
100 Mbit/s × 0.5 s
=
50 Mbit
=
6.25 MB
```

sonucuna ulaşılır.

Bu değer kabaca hattın tamamen kullanılabilmesi için ACK beklenirken network üzerinde bulunabilmesi gereken data miktarını ifade etmektedir.

Dolayısıyla:

```text
High Bandwidth
+
High RTT
=
High BDP
```

olduğunda TCP'nin yeterli miktarda in-flight data tutabilmesi kritik hale gelir.

---

## 13. Accelerator'ın Neden Yalnız Window Header'ını Değiştiremeyeceğinin Tekrarı

Basit bir yaklaşım:

```text
receiver küçük window bildiriyor
        ↓
accelerator bunu büyük gösteriyor
        ↓
sender daha fazla data yolluyor
```

şeklinde düşünülebilir.

Ancak gerçekte daha fazla data gönderildiğinde bu data'nın gerçekten bir yerde tutulması gerekir.

Dolayısıyla:

```text
Advertised Window
+
Real Buffer Capacity
+
Congestion State
+
RTT
+
Loss
```

birlikte değerlendirilmelidir.

Bu nedenle gerçek accelerator yalnız packet header üzerinde değer değiştiren bir cihaz değil, connection state ve buffering yöneten bir sistem olarak düşünülmelidir.

---

## 14. Split-TCP / PEP Tekrarı

Performance Enhancing Proxy yaklaşımında uzun end-to-end TCP connection farklı connection'lara ayrılabilir.

```text
Client
   │
 TCP #1
   │
   ▼
PEP / Accelerator
   │
 TCP #2
   │
   ▼
Server
```

Her connection bağımsız:

```text
SEQ
ACK
rwnd
cwnd
RTT
RTO
retransmission
```

state'ine sahiptir.

Bu nedenle accelerator'ın yalnız Layer-3 forwarding yapan bir router olmadığı tekrar edildi.

---

## 15. Uydu Haberleşmesinde PEP'in Konumu

PEP'in özellikle problemli/high-RTT link'in sınırına yerleştirilebileceği tekrar edildi.

```text
Local Network
     │
     ▼
   PEP A
     │
     │
     │ Satellite / High RTT Link
     │
     ▼
   PEP B
     │
     ▼
Remote Network
```

Buradaki amaç uzun RTT'nin doğrudan tüm end-to-end TCP davranışını etkilemesini azaltmak ve link'e uygun TCP optimizasyonu uygulayabilmektir.

---

## 16. Netfilter Firewall Mimarisinin Tekrarı

Daha önce geliştirilen Netfilter firewall'ın packet path üzerindeki yeri tekrar edildi.

```text
NIC
 │
 ▼
Driver
 │
 ▼
struct sk_buff
 │
 ▼
Linux Network Stack
 │
 ▼
Netfilter Hook
```

Netfilter callback içerisinde packet incelenerek:

```text
NF_ACCEPT
```

veya:

```text
NF_DROP
```

verdict'i verilebileceği tekrar edildi.

---

## 17. MAC Filtering Tekrarı

Ethernet header içerisindeki source MAC bilgisinin:

```c
eth_hdr(skb)
```

ile elde edilebildiği tekrar edildi.

```text
Ethernet Header
├── Destination MAC
├── Source MAC
└── EtherType
```

Blacklist içerisindeki MAC adresleri:

```text
Source MAC
    ↓
blocked_macs[]
    ↓
ether_addr_equal()
    ↓
eşleşti mi?
   /       \
 YES        NO
  ↓          ↓
DROP       devam
```

mantığıyla kontrol edilmektedir.

Ayrıca MAC adresinin end-to-end Internet kimliği olmadığı, router geçişlerinde Layer-2 frame'in yeniden oluşturulduğu ve MAC filtering'in özellikle local Layer-2 segment üzerinde anlamlı olduğu tekrar edildi.

---

# 18. C Program Memory Layout Tekrarı

Bugünkü çalışmanın ikinci büyük bölümü C dilinde stack yapısına ayrıldı.

Önce tipik Linux process memory layout tekrar edildi:

```text
High Address

┌──────────────────────────┐
│          STACK           │
│          ↓               │
│                          │
├──────────────────────────┤
│      mmap regions        │
│      shared libs         │
├──────────────────────────┤
│                          │
│          HEAP            │
│          ↑               │
├──────────────────────────┤
│           BSS            │
├──────────────────────────┤
│          DATA            │
├──────────────────────────┤
│         RODATA           │
├──────────────────────────┤
│          TEXT            │
└──────────────────────────┘

Low Address
```

Bu yapı üzerinden stack, heap, global/static data ve code segment'lerinin farkları tekrar edildi.

---

## 19. C'de Stack'in Temel Çalışma Mantığı

C standardının fiziksel bir stack implementasyonunu zorunlu kılmadığı, ancak Linux/x86-64 ve ARM gibi modern sistemlerde function çağrılarının pratikte call stack üzerinden yönetildiği tekrar edildi.

Stack:

```text
Last In
First Out
```

yani LIFO mantığıyla çalışır.

Function call zinciri:

```text
main()
   ↓
foo()
   ↓
bar()
   ↓
baz()
```

ise dönüş sırası:

```text
baz()
   ↓
bar()
   ↓
foo()
   ↓
main()
```

şeklindedir.

Bu yapı function call semantiği ile stack'in LIFO yapısının doğal biçimde uyumlu olduğunu göstermektedir.

---

## 20. Stack Frame Kavramı

Her aktif function invocation için bir stack frame bulunabileceği tekrar edildi.

Kavramsal olarak bir frame:

```text
┌──────────────────────────┐
│ Return Address           │
├──────────────────────────┤
│ Saved Registers          │
├──────────────────────────┤
│ Local Variables          │
├──────────────────────────┤
│ Temporary Values         │
├──────────────────────────┤
│ Register Spill Area      │
├──────────────────────────┤
│ Alignment / Padding      │
└──────────────────────────┘
```

gibi bilgiler içerebilir.

Bu yerleşimin compiler, architecture ve ABI'ye bağlı olduğu vurgulandı.

---

## 21. Stack Pointer Tekrarı

CPU'nun stack'in mevcut konumunu takip edebilmesi için stack pointer register kullandığı tekrar edildi.

Örneğin x86-64 üzerinde:

```text
RSP
```

ARM64 üzerinde:

```text
SP
```

register'ı kullanılır.

x86-64'te stack tipik olarak düşük adreslere doğru büyüdüğünden function'a alan ayırmak için kavramsal olarak:

```asm
sub rsp, 32
```

ve function sonunda:

```asm
add rsp, 32
```

gibi işlemler görülebilir.

---

## 22. Function Call ve Return Address

Function çağrıldığında CPU'nun caller'a geri dönebilmesi için return address bilgisinin saklanması gerektiği tekrar edildi.

```text
main
 │
 │ call foo
 ▼
foo
```

çağrısında:

```text
foo bittikten sonra
main içerisinde hangi instruction'a dönülecek?
```

bilgisinin korunması gerekir.

x86 mimarisinde `call` instruction dönüş adresini stack ile ilişkilendirerek callee'ye geçer, `ret` instruction ise bu adresi kullanarak caller'a döner.

---

## 23. Local Variable Lifetime

Örneğin:

```c
void foo(void)
{
    int x = 42;
}
```

içerisindeki `x` automatic storage duration'a sahiptir.

```text
foo çağrıldı
    ↓
x lifetime başlar
    ↓
foo çalışır
    ↓
foo return
    ↓
x lifetime sona erer
```

Function return ettiğinde stack memory'nin fiziksel olarak sıfırlanması gerekmediği, fakat variable'ın artık geçerli olmadığı tekrar edildi.

---

## 24. Local Variable Adresini Return Etmenin Hatası

Aşağıdaki kod tekrar analiz edildi:

```c
int *foo(void)
{
    int x = 10;

    return &x;
}
```

`foo()` çalışırken:

```text
foo frame
   │
   └── x
```

geçerlidir.

Ancak `foo()` return ettiğinde `x`'in lifetime'ı sona erer.

Bu nedenle döndürülen pointer:

```text
dangling pointer
```

haline gelir.

Pointer'ın dereference edilmesinin undefined behavior olduğu tekrar edildi.

---

## 25. Stack ile Heap Arasındaki Fark

Stack ve heap tekrar karşılaştırıldı.

```text
STACK
├── automatic management
├── function-call oriented
├── hızlı allocation/deallocation
├── sınırlı kapasite
└── LIFO davranışı

HEAP
├── dynamic allocation
├── malloc/calloc/realloc
├── free ile yönetim
├── daha uzun/bağımsız lifetime
└── fragmentation mümkün
```

Örneğin:

```c
int *p = malloc(sizeof(int));
```

kodunda:

```text
p
↓
local pointer
↓
tipik olarak stack

*p
↓
malloc ile ayrılan data
↓
heap
```

olduğu tekrar edildi.

---

## 26. Global ve Static Değişkenlerin Stack'ten Ayrılması

Initialize edilmiş global variable'ın tipik olarak `.data` bölümünde, initialize edilmemiş global variable'ın `.bss` bölümünde tutulduğu tekrar edildi.

```c
int global_a = 10;
```

tipik olarak:

```text
.data
```

ve:

```c
int global_b;
```

tipik olarak:

```text
.bss
```

içerisindedir.

Ayrıca:

```c
void foo(void)
{
    static int counter;
}
```

şeklindeki static local variable'ın scope olarak function'a ait olmasına rağmen storage duration olarak static olduğu ve stack frame'de yaşayan normal automatic local variable gibi davranmadığı tekrar edildi.

---

## 27. Recursion ve Stack Consumption

Recursive function'larda her function invocation için yeni execution state gerektiği tekrar edildi.

Örneğin:

```c
int factorial(int n)
{
    if (n <= 1)
        return 1;

    return n * factorial(n - 1);
}
```

çağrısı:

```text
factorial(4)
     ↓
factorial(3)
     ↓
factorial(2)
     ↓
factorial(1)
```

şeklinde ilerler.

Stack üzerinde:

```text
TOP
┌───────────────────┐
│ factorial(1)      │
├───────────────────┤
│ factorial(2)      │
├───────────────────┤
│ factorial(3)      │
├───────────────────┤
│ factorial(4)      │
├───────────────────┤
│ main              │
└───────────────────┘
```

gibi bir yapı oluşabilir.

Return sırasında frame'ler ters sırayla kaldırılır.

---

## 28. Stack Overflow

Stack'in sınırlı olduğu ve çok büyük local variable veya çok derin recursion nedeniyle stack overflow oluşabileceği tekrar edildi.

Örneğin:

```c
void recursive(void)
{
    char buffer[1024];

    recursive();
}
```

fonksiyonunun base case olmadan çağrılması:

```text
frame
 ↓
frame
 ↓
frame
 ↓
frame
 ↓
...
 ↓
stack limit
```

sonucunda process'in stack sınırını aşmasına neden olabilir.

---

## 29. Local Değişkenlerin Her Zaman Stack'te Olmadığının Tekrarı

Compiler optimizasyonları nedeniyle:

```c
int x;
```

şeklindeki local variable'ın mutlaka fiziksel stack memory içerisinde bulunmak zorunda olmadığı tekrar edildi.

Compiler variable'ı:

```text
register
```

içerisinde tutabilir veya tamamen optimize edebilir.

Dolayısıyla daha doğru ifade:

> Automatic local state tipik olarak stack frame ile ilişkilidir ancak gerçek fiziksel yerleşimi compiler ve optimizasyona bağlıdır.

şeklindedir.

---

## 30. Function Parametrelerinin Stack veya Register ile Geçebilmesi

Modern ABI'lerde function argümanlarının yalnız stack üzerinden geçmediği tekrar edildi.

Örneğin x86-64 Linux SysV ABI'de ilk integer/pointer argument'ların önemli bir kısmı:

```text
RDI
RSI
RDX
RCX
R8
R9
```

register'ları üzerinden aktarılabilir.

Daha fazla argument gerektiğinde stack de kullanılabilir.

Bu nedenle:

```text
Function parameters always live on the stack
```

ifadesinin teknik olarak doğru olmadığı tekrar edildi.

---

## 31. Stack Buffer Overflow Güvenlik Problemi

Aşağıdaki örnek:

```c
void foo(char *input)
{
    char buffer[16];

    strcpy(buffer, input);
}
```

üzerinden stack buffer overflow tekrar incelendi.

Input buffer boyutunu aşarsa komşu stack state zarar görebilir.

Kavramsal olarak:

```text
┌───────────────────┐
│ Return Address    │
├───────────────────┤
│ Saved State       │
├───────────────────┤
│ buffer[16]        │
└───────────────────┘
```

şeklindeki bir frame'de buffer sınırının aşılması saved state veya return information gibi kritik bilgilerin bozulmasına neden olabilir.

Modern sistemlerde:

```text
Stack Canary
ASLR
NX
FORTIFY
```

gibi güvenlik mekanizmalarının bu tür hataların etkisini azaltmak amacıyla kullanılabileceği tekrar edildi.

---

## 32. Thread Başına Ayrı Stack Bulunması

Bir process içerisindeki thread'lerin:

```text
Code
Heap
Global Data
```

gibi alanları paylaştığı, ancak her thread'in kendi call stack'ine sahip olduğu tekrar edildi.

```text
PROCESS
 │
 ├── Code              shared
 ├── Data              shared
 ├── Heap              shared
 │
 ├── Thread 1 Stack
 ├── Thread 2 Stack
 └── Thread 3 Stack
```

Bu konu TCP server mimarileriyle ilişkilendirildi.

Her client için ayrı thread oluşturulduğunda yalnız thread scheduling değil, thread stack memory tüketiminin de önemli hale gelebileceği görüldü.

---

## 33. Userspace Stack ve Kernel Stack Ayrımı

Userspace thread stack'i ile kernel tarafında kullanılan execution stack'in aynı olmadığı tekrar edildi.

```text
User Process
     │
     │ syscall
     ▼
Kernel
```

geçişinde kernel kendi execution context ve kernel stack mekanizmasını kullanmaktadır.

Kernel stack'in görece sınırlı olması nedeniyle kernel function'larında çok büyük automatic local arrays kullanmanın kötü bir tasarım olduğu tekrar vurgulandı.

Bu konu daha önce çalışılan kernel driver ve Netfilter kodlarıyla ilişkilendirildi.

---

# 34. Bugünkü Konuların Birbirine Bağlanması

Gün sonunda bugün tekrar edilen başlıkların aslında birbirinden bağımsız olmadığı görüldü.

```text
C STACK
   │
   ├── function calls
   ├── thread model
   └── memory lifetime
          │
          ▼
SYSTEM PROGRAMMING
          │
          ├── file descriptors
          ├── blocking / non-blocking
          └── epoll
                  │
                  ▼
TCP SERVER
                  │
          ┌───────┴─────────┐
          │                 │
      TCP STATE        MULTI-CLIENT
          │
   ┌──────┼───────┐
   │      │       │
  ACK    Window  Retransmission
           │
       rwnd / cwnd
           │
           ▼
          BDP
           │
           ▼
      High RTT Link
           │
           ▼
     Split TCP / PEP
           │
           ▼
      TCP Accelerator
```

şeklinde tek bir Linux network/system programming zinciri oluştu.

---

# 35. 20. Gün → 21. Gün İlerlemesi

Önceki gün:

```text
20. GÜN
─────────────────────────

TCP Accelerator
      ↓
Split TCP
      ↓
Bidirectional Forwarding
      ↓
Buffering
      ↓
Non-Blocking I/O
      ↓
Backpressure
      ↓
Half-Close
      ↓
epoll
      ↓
Multi-Client
```

seviyesine ulaşılmıştı.

Bugün yeni feature eklemek yerine mevcut altyapının teorik temeli ayrıntılı olarak tekrar edildi:

```text
21. GÜN
────────────────────────────────

TCP Server Models
       ↓
Blocking / Non-Blocking
       ↓
epoll / Scalability
       ↓
TCP Connection Identity
       ↓
4-Tuple
       ↓
High Connection Count
       ↓
SEQ / ACK
       ↓
Cumulative ACK
       ↓
Packet Loss
       ↓
RTO / Fast Retransmit / SACK
       ↓
rwnd / cwnd
       ↓
Window Scaling
       ↓
BDP
       ↓
High RTT
       ↓
PEP / Split TCP
       ↓
Netfilter / MAC Firewall
       ↓
C MEMORY MODEL
       ↓
STACK
       ↓
Stack Frame
       ↓
Stack Pointer
       ↓
Function Call / Return
       ↓
Local Variable Lifetime
       ↓
Stack vs Heap
       ↓
Recursion
       ↓
Stack Overflow
       ↓
Thread Stack
       ↓
Kernel Stack
       ↓
SYSTEM + NETWORK FUNDAMENTALS TEKRARI
```

seviyesinde tamamlandı.

---

# Gün Sonu Değerlendirmesi

Yirmi birinci gün yeni bir modül geliştirmek yerine daha önce çalışılmış ancak sonraki network ve kernel konularının anlaşılması açısından kritik olan temel kavramların ayrıntılı tekrarına ayrıldı.

TCP tarafında blocking ve non-blocking server modelleri, `epoll` tabanlı ölçeklenebilir connection yönetimi, TCP connection'ların 4-tuple ile tanımlanması ve 65 bin port sayısının maksimum connection sayısı anlamına gelmediği tekrar edildi. TCP reliability mekanizmasında sequence number, cumulative ACK, duplicate ACK, RTO, Fast Retransmit ve SACK birlikte değerlendirildi.

Flow ve congestion control tarafında `rwnd` ile `cwnd` ayrımı tekrar edilerek gerçek gönderim kapasitesinin yalnız advertised window'a bağlı olmadığı görüldü. Window Scaling ve Bandwidth-Delay Product konuları yüksek RTT bağlantılarıyla ilişkilendirilerek daha önce geliştirilen Split-TCP / PEP accelerator mimarisinin teorik gerekçesi yeniden oluşturuldu.

Daha önce tamamlanan Netfilter firewall üzerinden kernel packet path, `struct sk_buff`, Ethernet source MAC ve `NF_ACCEPT/NF_DROP` karar mekanizması tekrar edildi.

Günün ikinci büyük bölümünde C dilindeki stack yapısı derinlemesine incelendi. Function call sırasında stack frame oluşturulması, stack pointer, return address, local variable lifetime, stack ve heap arasındaki farklar, static/global variables, recursion, stack overflow, dangling pointer, thread başına ayrı stack ve userspace/kernel stack ayrımı tekrar edildi.

Bugünün en önemli kazanımı, daha önce ayrı günlerde öğrenilmiş olan C memory management, Linux system programming, TCP protocol mechanics, event-driven server architecture ve TCP Accelerator konularının tek bir bütün halinde tekrar edilmesi ve aralarındaki ilişkinin netleştirilmesi oldu.

Gün sonunda ulaşılan kavramsal bütünlük:

```text
C LANGUAGE
    │
    ▼
STACK / MEMORY MODEL
    │
    ▼
FUNCTION & THREAD EXECUTION
    │
    ▼
LINUX SYSTEM PROGRAMMING
    │
    ▼
FD / BLOCKING / NON-BLOCKING
    │
    ▼
EPOLL / MULTI-CLIENT
    │
    ▼
TCP
    │
    ├── SEQ / ACK
    ├── RETRANSMISSION
    ├── RWND / CWND
    └── WINDOW SCALING
            │
            ▼
           BDP
            │
            ▼
        HIGH RTT
            │
            ▼
      SPLIT TCP / PEP
            │
            ▼
      TCP ACCELERATOR
```

şeklinde oldu.
