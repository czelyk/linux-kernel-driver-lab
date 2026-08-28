# STAJ RAPORU – 20. GÜN

## Linux Networking – TCP Accelerator / Split-TCP Proxy, Buffering, Non-Blocking I/O, epoll ve Multi-Client Mimarisi

On dokuzuncu gün sonunda Netfilter tabanlı firewall çalışması tamamlanmış; MAC, IP ve TCP port filtering mekanizmaları uygulanmıştı. Ardından yüksek RTT ve yüksek bant genişliğine sahip bağlantılarda TCP performansı incelenerek Bandwidth-Delay Product (BDP), TCP receive window, congestion window ve buffering ilişkileri ele alınmıştı. Yalnız TCP header içerisindeki advertised window değerini büyütmenin yeterli olmadığı görülmüş ve daha gerçekçi bir çözüm olarak Split-TCP / Performance Enhancing Proxy (PEP) tabanlı TCP Accelerator mimarisi tasarlanmıştı.

Bugünkü çalışmada bu teorik mimari doğrudan userspace C programına dönüştürüldü. İlk olarak tek client destekleyen Split-TCP proxy oluşturuldu. Daha sonra socket buffer tuning, çift yönlü data forwarding, partial send, userspace queue, non-blocking I/O, backpressure ve TCP half-close davranışları incelendi. Son aşamada `epoll` ve connection-state yapıları kullanılarak accelerator'ın birden fazla client'ı aynı event loop içerisinde yönetebileceği mimariye geçildi.

---

## 1. TCP Accelerator'ın Temel Mimarisi

Çalışmanın başlangıcında oluşturulan yapı:

```text
CLIENT
   │
   │ TCP CONNECTION #1
   ▼
┌───────────────────┐
│    ACCELERATOR    │
└─────────┬─────────┘
          │
          │ TCP CONNECTION #2
          ▼
        SERVER
```

şeklindedir.

Normal TCP bağlantısında:

```text
Client ←────────────────────────→ Server
```

tek bir end-to-end TCP connection bulunurken accelerator kullanıldığında bu bağlantı iki bağımsız TCP connection'a ayrılmaktadır:

```text
Client ←──── TCP #1 ────→ Accelerator
                            │
                            │
Accelerator ←── TCP #2 ───→ Server
```

Bu nedenle accelerator aynı anda iki farklı rol üstlenmektedir.

Client açısından:

```text
Accelerator = Server
```

Remote server açısından:

```text
Accelerator = Client
```

durumundadır.

Bu çalışma ile “client” ve “server” ifadelerinin cihazın sabit kimliği değil, belirli bir TCP connection içerisindeki rolü ifade ettiği pekiştirildi.

---

## 2. Split-TCP'nin TCP State Açısından Anlamı

İki connection birbirinden bağımsız olduğu için her connection'ın kendi TCP state'i bulunmaktadır:

```text
TCP CONNECTION #1
├── SEQ / ACK
├── rwnd
├── cwnd
├── RTT
├── RTO
├── retransmission state
├── send buffer
└── receive buffer


TCP CONNECTION #2
├── SEQ / ACK
├── rwnd
├── cwnd
├── RTT
├── RTO
├── retransmission state
├── send buffer
└── receive buffer
```

Dolayısıyla accelerator gelen TCP packet'ı yalnızca diğer tarafa aktaran basit bir Layer-3 router değildir.

Birinci TCP connection accelerator üzerinde sonlandırılır, uygulama payload'ı userspace'e alır ve ikinci connection üzerinden tekrar gönderir.

Temel veri yolu:

```text
TCP #1
   │
   ▼
Linux TCP Stack
   │
   ▼
recv()
   │
   ▼
Userspace Accelerator
   │
   ▼
send()
   │
   ▼
Linux TCP Stack
   │
   ▼
TCP #2
```

şeklindedir.

---

## 3. İlk Blocking TCP Accelerator

İlk prototipte accelerator bir listening socket oluşturacak şekilde tasarlandı:

```text
socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
   ↓
client_fd
```

Ardından remote server'a ikinci bir socket açıldı:

```text
socket()
   ↓
connect()
   ↓
server_fd
```

Böylece program içerisinde iki temel connected socket oluştu:

```text
client_fd                 server_fd
    │                         │
    ▼                         ▼
 Client                  Remote Server
```

