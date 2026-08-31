# STAJ RAPORU – 23. GÜN

## TCP Server Execution Modeli, Blocking Socket Davranışı, Port–Socket–Connection İlişkisi, TCP 4-Tuple, Cumulative ACK, Duplicate ACK ve rwnd/cwnd Davranışının Derinlemesine İncelenmesi

Yirmi ikinci gün sonunda Linux networking ve TCP konuları yalnız kavramsal tanımlar üzerinden değil, **problem–mekanizma eşleştirmesi** üzerinden değerlendirilmişti. Blocking/non-blocking I/O, `epoll`, TCP loss recovery, receive/congestion window, Netfilter packet path ve Split-TCP/PEP mimarileri aynı sistem akışı içerisinde birleştirilmişti.

Bugünkü çalışmada bu yaklaşım daha spesifik TCP soruları üzerinden sürdürüldü. Özellikle aşağıdaki soruların altında yatan mekanizmalar incelendi:

```text
Bir blocking thread birden fazla socket ile çalışabilir mi?

Bir socket'te data yokken başka socket'te data varsa
thread neden diğer socket'e geçemiyor?

Bir server portunda kaç socket bulunabilir?

16-bit port alanı neden 65.536 connection limiti anlamına gelmiyor?

Bir TCP segmenti kaybolduğu halde sonraki segmentler
neden gönderilmeye devam edebilir?

Receiver neden sürekli aynı ACK'i gönderirken
advertised window değerini yüksek tutabilir?

rwnd yüksekken cwnd neden küçülebilir?

Eksik segment retransmit edildiğinde cumulative ACK
neden bir anda ileri sıçrayabilir?
```

Bu sorular üzerinden socket, thread, TCP connection identity, ACK, receive window ve congestion window arasındaki ilişkiler ayrıntılı olarak pekiştirildi.

---

# 1. Tek Thread'in Birden Fazla Blocking Socket ile Çalışabilmesi

İlk olarak önemli bir yanlış anlaşılma giderildi.

Blocking socket kullanılması:

```text
1 thread = 1 socket
```

anlamına gelmemektedir.

Tek bir thread teknik olarak birden fazla socket üzerinde sırayla işlem yapabilir.

Örneğin:

```c
recv(socket1, buffer, sizeof(buffer), 0);
recv(socket2, buffer, sizeof(buffer), 0);
recv(socket3, buffer, sizeof(buffer), 0);
```

şeklinde bir kod mümkündür.

Execution flow:

```text
THREAD
  │
  ▼
recv(socket1)
  │
  ▼
recv(socket2)
  │
  ▼
recv(socket3)
```

şeklindedir.

Buradaki problem socket sayısı değildir.

Problem, herhangi bir blocking I/O çağrısının thread'in ilerlemesini durdurabilmesidir.

---

# 2. Blocking recv() Çağrısında Data Bulunmaması

Örneğin:

```text
socket1 → data yok
socket2 → data var
socket3 → data var
```

olsun.

Thread önce:

```c
recv(socket1, ...);
```

çağrısını yaparsa ve `socket1` üzerinde okunabilecek data bulunmuyorsa:

```text
THREAD
  │
  ▼
recv(socket1)
  │
  ▼
data yok
  │
  ▼
BLOCKED
```

durumu oluşabilir.

Thread `socket1` için beklediğinden:

```text
socket2 → DATA VAR
socket3 → DATA VAR
```

olmasına rağmen aynı execution flow henüz:

```c
recv(socket2, ...);
```

satırına ulaşamaz.

Bu durum:

```text
socket1 data yok
      ↓
thread socket1 üzerinde bekliyor
      ↓
socket2 data hazır
      ↓
AMA
      ↓
aynı thread socket2'yi henüz kontrol edemiyor
```

şeklinde ifade edilebilir.

---

# 3. Blocking Durumda Thread'in CPU'yu Sürekli Tüketmemesi

Blocking `recv()` çağrısının:

```text
while(data_yok)
    sürekli CPU kullan
```

şeklinde çalışmadığı tekrar edildi.

Kavramsal olarak kernel:

```text
recv(socket1)
      ↓
data yok
      ↓
thread waiting/blocking state
      ↓
CPU başka runnable task'a verilebilir
```

davranışı gösterebilir.

Daha sonra socket üzerinde data hazır olduğunda thread yeniden runnable hale getirilebilir.

```text
socket1'e data geldi
        ↓
kernel ilgili beklemeyi sonlandırabilir
        ↓
thread yeniden çalıştırılabilir
        ↓
recv(socket1) tamamlanır
        ↓
execution devam eder
```

