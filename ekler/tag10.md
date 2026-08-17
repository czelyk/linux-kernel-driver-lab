# STAJ RAPORU – 10. GÜN

## UDP Ephemeral Port Mekanizması, Kernel Socket Lookup, Socket Receive Queue ve Linux Socket Durumlarının İncelenmesi

Dokuzuncu gün sonunda Linux networking mimarisi, Ethernet–ARP–IPv4–TCP/UDP katmanları ve Linux Socket API incelenmiş; C dilinde UDP client-server uygulaması geliştirilerek `127.0.0.1:5000` üzerinden `"merhaba"` payload'ının başarılı şekilde iletilmesi sağlanmıştı. Server tarafında `recvfrom()` ile client IP ve source port bilgisinin de elde edilebildiği görülmüştü.

Onuncu gün çalışmalarında mevcut UDP client-server uygulaması üzerinden Linux kernel'in socket yönetimi daha ayrıntılı incelendi. Özellikle client uygulamasında açıkça `bind()` kullanılmamasına rağmen source IP ve source port bilgilerinin nasıl oluştuğu, ephemeral port mekanizması, UDP socket lookup, socket receive queue, blocking I/O davranışı ve `ss` aracılığıyla kernel socket durumlarının gözlemlenmesi üzerinde duruldu.

## 1. UDP Client'ta Ephemeral Source Port Mekanizmasının İncelenmesi

Önceki uygulamada UDP client içerisinde;

```c
socket(AF_INET, SOCK_DGRAM, 0);
```

ile socket oluşturulmasına rağmen client tarafında açık bir `bind()` işlemi gerçekleştirilmemişti.

Buna karşılık server çıktısında;

```text
Received 7 bytes from 127.0.0.1:<source_port>: merhaba
```

şeklinde bir source port görülebildiği tespit edilmişti. Bu davranış üzerinden **ephemeral port** kavramı incelendi.

Ephemeral port, uygulamanın belirli bir local port seçmediği durumlarda kernel tarafından dinamik olarak atanabilen geçici source port olarak değerlendirildi.

Temel akış;

```text
UDP client
    ↓
socket()
    ↓
explicit bind() yok
    ↓
sendto()
    ↓
kernel local port ihtiyacını belirler
    ↓
ephemeral port seçilir
    ↓
UDP datagram gönderilir
```

şeklinde incelendi.

Gerçek testlerde farklı client çalıştırmalarında örneğin;

```text
127.0.0.1:56736
127.0.0.1:44971
```

gibi farklı source port değerleri gözlemlendi.

Böylece her yeni client çalıştırmasının yeni bir socket oluşturabildiği ve kernel'in bu socket için uygun bir ephemeral port seçebildiği görüldü.

## 2. Source IP ile Source Port Seçiminin Ayrıştırılması

Source IP ve source port bilgilerinin aynı mekanizma tarafından belirlenmesi gerekmediği üzerinde duruldu.

Source port açısından;

```text
UDP socket
    ↓
explicit local port yok
    ↓
kernel ephemeral port seçimi
```

gerçekleşebilirken, source IP seçiminin destination ve routing kararıyla ilişkili olduğu değerlendirildi.

Mevcut uygulamada destination;

```text
127.0.0.1:5000
```

olduğu için kernel'in routing sonucunda loopback interface üzerinden iletişim gerçekleştirdiği ve gerçek datagram üzerinde source IPv4 adresi olarak `127.0.0.1` kullanılabildiği görüldü.

Temel ayrım;

```text
Source Port
    ↓
UDP socket / local port seçimi

Source IP
    ↓
destination + routing kararı
```

şeklinde oluşturuldu.

## 3. UDP'de Explicit `bind()` ile Kernel Autobind Davranışının Karşılaştırılması

Server tarafında;

```c
bind(sockfd, ...);
```

kullanılarak local endpoint açıkça;

```text
0.0.0.0:5000
```

olarak belirlenmişti.

Client tarafında ise explicit `bind()` bulunmadığından kernel'in ihtiyaç oluştuğunda local port atayabildiği görüldü.

Bu davranış kavramsal olarak **autobind** mekanizması üzerinden değerlendirildi.