İlk aşamada bu yapı blocking socket'lerle çalıştırılarak Split-TCP mimarisinin temel davranışı doğrulandı.

---

## 4. Çift Yönlü Data Forwarding

Accelerator'ın yalnız Client → Server yönünde değil, iki yönde de çalışması gerektiği ele alındı.

```text
CLIENT
   │
   │ Client → Server
   ▼
ACCELERATOR
   │
   ▼
SERVER


SERVER
   │
   │ Server → Client
   ▼
ACCELERATOR
   │
   ▼
CLIENT
```

Bu nedenle iki ayrı veri yönü tanımlandı:

```text
C2S = Client To Server
S2C = Server To Client
```

Accelerator içerisindeki mantık:

```text
client_fd
    │
   recv()
    │
    ▼
  C2S BUFFER
    │
   send()
    │
    ▼
server_fd
```

ve ters yönde:

```text
server_fd
    │
   recv()
    │
    ▼
  S2C BUFFER
    │
   send()
    │
    ▼
client_fd
```

şeklinde oluşturuldu.

---

## 5. TCP'nin Byte-Stream Olmasının Accelerator'a Etkisi

Bugün tekrar üzerinde durulan önemli noktalardan biri TCP'nin packet/message tabanlı değil byte-stream tabanlı olmasıdır.

Uygulama:

```text
send("ABC")
send("DEF")
```

yapsa bile karşı tarafın:

```text
recv() → "ABCDEF"
```

alması mümkündür.

Benzer şekilde:

```text
recv() → "AB"
recv() → "CDEF"
```

şeklinde de okunabilir.

Bu nedenle accelerator:

> “Bir recv çağrısı = bir TCP packet”

varsayımıyla tasarlanamaz.

Accelerator açısından temel birim userspace'e teslim edilen byte'lardır.

---

## 6. Socket Buffer Tuning

Accelerator'ın yüksek miktarda data ile çalışabilmesi için socket buffer'ları üzerinde:

```c
setsockopt(
    fd,
    SOL_SOCKET,
    SO_RCVBUF,
    &size,
    sizeof(size)
);
```

ve:

```c
setsockopt(
    fd,
    SOL_SOCKET,
    SO_SNDBUF,
    &size,
    sizeof(size)
);
```

kullanıldı.

Bu noktada üç farklı buffer seviyesinin birbirinden ayrılması gerektiği görüldü:

```text
Network
   ↓
Kernel TCP Receive Buffer
   ↓
recv()
   ↓
Userspace Accelerator Buffer
   ↓
send()
   ↓
Kernel TCP Send Buffer
   ↓
Network
```

Dolayısıyla accelerator içerisindeki userspace buffer ile kernel'in TCP send/receive buffer'larının aynı yapı olmadığı netleştirildi.

---

## 7. Tek Buffer Yerine İki Yönlü Queue Yapısı

Çift yönlü iletişim nedeniyle tek buffer kullanmak yerine connection başına iki ayrı queue oluşturuldu:

```c
struct connection
{
    ...

    struct io_buffer c2s;
    struct io_buffer s2c;

    ...
};
```

Burada:

```text
c2s
=
Client → Server bekleyen data
```

ve:

```text
s2c
=
Server → Client bekleyen data
```

anlamına gelmektedir.

Bir yönün yavaşlamasının diğer yönün verisiyle karışmaması için iki queue birbirinden bağımsız tutulmaktadır.

---

## 8. Partial Send Problemi

TCP `send()` çağrısının verilen buffer'ın tamamını tek seferde kabul etmek zorunda olmadığı incelendi.

Örneğin:

```text
Userspace Queue
10.000 byte
```

bulunmasına rağmen:

```c
send(...)
```

yalnızca:

```text
3.000 byte
```

kabul edebilir.

Bu durumda kalan:

```text
7.000 byte
```

kaybedilmemelidir.

Bu nedenle buffer içerisinde:

```c
size_t start;
size_t end;
```

indeksleri tutuldu.

Pending data:

```text
pending = end - start
```

olarak hesaplandı.

Partial send sonrası:

```text
start += sent_bytes
```

yapılarak gönderilemeyen byte'ların queue içerisinde kalması sağlandı.

---

## 9. Non-Blocking Socket Yapısına Geçiş