Buradaki kritik ayrım:

```text
BLOCKING
≠
CPU'yu sürekli meşgul etmek
```

şeklindedir.

Blocking'in temel problemi:

```text
execution flow'un
belirli I/O'nun tamamlanmasını
beklemesi
```

olarak değerlendirildi.

---

# 4. Başka Socket'te Data Hazır Olsa Bile Neden İşlenemediği

Örnek zaman akışı:

```text
TIME ─────────────────────────────────────────►

socket1:
data yok -------------------------- data geldi
                                    │
                                    ▼

socket2:
------ DATA GELDİ ============================

thread:
recv(socket1)
     │
     └──────── BLOCKED ─────────────┘
                                    │
                                    ▼
                              recv() döndü
                                    │
                                    ▼
                              recv(socket2)
```

Burada `socket2` üzerindeki data uzun süredir kernel buffer'ında hazır olabilir.

Ancak thread:

```text
recv(socket1)
```

çağrısından dönmediği için `socket2` ile ilgilenemez.

Bu örnek blocking server'ın ölçeklenebilirlik problemini daha somut hale getirdi.

---

# 5. epoll_wait() ile Blocking recv() Arasındaki Temel Fark

Bu noktadan tekrar `epoll` mekanizmasına geçildi.

Blocking `recv()`:

```text
"BU SOCKET hazır olana kadar bekle."
```

anlamına gelirken `epoll_wait()`:

```text
"Takip ettiğim socket'lerden
HERHANGİ BİRİ hazır olduğunda
beni uyandır."
```

mantığında çalışmaktadır.

Örneğin:

```text
socket1 → hazır değil
socket2 → hazır
socket3 → hazır değil
```

ise:

```text
socket1 ─┐
socket2 ─┤
socket3 ─┤
...      ├──► epoll
socketN ─┘
            │
            ▼
        socket2 ready
            │
            ▼
          THREAD
            │
            ▼
      recv(socket2)
```

şeklinde yalnız hazır endpoint işlenebilir.

Dolayısıyla iki modelde de thread bekleyebilir.

Ancak beklenen şey farklıdır:

```text
blocking recv(socket1)

→ socket1'i bekle
```

karşısında:

```text
epoll_wait()

→ kayıtlı descriptor'lardan
  herhangi birinin ready
  olmasını bekle
```

ayrımı oluşturuldu.

---

# 6. Port ile Socket Kavramlarının Ayrılması

Bugünün ikinci önemli konusu:

```text
Bir portta kaç socket olabilir?
```

sorusu oldu.

İlk olarak:

```text
PORT ≠ SOCKET
```

ayrımı yapıldı.

TCP/UDP port alanı 16-bit'tir:

```text
Port Number
    │
    ▼
  16 bit
    │
    ▼
2^16 değer
    │
    ▼
65536 farklı port numarası
```

Ancak bu:

```text
Bir portta maksimum 65536 socket olabilir.
```

anlamına gelmemektedir.

`2^16` değeri **port numarası alanının büyüklüğüdür**, socket sayısının doğrudan limiti değildir.

---

# 7. Listening Socket ve Connected Socket Ayrımı

Örneğin TCP server:

```text
192.168.1.10:5001
```

üzerinde çalışsın.

İlk olarak:

```c
socket();
bind();
listen();
```

sonrasında bir listening socket bulunmaktadır:

```text
SERVER

listen_fd
   │
   └── :5001
```

Client connection geldiğinde:

```c
client_fd = accept(listen_fd, ...);
```

ile yeni connected socket oluşturulur.

Önemli nokta:

```text
accept()
```

listening socket'i connected socket'e dönüştürmez.

Listening socket çalışmaya devam eder.

```text
                     :5001

                  listen_fd
                     │
          ┌──────────┼──────────┐
          │          │          │
          ▼          ▼          ▼
       socket A   socket B   socket C
```

Connected socket'lerin server tarafındaki local port'u aynı olabilir:

```text
socket A local port = 5001
socket B local port = 5001
socket C local port = 5001
```

Buna rağmen bunlar farklı TCP connection'lardır.

---

# 8. Aynı Server Portunda Çok Sayıda TCP Connection

Örneğin:

```text
10.0.0.1:40001 ───► 192.168.1.10:5001

10.0.0.2:51022 ───► 192.168.1.10:5001

10.0.0.3:37891 ───► 192.168.1.10:5001

10.0.0.4:60000 ───► 192.168.1.10:5001
```