```text
SERVER

socket()
    ↓
bind(0.0.0.0:5000)
    ↓
local port application tarafından seçildi
```

Client tarafında ise;

```text
CLIENT

socket()
    ↓
bind() yok
    ↓
sendto()
    ↓
kernel autobind
    ↓
ephemeral port
```

akışı oluşturuldu.

Client explicit olarak örneğin `40000` portuna bind edilmiş olsaydı kernel'in başka bir ephemeral port seçmesine gerek kalmayacağı değerlendirildi.

## 4. UDP `connect()` Kavramının İncelenmesi

UDP connectionless bir transport protokolü olmasına rağmen Socket API üzerinde UDP socket'ler için `connect()` kullanılabildiği incelendi.

UDP'deki `connect()` işleminin TCP'deki connection establishment mekanizmasıyla aynı anlama gelmediği özellikle ayrıştırıldı.

TCP tarafında;

```text
connect()
    ↓
SYN
    ↓
SYN/ACK
    ↓
ACK
    ↓
ESTABLISHED
```

gibi bir connection establishment süreci bulunurken UDP'de `connect()` çağrısının TCP benzeri three-way handshake oluşturmadığı görüldü.

UDP için temel fikir;

```text
UDP socket
    ↓
connect(remote IP:port)
    ↓
varsayılan peer bilgisi socket ile ilişkilendirilir
```

şeklinde değerlendirildi.

Bu durumda her gönderimde destination bilgisini `sendto()` ile tekrar vermek yerine `send()` gibi bir kullanım mümkün hale gelebilir.

Böylece;

```text
bind()
    ↓
local endpoint

connect()
    ↓
peer / remote endpoint

sendto()
    ↓
destination'ı gönderim sırasında belirtme
```

ayrımı oluşturuldu.

## 5. Network 5-Tuple Kavramının İncelenmesi

Bir network flow veya connection'ın tanımlanmasında sık kullanılan **5-tuple** kavramı incelendi.

5-tuple;

```text
Protocol
Source IP
Source Port
Destination IP
Destination Port
```

bilgilerinden oluşmaktadır.

Mevcut UDP uygulaması için örnek;

```text
Protocol         = UDP
Source IP        = 127.0.0.1
Source Port      = 44971
Destination IP   = 127.0.0.1
Destination Port = 5000
```

şeklinde değerlendirildi.

Bu bilgiler;

```text
{ UDP,
  127.0.0.1,
  44971,
  127.0.0.1,
  5000 }
```

şeklinde bir flow'u tanımlamak için kullanılabilir.

5-tuple kavramının ileride;

* socket lookup,
* NAT,
* conntrack,
* firewall,
* packet capture,
* load balancing,
* eBPF/XDP

gibi network mekanizmalarında tekrar karşılaşılacak önemli bir temel olduğu görüldü.

## 6. Socket Lookup Kavramının İncelenmesi

**Lookup** teriminin verilen bir key/bilgi kullanılarak uygun kayıt veya kernel nesnesinin bulunması anlamına geldiği incelendi.

Networking içerisinde farklı lookup örnekleri karşılaştırıldı:

```text
Routing lookup

Destination IP
    ↓
routing table
    ↓
uygun route/interface
```

```text
Neighbor lookup

Next-hop IP
    ↓
neighbor table
    ↓
MAC address
```

```text
UDP socket lookup

packet endpoint bilgileri
    ↓
kernel UDP socket yapıları
    ↓
uygun socket
```

Gelen UDP datagramı için;

```text
Source      = 127.0.0.1:44971
Destination = 127.0.0.1:5000
Protocol    = UDP
```

bilgileri bulunduğunda kernel'in datagramı teslim edebileceği uygun UDP socket'i belirlemesi gerektiği görüldü.

Mevcut server socket'i;

```text
UDP
Local address = 0.0.0.0
Local port    = 5000
```

olarak bind edildiğinden `127.0.0.1:5000` hedefine gelen uygun datagramların wildcard bind nedeniyle bu socket ile eşleşebildiği değerlendirildi.

