# Fase B — plan de trabajo

Objetivo (decisión de André): **el main pawn se convierte en una entidad jugador
completa** controlada por el mando 2, con todas las acciones (mover, atacar,
skills, ítems), cámara propia y HUD.

---

## Estado actual

| Pieza | Estado |
|---|---|
| Loader + core | ✅ funcionando (Fase A) |
| Acceso al bloque del Arisen y del pawn | ✅ `base + 0xA7000 + slot*0x7F0` |
| Telemetría HP/stamina por slot | ✅ en vivo |
| Proxy XInput (mando 2) | ✅ ya es nuestro: `XInputGetState(idx, ...)` pasa por código propio |
| Tracer de escrituras (guard page) | ✅ implementado, a la espera de capturar el pawn |
| Posición / transform de personaje | ❌ desconocido |
| Ruta input → acción del motor | ❌ desconocida |
| Cámara y viewport | ❌ desconocido |

## Milestones

**M1 — Mapa de escritura del pawn** (en curso)
Guard-page sobre `pawn + 0x96C` → RVAs que escriben HP. De ahí se sube al
actualizador por frame, que es la puerta de entrada a la lógica de personaje.

**M2 — Transform**
Encontrar posición (vec3), orientación y velocidad de cada bloque de personaje.
Validación: mover el pawn con el mando de P1 y ver cómo cambian los floats.

**M3 — Captura de input del mando 2**
El proxy XInput ya intercepta; hay que enrutar el estado del mando 2 a un buffer
propio y dejar de entregárselo al motor como si fuera "otro mando".

**M4 — Inyección de comandos en el pawn**
Escribir en la estructura de control del pawn (o en su "AI state") para que el
movimiento venga del mando en vez de la IA. Riesgo principal: la IA reescribe el
estado cada frame; hay que saber a quién se le gana el turno.

**M5 — Cámara compartida**
Reapuntar la cámara al pawn cuando corresponda (o duplicar el objeto cámara).

**M6 — Split-screen**
Segundo viewport D3D9 con su propia cámara. Requiere interceptar `Present`/`Reset`
y `SetViewport`.

**M7 — HUD y reglas**
Vidas compartidas, avisos por jugador, menús.

## Riesgos conocidos

- **Anti-tamper**: no detectamos protección activa, pero escribir en estructuras
  del motor puede crashear de formas creativas. Cada milestone lleva su rollback.
- **Orden de slots**: hay que confirmar si el slot 1 es siempre el main pawn o si
  depende del orden de la party.
- **Herramientas**: para leer el código de los RVAs hace falta un desensamblador
  (capstone) o x32dbg. Sin eso, los RVAs son solo números.
