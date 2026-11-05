# Fase A — hallazgos de ingeniería inversa

Verificado contra **DDDA.exe Steam build 2364871** (x86, ImageBase `0x00400000`,
SizeOfImage `0x0160C000`, 21 841 776 bytes, timestamp `0x5A314C31`).

Método: escaneo estático del PE (`tools/` + script de verificación) **y** lectura
en vivo de memoria del proceso con `ReadProcessMemory` mientras el juego corría.

---

## 1. Firma AOB → variable global `pBase`

```
8B 15 ?? ?? ?? ??   mov edx, dword ptr [imm32]
33 DB               xor ebx, ebx
8B F8               mov edi, eax
```

| Propiedad | Valor |
|---|---|
| Hits en el ejecutable | **1** (único) |
| RVA de la instrucción | `0x0026A7C5` |
| Sección | `.text` |
| VA con base `0x400000` | `0x0066A7C5` |
| Operando `imm32` | `0x018FA4BC` |
| RVA del global | `0x014FA4BC` (`.data`) |

**El `imm32` es una dirección absoluta, no un offset relativo a la instrucción.**
Hay que leerlo de los bytes de la instrucción y usar *ese valor* como dirección de
la variable:

```c
DWORD varAddr = *(const DWORD*)(hit + 2);   // 0x018FA4BC
BYTE** pBase  = (BYTE**)varAddr;
```

Tratar `hit + 2` como si fuera la dirección de la variable (bug original) hace que
se lea la propia instrucción: devuelve `0x018FA4BC` como si fuera el "player base".

### Contenido del global

`*(void**)0x018FA4BC` = base de un objeto singleton del juego (vtable en `.rdata`,
sin RTTI: el build retail va sin type descriptors). Valor observado en gameplay:
`0x0DEB0820`, cambia entre ejecuciones (heap).

---

## 2. Offsets verificados en runtime

Con `base = *(void**)0x018FA4BC`:

| Expresión | Tipo | Valor observado |
|---|---|---|
| `base + 0xA7000` | struct entidad | punteros a `.rdata`, campos `0x0000FFFF` |
| `base + 0xA7000 + 0x96C` | **float** | **530.0** (HP actual) |
| `base + 0xA7000 + 0x970` | **float** | 530.0 (HP máximo, hipótesis) |
| `base + 0xA7000 + 0x968` | float | 0.0 |

`DDPUCK_OFF_PLAYER (0xA7000)` y `DDPUCK_OFF_HP (0x96C)` quedan **validados**.

---

## 3. Bugs encontrados y corregidos (todos bloqueaban la Fase A)

| # | Síntoma | Causa | Fix |
|---|---|---|---|
| 1 | `ddpuck.log` nunca aparecía | El export real era `_DdpuckCoreInit@4` (decoración `__stdcall` x86) y el loader pedía `DdpuckCoreInit` → `GetProcAddress` fallaba y el core nunca se inicializaba | `src/core/ddpuck_core.def` exporta el nombre limpio + fallback en ambos loaders |
| 2 | **Crash `0xC0000409`** (fastfail) del juego al primer log | Stream abierto con `_wfopen_s(..., L"a, ccs=UTF-8")` + escrituras **narrow** (`fprintf`) → el UCRT lo considera parámetro inválido | Abrir en modo byte (`L"a"`) |
| 3 | No se podía leer el log con el juego abierto | `fopen` bloquea el archivo | `_wfsopen(path, L"a", _SH_DENYWR)` |
| 4 | `pBase` leído como basura (`0x018FA4BC`) | `hit + 2` usado como dirección de la variable en vez de desreferenciar el `imm32` | Leer el `imm32` y castearlo |
| 5 | Thread del core usaba CRT sin inicializar | `CreateThread` no inicializa el estado por-hilo del CRT | `_beginthreadex` |

---

## 4. Cómo probar sin abrir el juego

`tools/testhost.cpp` → `dist\testhost.exe` carga `ddpuck_core.dll`, llama al
export y espera 12 s. Reproduce crashes en segundos. En el host la firma no existe,
así que el resultado esperado es `ERROR: pBase signature not found`.

```
build.bat                       # compila core + loaders
dist\testhost.exe               # smoke test del core
```

## 5. Instalación de prueba

Carpeta del juego **sin otros mods** (ddcoop movido a `test/backup-ddcoop/`):

```
DDDA.exe
steam_api.dll
ddpuck_core.dll      <- dist
xinput1_3.dll        <- dist
```