## 7. Kernel'in Socket Lookup İçin Process'leri Tek Tek Aramadığının Değerlendirilmesi

Kernel'in gelen her paket için bütün process'leri ve bunların bütün file descriptor'larını sırayla dolaşarak socket araması şeklinde basit bir model kullanılmadığı ele alındı.

Bunun yerine networking subsystem içerisinde socket lookup işlemini verimli gerçekleştirebilmek amacıyla uygun kernel veri yapılarının kullanıldığı değerlendirildi.

Kavramsal model;

```text
Incoming UDP datagram
        ↓
UDP layer
        ↓
socket lookup structures
        ↓
matching UDP socket
```

şeklinde oluşturuldu.

Bu aşamada gerçek kernel hash table implementation detaylarına girilmeden temel lookup mantığının anlaşılması amaçlandı.

## 8. Socket Receive Queue Mekanizmasının İncelenmesi

Socket lookup sonucunda doğru socket bulunduktan sonra paketin doğrudan userspace içerisindeki;

```c
char buffer[BUFFER_SIZE];
```

değişkenine yazılmadığı özellikle incelendi.

Paketin önce kernel networking yapıları içerisinde işlenerek ilgili socket'in receive tarafına ulaştığı değerlendirildi.

Temel akış;

```text
Packet arrival
    ↓
IP
    ↓
UDP
    ↓
socket lookup
    ↓
matching socket
    ↓
socket receive queue
    ↓
recvfrom()
    ↓
userspace buffer
```

şeklinde oluşturuldu.

Böylece **packet arrival** ile application'ın `recvfrom()` çağrısının birbirinden farklı olaylar olduğu görüldü.

## 9. Blocking `recvfrom()` Davranışının İncelenmesi

Mevcut server socket'i blocking modda çalışmaktadır.

Server;

```c
recvfrom(...)
```

çağrısına ulaştığında socket receive queue içerisinde veri yoksa process'in veri gelene kadar bekleyebildiği incelendi.

```text
recvfrom()
    ↓
receive queue kontrolü
    ↓
queue boş
    ↓
process bekler
```

Daha sonra bir UDP datagram geldiğinde;

```text
packet
    ↓
UDP
    ↓
socket lookup
    ↓
receive queue
    ↓
bekleyen process uyandırılabilir
    ↓
recvfrom() devam eder
```

akışı değerlendirildi.

Bu mekanizma daha önce character device driver çalışmalarında incelenen blocking `read()`, wait queue ve wake-up mekanizmalarıyla ilişkilendirildi.

## 10. Character Driver Blocking I/O ile Socket Blocking I/O İlişkisinin Kurulması

Daha önce geliştirilen character driver'da;

```text
read()
    ↓
FIFO boş
    ↓
wait queue
    ↓
process bekler
    ↓
producer veri üretir
    ↓
wake
    ↓
read() devam eder
    ↓
copy_to_user()
```

mantığı incelenmişti.

Network socket tarafında benzer genel işletim sistemi prensibi;

```text
recvfrom()
    ↓
socket receive queue boş
    ↓
process bekler
    ↓
network packet gelir
    ↓
receive queue'ya veri ulaşır
    ↓
process tekrar çalıştırılabilir
    ↓
recvfrom() devam eder
    ↓
payload userspace'e aktarılır
```

şeklinde değerlendirildi.

Böylece character device ve networking subsystem'lerinin farklı yapılar olmasına rağmen blocking I/O açısından ortak Linux kernel prensipleri kullandıkları görüldü.

## 11. Receiver Yavaş Olduğunda UDP Receive Queue Davranışının İncelenmesi

Application'ın gelen UDP datagramlarını yeterince hızlı tüketememesi durumunda socket receive queue/buffer kullanımının artabileceği değerlendirildi.

```text
Arrival rate > Application processing rate
                ↓
        receive queue büyür
                ↓
        buffer sınırına ulaşılır
                ↓
        datagram drop oluşabilir
```

UDP'nin TCP'deki receive-window tabanlı flow-control ve otomatik retransmission mekanizmalarına sahip olmaması nedeniyle buffer taşması durumunda datagram kaybı yaşanabileceği incelendi.

