STAJ RAPORU – 24. GÜN
Çok İstemcili TCP Server Tasarımına Geçiş, Socket Oluşturma Adımları ve Cisco Packet Tracer Üzerinde LAN–Switch–Router Topolojisinin Kurulması

Yirmi üçüncü gün sonunda TCP server execution modeli, blocking socket davranışı, listening ve connected socket ayrımı, TCP 4-tuple, cumulative ACK, duplicate ACK, Fast Retransmit ve rwnd/cwnd ilişkileri ayrıntılı olarak incelenmişti. Özellikle socket readiness, TCP connection identity ve loss/window mekanizmalarının gerçek çalışma senaryoları üzerinden birbirleriyle ilişkilendirilmesi sağlanmıştı.

Bugünkü çalışmada bu teorik altyapının uygulamaya aktarılmasına başlandı. İlk olarak birden fazla kullanıcının merkezi bir TCP server üzerinden haberleşeceği sistemin temel yapısı değerlendirildi. Ardından Linux üzerinde TCP server implementasyonunun ilk adımları incelendi. Çalışmanın ikinci bölümünde ise socket programlamasında kullanılan IP adresi, interface, port ve routing kavramlarının ağ üzerindeki fiziksel karşılığını daha iyi anlamak amacıyla Cisco Packet Tracer üzerinde iki LAN'dan oluşan bir network topolojisi kuruldu.

1. Çok İstemcili TCP Server Senaryosunun Belirlenmesi

Bugünkü çalışmanın ilk bölümünde birden fazla client'ın merkezi bir server üzerinden haberleşeceği yapı ele alındı.

Örnek olarak:

Ahmet ───┐
Mehmet ──┤
Hasan ───┼──► SERVER
Hüseyin ─┘

şeklinde bir sistem düşünüldü.

Buradaki temel amaç, her kullanıcının server'a TCP connection oluşturması ve kullanıcılar arasındaki iletişimin server üzerinden gerçekleştirilmesidir.

Kavramsal olarak:

Client A ──TCP──┐
                │
Client B ──TCP──┤
                ├──► SERVER
Client C ──TCP──┤
                │
Client D ──TCP──┘

yapısı bulunmaktadır.

Bu model, önceki gün incelenen:

Listening Socket
       │
       ├── accept() → Connected Socket A
       ├── accept() → Connected Socket B
       ├── accept() → Connected Socket C
       └── accept() → Connected Socket D

mekanizmasının uygulamadaki karşılığını oluşturmaktadır.

Önceki çalışmada accept() sonrasında listening socket'in çalışmaya devam ettiği ve her client için ayrı connected socket oluştuğu teorik olarak incelenmişti.

Bugün bu yapının gerçek bir server uygulamasına dönüştürülmesine başlanmıştır.

2. TCP Server Implementasyonunun Başlatılması

Server implementasyonuna doğrudan bütün kod yazılarak değil, socket lifecycle içerisindeki her adımın sistemde ne değiştirdiği incelenerek başlandı.

Temel TCP server akışı:

socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
   ↓
recv() / send()

şeklinde ele alındı.

İlk olarak gerekli değişkenler oluşturuldu:

int server_fd;
struct sockaddr_in server_addr;

Burada:

server_fd

kernel içerisinde oluşturulacak socket'e userspace tarafından erişmek için kullanılan file descriptor olarak değerlendirildi.

server_addr ise socket'in bağlanacağı IPv4 adres ve port bilgisini taşıyan yapı olarak ele alındı.

3. socket() Sistem Çağrısının İncelenmesi

Server socket'i:

server_fd = socket(AF_INET, SOCK_STREAM, 0);

ile oluşturuldu.

Parametrelerin anlamları ayrı ayrı değerlendirildi:

AF_INET
   ↓
IPv4 address family

SOCK_STREAM
   ↓
Stream-oriented socket
   ↓
TCP

0
   ↓
Seçilen family/type kombinasyonuna
uygun varsayılan protocol

Bu noktada önemli bir ayrım yapıldı.