Blocking I/O'nun çoklu connection yönetimindeki dezavantajları nedeniyle socket'ler:

```c
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

ile non-blocking hale getirildi.

Blocking socket:

```text
recv()
   ↓
data yok
   ↓
THREAD BEKLER
```

şeklinde davranırken non-blocking socket:

```text
recv()
   ↓
data yok
   ↓
-1
   ↓
EAGAIN / EWOULDBLOCK
```

şeklinde davranabilmektedir.

Bu sayede tek bir connection'ın data beklemesi bütün accelerator event loop'unu durdurmamaktadır.

---

## 10. Backpressure Mekanizması

Accelerator tasarımında en önemli konulardan biri producer'ın consumer'dan hızlı olmasıdır.

Örneğin:

```text
CLIENT
  │
  │ hızlı
  ▼
ACCELERATOR
  │
  │ yavaş
  ▼
SERVER
```

durumunda Client'tan sürekli data okumaya devam edilirse userspace queue sınırsız büyüyemez.

Bu nedenle:

```text
C2S BUFFER FULL
       ↓
Client'tan recv() yapmayı bırak
       ↓
Kernel receive buffer dolar
       ↓
TCP receive-window etkilenir
       ↓
Sender yavaşlamak zorunda kalır
```

mantığı oluşturuldu.

Bu mekanizma **backpressure** olarak incelendi.

Böylece TCP flow control ile userspace accelerator buffer yönetiminin birbirinden bağımsız olmadığı görüldü.

---

## 11. EPOLLIN ve EPOLLOUT Mantığı

Non-blocking socket'lerin sürekli kontrol edilmesi yerine event-driven I/O yapısına geçildi.

`EPOLLIN`:

```text
Bu FD üzerinde okunabilecek data var.
```

`EPOLLOUT`:

```text
Bu FD'ye write/send yapılabilecek durumda.
```

anlamında kullanıldı.

Accelerator açısından temel event matrisi:

```text
CLIENT EPOLLIN
       ↓
Client'tan recv()
       ↓
C2S Queue


SERVER EPOLLOUT
       ↓
C2S Queue
       ↓
Server'a send()
```

Ters yönde:

```text
SERVER EPOLLIN
       ↓
Server'dan recv()
       ↓
S2C Queue


CLIENT EPOLLOUT
       ↓
S2C Queue
       ↓
Client'a send()
```

şeklindedir.

---

## 12. epoll Event Loop

Programın merkezi yapısı:

```c
epoll_wait(
    epoll_fd,
    events,
    MAX_EVENTS,
    -1
);
```

üzerine kuruldu.

Eski düşünce:

```text
fd1 hazır mı?
fd2 hazır mı?
fd3 hazır mı?
fd4 hazır mı?
...
```

yerine:

```text
              KERNEL
                │
              epoll
                │
         hazır FD'leri bildir
                │
                ▼
           APPLICATION
```

modeline geçildi.

Böylece uygulama bütün socket'leri tek tek polling yapmak yerine kernel'in hazır olduğunu bildirdiği endpoint'leri işlemektedir.

---

## 13. TCP Half-Close ve FIN Davranışı

TCP connection'ın iki bağımsız veri yönüne sahip olduğu tekrar ele alındı.

Bir endpoint:

```text
FIN
```

gönderdiğinde yalnızca kendi gönderme yönünü kapatabilir.

Örneğin:

```text
CLIENT ─── FIN ───► ACCELERATOR
```

geldiğinde:

```text
Client → Accelerator
```

yönünde artık yeni data gelmez.

Ancak:

```text
Accelerator → Client
```

yönünde data gönderilmeye devam edilebilir.

Bu nedenle accelerator FIN aldığında connection'ı doğrudan tamamen `close()` etmemelidir.

Önce ilgili yöndeki queue boşaltılır:

```text
Client FIN
    ↓
C2S queue'daki mevcut data
    ↓
Server'a gönder
    ↓
queue empty
    ↓
shutdown(server_fd, SHUT_WR)
    ↓
Server'a FIN
```

Bu mekanizma accelerator içerisinde half-close propagation olarak uygulandı.

---

## 14. Multi-Client Problemi

İlk accelerator yalnızca tek client için düşünülebilirdi:

```text
Client
   ↓
Accelerator
   ↓
