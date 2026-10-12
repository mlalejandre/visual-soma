# 🧠 SOMA v0.2 — AI-Native Operating System with Cosmic GUI

SOMA (Sistema Operativo Multi-Agente) es un sistema operativo experimental de 64 bits para arquitecturas x86_64 y AArch64 desarrollado desde cero en C freestanding y ensamblador, sin Linux, sin glibc ni dependencias externas.

Diseñado desde el hardware como entorno nativo para agentes de Inteligencia Artificial, SOMA integra un escritorio gráfico TrueColor acelerado en Ring 0, multitarea multiventana, pila de red TCP/IP y almacenamiento persistente.

---

## 🌌 Entorno de Escritorio Gráfico (Her-Aura Desktop)

SOMA incorpora un servidor visual con doble búfer, recorte por tijera (clipping), optimización por rectángulos de daño y mezcla alfa con soporte para resoluciones HD (1280×720) y Full HD (1920×1080) a 32 bpp TrueColor.

    +------------------------------------------------------------------------+
    |                        SOMA HER-AURA DESKTOP                           |
    |                                                                        |
    |        [Browser]          [Notepad]          [Files]         [Paint]   |
    |             \                 |                 |            /         |
    |              v                 v                 v          v          |
    |        +------------------------------------------------------+        |
    |        |        ANILLO ORBITAL DE ACRECIÓN Y HORIZONTE        |        |
    |        |            * Vórtice Central Cósmico (30 FPS)        |        |
    |        |     (Entrada Reactiva en Vivo / Streaming de LLM)    |        |
    |        +------------------------------------------------------+        |
    |              ^                 ^                 ^          ^          |
    |             /                 |                 |           \          |
    |        [Terminal]         [MemView]         [Layout]     [Agentes]     |
    |                                                                        |
    |  soma> analiza el estado del sistema y genera un informe_              |
    +------------------------------------------------------------------------+

- Escritorio Interactivo: Vórtice procedural de partículas a 30 FPS con renderizado de acreción y pulsos luminosos reactivos al razonamiento del LLM.
- Dock Orbital Dinámico: Los iconos orbitan alrededor del vórtice central y pueden ser absorbidos hacia el horizonte de sucesos mediante física en coma fija (16.8).
- Gestor Multiventana Flotante: Mueve, redimensiona interactivamente por los bordes (drag & resize), apila en profundidad Z y conmuta entre ventanas abiertas.
- Motor Tipográfico e Iconográfico Vectorial: Renderizado de curvas de Bézier y máscaras antialiasing de 8 sub-muestras por píxel (formato TrueType TTF sin librerías externas), junto a una colección de iconos vectoriales procedurales.

---

## 🖥️ Suite de Aplicaciones Gráficas Nativas

- SOMA Browser: Navegador visual con motor de renderizado HTML/CSS, imágenes locales/remotas BMP/PPM y adaptador para JavaScript reactivo.
- SOMA Paint: Editor bitmap sobre lienzo de 80×60 con lápiz, pincel, figuras geométricas, relleno por inundación (flood fill), paleta y guardado BMP a disco.
- SOMA File Manager: Explorador de archivos para SOMAFS02 con clasificación por tipo, tamaño, metadatos y apertura directa.
- SOMA Notepad: Editor de texto gráfico con seguimiento y volcado en vivo (live streaming) de los archivos creados por los agentes.
- SOMA Memory View: Visor en árbol del grafo de conocimiento persistente, la pizarra compartida (Blackboard) y la memoria episódica.
- Document Layout: Motor de maquetación editorial para previsualizar documentos estructurados bajo el estándar #SOMA-LAYOUT v1.
- Terminal Gráfica: Consola interactiva con emulación ANSI, historial persistente, autocompletado y tuberías (| , > , && , ||).

---

## 🤖 Jerarquía y Orquestación Multi-Agente

SOMA ejecuta un bucle cognitivo ReAct (Reason + Act) con agentes especializados:

- soma: Orquestador superior. Descompone objetivos complejos, delega tareas especializadas y sintetiza dictámenes finales.
- mayordomo: Agente operativo determinista y de baja latencia. Ejecuta consultas de memoria, operaciones de VFS, búsquedas y archivos sin consumir tokens LLM.
- buscador: Especialista en investigación externa web (DuckDuckGo / proxy HTTP) y recuperación semántica interna mediante algoritmo BM25 / TF-IDF.
- maquetador: Especialista editorial. Transforma síntesis e informes en documentos maquetados con jerarquía visual estructurada.
- Blackboard IPC: Pizarra persistente compartida (/agent/blackboard.txt) para consenso e intercambio de contexto entre agentes.

---

## 🌐 Red y Servicios Web en Ring 0

- VirtIO-NET: Controlador de red virtualizado para entornos QEMU (PCI legacy en x86_64, MMIO en AArch64).
- Pila TCP/IP Nativa: Ethernet, ARP dinámico, IPv4, ICMP (Ping), UDP y TCP de flujo confiable con retransmisión por temporizadores.
- Servidor HTTPD: Servidor web integrado en Ring 0 sirviendo un Dashboard interactivo y API REST en el puerto 80 (mapeado a http://localhost:8090/).

---

## 💾 Almacenamiento Persistente (SOMAFS02)

- VirtIO-BLK: Controlador de bloques de bajo nivel sobre disco físico virtual de 16 MiB.
- SOMAFS02: Sistema de archivos con 64 inodos, hasta 16 KiB por archivo, sincronización transaccional (dirty tracking) y partición de fábrica.
- SRCFS: Empaquetador que almacena el código fuente del kernel en sectores reservados para auto-inspección en tiempo de ejecución.
- Singularity Loop: Mecanismo donde el agente puede proponer parches de código C (SEARCH / REPLACE), reiniciar en modo prueba canaria y validar o revertir el parche automáticamente.

---

## 🚀 Puesta en Marcha

1. Clonar el repositorio:
    git clone https://github.com/tu-usuario/soma.git
    cd soma

2. Compilar y arrancar el entorno gráfico:
    python3 ejecutar.py

   (Para arquitectura ARM64 añadir: python3 ejecutar.py --arch aarch64)

3. Dashboard Web:
    Abrir en el navegador del host: http://localhost:8090/

4. Controles básicos de la interfaz:
    - Escribir en cualquier momento: el orquestador captura el texto directamente.
    - Tecla X: alterna la visibilidad del dock orbital de iconos.
    - ESC: cierra o minimiza la ventana activa hacia el escritorio cósmico.

---

## 📂 Estructura del Proyecto

    soma/
    ├── Dockerfile          # Toolchain de compilación cruzada reproducible
    ├── Makefile            # Reglas de construcción para x86_64 y AArch64
    ├── ejecutar.py         # Runner maestro con soporte multi-arquitectura
    ├── grub/               # Configuración de arranque GRUB Multiboot
    ├── src/
    │   ├── arch/           # HAL: arranque, IDT, VMM (paginación), PMM
    │   ├── core/           # Kernel, planificador KThreads, mutex, heap
    │   ├── drivers/        # VirtIO-NET, VirtIO-BLK, Framebuffer BGA, Ratón
    │   ├── fs/             # SOMAFS02, VFS virtual, BM25 y SRCFS
    │   ├── net/            # Pila IP, TCP, UDP, ARP, DNS, cliente HTTP y HTTPD
    │   ├── ui/             # Escritorio Her-Aura, compositor 2D y consola
    │   ├── apps/           # Browser, Paint, Notepad, File Manager, MemView, Layout
    │   ├── fonts/          # Rasterizador vectorial TrueType e iconos
    │   └── agent/          # Agentes ReAct, integración LLM y shell
    └── docs/               # Especificaciones y diseño del sistema

---

## 📄 Licencia

Distribuido bajo licencia MIT. Consulta el archivo LICENSE para más detalles.