Bu konu dokuzuncu gün incelenen backpressure ve drop kavramlarıyla doğrudan ilişkilendirildi. Önceki raporda UDP'de TCP benzeri receive-window mekanizması bulunmadığı ve buffer overflow durumunda datagram kaybı oluşabileceği ele alınmıştı.

## 12. `ss` ile Kernel Socket Durumlarının Gözlemlenmesi

Ubuntu üzerinde çalışan UDP server gerçek sistemde;

```bash
ss -uanp
```

komutu kullanılarak gözlemlendi.

Server çalışırken aşağıdaki türde bir kayıt elde edildi:

```text
UNCONN  0  0  0.0.0.0:5000  0.0.0.0:*  users:(("udp_server",pid=23297,fd=3))
```

Bu kayıt üzerinden `ss` çıktısındaki alanlar incelendi.

```text
UNCONN
```

socket'in belirli bir UDP peer'a connected durumda olmadığını;

```text
Recv-Q
Send-Q
```

alanlarının socket'in receive/send tarafındaki kuyruk durumlarıyla ilişkili olduğunu;

```text
0.0.0.0:5000
```

alanının local endpoint'i;

```text
0.0.0.0:*
```

alanının ise belirli tek bir peer ile sınırlandırılmamış remote tarafı ifade edebildiği değerlendirildi.

## 13. `ss` Çıktısında PID ve File Descriptor İlişkisinin İncelenmesi

`ss` çıktısında;

```text
users:(("udp_server",pid=23297,fd=3))
```

bilgisi gözlemlendi.

Burada;

```text
udp_server
```

socket'i kullanan process'i,

```text
pid=23297
```

process ID bilgisini,

```text
fd=3
```

ise process içerisindeki socket file descriptor'ını göstermektedir.

Bu bilgi C uygulamasındaki;

```c
int sockfd;

sockfd = socket(AF_INET, SOCK_DGRAM, 0);
```

koduyla ilişkilendirildi.

Kavramsal yapı;

```text
udp_server process
        ↓
PID 23297
        ↓
process file descriptor table
        ↓
fd = 3
        ↓
kernel socket
        ↓
UDP / 0.0.0.0:5000
```

şeklinde değerlendirildi.

Terminalden çalışan basit process'lerde `0`, `1` ve `2` numaralı descriptor'ların genellikle standart input, output ve error için kullanılabilmesi nedeniyle yeni oluşturulan socket'in `3` numaralı descriptor'ı almasının mümkün olduğu; ancak `socket()` çağrısının her zaman `3` döndürmek zorunda olmadığı belirtildi.

## 14. Gerçek Sistemde Diğer UDP Socket'lerin Gözlemlenmesi

`ss -uanp` çıktısında yalnızca geliştirilen UDP server değil, sistemde bulunan diğer UDP socket'ler de gözlemlendi.

Örneğin;

```text
127.0.0.53%lo:53
```

gibi DNS ile ilişkili local socket'ler ve;

```text
10.10.3.47%wlo1:68  →  10.10.2.1:67
```

şeklinde DHCP ile ilişkili UDP endpoint'leri incelendi.

Burada;

```text
lo
```

loopback interface'i,

```text
wlo1
```

ise sistemde kullanılan wireless network interface'i olarak değerlendirildi.

Böylece daha önce yalnızca C kodu üzerinden incelenen socket kavramının gerçek Linux sistemindeki diğer servisler tarafından da aynı temel Socket API/networking modeli içerisinde kullanıldığı görüldü.

## 15. Client Socket'in Gözlemlenebilmesi İçin Yaşam Süresinin Uzatılması

UDP client'ın mevcut hali;

```text
socket()
    ↓
sendto()
    ↓
close()
    ↓
process exit
```

şeklinde çok kısa süre çalıştığından `ss` ile gözlemlenmesi zor oldu.

Bu nedenle client uygulamasına;

```c
printf("Press Enter to close socket...\n");
getchar();
```

eklenerek socket'in `close()` çağrısından önce açık tutulması planlandı.

Doğru sıralamanın;

```text
sendto()
    ↓
printf()
    ↓
getchar()
    ↓
socket açık
    ↓
close(sockfd)
```