connection'larının tamamında:

```text
Destination Port = 5001
```

olmasına rağmen connection'lar birbirinden farklıdır.

Bunun sebebi TCP connection identity'nin yalnız port üzerinden belirlenmemesidir.

---

# 9. TCP 4-Tuple Yapısının Connection Sayısıyla İlişkisi

Bir TCP connection temel olarak:

```text
┌────────────────────────────┐
│ Source IP                  │
│ Source Port                │
│ Destination IP             │
│ Destination Port           │
└────────────────────────────┘
```

kombinasyonuyla ayrılır.

Yani:

```text
TCP Connection Identity

Source IP
   +
Source Port
   +
Destination IP
   +
Destination Port
```

şeklindedir.

Örneğin:

```text
Connection A:

10.0.0.1:40000
        ↓
192.168.1.10:5001
```

ve:

```text
Connection B:

10.0.0.2:40000
        ↓
192.168.1.10:5001
```

aynı source port ve aynı destination port kullanılmasına rağmen farklı source IP nedeniyle farklı TCP connection'lardır.

---

# 10. 65.536 Port ile 65.536 Connection Yanılgısı

Bugün özellikle:

```text
TCP port alanı 16-bit
        ↓
2^16 = 65536
        ↓
maximum 65536 TCP connection
```

çıkarımının neden yanlış olduğu incelendi.

Doğru düşünme:

```text
2^16
  ↓
Port Number field'ın
alabileceği değer sayısı
```

şeklindedir.

Connection ise:

```text
(src IP, src port, dst IP, dst port)
```

kombinasyonuyla ayrılır.

Dolayısıyla:

```text
65536 port
    ≠
65536 socket
    ≠
65536 global TCP connection limiti
```

sonucuna ulaşıldı.

---

# 11. Tek Source IP → Tek Destination IP:Port Özel Durumu

Bununla birlikte 65.536 sayısının neden connection tartışmalarında ortaya çıktığı da incelendi.

Şu değerler sabit olsun:

```text
Source IP       = 10.0.0.1
Destination IP  = 192.168.1.10
Destination Port = 5001
```

Bu durumda:

```text
10.0.0.1:???? → 192.168.1.10:5001
```

connection'larını ayırmak için değiştirilebilecek temel tuple elemanı source port olmaktadır.

Source port 16-bit olduğu için teorik tuple alanı açısından:

```text
Source IP sabit
Destination IP sabit
Destination Port sabit
          ↓
Source Port değişiyor
          ↓
16-bit alan
          ↓
yaklaşık 2^16 olası değer
```

düşünülebilir.

Ancak gerçek sistemde:

```text
ephemeral port range
mevcut socket'ler
TIME_WAIT
reserved ports
OS configuration
```

gibi etkenler nedeniyle kullanılabilir sayı teorik 65.536 değerinden daha düşük olabilir.

Dolayısıyla:

```text
"Bir portta 2^16 socket olur."
```

ifadesinin genel bir TCP kuralı olmadığı görüldü.

---

# 12. TCP ACK Semantiğinin Yeniden İncelenmesi

Bugünün üçüncü önemli konusu TCP ACK davranışı oldu.

ACK değerinin:

```text
"aldığım son segment"
```

anlamına gelmediği tekrar edildi.

TCP ACK:

```text
ACK = next expected byte
```

mantığıyla değerlendirilmelidir.

Basitleştirilmiş segment numaraları kullanılarak:

```text
1
2
3
4
5
6
...
```

şeklinde bir örnek oluşturuldu.

Burada `3` numaralı data kaybolmuş olsun.

```text
Sender                       Receiver

1 ─────────────────────────►
2 ─────────────────────────►
3 ─────────── X
```

Receiver:

```text
1 ✅
2 ✅
3 ❌
```

durumunda olduğundan:

```text
ACK 3
```

gönderir.

Anlamı:

```text
"3 numaralı datayı bekliyorum."
```

şeklindedir.

---

# 13. Eksik Segmentten Sonraki Datanın Gelmesi

Sender'ın window'u yeni data göndermeye izin veriyorsa sonraki segmentler gönderilmeye devam edebilir.

Örneğin:

```text
4 ─────────────────────────►
5 ─────────────────────────►
6 ─────────────────────────►
```

Receiver tarafında:

```text
1 ✅
2 ✅
3 ❌
4 ✅
5 ✅
6 ✅
```

durumu oluşabilir.

Ancak contiguous byte stream hâlâ `3` noktasında kesiktir.

