# Spider-Man 2 (Xbox) — Android kurulum rehberi

[English](ANDROID.md) · **Türkçe**

Bu rehber, kendi `default.xbe`'nden derlenen Android sürümünü telefona kurmayı ve oyunu çalıştırmayı adım adım anlatır.

> [!IMPORTANT]
> APK, senin oyun kopyandan üretilmiş kod içerir. **Başkalarıyla paylaşma, internete yükleme.** Oyun dosyaları da
> APK'nın içinde değildir; kendi kopyanı telefona ayrıca koyman gerekir.

> [!NOTE]
> Bu sürüm deneysel. Bir Mali-G610'lu telefonda (Redmi Note 13 Pro+) ve Android emülatöründe oyun içine kadar
> test edildi. Kayıt alma/yükleme ve uzun süreli oynanış henüz denenmedi.

## Gerekenler

| | |
|---|---|
| **Telefon** | Android 8.0 veya üstü, 64-bit (ARM64) işlemci. Son 6-7 yılın telefonlarının neredeyse hepsi uyar. |
| **Grafik** | OpenGL ES 3.1 veya üstü (3.2 önerilir). Adreno (Snapdragon), Mali ve Xclipse GPU'lar uygundur. |
| **Bellek** | En az 4 GB RAM önerilir. |
| **Depolama** | Oyun için yaklaşık 1,7 GB, ayrıca uygulama ve önbellek için ~200 MB boş alan. |
| **Oyun** | Kendi *Spider-Man 2* **orijinal Xbox** kopyan. `default.xbe` dosyasının SHA-1'i `853bcb253c5baea2d7e2d9b59e5888b4c87bb402` olmalı (ABD sürümü). |
| **APK** | Kendin derlersin: [APK'yı derlemek](#apkyı-yeniden-derlemek-bilgisayarda). Oyundan üretilen kod içerdiği için hazır APK dağıtılmaz. |

## 1. APK'yı telefona kur

1. Telefonu USB kablosuyla bilgisayara bağla. Telefonda çıkan bildirimden **Dosya aktarımı** (MTP) modunu seç.
2. `SpiderMan2-Android.apk` dosyasını telefonun **Download** (İndirilenler) klasörüne kopyala.
3. Telefonda **Dosyalar** uygulamasını aç, APK'ya dokun.
4. "Bilinmeyen uygulamalar" uyarısı çıkarsa **Ayarlar**'a dokun ve dosya uygulamasına **Bu kaynaktan izin ver** iznini ver.
   Geri dönüp **Yükle**'ye dokun.
5. Play Protect uyarı verirse **Yine de yükle**'yi seç. Uygulama imzasız bir test sürümü olduğu için bu normaldir.

> Geliştirici seçenekleri ve USB hata ayıklama açıksa, bilgisayardan tek komutla da kurabilirsin:
>
> ```bash
> adb install -r dist-android/SpiderMan2-Android.apk
> ```

## 2. Uygulamayı bir kez aç

Ana ekranda **Spider-Man 2** simgesine dokun. İlk açılışta oyun bulunamadığı için şu mesajı görürsün:

> *Game not found — Put your own copy of Spider-Man 2 (original Xbox) in …/files/game*

Bu beklenen bir durumdur: uygulama kendi klasörünü oluşturmuş olur. **Tamam**'a dokunup kapat.

## 3. Oyunu telefona kopyala

Oyunun yeri:

```
Telefon depolama alanı / Android / data / com.spiderman2.xbox / files / game /
```

1. Telefon USB ile bağlıyken bilgisayarda (Dosya Gezgini) telefonun depolama alanını aç.
2. `Android` → `data` → `com.spiderman2.xbox` → `files` → `game` klasörüne gir.
3. Oyunu buraya kopyala. İki seçenek var:
   - **Disk imajı (önerilen):** `.iso`, `.xiso`, `.cci` veya `.cso` dosyasının kendisi, örneğin
     `Spider-Man 2 (World).xiso.iso`. Dosya adının önemi yok.
   - **Diskin dosyaları:** `default.xbe` ve yanında `data` klasörü (`data\game.ini`, `data\packs\…`, `data\sound\…`, `data\movies\…`).
4. Kopyalama bitene kadar bekle (1,7 GB, kabloya göre 1-3 dakika).

> [!TIP]
> **`Android/data` klasörü görünmüyorsa:** Android 11 ve sonrası bu klasörü telefonun kendi dosya uygulamasından gizler,
> ama **bilgisayardan USB ile** genelde görünür. Görünmüyorsa telefonu çıkarıp tekrar tak veya kilit ekranını açık tut.
>
> **`adb push` kullanma:** `adb` ile oraya kopyalanan dosyanın sahibi uygulama olmaz ve uygulama dosyayı okuyamaz
> ("Game not found" der). USB dosya aktarımı (MTP) ile kopyala.

## 4. Oyna

Uygulamayı yeniden aç. Kısa bir yükleme ekranından sonra oyunun açılış videoları ve **PRESS START TO BEGIN** ekranı gelir.
Telefon yatay moda geçer. İlk açılışta Android'in "Tam ekran görüntüleniyor" uyarısını **Anladım** ile kapat.

İlk dakikalarda kısa takılmalar olabilir: oyun her yeni efekti ilk gördüğünde shader'ını derler. Bunlar
`cache/shaders.cache` dosyasına kaydedilir ve sonraki açılışlarda önceden hazırlanır.

## Kontroller

### Dokunmatik

| Ekrandaki yer | Xbox tuşu | Oyunda |
|---|---|---|
| Sol altta çubuk (başparmağın değdiği yerden başlar) | Sol analog | Yürüme / koşma, menüde gezinme |
| Sağ yarıda boş bir yere parmağı sürükle | Sağ analog | Kamera |
| **A** | A | Zıplama, seçme |
| **B** | B | Geri, duvara tutunma |
| **X** | X | Yumruk, menüde silme |
| **Y** | Y | Menüde onay (Enter) |
| **RT** (sağ üst) | Sağ tetik | Ağ sallanması |
| **LT** (sol üst) | Sol tetik | Hızlı koşu |
| **BLK** / **WHT** | Black / White | |
| **START** / **BACK** (üst orta) | Start / Back | Duraklatma, harita |
| Telefonun geri tuşu / hareketi | Start | Duraklatma |

Joystick, sol yarıda başparmağının değdiği yerde belirir ve topuzu parmağını takip eder. Basılan tuşlar parlar.
Kamera sürüklemeyi birebir takip eder: oyunun çubuk eşiği ve hızlanması sürüklerken kapatılır.

### Hassasiyet ve boyut: `controls.ini`

İlk açılışta `files/controls.ini` oluşur. Bilgisayardan açıp düzenle, oyunu yeniden başlat:

| Ayar | Varsayılan | Ne yapar |
|---|---|---|
| `camera_sensitivity` | 1.0 | Kamera hızı. 1 = ekran genişliği boyunca sürükleme ≈ yarım tur. 2 iki katı, 0.5 yarısı. |
| `invert_y` | 0 | 1 yaparsan yukarı sürüklemek aşağı baktırır |
| `size` | 1.0 | Tuşların ve joystick'in boyutu (0.7 – 1.5) |
| `opacity` | 1.0 | Tuşların görünürlüğü (0.3 – 1.5) |

### Kontrolcü

Bluetooth veya USB ile bağlanan Xbox, PlayStation ve Switch Pro kontrolcüleri çalışır. Kontrolcüye bastığında ekrandaki
tuşlar gizlenir. Ekrana dokunduğunda geri gelirler.

## Dosyalar

Hepsi `Android/data/com.spiderman2.xbox/files/` altında:

| Klasör | İçerik |
|---|---|
| `game/` | Senin oyun kopyan |
| `save/progress/` | Oyun kayıtları. **Uygulamayı kaldırmadan önce bunu bilgisayara yedekle**: kaldırınca Android bu klasörü siler. |
| `save/data/` | Oyunun Xbox sabit diskine yazdığı kendi verisi |
| `cache/` | Shader listesi ve Xbox sabit diskinin geri kalanı; silinebilir |
| `logs/` | `sm2.log` — sorun olduğunda bakılacak yer |

## Güncelleme

Yeni bir APK'yı eskisinin üstüne kurman yeterli. `game/` ve `save/` klasörleri korunur.

## Sorun giderme

| Belirti | Çözüm |
|---|---|
| **"Game not found"** | Dosya `files/game/` klasöründe mi? Uzantı `.iso`, `.xiso`, `.cci` veya `.cso` mu? `adb` ile kopyaladıysan USB dosya aktarımıyla yeniden kopyala. |
| **"Another release of the game"** | Kopyan desteklenen ABD sürümü değil. APK, derlendiği `default.xbe` ile birebir aynı oyunu ister. |
| **"OpenGL ES 3.1 is needed"** | Telefonun GPU'su çok eski; bu sürüm o telefonda çalışmaz. |
| **Uygulama hemen kapanıyor** | `logs/` klasöründeki en yeni dosyanın sonuna bak; satırlar `error:` ile başlayan yerler sorunu gösterir. |
| **Arka plandan dönünce siyah ekran / kapanma** | Bilinen sorun: uygulamayı tamamen kapatıp yeniden aç. Kayıt noktalarında kaydetmeyi unutma. |
| **Bozuk / eksik grafik** | Telefonun GPU modelini ve `logs/` dosyasını not al. GPU sürücüleri arasında farklılıklar olabiliyor. |
| **Yavaş** | Telefonu şarja tak, pil tasarrufunu kapat. İlk dakikalardaki takılmalar shader derlemesindendir ve geçer. |

## APK'yı yeniden derlemek (bilgisayarda)

Kaynakta değişiklik yaptıysan veya yeni bir sürüm istiyorsan:

1. Önce Windows sürümü bir kez derlenmiş olmalı. Bu adım `generated/game` klasöründeki oyun kodunu `assets/default.xbe`'den üretir.
2. Gerekenler: Android SDK, NDK 28.2.13676358, JDK 17 veya 21. `android/local.properties` dosyasında SDK yolu
   (`sdk.dir=C:/Users/<kullanıcı>/AppData/Local/Android/Sdk`) olmalı.
3. `android` klasöründe:

   ```bash
   gradlew.bat assembleDebug -PABIS=arm64-v8a
   ```

   Sadece telefon için `arm64-v8a`; emülatörü de istiyorsan `-PABIS=arm64-v8a,x86_64`. İlk derleme 5-10 dakika sürer.
4. Çıkan dosya: `android/app/build/outputs/apk/debug/app-debug.apk`.
