# SOMA OS: base para JavaScript

Este parche añade un adaptador opcional a QuickJS. **No activa JavaScript en las
páginas por sí solo**: el parser y el renderizador actuales aplanan HTML a una
lista de dibujo y no proporcionan un DOM completo al que pueda conectarse el
motor. Se conserva el navegador existente y se entrega una API independiente
para integrar la ejecución de scripts por pasos.

## Qué instala

- `src/apps/js_runtime.h` / `src/apps/js_runtime.c`: API y adaptador de QuickJS.
- `tests/js_runtime_contract.c`: prueba de contrato para modo deshabilitado y,
  si se activa QuickJS, una prueba mínima de ejecución.
- La interfaz host prevista enlaza `document.getElementById(id).textContent`,
  `element.onclick = fn`, `element.addEventListener("click", fn)` y `console.log`.
- Límites configurables de memoria, pila y ejecución. Sin reloj monotónico host,
  el tiempo se acota mediante un presupuesto de comprobaciones de interrupción.

## Paso 1: verificar el modo seguro (sin motor)

Desde la raíz de SOMA:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/js_runtime_contract.c src/apps/js_runtime.c -o /tmp/soma-js-runtime-test
/tmp/soma-js-runtime-test
```

En este modo no se interpreta JavaScript. El propósito es comprobar que el módulo
puede incorporarse sin introducir una dependencia obligatoria.

## Paso 2: portar QuickJS al target

Obtén el código oficial de QuickJS desde <https://bellard.org/quickjs/> o su
repositorio oficial <https://github.com/bellard/quickjs>, revisa y conserva el
archivo `LICENSE`, y colócalo en `third_party/quickjs/`.

Antes de definir `SOMA_ENABLE_QUICKJS`, comprueba la portabilidad del motor con
el toolchain y los servicios del kernel de SOMA. El adaptador de esta primera
versión usa las asignaciones estándar que ofrece QuickJS (`JS_NewRuntime()` y
`malloc/free` para los objetos puente). Para un kernel freestanding hay que
adaptar la asignación de QuickJS al heap real de SOMA y no dar por supuesta la
presencia de una libc completa.

Al compilar `src/apps/js_runtime.c`, añade `-DSOMA_ENABLE_QUICKJS` y el include
del directorio QuickJS. El enlazador debe incluir los fuentes del motor requeridos
por la versión elegida; no basta con añadir el include. El Makefile del proyecto
no se modifica automáticamente porque no se dispone aquí de su lista real de
fuentes ni de sus reglas de enlace.

## Paso 3: integrar con el navegador real

El módulo debe vivir en el hilo de navegador; no se deben llamar objetos QuickJS
simultáneamente desde varios hilos. El integrador debe:

1. Crear una instancia al abrir una página y destruirla al navegar a otra.
2. Mantener un DOM o un mapa estable de IDs a nodos/elementos renderizables.
3. Implementar `has_element`, `get_text_content`, `set_text_content` y `log_message`
   usando dicho DOM. Nunca se debe mapear un ID web a memoria arbitraria del kernel.
4. Extraer scripts inline del HTML y llamar a `soma_js_eval()` una vez que el DOM
   inicial relevante esté registrado. Mantener el orden de ejecución HTML.
5. Al clicar en un elemento, llamar a `soma_js_dispatch_click(runtime, id)`.
6. Invalidar nodos y controladores al destruir el documento para que no sobrevivan
   callbacks de una página anterior.

La implementación legacy que se describe en `estructuranavegador.txt` no conserva
IDs ni nodos DOM en su display list; por ello, el parche no altera de forma automática
`browser.c` ni simula que ya existe un DOM. Esa integración requiere los fuentes
completos y cambios verificables en parser, renderizador y manejador de clics.

## Límites funcionales de esta base

- JavaScript clásico inline; aún no módulos ES, `async`/`await` ligado a un event loop,
  promesas con scheduling web, `fetch`, timers, cookies, `localStorage` ni Web APIs.
- `document.getElementById`, `textContent`, `onclick`, `addEventListener("click", ...)`
  y `console.log/warn/error` están previstos en el puente QuickJS; dependen de que
  el host proporcione IDs y callbacks correctos.
- `addEventListener` solo registra el evento `click` en esta iteración.
- No se exponen módulos de sistema `std`/`os`, syscalls, sockets ni acceso directo al kernel.
- Los límites de memoria de QuickJS no sustituyen el aislamiento del proceso ni una
  auditoría de seguridad. Todo script remoto debe tratarse como código no confiable.

## Compatibilidad del Makefile

El instalador no cambia reglas de compilación que no puede reconocer. Si el Makefile
usa una lista fija de fuentes C, añade `src/apps/js_runtime.c` junto al fuente del
navegador. En modo deshabilitado no tiene dependencia de QuickJS. Para habilitar el
motor, además hay que compilar y enlazar la versión portada de QuickJS.
