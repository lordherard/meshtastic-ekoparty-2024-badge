# Compilar el port

Fuente Meshtastic upstream: 60f82b448883098b83f3b4b43687b3fcdbab7069 (2.8.1). Snapshot local base del port: 8c3f781d3cb7fd4cb50bd31aabb16c51c0b6cbf2. Las modificaciones finales están incorporadas en este árbol; build-identity.json permite comprobarlas.

Inicializar submódulos con `git submodule update --init --recursive` tras clonar. Protobufs: 9d6a3d1bd14eccb4581a77af42dc615c911a07c9; meshtestic: dcac7e5673005f4d8a2b1f0f6e06877b689d7519.

Entorno usado: Python 3.12, pioarduino 6.1.19, esptool 5.3.0. En PowerShell, desde la raíz:

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) '.pio-core'
py -3.12 -m venv .pio-core/penv
.\.pio-core\penv\Scripts\python.exe -m pip install pioarduino==6.1.19 esptool==5.3.0
.\.pio-core\penv\Scripts\python.exe -m platformio run -e meshtastic_eko_badge_2024
```

La plataforma está fijada en variants/esp32c6/eko_badge_2024/platformio.ini. La primera compilación descarga herramientas y requiere varios GB. No actualizar herramientas indiscriminadamente: pioarduino 6.2.0/SCons 4.11.1 fue incompatible con la plataforma usada.

Salida: .pio/build/meshtastic_eko_badge_2024/. No se garantiza igualdad binaria al recompilar por dependencias transitivas y metadatos de build. Para instalar la versión validada usar la release EKOPARTY_FINAL. Esta preparación para GitHub no recompiló firmware ni probó una compilación desde un clon remoto limpio.

La aplicación publicada requiere el bootloader y particionado del port ya instalados: app 0x10000, 4 MiB de flash. No se incluye una imagen de fábrica para un badge virgen. No escribir la app en 0x0 ni borrar toda la flash.