socket() çağrısından hemen sonra:

Socket oluşturuldu       ✅
File descriptor alındı   ✅

IP adresi atandı          ❌
Port atandı               ❌
Listening başladı         ❌
Client bağlandı           ❌

durumundadır.

Dolayısıyla socket() çağrısının tek başına bir TCP server oluşturmadığı, yalnızca sonraki network işlemlerinde kullanılacak socket endpoint'inin temelini oluşturduğu pekiştirildi.

4. sockaddr_in Yapısının Hazırlanması

Server'ın network adres bilgisini tutmak için:

struct sockaddr_in server_addr;

kullanıldı.

Yapının önce temizlenmesi:

memset(&server_addr, 0, sizeof(server_addr));

ile gerçekleştirildi.

Daha sonra:

server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(PORT);
server_addr.sin_addr.s_addr = INADDR_ANY;

alanları incelendi.

Kavramsal olarak:

server_addr
│
├── sin_family
│      └── IPv4
│
├── sin_port
│      └── Server port
│
└── sin_addr
       └── Server'ın kabul edeceği local address

şeklinde değerlendirildi.

5. htons() ve Network Byte Order

Port atanırken:

server_addr.sin_port = htons(PORT);

kullanılmasının sebebi incelendi.

Network protokollerinde çok byte'lı integer değerler network byte order biçiminde taşınmaktadır.

Bu nedenle:

Host üzerindeki PORT
        │
        │ htons()
        ▼
Network byte order

dönüşümü gerçekleştirilmektedir.

Burada htons ifadesi:

host
to
network
short

olarak açıldı.

Böylece socket API içerisinde port değerinin yalnız sayısal olarak atanmasının değil, beklenen byte order biçimine dönüştürülmesinin de gerekli olduğu tekrar edildi.

6. INADDR_ANY Kavramı

Server adresi için:

server_addr.sin_addr.s_addr = INADDR_ANY;

kullanımı değerlendirildi.

INADDR_ANY, server'ın yalnız belirli tek bir local interface IP'sine bağlanması yerine uygun local IPv4 interface'leri üzerinden bağlantı kabul edebilmesine olanak sağlayan wildcard address olarak ele alındı.

Örneğin bir Linux sisteminde:

lo      → 127.0.0.1
eth0    → 192.168.1.50
wlan0   → 192.168.1.60

gibi farklı interface'ler bulunabilir.

Bu çalışma, socket programlamasındaki:

IP
Interface
Port
Socket

kavramlarının gerçek network topolojisiyle ilişkisini daha ayrıntılı inceleme ihtiyacını ortaya çıkardı.

Bu nedenle TCP server implementasyonuna kısa süre ara verilerek Cisco Packet Tracer üzerinde network pratiğine geçildi.

7. Cisco Packet Tracer Topolojisinin Oluşturulması

Packet Tracer üzerinde aşağıdaki temel topoloji oluşturuldu:

                    ROUTER
                  Cisco 1941
                  /        \
                 /          \
                /            \
          SWITCH 1          SWITCH 2
           2960               2960
        / / | \             / / | \ \
       / /  |  \           / /  |  \  \
     PC PC  PC  PC        PC PC PC  PC SERVER

Topolojide:

1 Router
2 Switch
8 PC
1 Server

kullanıldı.

Router olarak Cisco 1941, switch olarak Cisco 2960 cihazları tercih edildi.

8. Fiziksel Bağlantıların Oluşturulması

Cihazların Ethernet bağlantılarında temel olarak Copper Straight-Through kablo kullanıldı.

Bağlantılar:

PC       ─── Switch
Server   ─── Switch
Switch   ─── Router

şeklinde oluşturuldu.

Bu aşamada fiziksel network topolojisinin socket programlamasındaki soyut kavramlarla ilişkisi görülmeye başlandı.

Örneğin:

Linux socket programlama

struct sockaddr_in
IP address
port
interface

        ↓

Gerçek network

NIC
Ethernet link
Switch
Router
Subnet