Bu nedenle receiver:

```text
4 geldi → ACK 3
5 geldi → ACK 3
6 geldi → ACK 3
```

gönderebilir.

---

# 14. Duplicate ACK'in Oluşumu

Aynı next-expected değerinin tekrar tekrar gönderilmesi:

```text
ACK 3
ACK 3
ACK 3
ACK 3
```

duplicate ACK davranışını oluşturabilir.

Bu pattern sender açısından önemli bilgi taşımaktadır.

```text
Sonraki data receiver'a ulaşıyor
             +
ACK ilerlemiyor
             ↓
Arada eksik data olabilir
```

şeklinde yorumlanabilir.

Böylece sender yalnız RTO'ya bağımlı kalmadan kayıp hakkında daha erken sinyal elde edebilir.

---

# 15. Fast Retransmit ile Eksik Segmentin Yeniden Gönderilmesi

Yeterli duplicate ACK pattern'i loss indication oluşturduğunda sender eksik datayı yeniden gönderebilir.

```text
ACK 3
ACK 3
ACK 3
     ↓
Loss indication
     ↓
Fast Retransmit
     ↓
3 yeniden gönder
```

Akış:

```text
Sender                         Receiver

3 RETRANSMIT ────────────────►
```

şeklinde devam eder.

Bu sırada sender'ın yeni data gönderme davranışının yalnız `rwnd` tarafından belirlenmediği özellikle incelendi.

---

# 16. Receiver'ın Out-of-Order Data Tutabilmesi

Receiver'ın:

```text
3 ❌
4 ✅
5 ✅
6 ✅
7 ✅
8 ✅
9 ✅
```

şeklindeki data durumunda sonraki segmentleri doğrudan çöpe atmak zorunda olmadığı değerlendirildi.

TCP implementation'ı uygun koşullarda out-of-order datayı receive buffer içerisinde tutabilir.

Kavramsal olarak:

```text
Receive side

1 2 [HOLE] 4 5 6 7 8 9
      ↑
      3 eksik
```

şeklinde bir yapı oluşabilir.

Application'a contiguous olarak teslim edilebilecek stream ise:

```text
1 2
```

noktasında kalır.

Çünkü:

```text
3
```

henüz eksiktir.

---

# 17. ACK 3 Kalırken Advertised Window'un Yüksek Olabilmesi

Bugünkü önemli sorulardan biri:

> Receiver sürekli ACK 3 gönderiyorsa neden Window Size hâlâ yüksek olabilir?

şeklindeydi.

Burada ACK ve receive window'un iki farklı bilgiyi ifade ettiği görüldü.

```text
ACK = 3
```

şu anlama gelir:

```text
"Contiguous stream açısından
sıradaki beklediğim data 3."
```

Buna karşılık:

```text
rwnd = yüksek
```

şu anlama gelir:

```text
"Receive tarafında hâlâ
kullanılabilir buffer kapasitem var."
```

Dolayısıyla:

```text
ACK ilerlemiyor
```

ile:

```text
Receiver buffer tamamen dolu
```

aynı şey değildir.

---

# 18. rwnd'nin Gerçek Görevi

`rwnd`:

```text
Receive Window
```

receiver tarafından advertised edilen **flow-control limitidir**.

Amaç:

```text
Sender receiver'ın
kaldırabileceğinden fazla
data göndermesin.
```

şeklindedir.

Kavramsal olarak:

```text
Receiver Buffer

┌────────────────────────────────────┐
│ kullanılan │       boş alan        │
└────────────────────────────────────┘
                   ↑
                   │
                  rwnd
```

şeklinde düşünülebilir.

Out-of-order segmentler buffer'da yer kapladıkça kullanılabilir kapasite azalabilir.

Ancak buffer yeterince büyükse:

```text
3 eksik

4 5 6 7 8 9 buffer'da

ve hâlâ büyük boş alan var
```

durumunda receiver yüksek bir advertised window bildirmeye devam edebilir.

---

# 19. rwnd ile ACK'in Birbirinden Bağımsız Anlamları

Bugün şu ayrım özellikle oluşturuldu:

```text
ACK
 │
 └── Byte stream'de
     hangi byte'ı sırada bekliyorum?


rwnd
 │
 └── Daha ne kadar receive
     kapasitem var?
```

Dolayısıyla:

```text
ACK = 3
rwnd = 60 KB
```

gibi bir durum tamamen mümkündür.

Bunun anlamı:

```text
"3 hâlâ eksik,
ama daha fazla data kabul edebilecek
receive kapasitem bulunuyor."
```