olması gerektiği görüldü.

`close(sockfd)` çağrısının `getchar()` öncesinde yapılması halinde process beklemeye devam etse bile socket'in artık kernel tarafında açık olmayacağı fark edildi.

## 16. `getsockname()` ile Client Local Endpoint Bilgisinin Sorgulanması

Client'a kernel tarafından otomatik olarak atanan local endpoint bilgisini doğrudan application içerisinden sorgulamak amacıyla `getsockname()` mekanizması incelendi.

Bunun için;

```c
struct sockaddr_in local_addr;
socklen_t local_addr_len;
```

değişkenleri oluşturuldu.

`server_addr` ile `local_addr` arasındaki kavramsal ayrım;

```text
server_addr
    ↓
Nereye gönderiyorum?
    ↓
remote/destination endpoint


local_addr
    ↓
Benim socket'imin local endpoint'i nedir?
```

şeklinde oluşturuldu.

`sendto()` başarılı olduktan sonra;

```c
local_addr_len = sizeof(local_addr);

getsockname(
    sockfd,
    (struct sockaddr *)&local_addr,
    &local_addr_len
);
```

kullanılarak kernel socket'in local endpoint bilgisinin userspace'e alınabileceği incelendi.

## 17. `getsockname()` Sonucundaki Port ve IP Bilgisinin Dönüştürülmesi

`getsockname()` tarafından elde edilen;

```c
local_addr.sin_port
```

alanı network byte order biçiminde bulunduğundan;

```c
local_port = ntohs(local_addr.sin_port);
```

ile host byte order'a dönüştürüldü.

IPv4 adresinin okunabilir string biçimine dönüştürülmesi için;

```c
inet_ntop(
    AF_INET,
    &local_addr.sin_addr,
    local_ip,
    sizeof(local_ip)
);
```

kullanımı incelendi.

Böylece client'ın local endpoint bilgisinin;

```text
Client local endpoint: <IP>:<ephemeral_port>
```

şeklinde application içerisinden gözlemlenebilmesi amaçlandı.

Bu yöntem ile ileride üç farklı gözlem noktasının karşılaştırılması planlandı:

```text
Client getsockname()
        ↓
kernel'in client socket için tuttuğu local endpoint


Server recvfrom()
        ↓
server'ın datagramın sender'ı olarak gördüğü endpoint


ss
        ↓
kernel socket durumunun sistem genelindeki görünümü
```

## 18. Socket Local Binding State ile Gerçek Paket Source Address Ayrımının İncelenmesi

Client belirli bir local IPv4 adresine explicit olarak bind edilmediğinde socket'in local binding durumu ile gerçek gönderilen datagram üzerindeki source address'in kavramsal olarak ayrılması gerektiği incelendi.

Temel model;

```text
Socket local state
0.0.0.0:<ephemeral_port>

            ≠

Gerçek gönderilen datagram
127.0.0.1:<ephemeral_port>
        ↓
127.0.0.1:5000
```

şeklinde değerlendirildi.

Böylece wildcard/unbound local address durumu ile route sonucunda gerçek paket için seçilen source IP bilgisinin aynı kavram olmadığı görüldü.

## 19. NAT Mekanizmasının 5-Tuple ile İlişkisinin İncelenmesi

Ephemeral port ve 5-tuple konularından hareketle NAT mekanizması tekrar değerlendirildi.

Private network içerisinde örneğin;

```text
192.168.1.10:45000
```

endpoint'inden çıkan bir flow'un NAT cihazı üzerinde;

```text
85.100.20.30:62001
```

gibi farklı bir public IP/port kombinasyonuyla temsil edilebileceği incelendi.

Temel fikir;

```text
Private endpoint
192.168.1.10:45000
        ↓
NAT
        ↓
Public endpoint
85.100.20.30:62001
```

şeklinde değerlendirildi.

Böylece birden fazla internal cihazın aynı public IPv4 adresini kullanabilmesinde port ve flow bilgilerinin önemli olduğu tekrar görüldü.

## 20. Conntrack Kavramına Giriş