ilişkisi kurulmaya başlandı.

9. Switch ile Router'ın Görevlerinin Ayrılması

Topoloji üzerinden switch ve router'ın görevleri karşılaştırıldı.

Switch temel olarak aynı Layer-2 network içerisindeki cihazların birbirine bağlanmasını sağlamaktadır.

Örneğin:

PC0 ─┐
PC1 ─┤
PC2 ─┼── SWITCH
PC3 ─┘

aynı LAN içerisinde bulunabilir.

Router ise farklı IP network'leri arasında iletişim sağlamaktadır.

Planlanan yapı:

192.168.1.0/24
       │
    Switch 1
       │
      G0/0
       │
     ROUTER
       │
      G0/1
       │
    Switch 2
       │
192.168.2.0/24

şeklindedir.

Böylece iki switch yalnız fiziksel olarak cihazları çoğaltmak için değil, router'ın iki farklı interface'i üzerinden iki ayrı Layer-3 network oluşturmak için kullanılacaktır.

10. Server'ın Switch veya Router'a Bağlanması

Server'ın doğrudan router'a bağlanıp bağlanamayacağı da değerlendirildi.

Teknik olarak:

SERVER ─── ROUTER

bağlantısının mümkün olduğu görüldü.

Ancak bu durumda server tarafının ayrı bir Layer-3 network oluşturabileceği değerlendirildi:

LAN 1 ──┐
        │
        ROUTER ─── SERVER NETWORK
        │
LAN 2 ──┘

Mevcut çalışmada ağ yapısını daha sade tutmak amacıyla server ikinci switch'e bağlandı:

ROUTER
   │
SWITCH 2
   │
 SERVER

Bu sayede server da ikinci LAN içerisindeki normal bir network host'u olarak konumlandırıldı.

11. Cisco Router'ın Başlatılması

Cisco 1941 router açıldığında IOS boot süreci gözlemlendi.

Boot sırasında:

System Bootstrap
      ↓
Hardware / Memory initialization
      ↓
IOS image loading
      ↓
IOS startup
      ↓
System Configuration Dialog

aşamaları görüldü.

Router başlangıçta:

Would you like to enter the initial configuration dialog?

sorusunu yöneltti.

Başlangıç configuration dialog'undan sonra:

Press RETURN to get started!

mesajı görüldü ve CLI'a geçildi.

Sonuçta:

Router>

prompt'una ulaşıldı.

12. Cisco IOS EXEC Modlarının İncelenmesi

Router> prompt'unun Cisco IOS içerisindeki User EXEC Mode olduğu görüldü.

Ardından:

Router> enable

komutu kullanıldı.

Prompt:

Router>

durumundan:

Router#

durumuna geçti.

Bu geçiş:

User EXEC Mode
Router>
     │
     │ enable
     ▼
Privileged EXEC Mode
Router#

şeklinde değerlendirildi.

enable komutunun router'ın network configuration'ını doğrudan değiştirmediği; kullanıcının daha yüksek yetkili EXEC seviyesine geçmesini sağladığı öğrenildi.

13. show ip interface brief Komutu

Router'ın mevcut interface durumunu görmek için:

show ip interface brief

komutu kullanıldı.

Gerçek çıktı:

Interface              IP-Address      OK? Method Status                Protocol
GigabitEthernet0/0     unassigned      YES unset  administratively down down
GigabitEthernet0/1     unassigned      YES unset  administratively down down
Vlan1                  unassigned      YES unset  administratively down down

şeklinde gözlemlendi.

Komut:

show ip interface brief
│    │      │        │
│    │      │        └── özet bilgi
│    │      └────────── interface'ler
│    └───────────────── IP bilgileri
└────────────────────── mevcut durumu göster

şeklinde parçalanarak incelendi.

Bu komutun configuration değiştirmeyen, mevcut durumu gözlemlemeye yönelik bir komut olduğu görüldü.

14. Router Interface'lerinin Başlangıç Durumu

Çıktıda:

