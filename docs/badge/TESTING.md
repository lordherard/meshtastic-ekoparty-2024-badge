# Evidencia de validación

30 grupos host Fases 1–5/audio pasaron con exit code 0 después de investigar el crash Windows 0xC000001D. Una ejecución aislada adicional también pasó. Las ejecuciones previas que fallaron se invalidaron completas. Causa: test que almacenaba 255 en enum Role fuera de rango; trampa UBSan. Se corrigió el test y la propagación del exit code, sin cambiar firmware.

Los tests están en test/test_eko_badge_composer. El runner original dependía de rutas locales/toolchain Zig 0.13 y stubs; no se presenta como una prueba portátil lista para CI. Se conserva una copia de referencia en tooling/badge/run_tests_reference.py (no ejecutar sin adaptar rutas). No se habilitan workflows upstream automáticamente.

Esta preparación no repite tests funcionales: conserva las fuentes de producción y binarios exactos, comprueba hashes y revisa el empaquetado. La suite upstream Linux/Docker no se ejecutó. Heap real cuantitativo, autonomía y caracterización eléctrica no fueron medidos en estos resultados. El OK físico fue declarado por el usuario, seguido de uso satisfactorio en el evento.