NAT ile ilişkili olarak **connection tracking (conntrack)** kavramına giriş yapıldı.

Conntrack'in NAT'ın kendisi olmadığı; kernel'in network flow/connection durumlarını takip etmesine yardımcı olan ayrı bir mekanizma olduğu belirtildi.

TCP gibi connection-oriented protokollerin yanında UDP flow'larının da kernel tarafından stateful şekilde takip edilebilmesinin mümkün olduğu değerlendirildi.

Böylece;

```text
UDP connectionless
```

ifadesinin;

```text
kernel UDP trafiği hakkında hiçbir state tutamaz
```

anlamına gelmediği görüldü.

## 21. Firewall Kavramına Giriş

Firewall'ın network trafiğinin kabul edilmesi veya engellenmesi konusunda politika uygulayan mekanizma olduğu incelendi.

Örneğin application;

```text
0.0.0.0:5000
```

üzerinde başarılı şekilde bind edilmiş olsa bile firewall kurallarının dışarıdan gelen UDP/5000 trafiğini drop edebileceği değerlendirildi.

Kavramsal olarak;

```text
Incoming packet
      ↓
firewall policy
   ↙       ↘
ACCEPT     DROP
```

modeli oluşturuldu.

Linux tarafında ileride Netfilter ve nftables konularının bu yapı ile ilişkilendirileceği belirtildi.

## 22. `tcpdump` ve Packet Capture Kavramına Giriş

`tcpdump` aracının network packet'lerini gözlemlemek amacıyla kullanılan bir packet capture/inspection aracı olduğu incelendi.

Mevcut UDP uygulamasının ileride Ubuntu üzerinde;

```text
udp_client
    ↓
sendto()
    ↓
UDP
    ↓
IPv4
    ↓
loopback
```

akışı sırasında gerçek paketlerin capture edilmesi planlandı.

Packet capture üzerinden özellikle;

```text
Source IP
Destination IP
Source Port
Destination Port
UDP Length
UDP Checksum
```

alanlarının incelenmesi hedeflendi.

Bu çalışma, dokuzuncu gün oluşturulan UDP client-server uygulamasının gerçek packet seviyesinde doğrulanmasına yönelik bir sonraki aşama olarak belirlendi.

## 23. eBPF ve XDP Kavramlarına Ön Giriş

Linux networking içerisinde ileride kullanılacak eBPF kavramına temel seviyede giriş yapıldı.

eBPF'nin kernel'in belirli hook noktalarında program çalıştırılmasına ve network trafiğinin gözlemlenmesi veya işlenmesine olanak sağlayan bir teknoloji olduğu değerlendirildi.

Networking açısından kavramsal model;

```text
Network packet
      ↓
kernel
      ↓
eBPF program / hook
      ↓
observation / processing / decision
      ↓
network stack
```

şeklinde oluşturuldu.

XDP'nin ise ileride RX path ve network driver konularıyla ilişkilendirilecek önemli bir eBPF kullanım alanı olduğu belirtildi.

Bu konulara mevcut aşamada implementation seviyesinde girilmedi.

## 24. Load Balancer Kavramına Giriş

Bir servisin tek backend yerine birden fazla server tarafından sunulması durumunda gelen trafiğin uygun backend'lere dağıtılabilmesi için load balancer kullanılabileceği incelendi.

```text
                 ┌──→ Server A
                 │
Clients → Load Balancer ──→ Server B
                 │
                 └──→ Server C
```

temel modeli oluşturuldu.

Load balancing mekanizmalarının ileride 5-tuple, hashing, connection state ve network flow kavramlarıyla ilişkilendirilebileceği değerlendirildi.

## 25. Socket File Descriptor Kavramının Kernel Bilgileriyle İlişkilendirilmesi

Socket'in userspace açısından bir integer file descriptor üzerinden temsil edildiği tekrar değerlendirildi.

```c
int sockfd;

sockfd = socket(AF_INET, SOCK_DGRAM, 0);
```

kullanımı;

```text
Userspace process
       ↓
sockfd
       ↓
process file descriptor table
       ↓
kernel socket ile ilişkili nesneler
```

şeklinde ele alındı.

