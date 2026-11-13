# AGENTS.md

Repo untuk mengekstrak stream H264 dari kamera V380. Dua implementasi **independen** (tidak ada kode bersama):

- `v380/` — CLI C++, menulis raw H264 ke stdout. Entry: `v380/v380.cpp` (`main`).
- `v380-nodejs/` — server TypeScript/Express, menyajikan FLV via HTTP. Entry: `src/app.ts`; handler stream & endpoint di `src/stream.ts`; struct protokol didekode manual via `typebase` di `app.ts`.
- Root: `v380.sln` (proyek VS2015) dan `start.sh` (contoh personal dengan IP/credentials hardcoded — bukan script yang dipelihara).

## Build & run

C++:
- Linux/macOS: `make -C v380` → menghasilkan `v380/v380` (g++ polos, link `-lpthread`).
- Windows: install MinGW-w64 dulu (mis. `winget install BrechtSanders.WinLibs.POSIX.UCRT`), lalu `mingw32-make` di folder `v380/` → menghasilkan `v380/v380.exe`. Makefile sudah menambahkan `-lws2_32` otomatis di Windows (diperlukan untuk WinSock). PATH tidak ter-update sampai shell baru dibuka.
- Windows tanpa MinGW: build `v380.sln` (VS2015).
- `v380/v380.xcodeproj` juga ada untuk Xcode.

Node (wajib `cd v380-nodejs` dulu — semua path relatif terhadap cwd):
1. `npm install` — **tidak ada lockfile** (`package-lock.json` di-gitignore), jadi versi dependency tidak terkunci.
2. Salin `config.example.json` → `config.json` (di-gitignore; tanpa ini app langsung exit dengan pesan error). Field `ip` wajib diisi — discovery belum mengisi IP (TODO di kode).
3. Jalankan: `npm run start` (ts-node) atau `npm run dev2` (ts-node-dev, inspector di port 7321).
4. Stream bisa dilihat di `http://localhost:4000/video/stream.flv` (port = `serverPort` di config.json).

## Verifikasi

- `npm run lint` (tslint) lalu `npm run tsc` (typecheck). Urutan: lint → tsc.
- **Jangan upgrade `typescript` di atas 4.6**: `ts-node@9` crash dengan TS ≥ 4.7 (signature API `resolveTypeReferenceDirective` berubah). Sudah dipin `~4.6.4`.
- `tsconfig.json` punya `paths` yang memaksa modul `typebase` resolve ke `typebase.js`-nya — tanpa itu, `tsc` ikut meng-typecheck source `.ts` ketat di dalam `node_modules` dan gagal. Jangan dihapus.
- `npm run tsc` akan gagal jika ada unused local (`noUnusedLocals: true`).
- `npm test` hanya stub yang exit 1 — **tidak ada test suite** di repo ini (C++ maupun Node). Jangan mencoba menjalankan test.
- `npm run dev` (nodemon) **rusak**: nodemon bukan dependency dan tidak ada `nodemon.json`. Gunakan `dev2`.
- Tidak ada CI maupun pre-commit config.

## Protokol login & stream (hasil reverse-engineering, sudah teruji)

Login (`v380/v380.cpp` sudah mengikuti ini; `v380-nodejs` **belum**):

- `cmd 1167` → respons `1168`, isi: `u1@4 = 120` (layout LAN), `version byte @8 = 31`, `u3@9 = 1`, `deviceId@13`, `username@49`, `password@81`.
- **Versi byte @8 wajib ≥ 31** untuk firmware ini — nilai 1/2/3/21/30 dibalas `-100` ("unknown command"). `1022` + username@71/password@103 (layout cloud) selalu `1011`.
- Password field = `randomKey(16 ASCII acak) + AES-128-ECB-noPad(password di-zero-pad ke 48 byte)`, dienkripsi **2×**: dulu key `macrovideo+*#!^@`, lalu randomKey. Encoding pkcs7 (32 byte) juga diterima; tanpa randomKey → `1012`.
- Kode hasil: `1001` sukses, `1011` username salah, `1012` password salah, `1018` deviceId salah.
- Respons login = **256 byte**; respons stream login `301` = **tepat 32 byte** (`cmd 401`@0, `result`@4, `version` u16@8, `width`@10, `height`@14, `maxPackSize`@18, `audioFreq`@22, `audioBits`@23, `audioChannels`@24).
- Stream start `303`: `u32@4` = nilai `result` dari `401` (1001). Pakai `u16 0x3001@4` (seperti referensi lain) **tidak** mengalir di firmware ini.
- `deviceVersion` (byte@12 respons login) `> 30` → media terenkripsi AES-128-ECB. Key = `ticket(u32 LE) + 0x618123462c14795c(u64 LE) + 0x82800df0(u32 LE)`.
  Video: **64 dari setiap 80 byte** pertama di-decrypt. Tiap frame = header luar 16 byte (`frameId:4, frameType:2, frameRate:2, timestamp:8`) + payload → buang 16 byte, dekripsi, lalu cari start code `00 00 01` dan selaraskan jadi `00 00 00 01`.
  FLV butuh header 16 byte itu tetap di depan (dipakai `FlvStream` untuk timestamp) — makanya ada dua jalur keluar (`nframe` untuk stdout, `fframe` untuk FLV).

Gotchas protokol:
- **TCP bisa memecah header 12 byte** jadi beberapa segmen. `UtlSocket::Recv` hanya sekali `recv()` — baca header/respons wajib di-loop sampai penuh, kalau tidak stream langsung mati (`hdrn=8` → restart terus).
- Respons dibaca ke `rbuf` terpisah, jangan ke `buf` (request): `vector::resize` membuat pointer `req` jadi dangling.
- Kredensial kamera (IP, device ID, username, password) **jangan di-commit**. Skrip scratch `v380/probe_*.ps1` dan hasil stream sudah masuk `.gitignore`.

## Gotchas

- Runtime Node membutuhkan binary `ffmpeg` di PATH (fluent-ffmpeg mem-spawn-nya per request di `stream.ts`).
- Endpoint `/video/:filename` internally meng-fetch `http://localhost:<serverPort>/audio/stream.flv` — route audio harus berfungsi agar video jalan.
- Stream kamera mulai sebelum client terhubung (bug diketahui); workaround buffer I-frame ada di `stream.ts` (`iFrame`/`pFrame`, `TOTAL_PFRAME`). Jangan dihapus sembarangan.
- Discover: binary C++ `./v380/v380 --discover`; untuk Node: `npm run start -- discover` (kode mengecek `argv` mengandung `discover`).
- Struct paket protokol di `app.ts`/`stream.ts` diberi komentar referensi offset DLL vendor (HSPC_PreviewDLL.dll) — pertahankan komentar tersebut saat mengedit.
- `stdafx.h` memetakan `_stricmp` → `strcasecmp` agar kompatibel non-Windows.