şeklindedir.

---

# 20. cwnd'nin Receiver Tarafından Gönderilmemesi

`cwnd` ile `rwnd` arasındaki önemli fark tekrar edildi.

Receiver TCP header içerisinde advertised receive window bilgisini gönderebilir.

Ancak:

```text
cwnd
```

receiver'ın sender'a gönderdiği bir TCP header field değildir.

`cwnd`:

```text
Congestion Window
```

sender'ın kendi congestion-control state'idir.

```text
RECEIVER
   │
   ├── ACK
   │
   └── advertised rwnd
            │
            ▼
          SENDER
            │
            └── kendi cwnd state'i
```

şeklinde düşünülebilir.

---

# 21. rwnd Yüksekken cwnd'nin Düşebilmesi

Bugünkü senaryoda receiver:

```text
"Benim buffer kapasitem var."
```

dediği için:

```text
rwnd yüksek
```

olabilir.

Ancak sender:

```text
duplicate ACK
duplicate ACK
duplicate ACK
```

gördüğünde network üzerinde loss olduğuna ilişkin congestion signal elde edebilir.

Bu durumda congestion-control mekanizması:

```text
Loss indication
      ↓
Congestion recovery
      ↓
cwnd davranışı değişir
```

şeklinde tepki verebilir.

Dolayısıyla aynı anda:

```text
rwnd → yüksek

cwnd → loss/recovery nedeniyle
       daha kısıtlayıcı hale gelebilir
```

durumu mümkündür.

Bu örnek flow control ile congestion control arasındaki farkı çok net göstermektedir.

---

# 22. Flow Control ve Congestion Control Ayrımı

İki window şu sorulara cevap vermektedir:

```text
rwnd:

RECEIVER ne kadar data
kaldırabilir?
```

karşısında:

```text
cwnd:

NETWORK koşullarına göre
sender ne kadar data'yı
in-flight tutmalı?
```

Bu nedenle:

```text
                SENDER
                   │
          ┌────────┴────────┐
          │                 │
         rwnd              cwnd
          │                 │
          ▼                 ▼
     Receiver limiti   Network limiti
          │                 │
          └────────┬────────┘
                   ▼
        Effective sending limit
                   ≈
             min(rwnd,cwnd)
```

şeklinde değerlendirilir.

Receiver'ın çok büyük buffer'ı olması network'te congestion olmadığı anlamına gelmez.

---

# 23. Eksik Segment Geldiğinde Cumulative ACK'in Sıçraması

Receiver'ın elinde:

```text
1 ✅
2 ✅
3 ❌
4 ✅
5 ✅
6 ✅
7 ✅
8 ✅
9 ✅
```

bulunduğu için ACK:

```text
ACK 3
```

seviyesinde kalmış olsun.

Daha sonra retransmitted `3` ulaşır:

```text
3 RETRANSMIT ─────────────►
```

Artık:

```text
1 ✅
2 ✅
3 ✅
4 ✅
5 ✅
6 ✅
7 ✅
8 ✅
9 ✅
```

haline gelir.

Aradaki hole kapanmıştır.

Receiver artık:

```text
ACK 10
```

gönderebilir.

Akış:

```text
ACK 3
ACK 3
ACK 3
ACK 3
   │
   ▼
3 retransmitted
   │
   ▼
ACK 10
```

şeklinde olabilir.

Bu davranış TCP'nin **cumulative ACK** mekanizmasının doğal sonucudur.

---

# 24. Retransmission Sırasında Yeni Data Gönderilip Gönderilemeyeceği

Bir diğer önemli soru:

> 3 kaybolduysa sender 3'ü retransmit edip ardından 10 ve 11'i de gönderir mi?

şeklindeydi.

Bunun sabit bir:

```text
3 retransmit
10 gönder
11 gönder
```

kuralı olmadığı görüldü.

Yeni data gönderme kararı:

```text
rwnd
+
cwnd
+
bytes in flight
+
congestion recovery state
```

ile ilişkilidir.

Kavramsal olarak:

```text
3 kayıp
   ↓
Duplicate ACK
   ↓
Fast Retransmit
   ↓
3 yeniden gönder
   ↓
Gönderim kapasitesi var mı?
   │
   ├── EVET → yeni data gönderilebilir
   │
   └── HAYIR → ACK/window/recovery ilerlemesi beklenebilir
```

şeklinde değerlendirilmelidir.

---

# 25. “Bir Segment Kayıpsa Sender Tamamen Durur” Yanılgısı