Bu yapı daha önce character device çalışmalarında kullanılan file descriptor modeliyle ilişkilendirildi.

Regular file, character device, pipe ve socket gibi farklı kaynakların userspace'e file descriptor üzerinden sunulabilmesinin Linux I/O modelinin önemli özelliklerinden biri olduğu değerlendirildi.

Kernel tarafındaki `struct file` ve socket ilişkilerinin ilerleyen çalışmalarda network stack internals konusu içerisinde daha ayrıntılı incelenmesine karar verildi.

# Gün Sonu Değerlendirmesi

Onuncu gün sonunda önceki gün geliştirilen UDP client-server uygulaması yalnızca çalışan bir userspace programı olarak değil, Linux kernel'in socket yönetimi açısından daha ayrıntılı şekilde incelenmiştir.

Client tarafında explicit `bind()` bulunmamasına rağmen kernel'in gerekli olduğunda ephemeral source port atayabildiği görülmüş; source port seçimi ile routing sonucunda source IP seçimi birbirinden ayrıştırılmıştır. UDP `connect()` kullanımının TCP connection establishment mekanizması olmadığı ve UDP socket için peer bilgisinin belirlenmesi amacıyla kullanılabildiği değerlendirilmiştir.

Network flow'ların tanımlanmasında kullanılan protocol, source IP, source port, destination IP ve destination port bilgilerinden oluşan 5-tuple kavramı öğrenilmiş ve bu kavram socket lookup, NAT, conntrack, firewall, load balancing ve ileride incelenecek eBPF/XDP mekanizmalarıyla ilişkilendirilmiştir.

Kernel'in gelen UDP datagramını doğru socket'e ulaştırabilmek için socket lookup gerçekleştirdiği; doğru socket bulunduktan sonra datagramın socket receive queue üzerinden application'a ulaştırıldığı incelenmiştir. Blocking `recvfrom()` davranışı daha önce character driver çalışmalarında öğrenilen wait queue ve blocking `read()` mekanizmalarıyla ilişkilendirilmiştir.

Ubuntu üzerinde `ss -uanp` kullanılarak gerçek UDP server socket'i gözlemlenmiş;

```text
0.0.0.0:5000
pid=23297
fd=3
```

bilgileri üzerinden application'daki `sockfd`, process ID ve kernel socket durumu arasında bağlantı kurulmuştur.

Client socket'in kısa yaşam süresi nedeniyle sistem üzerinde gözlemlenmesinin zor olduğu görülmüş ve socket'i `close()` öncesinde açık tutmak amacıyla `getchar()` kullanılmıştır. Ayrıca kernel tarafından belirlenen local endpoint bilgisini application içerisinden sorgulamak için `getsockname()` mekanizması eklenmiş; `ntohs()` ve `inet_ntop()` kullanılarak local port ve IPv4 adresinin okunabilir hale getirilmesi üzerinde çalışılmıştır.

Günün sonunda oluşturulan temel model;

```text
                    UDP CLIENT

                       socket()
                          ↓
                 explicit bind yok
                          ↓
                       sendto()
                          ↓
                  kernel autobind
                          ↓
                   ephemeral port
                          ↓
                   routing decision
                          ↓
                  source IP seçimi
                          ↓
                    UDP datagram
                          ↓
                 5-tuple bilgileri
                          ↓
                 loopback / network
                          ↓
                       IPv4
                          ↓
                        UDP
                          ↓
                   socket lookup
                          ↓
                 UDP server socket
                          ↓
                  receive queue
                          ↓
              blocking recvfrom()
                          ↓
                 userspace buffer
```

şeklinde oluşturulmuştur.

Bu çalışmalar sonucunda Socket API ile kernel networking subsystem arasındaki bağlantı daha ayrıntılı hale getirilmiş ve bir sonraki aşamada `tcpdump`/Wireshark kullanılarak oluşturulan UDP datagramlarının gerçek packet seviyesinde incelenmesi, loopback ile fiziksel network interface capture arasındaki farkların gözlemlenmesi ve Ethernet/IP/UDP header yapılarının gerçek trafik üzerinde analiz edilmesi için gerekli temel hazırlanmıştır.
