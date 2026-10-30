# POST-EKOPARTY — no implementar durante el freeze

- Investigar potencia RF superior a 10 dBm: requiere verificar físicamente módulo/etapa RF y mediciones con USB y 2×AA. Hasta entonces, 10 dBm.
- Caracterización eléctrica no invasiva de audio: carga/impedancia y potencia del parlante no declaradas; alimentación y acondicionamiento medidos. No aumentar excitación sobre L4 ni cambiar firma validada durante el freeze.
- Mediciones cuantitativas de free heap, minimum free heap y largest free block con historial vacío/20 y actividad 16; fragmentación, consumo y autonomía USB/2×AA en ensayos prolongados.
- Evaluar sólo después del evento vecinos directos actuales, hooks RF, sondeos, nuevos atajos y cualquier mejora experimental. No quedan aprobados automáticamente.
- Cualquier aumento de historial sobre 20, persistencia de historial o cambio de arquitectura requiere evaluación nueva y evidencia de memoria.
- Ampliar pruebas upstream Linux/Docker cuando exista entorno compatible.

Límites conocidos conservados: RSSI es la muestra del último salto; datos remotos pueden ser antiguos; contadores son de 32 bits; TX en cola no prueba entrega; alimentación/batería no fiable muestra --. Activity es volátil y no almacena textos.