Bugün düzeltilen önemli düşüncelerden biri de:

```text
Bir segment kayboldu
       ↓
Sender tamamen durur
       ↓
Sadece eksik segmenti bekler
```

şeklindeki model oldu.

TCP window mekanizması sayesinde sender'ın birden fazla byte/segmenti aynı anda in-flight tutabilmesi mümkündür.

Bu nedenle:

```text
1
2
3 X
4
5
6
7
8
9
```

gibi bir durum oluşabilir.

Receiver sonraki datayı alırken:

```text
ACK 3
ACK 3
ACK 3
...
```

gönderebilir.

Sender da congestion-control/recovery kuralları çerçevesinde hem eksik datayı retransmit eder hem de izin varsa yeni data göndermeye devam edebilir.

---

# 26. Bugünkü TCP Loss Senaryosunun Bütünsel Akışı

Bugün incelenen senaryo tek akışta aşağıdaki şekilde birleştirildi:

```text
SENDER                                  RECEIVER

1 ────────────────────────────────────►
2 ────────────────────────────────────►
3 ─────────────── X

                                  ◄──── ACK 3


4 ────────────────────────────────────►
                                  ◄──── ACK 3

5 ────────────────────────────────────►
                                  ◄──── ACK 3

6 ────────────────────────────────────►
                                  ◄──── ACK 3

                                      Duplicate ACK
                                           ↓
                                      Loss indication


3 RETRANSMIT ─────────────────────────►

Receiver:
1 2 3 4 5 6 contiguous
        ↓
                                  ◄──── ACK 7
```

Eğer receiver daha önce `7,8,9` segmentlerini de buffer'lamışsa:

```text
1 2 [3 X] 4 5 6 7 8 9
```

retransmitted `3` geldikten sonra:

```text
1 2 3 4 5 6 7 8 9
```

oluşur ve:

```text
ACK 10
```

seviyesine ilerlenebilir.

---

# 27. Bugünkü Problem–Mekanizma Eşleştirmeleri

Bugün ele alınan sorular aşağıdaki mekanizmalarla eşleştirildi:

```text
Bir blocking socket'te data yok,
başka socket'te data var
        ↓
Thread ilk recv() içinde bekliyor
        ↓
Blocking I/O problemi
        ↓
Non-blocking + readiness mechanism
        ↓
epoll
```

```text
Bir server portunda çok fazla
client connection var
        ↓
"Bir port = bir socket" değil
        ↓
Listening socket + connected sockets
        ↓
TCP 4-tuple
```

```text
Port 16-bit,
ama server 65k'dan fazla
connection yönetebiliyor
        ↓
2^16 port-number space
        ≠
global connection limit
        ↓
4-tuple connection identity
```

```text
Aradaki segment kayıp,
sonraki segmentler geliyor
        ↓
ACK ilerlemiyor
        ↓
Duplicate ACK
        ↓
Loss indication
        ↓
Fast Retransmit
```

```text
ACK ilerlemiyor,
ama Window Size yüksek
        ↓
ACK ve rwnd farklı bilgi
        ↓
ACK = next expected byte
rwnd = available receive capacity
```

```text
Receiver'ın buffer'ı geniş
ama sender yavaşlıyor
        ↓
rwnd yüksek olabilir
        ↓
Loss/congestion signal
        ↓
cwnd kısıtlayıcı olabilir
```

---

# 28. 22. Gün → 23. Gün İlerlemesi

Önceki gün oluşturulan yaklaşım:

```text
PROBLEM
   ↓
HANGİ KATMAN?
   ↓
SEBEP
   ↓
MEKANİZMA
   ↓
ÇÖZÜM
   ↓
TRADE-OFF
```

bugün daha spesifik TCP soruları üzerinde uygulandı.

```text
22. GÜN
────────────────────────

GENEL PROBLEM–MEKANİZMA MODELİ

Blocking / Non-blocking
epoll
TCP Loss Recovery
rwnd / cwnd
BDP
Netfilter
Split TCP / PEP

             ↓

23. GÜN
────────────────────────

SPESİFİK TCP DAVRANIŞLARI

Thread
  ↓
Blocking recv
  ↓
Socket readiness
  ↓
epoll

Port
  ↓
Listening socket
  ↓
Connected sockets
  ↓
4-Tuple
  ↓
Connection identity

TCP Stream
  ↓
Missing data
  ↓
Cumulative ACK
  ↓
Duplicate ACK
  ↓
Fast Retransmit
  ↓
Out-of-order buffering

Flow Control
  ↓
rwnd

Congestion Control
  ↓
cwnd

        ↓

Effective sending behavior
```