Server
```

Gerçek bir network servisinde ise:

```text
Client 1 ──┐
Client 2 ──┤
Client 3 ──┤
Client 4 ──┤
           ▼
       ACCELERATOR
```

şeklinde çok sayıda connection aynı anda bulunabilir.

Her client için iki TCP endpoint gerektiğinden:

```text
Client 1
   │
   ▼
conn1
├── client_fd
└── server_fd


Client 2
   │
   ▼
conn2
├── client_fd
└── server_fd
```

yapısına ihtiyaç duyuldu.

---

## 15. `struct connection` Yapısı

Her TCP accelerator oturumunun state'ini tek bir yapı içerisinde tutmak için:

```c
struct connection
{
    struct endpoint client;
    struct endpoint server;

    struct io_buffer c2s;
    struct io_buffer s2c;

    int client_read_open;
    int server_read_open;

    int client_write_closed;
    int server_write_closed;
};
```

tasarlandı.

Böylece her connection:

```text
connection
   │
   ├── Client socket
   ├── Server socket
   │
   ├── Client → Server queue
   ├── Server → Client queue
   │
   ├── Client read state
   ├── Server read state
   │
   └── Half-close state
```

bilgilerini kendi içerisinde taşımaktadır.

---

## 16. Endpoint Yapısının Kullanılması

`epoll` event'i geldiğinde yalnız FD'nin değil, FD'nin hangi connection'a ve hangi tarafa ait olduğunun bilinmesi gerekmektedir.

Bu nedenle:

```c
struct endpoint
{
    int fd;

    enum endpoint_type type;

    struct connection *conn;
};
```

oluşturuldu.

Endpoint türleri:

```text
ENDPOINT_LISTENER
ENDPOINT_CLIENT
ENDPOINT_SERVER
```

olarak ayrıldı.

`epoll_event` içerisine:

```c
ev.data.ptr = endpoint;
```

verilerek event geldiğinde:

```text
Event
  ↓
endpoint
  ↓
hangi FD?
  ↓
Client mı Server mı?
  ↓
hangi connection?
```

bilgilerine doğrudan ulaşılması sağlandı.

---

## 17. Multi-Client epoll Mimarisi

Yeni client geldiğinde listener üzerinde:

```text
EPOLLIN
   ↓
accept()
   ↓
client_fd
   ↓
create_connection()
   ↓
server_fd oluştur
   ↓
remote server'a connect()
```

işlemleri gerçekleştirilmektedir.

Örneğin üç client için:

```text
                         epoll
                           │
             ┌─────────────┼─────────────┐
             │             │             │
           conn1         conn2         conn3
          /     \       /     \       /     \
     client   server client server client  server
       fd5     fd6    fd7    fd8    fd9    fd10
```

oluşmaktadır.

Tek thread:

```text
epoll_wait()
```

üzerinden bütün bu endpoint'leri yönetebilmektedir.

Bu yapı sayesinde bir connection'ın yavaşlaması diğer connection'ın blocking `recv()` veya `send()` nedeniyle bekletilmesine yol açmamaktadır.

---

## 18. 65 Bin Port ile Bir Milyon TCP Connection Konusunun İncelenmesi

Bugün hocanın soruları kapsamında TCP'de yaklaşık 65 bin port bulunması ile TCP connection sayısının aynı kavram olmadığı tekrar incelendi.

TCP port numarası 16-bit'tir:

```text
0 ... 65535
```

Ancak TCP connection yalnız destination port ile tanımlanmaz.

Temel connection kimliği:

```text
Source IP
Source Port
Destination IP
Destination Port
```

yani 4-tuple'dır.

Örneğin:

```text
10.0.0.1:40000 → 192.168.1.10:5001
10.0.0.2:40000 → 192.168.1.10:5001
10.0.0.3:40000 → 192.168.1.10:5001
```

üç ayrı TCP connection'dır.

Çok yüksek connection sayılarında asıl sınırlar:

```text
File descriptor
       +
Kernel socket memory
       +
TCP buffers
       +
Userspace connection state
       +
RAM
       +
CPU
       +
Network capacity
       +
