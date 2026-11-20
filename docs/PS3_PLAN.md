# Spider-Man 2 (Xbox) → PS3: araçlar ve plan

Hazırlanma tarihi: 2026-11-18. Hedef: yalnızca açık kaynak araçlarla, CFW/HEN'li bir PS3'te ve RPCS3 emülatöründe
çalışan bir homebrew. Bu bir deneme planı: her aşamanın sonunda devam edip etmeme kararı veriliyor.

> [!NOTE]
> Araçların durumu GitHub API'sinden doğrudan okundu. RPCS3'ün PSL1GHT homebrew desteği gibi kontrol edilemeyen
> birkaç nokta "doğrulanacak" diye işaretli, çünkü web arama aracı o sırada çalışmıyordu.

## 1. Seçilen araçlar

| Ne için | Araç | Durum (kontrol edildi) | Neden |
|---|---|---|---|
| Derleyici | [ps3dev/ps3toolchain](https://github.com/ps3dev/ps3toolchain) | **GCC 13.2.0**, binutils 2.42, newlib 1.20.0. Son güncelleme 2026-10. | C++20 destekliyor (SDK bunu istiyor). PPU ve SPU derleyicileri. Linux/WSL'de kuruluyor. |
| SDK | [ps3dev/PSL1GHT](https://github.com/ps3dev/PSL1GHT) | Son güncelleme 2026-11. `rsx/` (GPU), `sys/` (thread, mutex, cond, sem, file, memory), `audio/`, `io/` (pad), örnek `samples/graphics/rsxtest*`. | Tek aktif açık kaynak PS3 SDK'sı. |
| GPU / shader | [gzorin/RSXGL](https://github.com/gzorin/RSXGL) | OpenGL 3.1 core'un bir alt kümesi. **Mesa'nın GLSL 1.30 derleyicisiyle shader'ları çalışma anında RSX koduna çeviriyor.** 2026-04'te RetroArch için güncellenmiş, ps3dev SDK'sını destekliyor. | Projenin en zor parçası olan "shader'ı RSX koduna çevir" işini hazır çözüyor. |
| Shader (yedek yol) | Mesa nouveau **nv30/nvfx** kodu (RSXGL'in içinde `extsrc/mesa`) | MIT lisans | RSXGL yetmezse kendi renderer'ımız bu çeviriciyi doğrudan kullanır. |
| Test (konsolsuz) | **RPCS3** | Doğrulanacak: PSL1GHT `.self`/`.pkg` çalıştırma | Geliştirmenin çoğu PS3 olmadan yapılır. |
| Big-endian testi | **qemu-ppc64** + `powerpc64-linux-gnu-g++` (WSL Ubuntu) | Standart Ubuntu paketleri | Grafik ve PS3'e hiç girmeden byte sırası işini kanıtlar. Proje ARM için aynı yöntemi `qemu-aarch64` ile kullanıyor. |
| Gerçek konsol | CFW veya PS3HEN, `ps3load` (PSL1GHT `tools/`) | — | Ağ üzerinden hızlı yükleme. |

**Kapalı kaynak tek parça:** PSL1GHT'ın `cgcomp` aracı NVIDIA'nın ücretsiz ama kapalı Cg Toolkit'ini istiyor.
RSXGL bunu yalnızca kendi örnekleri ve isteğe bağlı çevrimdışı derleme için kullanıyor. Çalışma anındaki GLSL
yolu Mesa ile tamamen açık. Plan Cg'ye bağımlı değil.

> PSL1GHT'ın bir [AI kullanım politikası](https://github.com/ps3dev/PSL1GHT/blob/master/AI_POLICY.md) var. Biz
> SDK'yı sadece kullanıyoruz, bu yüzden bizi etkilemiyor. Ama PSL1GHT veya RSXGL'e geri katkı (PR) yapılacaksa AI
> kullanımı açıkça belirtilmeli ve kodu gönderen kişi her satırını anlayıp açıklayabilmeli.

## 2. Koddan çıkan kritik bulgular

1. **Üretilmiş oyun kodu belleğe sadece makrolarla erişiyor.** Örnek dosya `generated/game/x86_recomp.10.cpp`:
   `X86_MEM32` 4.380, `X86_MEMF32` 453, `X86_MEM8` 375, `X86_MEMF64` 14, `X86_MEM16` 5 kullanım. Doğrudan
   `memcpy` veya işaretçi erişimi yok. Yani **2,9 milyon satırlık üretilmiş kodun byte çevirmesi tek dosyada**
   (`sdk/runtime/cpu/x86_ops.h`) çözülebilir: makrolar big-endian'da okurken ve yazarken byte çeviren bir "referans"
   tipine dönüşür. PowerPC'nin `lwbrx`/`stwbrx` komutları bunu ek maliyetsiz yapıyor.
2. **Asıl denetim işi runtime'da** (`sdk/runtime`, ~16.300 satır): kernel yapıları, D3D push buffer, vertex, texture
   ve ses verisini okuyan elle yazılmış kod. Konsolun verisini little-endian sayan her satır elden geçmeli.
3. **Runtime'ın platformdan istedikleri:** `std::thread` (7 dosya), `std::filesystem` (9 dosya), mutex/condvar
   (20 dosya). PS3'te newlib 1.20 ve libstdc++ ile bunların çalışıp çalışmadığı **belirsiz**. Aşama 0'da ilk test
   edilecek şey bu. Çalışmıyorsa PSL1GHT'ın `sys/thread.h` ve `sys/file.h` fonksiyonları üzerine ince bir katman yazılır.
4. **4 GB adres alanı:** PC ve Android'deki gibi 4 GB ayırmak mümkün değil. Switch için yazılmış **segment modu**
   (`XBE_SEGMENTED`, kullanılan yerler 1 GB'lık bloklarda) hazır; PS3'te 256 MB ana bellekle daha küçük bir düzen gerekecek.
5. **Renderer:** `gl/gl_backend.cpp` OpenGL 4.3 kullanıyor (uniform buffer, vertex attrib binding). RSXGL ise
   OpenGL 3.1 alt kümesi: **uniform buffer, sıkıştırılmış texture, cube texture ve framebuffer'dan kopyalama yok.**
   Android için yazdığımız uyum katmanı (`gl/gl_api.cpp`: BGRA, S3TC'yi CPU'da açma, shader başlığını değiştirme)
   burada da işe yarar. Ama uniform buffer'ın yerine düz uniform kullanmak ve eksik RSXGL özelliklerini eklemek gerekecek.

## 3. Aşamalar

Her aşamanın sonunda ölçülebilir bir "bitti" kriteri var. Bir aşama başarısız olursa sonrakine geçmeye değmez.

### Aşama 0 — Kurulum ve ön testler (1-2 gün)
- WSL Ubuntu'ya ps3toolchain'i kur (derleme birkaç saat sürer) ve PSL1GHT örneklerini derle.
- RPCS3'ü kur; `rsxtest` örneğini RPCS3'te çalıştır.
- **Kritik test:** PS3'te `std::thread`, `std::mutex`, `std::condition_variable`, `std::filesystem` ve C++20
  kullanan küçük bir program, RPCS3'te çalışıyor mu?
- RSXGL'i derle, bir örneğini RPCS3'te çalıştır.
- **Bitti:** dört örnek RPCS3'te çalışıyor; libstdc++ eksikleri listelendi.

### Aşama 1 — Big-endian (1-3 hafta) — PS3'in mümkün olup olmadığını gösteren aşama
- `x86_ops.h`: `XBE_BIG_ENDIAN` modunda bellek makroları byte çeviren referans tipine dönüşür. Gerekirse
  recompiler (`sdk/tools/recomp`) tarafında da küçük değişiklikler.
- Runtime denetimi: konsol verisini okuyan her yere byte çevirme (`kernel/`, `xdk/`, `gpu/`).
- Linux test build'ini (`sdk/harness`) `powerpc64-linux-gnu-g++` ile derleyip WSL'de `qemu-ppc64` altında
  ekransız çalıştır. CPU renderer'ı (`reference_renderer.cpp`) kare yakalasın.
- **Bitti:** oyun qemu-ppc64'te başlık ekranına ve şehre geliyor; yakalanan kareler PC'dekilerle aynı.

### Aşama 2 — PS3 uygulama katmanı, CPU renderer ile (1-2 hafta)
- `ps3/` klasörü (Switch ve Android katmanlarının karşılığı): giriş noktası, oyun klasörü
  (`/dev_hdd0/game/…` veya USB), log, kayıt, PSL1GHT ses ve pad, thread eşlemesi.
- Segment modunun PS3 bellek düzeni.
- Grafik için önce Switch'in yedek yolu: CPU renderer arada bir kare çizer ve ekrana basar (yavaş ama doğru).
- **Bitti:** RPCS3'te başlık ekranı görünüyor, ses geliyor, pad çalışıyor.

### Aşama 3 — GPU renderer (4-8 hafta)
- **Yol A (önerilen ilk deneme): RSXGL.** `gl/` backend'ini OpenGL 3.1 / GLSL 1.30 alt kümesine uyarla
  (uniform buffer → uniform, attrib binding → `glVertexAttribPointer`, eksik formatlar CPU'da). RSXGL'de eksik olan
  cube texture ve framebuffer kopyalamayı RSXGL'e ekle.
- **Yol B (A tıkanırsa):** doğrudan PSL1GHT `rsx` kütüphanesiyle ince bir renderer. Shader'lar Mesa nvfx
  çeviricisiyle (RSXGL içindeki kod) veya NV2A mikrokodundan RSX mikrokoduna doğrudan çeviriyle.
- Vertex ve texture verisi little-endian; RSX'e yüklerken çevirme maliyeti ölçülecek.
- **Bitti:** RPCS3'te şehir GPU ile çiziliyor.

### Aşama 4 — Gerçek konsol ve performans (2-4 hafta)
- CFW/HEN'li PS3'te `ps3load` ile test.
- PPU sırayla çalışan (in-order) bir çekirdek: sıcak noktaları ölç. Ses karıştırma veya texture açma gibi işleri
  SPU'lara taşımak sonraki bir iyileştirme.
- **Bitti:** konsolda oynanabilir kare hızı (hedef 30 FPS, oyunun kendi hızı).

## 4. Riskler

| Risk | Etki | Önlem |
|---|---|---|
| libstdc++ PS3'te thread/filesystem sunmuyor | Aşama 0-2 uzar | PSL1GHT `sys/` üzerine ince bir katman |
| RSXGL'in eksikleri beklenenden büyük | Aşama 3 uzar | Yol B |
| PPU yavaş kalıyor | Konsolda düşük FPS | Ölç, sıcak noktaları optimize et, SPU |
| 256 MB ana bellek | Bellek yetmezliği | Segment düzenini küçült, texture'ları RSX belleğinde tut |
| RPCS3 ile gerçek konsol farklı davranıyor | Geç keşfedilen hatalar | Aşama 2'den itibaren arada bir gerçek konsolda test |

## 5. İlk gün yapılacaklar

1. WSL Ubuntu'yu kontrol et (proje `build-windows.ps1` ile WSL kullanıyor, kurulu olabilir).
2. `qemu-user-static` ve `g++-powerpc64-linux-gnu` paketlerini kur. Bunlar Aşama 1 için yeterli, ps3toolchain'i beklemiyor.
3. Arka planda ps3toolchain derlemesini başlat.
4. Aşama 1'in ilk adımı: `x86_ops.h`'de big-endian bellek makroları ve test build'inin ppc64 derlemesi.
5. Gün sonunda rapor: oyun qemu-ppc64'te nereye kadar geldi, hangi runtime parçaları bozuk.

Tahmini toplam süre: **2-4 ay**. Byte çevirmenin üretilmiş kodda tek dosyada çözülebilmesi ve RSXGL'in
çalışma anı shader derleyicisi sayesinde ilk tahminden (4-6 ay) kısa.

## 6. Durum (2026-11-18)

- **Aşama 1, ilk sonuç:** `XBE_BIG_ENDIAN` modu çalışıyor. Test build'i `powerpc64-linux-gnu-g++` ile derlenip
  `qemu-ppc64` altında oyun açılış logolarını, videoları, başlık ekranını, isim girişini, yüklemeyi ve şehri
  (Chapter 1, çatı) x86 ile aynı çiziyor. Tarifi: [BUILDING.md](BUILDING.md), "Big-endian (PowerPC)".
  - Üretilmiş kod değişmedi: bellek makroları byte çeviren referanslar ([`x86_ops.h`](../sdk/runtime/cpu/x86_ops.h)),
    runtime konsol verisini [`core/le.h`](../sdk/runtime/core/le.h) ile okuyor.
  - Henüz big-endian için elden geçmedi: `gl/` renderer, `gpu/render_thread.cpp`, `xnet` (sockaddr dışındakiler).
- **Aşama 0, derleme kısmı:** ps3toolchain (PPU GCC 13.2.0) ve PSL1GHT kuruldu, `blitting` örneği `.self` oluyor.
  - libstdc++'ta **thread desteği yok** (`std::thread`, `std::mutex`, `std::condition_variable`): PSL1GHT `sys/`
    üzerine ince bir katman gerekecek.
  - `std::filesystem` derleniyor; bağlamada sadece `symlink`, `readlink`, `fchmod`, `pathconf` eksik (boş stub yeter).
  - `rsxtest` örneği Cg toolkit istiyor (beklendiği gibi).
- **Aşama 0, RPCS3 testleri** (RPCS3 0.0.42, firmware 4.93):
  - Çalışıyor: C++20, istisnalar (`try`/`catch`), atomikler, PSL1GHT thread + mutex + koşul değişkeni
    (4 thread, 400.000 sayım doğru), `steady_clock`/`usleep`, `std::filesystem` ile klasör açma, dosya
    yazma/okuma/boyut/listeleme/yeniden adlandırma. `blitting` örneği RPCS3'te Vulkan ile çiziyor.
  - Çalışmıyor: `std::filesystem::remove_all` klasör silemiyor (newlib `remove()`): küçük bir katman gerekli.
  - Bellek: en büyük `malloc` 208 MB. Adres alanında 256 MB, 512 MB ve 1 GB'lık bloklar ayrılabiliyor, 2 GB
    ayrılamıyor. Yani düz 4 GB pencere yok, **segment modu şart**.
  - Derleme ayarları: `-std=gnu++20` (newlib 1.20 sıkı `c++20`'de `snprintf` gibi fonksiyonları gizliyor),
    `std::snprintf` için `namespace std { using ::snprintf; }`, newlib `printf`'i `%zu` bilmiyor.
- **Aşama 2, ilk sonuç (aynı gün):** `ps3/` katmanı yazıldı ([ps3/README.md](../ps3/README.md)). PS3 sürümü
  (`SpiderMan2Xbox.self`) RPCS3'te açılıyor: logolar, videolar, başlık ekranı, yeni oyun ve **şehir** (Chapter 1)
  CPU renderer ile doğru görünüyor; 10 dakikalık koşuda çökme yok. Bellek: guest ~66 MB, heap ~60 MB.
  - Ses portu açılıyor ve dolduruluyor (kulakla denenmedi). Kol eşlemesi yazıldı, gerçek DS3 ile denenmedi
    (testler `pad.txt` betiğiyle).
  - CPU renderer RPCS3'te saniyede ~1 kare çiziyor: oynanabilirlik Aşama 3'ün (GPU renderer) işi.

- **Aşama 3 başladı (2026-11-19): RSXGL.** RSXGL (gzorin/RSXGL, 2026-04 sürümü) WSL'de ps3toolchain ile derlendi.
  - Derleme için Python 2 gerekmedi: Mesa'nın 2012 tarihli üreteç betikleri (GLSL builtin'leri, gallium format
    tabloları) Python 3'e uyarlandı, `makedepend` ve `xxd` için küçük yerel karşılıklar kullanıldı, GLES dosyaları
    boş bırakıldı (RSXGL'de kapalı).
  - **libstdc++'ın iostream başlatması bu toolchain'de RPCS3'te `main`'den önce takılıyor** (`std::cout` kullanan her
    program). RSXGL'de tek bir `std::cout` vardı, `printf` yapıldı. Oyun kodu iostream kullanmamalı.
  - Deneme programı RPCS3'te çalışıyor: EGL, 1280x720 yüzey, `GL_VERSION 3.1.0 RSXGL`, `NV40`. Oyunun shader'larına
    benzeyen GLSL 1.30 programları (hesaplanan indisle sabit okuma, `bvec`, `texture`) derleniyor: shader başına ~0,1 s.
  - Bulunan engeller:
    1. **Uniform dizileri desteklenmiyor**: `glUniform4fv(c, 192, …)` `GL_INVALID_OPERATION` veriyor (RSXGL diziyi tek
       eleman sayıyor). Vertex programın `c[192]`'si için RSXGL'e dizi desteği eklenmeli; fragment tarafında
       NV40'ta sabitler program kodunun içinde, dizi yerine tek tek uniform yazılmalı.
    2. `glReadPixels` sıfır döndürüyor (yeniden okuma, CPU'nun kilitlediği yüzeyler için gerekli).
    3. **Çizim başına bekleme:** çizimler normalde mikrosaniye sürüyor ama arada bir çizim tam **300 ms** bekliyor;
       500 çizimlik bir kare ~470 ms. Komut tamponunu 8 MB yapmak değiştirmedi. Kaynağı bulunmalı (RSXGL'in
       senkronizasyonu ya da RPCS3'ün bu kullanım şeklindeki davranışı); çözülmezse Yol A'nın performansı yetmez.
  - **Güncelleme:** 300 ms bekleme çözüldü. Sebep: libgcm'in 32 KB'lık komut pencereleri; her pencere sonunda
    RPCS3'te ~300 ms bekleniyor. Komut tamponunu tek bir halka olarak kullanan kendi callback'imizle 500 çizim
    0,6 ms sürüyor. Uniform dizileri için RSXGL yamalandı (`c[192]` artık yükleniyor). Kalan: vertex programda
    hesaplanan indisle sabit okuma yanlış sabiti okuyor ve `bvec` ile `mix` siyah veriyor (Mesa nvfx derleyicisi);
    `glReadPixels` hâlâ sıfır.
  - **Güncelleme 2 (2026-11-19 sabah): oyun GPU renderer ile çalışıyor.** `gl/` renderer'ı RSXGL'e bağlandı
    (`gl/gl_rsx.cpp`, `ps3/gl_host.cpp`, ayrıntılar [ps3/README.md](../ps3/README.md)). RPCS3'te logolar ve videolar
    40-60 FPS, başlık ekranı 60 FPS, 3D sahneler ~20 FPS (CPU renderer'da 0,2-0,3 FPS'ti); 7,5 dakikalık koşuda çökme yok.
    - RSXGL'de bulunup düzeltilenler (`ps3/rsxgl/rsxgl.patch`, `ps3/rsxgl/build.sh` ile sıfırdan derleniyor):
      framebuffer'a takılan nesnenin adı yerine referans sayısı saklanıyordu (renk + derinlik hiç tamamlanmıyordu),
      derinlik texture'ları, uniform dizileri, `glReadPixels`, çevrilemeyen programda süreci durdurmak, iostream,
      bağlanan her programın Mesa ara kodunu (~1 MB) sonsuza dek tutması.
    - Bellek: dağıtım tablosu PS3'te 39 MB'tan 9 MB'a indi; RSXGL+Mesa TOC'u aştığı için `-mminimal-toc`.
    - **Kalan:** (1) bazı vertex programları RSX'in 512 komut sınırını aşıyor (545); (2) bazı render target'larda
      "Framebuffer setup failed / depth buffer sığmıyor" (derinlik yüzeyi renkten küçük olabilir); (3) RPCS3'te görüntü
      ekrana gelmiyor (oyun çiziyor, ekran güncellenmiyor), muhtemelen 1 ve 2 ile ilgili; (4) hesaplanan indisle sabit
      okuma (`c[a0+4]`) ve `bvec` ile `mix` testleri bekliyor. RPCS3'te geri okuma (glReadPixels) gerçek kareyi vermiyor.