Böylece önceki gün oluşturulan problem çözme yaklaşımı daha ayrıntılı TCP senaryolarına uygulanmış oldu.

---

# 29. Gün Sonunda Oluşturulan Bütünsel Model

Bugün server execution modelinden TCP congestion control'e kadar olan konular aşağıdaki zincir üzerinde birleştirildi:

```text
APPLICATION
     │
     ▼
   THREAD
     │
     ▼
   SOCKET
     │
     ├── blocking
     │      ↓
     │   recv() waiting
     │
     └── non-blocking
            ↓
          EAGAIN
            ↓
          epoll

──────────────────────────────

SERVER
     │
     ▼
listen socket :5001
     │
     ├── accept → connected socket A
     ├── accept → connected socket B
     ├── accept → connected socket C
     └── ...

             ↓

TCP 4-TUPLE

src IP
src port
dst IP
dst port

──────────────────────────────

TCP DATA TRANSFER
     │
     ├── SEQ
     │
     ├── ACK
     │
     ├── cumulative ACK
     │
     ├── duplicate ACK
     │
     ├── Fast Retransmit
     │
     └── out-of-order buffering
             │
             ▼
       LOSS RECOVERY

──────────────────────────────

SENDING LIMIT
     │
     ├── rwnd
     │     ↓
     │   Receiver capacity
     │
     └── cwnd
           ↓
         Network congestion
             │
             ▼
       min(rwnd,cwnd)
```

Bu zincir sayesinde server tarafındaki userspace execution modeli ile TCP'nin kernel/network tarafındaki reliability ve flow/congestion-control mekanizmaları aynı bütün içerisinde değerlendirildi.

---

# Gün Sonu Değerlendirmesi

Yirmi üçüncü gün, önceki gün oluşturulan problem–mekanizma eşleştirme yaklaşımının daha spesifik TCP ve socket soruları üzerinde uygulanmasına ayrıldı.

İlk olarak tek thread'in birden fazla blocking socket üzerinde teknik olarak sırayla çalışabileceği, ancak herhangi bir socket üzerindeki blocking `recv()` çağrısının data bulunmadığında execution flow'u durdurabileceği netleştirildi. Başka socket'lerde data hazır olsa dahi aynı thread'in blocking çağrıdan dönmeden bu socket'lere ulaşamayacağı görüldü. Blocking durumda thread'in CPU üzerinde sürekli busy-loop yapmasının gerekmediği, kernel tarafından bekleme durumuna alınabileceği tekrar edildi.

Bu davranış üzerinden `epoll` mekanizmasının çözdüğü problem yeniden değerlendirildi. Blocking `recv()` belirli bir socket'in hazır olmasını beklerken `epoll_wait()` kayıtlı descriptor'lardan herhangi birinin hazır hale gelmesini bekleyebilmektedir. Böylece çok sayıda connection tek veya az sayıda thread ile daha verimli biçimde yönetilebilmektedir.

İkinci bölümde port, socket ve TCP connection kavramları ayrıştırıldı. TCP port numarasının 16-bit olması nedeniyle `2^16 = 65536` farklı port değeri bulunmasının bir server portunda yalnız 65.536 socket bulunabileceği anlamına gelmediği görüldü. Listening socket'in `accept()` sonrasında yaşamaya devam ettiği ve her client connection için ayrı connected socket oluşturulduğu tekrar edildi. Bu connected socket'lerin tamamının server tarafındaki local port'u aynı olabilmektedir.

TCP connection'ların yalnız port numarasıyla değil `(source IP, source port, destination IP, destination port)` 4-tuple kombinasyonuyla ayrıldığı pekiştirildi. Böylece aynı `5001` server portuna çok sayıda farklı client connection'ın bağlanabilmesinin mantığı oluşturuldu. Tek source IP ile tek destination IP:port arasındaki özel durumda source-port alanının sınırlayıcı hale gelebileceği, ancak bunun genel olarak “bir portta 2^16 socket bulunabilir” şeklinde yorumlanamayacağı değerlendirildi.

Günün üçüncü bölümünde TCP cumulative ACK ve loss recovery davranışı ayrıntılı bir segment kaybı senaryosu üzerinden incelendi. Aradaki bir data segmentinin kaybolması durumunda receiver'ın sonraki out-of-order segmentleri alabilmesine rağmen ACK değerini ilk eksik data üzerinde tutabileceği görüldü. Aynı ACK değerinin tekrar gönderilmesinin duplicate ACK oluşturduğu ve bunun sender açısından kayıp sinyali olarak kullanılabileceği tekrar edildi. Yeterli loss indication oluşması durumunda eksik data RTO beklenmeden Fast Retransmit ile yeniden gönderilebilir.