Application architecture
```

olarak ele alındı.

Bu konu multi-client `epoll` mimarisinin neden önemli olduğunu doğrudan açıklamaktadır.

---

## 19. TCP ACK ve Retransmission Konularının Accelerator ile Birleştirilmesi

TCP ACK'in packet numarasını değil next expected byte'ı gösterdiği tekrar edildi.

Örneğin:

```text
SEQ = 1000
LEN = 500
```

ile:

```text
1000 ... 1499
```

byte'ları gönderilmişse başarılı alımdan sonra:

```text
ACK = 1500
```

beklenir.

Bunun anlamı:

```text
1499'a kadar aldım.
Sıradaki beklediğim byte = 1500.
```

şeklindedir.

Kayıp durumunda duplicate ACK, Fast Retransmit, RTO ve SACK mekanizmalarının nasıl devreye girebildiği tekrar accelerator problemiyle ilişkilendirildi.

---

## 20. Receive Window, Congestion Window ve Effective Window

Hocanın “window size'ı maksimumda tutmaya çalışan accelerator” fikrini doğru yorumlamak amacıyla receive window ve congestion window farkı tekrar incelendi.

```text
rwnd
=
Receiver'ın flow-control limiti
```

```text
cwnd
=
Sender'ın congestion-control limiti
```

Basitleştirilmiş kullanılabilir gönderim sınırı:

```text
Effective Window ≈ min(rwnd, cwnd)
```

olarak düşünülebilir.

Örneğin:

```text
rwnd = 4 MB
cwnd = 64 KB
```

ise yalnız advertised receive window'un büyük olması sender'ın 4 MB in-flight data gönderebilmesi anlamına gelmemektedir.

Bu nedenle accelerator tasarımının yalnız TCP header içerisindeki window değerini değiştirmekten ibaret olamayacağı pekiştirildi.

---

## 21. PEP ve Uydu Haberleşmesi Bağlantısı

Hocanın accelerator'ın özellikle network/uydu haberleşmesinde kullanılabileceği ifadesi Performance Enhancing Proxy yaklaşımı üzerinden değerlendirildi.

Temel topoloji:

```text
Local Network
     │
     ▼
┌─────────┐
│  PEP A  │
└────┬────┘
     │
     │
     │   SATELLITE / HIGH RTT LINK
     │
     ▼
┌─────────┐
│  PEP B  │
└────┬────┘
     │
     ▼
Remote Network
```

Yüksek RTT'li bağlantılarda Bandwidth-Delay Product büyüdüğü için hattı tamamen kullanabilmek amacıyla daha fazla data'nın in-flight durumda bulunması gerekebilir.

Split-TCP / PEP yaklaşımında uzun end-to-end TCP davranışı farklı bölümlere ayrılabilir ve her bölüm kendi TCP state'ine sahip olabilir.

Bugün geliştirilen userspace accelerator bu yaklaşımın eğitim amaçlı temel prototipi olarak ele alındı.

---

## 22. Bugünkü Accelerator'ın Ulaştığı Seviye

Bugün accelerator:

```text
TCP ACCELERATOR
       │
       ├── Split TCP
       │
       ├── Listening socket
       ├── accept()
       │
       ├── Remote socket
       ├── connect()
       │
       ├── Bidirectional forwarding
       │
       ├── SO_RCVBUF
       ├── SO_SNDBUF
       │
       ├── Userspace buffering
       ├── C2S queue
       ├── S2C queue
       │
       ├── Partial send handling
       │
       ├── O_NONBLOCK
       ├── EAGAIN / EWOULDBLOCK
       │
       ├── Backpressure
       │
       ├── TCP half-close
       ├── shutdown(SHUT_WR)
       │
       ├── epoll
       ├── EPOLLIN
       ├── EPOLLOUT
       ├── EPOLLRDHUP
       │
       ├── struct connection
       ├── struct endpoint
       │
       └── Multi-client architecture
```

seviyesine getirildi.

---

## 23. 19. Gün → 20. Gün İlerlemesi

Önceki gün:

```text
19. GÜN
────────────────────────────

NETFILTER FIREWALL
       ↓
MAC / IP / PORT FILTERING
       ↓
High RTT + High Bandwidth
       ↓
BDP
       ↓
TCP Window / Buffer
       ↓
Split TCP / PEP
       ↓
TCP ACCELERATOR TASARIMI
```

seviyesinde kalınmıştı.

Bugün:

```text
20. GÜN
────────────────────────────

TCP ACCELERATOR TASARIMI
       ↓
Split-TCP Implementation
       ↓
Listening Socket
       ↓