GigabitEthernet0/0
GigabitEthernet0/1

interface'lerinin:

IP-Address = unassigned
Status     = administratively down
Protocol   = down

durumunda olduğu görüldü.

unassigned:

Interface'e henüz IP verilmemiş.

anlamına gelmektedir.

administratively down ise fiziksel kablonun mutlaka bozuk olduğu anlamına gelmemektedir.

Bunun yerine:

Interface
    │
    ▼
IOS configuration
    │
    ▼
shutdown
    │
    ▼
administratively down

durumunu göstermektedir.

Bu ayrım Packet Tracer üzerinde router ile switch arasındaki bağlantıların neden başlangıçta kırmızı göründüğünün anlaşılmasını sağladı.

15. Global Configuration Mode

Router üzerinde değişiklik yapmak için:

Router# configure terminal

komutu kullanıldı.

Bunun sonucunda:

Router(config)#

prompt'una geçildi.

Modlar:

Router>
   │
   │ enable
   ▼
Router#
   │
   │ configure terminal
   ▼
Router(config)#

şeklinde ilişkilendirildi.

Burada:

Router#

Privileged EXEC Mode,

Router(config)#

ise Global Configuration Mode olarak değerlendirildi.

16. Interface Configuration Mode

Belirli bir interface üzerinde işlem yapmak için:

interface gigabitEthernet 0/0

komutu kullanıldı.

Bunun sonucunda:

Router(config-if)#

moduna geçildi.

Akış:

Router(config)#
       │
       │ interface gigabitEthernet 0/0
       ▼
Router(config-if)#

şeklindedir.

config-if ifadesi, verilen configuration komutlarının artık seçilen interface üzerinde uygulanacağını göstermektedir.

17. no shutdown ile Interface'in Aktifleştirilmesi

GigabitEthernet0/0 interface'ini yönetimsel olarak aktifleştirmek için:

no shutdown

komutu incelendi.

Cisco IOS içerisinde:

shutdown

interface'i yönetimsel olarak kapatırken:

no shutdown

bu configuration durumunu kaldırarak interface'in aktif hale gelmesini sağlar.

Kavramsal geçiş:

GigabitEthernet0/0

administratively down
        │
        │ no shutdown
        ▼
       up

şeklindedir.

Kablo ve karşı uç uygun durumdaysa interface'in:

Status   = up
Protocol = up

durumuna geçmesi beklenmektedir.

Bu noktada:

up/up

durumunun interface'in çalışabilir hale geldiğini, fakat tek başına IP routing'in hazır olduğu anlamına gelmediği özellikle ayrıldı.

Çünkü:

Interface aktif      ✅
Physical link        ✅
IP address           ❌
Subnet configuration ❌
Routing              ❌

durumunda olunabilir.

18. Bugünkü Katmanlar Arası Bağlantının Kurulması

Bugünkü çalışmanın önemli kazanımlarından biri C socket programlamasında kullanılan kavramların Packet Tracer üzerinde fiziksel ve ağ katmanı karşılıklarının görülmeye başlanması oldu.

C APPLICATION
      │
      ▼
   socket()
      │
      ▼
File Descriptor
      │
      ▼
TCP / UDP
      │
      ▼
IP Address
      │
      ▼
Network Interface
      │
      ▼
     NIC
      │
      ▼
Ethernet
      │
      ▼
    SWITCH
      │
      ▼
    ROUTER
      │
      ▼
Other Subnet

Böylece daha önce kod içerisinde kullanılan:

AF_INET
INADDR_ANY
sockaddr_in

gibi kavramların altında gerçek bir network interface ve IP topolojisi bulunduğu daha somut hale getirildi.

19. 23. Gün → 24. Gün İlerlemesi

Önceki günün ağırlığı:

23. GÜN
────────────────────────

TCP SERVER THEORY
       │
       ├── Blocking recv()
       ├── epoll
       ├── Listening socket
       ├── Connected socket
       ├── TCP 4-Tuple
       ├── Cumulative ACK
       ├── Duplicate ACK
       ├── Fast Retransmit
       ├── rwnd
       └── cwnd

