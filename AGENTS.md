# AGENTS.md

Repo untuk mengekstrak stream H264 dari kamera V380. Dua implementasi **independen** (tidak ada kode bersama):

- `v380/` — CLI C++, menulis raw H264 ke stdout. Entry: `v380/v380.cpp` (`main`).
- `v380-nodejs/` — server TypeScript/Express, menyajikan FLV via HTTP. Entry: `src/app.ts`; handler stream & endpoint di `src/stream.ts`; struct protokol didekode manual via `typebase` di `app.ts`.
- Root: `v380.sln` (proyek VS2015) dan `start.sh` (contoh personal dengan IP/credentials hardcoded — bukan script yang dipelihara).

## Build & run

C++:
- Linux/macOS: `make -C v380` → menghasilkan `v380/v380` (g++ polos, link `-lpthread`).
- Windows: build `v380.sln`.
- `v380/v380.xcodeproj` juga ada untuk Xcode.

Node (wajib `cd v380-nodejs` dulu — semua path relatif terhadap cwd):
1. `npm install` — **tidak ada lockfile** (`package-lock.json` di-gitignore), jadi versi dependency tidak terkunci.
2. Salin `config.example.json` → `config.json` (di-gitignore; tanpa ini app langsung exit dengan pesan error). Field `ip` wajib diisi — discovery belum mengisi IP (TODO di kode).
3. Jalankan: `npm run start` (ts-node) atau `npm run dev2` (ts-node-dev, inspector di port 7321).
4. Stream bisa dilihat di `http://localhost:4000/video/stream.flv` (port = `serverPort` di config.json).

## Verifikasi

- `npm run lint` (tslint) lalu `npm run tsc` (typecheck). Urutan: lint → tsc.
- `npm run tsc` akan gagal jika ada unused local (`noUnusedLocals: true`).
- `npm test` hanya stub yang exit 1 — **tidak ada test suite** di repo ini (C++ maupun Node). Jangan mencoba menjalankan test.
- `npm run dev` (nodemon) **rusak**: nodemon bukan dependency dan tidak ada `nodemon.json`. Gunakan `dev2`.
- Tidak ada CI maupun pre-commit config.

## Gotchas

- Runtime Node membutuhkan binary `ffmpeg` di PATH (fluent-ffmpeg mem-spawn-nya per request di `stream.ts`).
- Endpoint `/video/:filename` internally meng-fetch `http://localhost:<serverPort>/audio/stream.flv` — route audio harus berfungsi agar video jalan.
- Stream kamera mulai sebelum client terhubung (bug diketahui); workaround buffer I-frame ada di `stream.ts` (`iFrame`/`pFrame`, `TOTAL_PFRAME`). Jangan dihapus sembarangan.
- Discover: binary C++ `./v380/v380 --discover`; untuk Node: `npm run start -- discover` (kode mengecek `argv` mengandung `discover`).
- Struct paket protokol di `app.ts`/`stream.ts` diberi komentar referensi offset DLL vendor (HSPC_PreviewDLL.dll) — pertahankan komentar tersebut saat mengedit.
- `stdafx.h` memetakan `_stricmp` → `strcasecmp` agar kompatibel non-Windows.
