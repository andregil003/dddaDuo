# dddaDuo

Local split-screen co-op for **Dragon's Dogma: Dark Arisen** (Steam, PC).
Two players, one machine, one copy of the game.

> Status: **early development (Fase A)** — the loader and the memory scanner work;
> gameplay features are being built. See [Roadmap](#roadmap).

---

## English

### What it is

A from-scratch mod that adds a second local player to DDDA. Player 1 controls the
Arisen, Player 2 controls the main pawn, with independent cameras and a split view.

No online multiplayer, no game files modified on disk, no account risk: everything
lives in DLLs that the game loads at startup.

### How it hooks in

`DDDA.exe` (32-bit, MT Framework) statically imports `XINPUT1_3.dll`, `DINPUT8.dll`
and `D3D9.dll`. dddaDuo ships **two interchangeable loaders** — you install exactly
one, never both:

```
DDDA.exe
  └─ xinput1_3.dll   (loader, proxy)      ← OR ─┐
  └─ dinput8.dll     (loader, proxy)            │
        └─ ddpuck_core.dll  ◄───────────────────┘
             ├─ AOB scanner  → finds the player base at runtime
             ├─ input routing (planned)
             └─ camera / D3D9 viewports (planned)
```

The core resolves the player struct by **signature scan**, not by a hardcoded
address, so it survives game updates and ASLR.

### Install (Fase A, developer build)

1. `build.bat` → produces `dist\`
2. Copy `ddpuck_core.dll` + **one** loader into the folder with `DDDA.exe`
3. Launch the game
4. Read `ddpuck.log` next to the executable

Expected log:

```
=== dddaDuo core Fase A ===
signature hit at 0x00xxxxxx (section .text)
pBase variable at 0x00xxxxxx -> player base 0x0xxxxxxx
player base 0x0xxxxxxx | HP = 530.0
```

### Uninstall

Delete the copied DLLs. Nothing else is touched.

---

## Español

### Qué es

Un mod hecho desde cero que agrega un segundo jugador local a DDDA. El jugador 1
controla al Arisen y el jugador 2 al main pawn, con cámaras independientes y
pantalla dividida.

Sin multijugador online, sin modificar archivos del juego en disco, sin riesgo de
cuenta: todo vive en DLLs que el juego carga al arrancar.

### Cómo se engancha

`DDDA.exe` (32 bits, MT Framework) importa estáticamente `XINPUT1_3.dll`,
`DINPUT8.dll` y `D3D9.dll`. dddaDuo trae **dos loaders intercambiables** — se instala
uno solo, nunca los dos:

```
DDDA.exe
  └─ xinput1_3.dll   (loader, proxy)      ← O ─┐
  └─ dinput8.dll     (loader, proxy)           │
        └─ ddpuck_core.dll  ◄──────────────────┘
             ├─ escáner AOB  → encuentra la base del jugador en runtime
             ├─ enrutado de input (planeado)
             └─ cámara / viewports D3D9 (planeado)
```

El core localiza la estructura del jugador por **firma de bytes**, no por dirección
fija: así sobrevive actualizaciones del juego y ASLR.

### Instalar (Fase A, build de desarrollo)

1. `build.bat` → genera `dist\`
2. Copiá `ddpuck_core.dll` + **un** loader a la carpeta donde está `DDDA.exe`
3. Abrí el juego
4. Leé `ddpuck.log` junto al ejecutable

### Desinstalar

Borrá las DLLs copiadas. No se toca nada más.

---

## Roadmap

| Fase | Objetivo | Estado |
|---|---|---|
| A | Core propio: loaders + AOB scanner + telemetría de HP | ✅ compila, pendiente prueba en juego |
| B | P2 controla el main pawn con el mando 2 (cámara compartida) | 🔜 |
| C | Split-screen real: 2 viewports D3D9 + segunda cámara | 🔜 |
| D | HUD por jugador, menús, vidas compartidas | 🔜 |

## Requisitos

- Windows 10/11 x64 con la versión Steam de DDDA (32-bit)
- Visual Studio 2022 Build Tools con workload C++ (solo para compilar)
- Dos mandos XInput distintos para jugar

## Licencia

MIT — ver [LICENSE](LICENSE). Créditos y trabajo previo de la comunidad: [CREDITS.md](CREDITS.md).

This is an unofficial fan project. Not affiliated with or endorsed by Capcom.