Receiver'ın ACK değerini ilerletememesinin receive buffer'ın dolu olduğu anlamına gelmediği özellikle netleştirildi. ACK değerinin **next expected byte** bilgisini, `rwnd` değerinin ise receiver'ın kullanılabilir receive kapasitesini temsil ettiği görüldü. Bu nedenle receiver'ın `ACK = 3` göndermeye devam ederken aynı zamanda yüksek bir advertised receive window bildirmesi mümkündür.

Flow control ve congestion control arasındaki ayrım da aynı senaryo üzerinden pekiştirildi. `rwnd` receiver'ın kapasitesini temsil ederken `cwnd` sender'ın network congestion durumuna göre tuttuğu congestion-control state'idir. Receiver yüksek `rwnd` bildirmesine rağmen duplicate ACK/loss sinyalleri nedeniyle sender'ın congestion-control mekanizması `cwnd` üzerinde daha kısıtlayıcı davranabilir. Böylece efektif gönderim davranışının yalnız advertised window'a bakılarak değerlendirilemeyeceği ve kabaca `min(rwnd, cwnd)` sınırı üzerinden düşünülmesi gerektiği tekrar edildi.

Son olarak eksik segmentin retransmission ile ulaşmasının cumulative ACK üzerindeki etkisi incelendi. Receiver'ın daha sonraki segmentleri out-of-order buffer'lamış olması halinde eksik segment geldiği anda aradaki hole kapanmakta ve cumulative ACK bir anda ileri bir sequence değerine sıçrayabilmektedir. Örneğin `3` eksikken `4–9` buffer'lanmışsa ACK değeri `3` seviyesinde kalabilir; retransmitted `3` ulaştığında receiver doğrudan `ACK 10` seviyesine ilerleyebilir.

Bugünün en önemli kazanımı, **socket readiness, TCP connection identity ve TCP window/loss recovery konularının birbirinden bağımsız ezber bilgiler olmaktan çıkarılarak gerçek çalışma senaryoları üzerinde birbirine bağlanması** oldu.

Gün sonunda ulaşılan temel düşünce zinciri:

```text
SOCKET'TE DATA YOK
      ↓
Blocking thread bekliyor
      ↓
Diğer socket işlenemiyor
      ↓
Readiness problemi
      ↓
epoll

────────────────────────

SERVER PORT = 5001
      ↓
Çok sayıda connected socket
      ↓
Nasıl ayrılıyor?
      ↓
TCP 4-Tuple

────────────────────────

ARADA DATA KAYIP
      ↓
Sonraki data geliyor
      ↓
ACK ilerlemiyor
      ↓
Duplicate ACK
      ↓
Fast Retransmit

────────────────────────

ACK İLERLEMİYOR
AMA WINDOW BÜYÜK
      ↓
ACK ≠ rwnd
      ↓
ACK = next expected byte
rwnd = receiver capacity

────────────────────────

rwnd yüksek
AMA loss var
      ↓
cwnd congestion nedeniyle
kısıtlayıcı olabilir
      ↓
Effective sending behavior
≈ min(rwnd,cwnd)
```

şeklinde oluşturuldu.

Bir sonraki çalışma aşamasında bu teorik bilgilerin doğrudan mevcut TCP Accelerator üzerinde gözlemlenmesi planlanmaktadır. Özellikle `getsockopt()` ile `TCP_INFO` kullanılarak accelerator'ın iki bağımsız TCP connection'ında RTT, RTO, `cwnd`, `ssthresh`, MSS, unacknowledged data ve retransmission state bilgilerinin okunması hedeflenmektedir.

Böylece:

```text
Bugün:

"cwnd ne yapar?"
"rwnd ne yapar?"
"loss olduğunda ne olur?"

        ↓

Sonraki aşama:

Gerçek çalışan TCP socket üzerinde
bunları ölç

        ↓

TCP_INFO

        ↓

Client ↔ Accelerator TCP #1
        +
Accelerator ↔ Server TCP #2

        ↓

İki connection'ın
RTT / RTO / cwnd / retrans
değerlerini karşılaştır

        ↓

OBSERVE
   ↓
ANALYZE
   ↓
TUNE
```

aşamasına geçilmesi planlanmaktadır.
