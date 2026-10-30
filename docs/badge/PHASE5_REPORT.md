# EKOPARTY_FINAL — Fase 5

Fecha de promoción: 2026-10-04.

**FEATURE FREEZE — EKOPARTY 2026**

RC1 validada físicamente por el usuario en el badge real, sin regresiones ni problemas funcionales reportados. Promoción exacta a EKOPARTY_FINAL autorizada por el usuario. No se recompiló, optimizó ni modificó código funcional. La aplicación FINAL es idéntica byte por byte a RC1.

## Validación física declarada por el usuario

- LoRa TX/RX: **OK**.
- BLE: **OK**.
- mensajería y mensajes directos: **OK**.
- teclado y quick replies: **OK**.
- notificaciones y no leídos: **OK**.
- audio L4 y alerta sonora: **OK**.
- Sound ON/OFF y Test Sound: **OK**.
- timeout/wake: **OK**.
- funcionamiento con 2×AA: **OK**.
- Mesh Stats: **OK**.
- Node Inspector: **OK**.
- Recent Activity: **OK**.
- Diagnostics: **OK**.
- About/Home: **OK**.
- reinicio y persistencia: **OK**.

RF permanece en **10 dBm**; audio L4 y firma sonora se mantienen. No se modifican bootloader ni tabla de particiones. Aplicación en **0x10000**. Historial volátil de 20 mensajes y Recent Activity de 16 eventos sin cambios.

## Identidad y regresión

El nombre de distribución pasa a FINAL. About conserva EKOPARTY_RC1 / 5.0.0-rc1, build UTC 261002T002257Z y source ID 5538602671b6f3c0, porque se preserva el binario validado. Esto es intencional. El badge con RC1 ya instalada no necesita flasheo adicional.

Regresión host posterior a la investigación de 0xC000001D: 30 grupos PASS, exit 0, más repetición aislada exit 0. Los procesos históricos que fallaron están invalidados íntegramente. Ver TESTING.md para el resumen público de esa investigación. Esta promoción no ejecuta otra compilación ni necesita repetir pruebas funcionales de un binario sin cambios; se comprueban identidad, hashes e integridad del paquete.

## Recursos y límites de evidencia

Binario app: 1.940.336 bytes. Secciones flash: 1.939.458 bytes; RAM estática: 91.784 bytes. Mediciones del build RC1 conservadas. La validación funcional física no aporta valores numéricos de free heap, minimum free heap, largest free block, autonomía ni mediciones eléctricas/RF; siguen sin datos cuantitativos. Suite upstream Linux/Docker no ejecutada. No se inventan mediciones a partir del OK funcional.

## Congelamiento

Hasta después del evento: NO agregar funciones nuevas. Sólo se autoriza corregir un bug crítico o una regresión que afecte el uso normal. Cualquier corrección futura debe identificarse aparte y validarse; no sobrescribir este FINAL ni la baseline preservada.

Pendientes y mejoras experimentales quedan POST-EKOPARTY, detallados en POST_EKOPARTY.md. No bloquean esta entrega aprobada.

## Binarios

FINAL SHA-256: `77990ffab2a173ea4287493e6b269d3f229d4a9b794b631694f1583f50c1a611`.

Rollback SHA-256: `c52704a2e5bb477e54d0488bc7c8112c81a075d57b3d2cc7883d2676044bd0d9`.

El rollback es la última baseline anterior validada, audio-messages; no se sustituye por RC1. Se conservan también RC1 y los ZIP históricos originales fuera de este paquete. SHA256SUMS.txt dentro del paquete cubre ambos binarios y los archivos de instalación.

## Publicación 2026-10-29

El usuario informó uso satisfactorio en Ekoparty 2026. Se publica el mismo firmware FINAL, sin recompilar ni modificar funciones. Las restricciones del evento anteriores son el registro histórico; esta publicación no autoriza automáticamente nuevas funciones.
