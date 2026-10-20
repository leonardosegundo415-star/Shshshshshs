# Predictive Input Buffer (Mobile)

Recreación para Android del concepto de lilWC.

- **Release buffer**: un toque corto se mantiene pulsado unos ticks, así aterrizas y saltas aunque sueltes antes.
- **Press buffer en orbes**: si pulsas unos ticks antes de tocar un orbe, el click se repite al tocarlo (una sola vez por click).
- **Dos límites**: el primer número vale para cube, ball, ufo y spider; el segundo para ship, swing, wave y robot.
- **Seguridad**: se reinicia al morir y un seguro por tiempo suelta cualquier hold que se quede pegado.

Cambia el nivel en los ajustes del mod o con el botón **Buffer** del menú de pausa (sustituye a F6 de PC).
Niveles: `2/2`, `4/4`, `8/8`, `16/8`, `TEST` (solo para comprobar) y `OFF`.
La pausa muestra `Inputs | Hold | Orb` para ver si el buffer está actuando.