accept()
       ↓
Remote connect()
       ↓
Bidirectional Forwarding
       ↓
Kernel Socket Buffers
       ↓
Userspace Queues
       ↓
Partial Send
       ↓
O_NONBLOCK
       ↓
EAGAIN / EWOULDBLOCK
       ↓
Backpressure
       ↓
Half-Close / FIN Propagation
       ↓
epoll
       ↓
Connection State
       ↓
Multi-Client
       ↓
EVENT-DRIVEN TCP ACCELERATOR
```

seviyesine ulaşıldı.

---

## 24. Sonraki Çalışma Rotası

Bir sonraki aşamada accelerator'ın yalnız data forwarding yapan bir proxy olmaktan çıkarılarak TCP bağlantısının durumunu gözlemleyebilen bir yapıya dönüştürülmesi planlanmaktadır.

Linux TCP stack'ten connection bilgilerini okuyabilmek için:

```text
getsockopt()
      ↓
TCP_INFO
      ↓
struct tcp_info
```

yapısı incelenecektir.

Amaç accelerator içerisinden:

```text
RTT
cwnd
ssthresh
MSS
unacked data
retransmission
```

gibi değerleri gözlemleyebilmektir.

Böylece:

```text
TCP Accelerator
       │
       ├── Forward Data
       │
       ├── Manage Buffers
       │
       ├── Apply Backpressure
       │
       └── Observe TCP State
                    ↓
             RTT / cwnd / loss
```

seviyesine geçilecektir.

Bu aşamadan sonra hocanın “TCP window size'ı maksimumda tutmaya çalışan accelerator” fikri yalnız teorik olarak değil, gerçek TCP state değerleri ölçülerek değerlendirilebilir hale gelecektir.

---

# Gün Sonu Değerlendirmesi

Yirminci günün en önemli kazanımı, önceki gün yalnız mimari seviyede tasarlanan TCP Accelerator'ın gerçek bir userspace network programına dönüştürülmesi oldu. Client ile accelerator ve accelerator ile remote server arasında iki bağımsız TCP connection oluşturularak Split-TCP yaklaşımının temel çalışma prensibi uygulandı.

Data forwarding sırasında TCP'nin byte-stream yapısı, partial send problemi ve kernel socket buffer'ları ile userspace queue'ların farkı uygulamalı olarak incelendi. Non-blocking socket'lere geçilerek `O_NONBLOCK`, `EAGAIN/EWOULDBLOCK`, `EPOLLIN` ve `EPOLLOUT` kavramları accelerator'ın gerçek data path'i üzerinde kullanıldı. Queue dolduğunda yeni data okumayı durduran backpressure mekanizması sayesinde userspace buffer yönetimi ile TCP flow control arasındaki ilişki somutlaştırıldı.

TCP'nin full-duplex yapısı nedeniyle FIN'in connection'ın iki yönünü aynı anda kapatmadığı görülerek half-close davranışı ve `shutdown(SHUT_WR)` kullanımı accelerator mimarisine eklendi.

Günün son bölümünde tek client modelinden multi-client event-driven modele geçildi. Her bağlantının iki socket'ini, iki yönlü queue'larını ve connection state'ini tutan `struct connection` yapısı oluşturuldu. `struct endpoint` ve `epoll` kullanılarak çok sayıda client/server endpoint'inin tek event loop içerisinde yönetilebileceği mimari kuruldu.

Günün sonunda ulaşılan genel seviye:

```text
TCP FUNDAMENTALS
       +
SEQ / ACK / RETRANSMISSION
       +
RWND / CWND / BDP
       │
       ▼
SPLIT TCP / PEP
       │
       ▼
TCP ACCELERATOR
       │
       ├── TWO TCP CONNECTIONS
       ├── BIDIRECTIONAL FORWARDING
       ├── SOCKET BUFFER TUNING
       ├── USERSPACE QUEUES
       ├── PARTIAL SEND
       ├── NON-BLOCKING I/O
       ├── BACKPRESSURE
       ├── HALF-CLOSE
       ├── EPOLL
       └── MULTI-CLIENT
               │
               ▼
     EVENT-DRIVEN TCP PROXY
               │
               ▼
        NEXT: TCP_INFO
               │
               ▼
     RTT / CWND / RETRANSMISSION
        OBSERVATION & TUNING
```

oldu.