şeklindeydi. Önceki raporda bu zincir userspace server execution modelinden TCP loss recovery ve effective sending limitine kadar birleştirilmişti.

Bugün:

24. GÜN
────────────────────────

THEORY
   │
   ▼
TCP SERVER IMPLEMENTATION
   │
   ├── socket()
   ├── sockaddr_in
   ├── htons()
   └── INADDR_ANY
   │
   ▼
NETWORK FUNDAMENTALS
   │
   ├── Host
   ├── NIC / Interface
   ├── Switch
   ├── Router
   └── Subnet
   │
   ▼
PACKET TRACER
   │
   ├── 2 LAN
   ├── Cisco 2960
   ├── Cisco 1941
   ├── IOS CLI
   └── Interface state

aşamasına geçildi.

Bu geçişle birlikte socket programlama yalnız API çağrıları üzerinden değil, bu çağrıların altında bulunan gerçek network topolojisiyle birlikte değerlendirilmeye başlandı.

Gün Sonu Değerlendirmesi

Yirmi dördüncü gün, önceki gün ayrıntılı olarak incelenen TCP ve socket teorisinin uygulamaya aktarılmasının ilk aşamasına ayrıldı. Öncelikle birden fazla client'ın merkezi bir TCP server'a bağlandığı ve kullanıcılar arasındaki iletişimin server üzerinden gerçekleştirileceği yapı ele alındı.

TCP server implementasyonuna socket() çağrısından başlanarak server_fd, sockaddr_in, AF_INET, SOCK_STREAM, htons() ve INADDR_ANY kavramları incelendi. Her adımda yalnız kodun ne yaptığı değil, kernel/network açısından hangi durumun oluştuğu değerlendirildi.

Ardından socket programlamasında sürekli kullanılan IP address ve interface kavramlarını daha somut hale getirmek amacıyla Cisco Packet Tracer üzerinde iki switch, bir router, sekiz PC ve bir server içeren topoloji oluşturuldu. Switch'in aynı LAN içerisindeki cihazları birbirine bağlama rolü ile router'ın farklı Layer-3 network'leri birbirine bağlama rolü ayrıştırıldı.

Cisco 1941 router'ın boot süreci gözlemlendi ve IOS CLI üzerinde User EXEC ile Privileged EXEC modları arasındaki geçiş uygulandı. show ip interface brief komutu kullanılarak GigabitEthernet interface'lerinin mevcut durumları incelendi. Interface'lerin başlangıçta unassigned ve administratively down/down durumda olduğu görüldü.

Son olarak Global Configuration ve Interface Configuration modlarına geçiş incelendi ve no shutdown komutuyla router interface'inin yönetimsel olarak aktifleştirilmesi ele alındı. Böylece fiziksel link durumu, interface state ve IP configuration kavramlarının birbirinden farklı olduğu görüldü.

Gün sonunda oluşan temel düşünce zinciri:

TCP SERVER
    │
    ▼
 socket()
    │
    ▼
IP / PORT
    │
    ▼
NETWORK INTERFACE
    │
    ▼
ETHERNET
    │
    ▼
 SWITCH
    │
    ▼
 ROUTER
    │
    ▼
DIFFERENT SUBNET

şeklinde oluşturuldu.

Bir sonraki çalışma aşamasında router'ın iki GigabitEthernet interface'ine farklı subnetlerden IP adresleri atanması, PC ve server cihazlarının IP/default gateway yapılandırmalarının yapılması ve aynı LAN ile farklı LAN arasındaki iletişimin ping, ARP ve routing davranışları üzerinden gözlemlenmesi planlanmaktadır.

Bunun ardından Packet Tracer üzerinde oluşturulan ağ modeli ile Linux TCP server uygulaması arasındaki bağlantının tekrar kurulması ve çok istemcili server implementasyonuna bind(), listen() ve accept() aşamaları üzerinden devam edilmesi hedeflenmektedir.
