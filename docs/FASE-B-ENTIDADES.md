# Fase B — entidades y party

Verificado en runtime sobre **DDDA.exe build 2364871** leyendo memoria del proceso
mientras el juego estaba en gameplay.

---

## 1. Cadena de acceso

```
pBaseGlobal (0x018FA4BC, RVA 0x014FA4BC, .data)   <- encontrado por la firma AOB de Fase A
   └─ *(void**)pBaseGlobal = base del objeto jugador   (heap, cambia cada ejecución)
        └─ base + 0xA7000 + slot*0x7F0 = bloque de personaje de la party
```

`base` es un objeto con vtable en `.rdata` (RTTI **stripped** en el build retail:
no hay type descriptors, la clase no se puede leer por nombre).

## 2. Party: array de 6 slots, stride `0x7F0`

Observado con un Arisen full HP y un main pawn herido:

| slot | offset desde `base` | HP | max HP | stamina | max stamina |
|---|---|---|---|---|---|
| 0 (Arisen) | `+0xA7000` | 530.0 | 530.0 | 514.5 | 640.0 |
| 1 (main pawn) | `+0xA77F0` | 181.9 | 510.0 | 198.3 | 580.0 |
| 2..5 | — | 0 | 0 | 0 | 0 |

## 3. Layout de un bloque de personaje

Offsets relativos al inicio del bloque:

| Offset | Tipo | Campo |
|---|---|---|
| `+0x96C` | float | **HP actual** |
| `+0x970` | float | HP máximo |
| `+0x974` | float | stamina actual |
| `+0x978` | float | stamina máxima |
| `+0x97C` | float | 540.0 al inicio (¿ataque o defensa?) |
| `+0x980` | float | 100.0 |
| `+0x984..0x98C` | float | 82, 82, 82 |
| `+0x990` | float | 78.0 |

> **Ojo:** el antiguo `DDPUCK_OFF_MAIN_PAWN = 0x7F0` era correcto como **stride**,
> no como offset hacia un puntero. Leer `*(void**)(base+0x7F0)` devuelve 0.

## 4. Lo que todavía NO sabemos (camino a "entidad jugador completa")

- Dónde está la **posición/orientación** (transform) de cada personaje.
- Cómo se le dan **órdenes de movimiento/acción** (input → AI → motor).
- Dónde vive la **cámara** del jugador y cómo se le inyecta un segundo target.
- Qué pasa si se escribe en el bloque del pawn (¿lo pisa la IA al frame siguiente?).

Método para la siguiente etapa: **x32dbg** (`C:\Users\andre\Tools\x64dbg\release\x32\x32dbg.exe`)
con hardware breakpoint de escritura sobre `base + 0xA77F0 + 0x96C` para encontrar la
función que escribe el HP del pawn, y desde ahí subir hasta el controlador de input.
