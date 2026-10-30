# Meshtastic para el badge EKOPARTY 2024

Port comunitario para el badge de Electronic Cats EKOPARTY 2024, basado en Meshtastic. **EKOPARTY_FINAL fue validado físicamente y utilizado satisfactoriamente en Ekoparty 2026 por su propietario.** No es una release oficial de Meshtastic ni implica certificación de otros badges.

## Funciones

- Mensajes por canal y directos, conversaciones e historial volátil compartido de 20 mensajes.
- Teclado en el badge y quick replies configurables.
- Notificaciones LED, no leídos y alerta sonora L4; Sound ON/OFF y Test Sound.
- BLE con la app Meshtastic; protección del destino del borrador ante cambios externos.
- Mesh Stats, Node Inspector, Recent Activity de 16 eventos, Diagnostics y About/Home.
- Timeout/wake, persistencia de ajustes y funcionamiento con 2×AA validados.

## Hardware y límites

ESP32-C6, flash 4 MiB, radio de la familia RF95, pantalla OLED y TPA321. El firmware mantiene **RF 10 dBm** y **audio L4**. No aumentar excitación ni inferir la impedancia/potencia del parlante. Configurar la región apropiada al lugar de uso; no se publican claves ni configuraciones personales de canales.

## Instalar o recuperar

Descargar y extraer el paquete EKOPARTY_FINAL de Releases. Requiere Python con esptool 5.3.0. Con el badge en modo de carga y COM6 libre:

```powershell
python flash.py --port COM6
```

Para volver a la última baseline anterior validada:

```powershell
python flash.py --rollback --port COM6
```

El instalador verifica SHA-256, escribe sólo aplicación en **0x10000** y verifica la flash. Al terminar, RESET sin BOOT. Conserva preferencias, bootloader y particiones. El paquete es una actualización para el particionado existente del port; no una instalación de fábrica. Si ya instalaste RC1, no necesitás reinstalar: FINAL es idéntico byte por byte. About muestra RC1 deliberadamente.

## Código y validación

- [Compilación y procedencia](docs/badge/BUILD.md).
- [Reporte final](docs/badge/PHASE5_REPORT.md).
- [Pruebas host y límites](docs/badge/TESTING.md).
- [Pendientes postevento](docs/badge/POST_EKOPARTY.md).
- [Proyecto Meshtastic original](https://github.com/meshtastic/firmware).

La publicación no modifica el firmware validado. El freeze de EKOPARTY_FINAL queda como referencia inmutable; mejoras futuras requieren versiones separadas y autorización, no sobrescribir FINAL.

## Licencia y créditos

Se conserva [GPL-3.0](LICENSE) del firmware Meshtastic y los avisos originales de sus autores. Los componentes/dependencias conservan sus respectivas licencias. Hardware: Electronic Cats / badge EKOPARTY 2024. Este repositorio es un port independiente; no representa a esos proyectos u organizaciones.
