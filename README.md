# 🧠 SOMA v0.2 — AI-Native Multi-Agent Operating System

![Architecture](https://img.shields.io/badge/Architecture-x86__64%20%7C%20AArch64-blue?style=for-the-badge)
![Language](https://img.shields.io/badge/Language-Freestanding%20C%20%7C%20ASM-green?style=for-the-badge)
![Storage](https://img.shields.io/badge/Storage-SOMAFS02%20%2816%20KiB%29-orange?style=for-the-badge)
![AI](https://img.shields.io/badge/AI-Hierarchical%20ReAct%20Agents-purple?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-brightgreen?style=for-the-badge)

**SOMA** (*Sistema Operativo Multi-Agente*) es un sistema operativo experimental de 64 bits desarrollado desde cero en **C freestanding y ensamblador**, sin Linux, glibc, POSIX ni dependencias de runtime de propósito general.

El objetivo de SOMA es explorar una arquitectura **AI-native**, donde agentes basados en LLM puedan interactuar con un entorno operativo controlado por el propio kernel, con acceso directo a los subsistemas de memoria, almacenamiento, red y ejecución.

> ⚠️ **Estado del proyecto:** experimental. SOMA está orientado a investigación, prototipado y experimentación con sistemas operativos y agentes autónomos.

---

## ✨ Características principales

### 🤖 Inteligencia artificial y orquestación multiagente

- **Kernel ReAct:** bucle de razonamiento/acción integrado en el entorno del sistema que permite ejecutar herramientas y evaluar telemetría.
- **Orquestación jerárquica:** el agente principal (`soma`) puede delegar tareas especializadas a otros agentes, como `buscador`.
- **Blackboard IPC:** intercambio de contexto entre agentes mediante una pizarra compartida persistente en `/agent/blackboard.txt`.
- **Herramientas nativas:** los agentes interactúan con funcionalidades del sistema mediante comandos y primitivas expuestas por el kernel.

### 🔍 Motor de búsqueda semántica bare-metal

- **BM25 / TF-IDF:** motor de recuperación de información implementado en C para buscar contenido almacenado en el sistema de archivos.
- **Normalización textual:** soporte para normalización UTF-8 y comparación de términos con normalización de diacríticos.
- **Búsqueda sobre memoria persistente:** integración con el almacenamiento de SOMA.

### 🌐 Networking y servicios Web

- **VirtIO-NET:** controlador de red para entornos virtualizados mediante QEMU.
- **Ethernet, ARP e IPv4:** implementación de los componentes fundamentales de la pila de red.
- **ICMP, UDP y TCP:** soporte para comunicación y diagnóstico.
- **HTTPD:** servidor HTTP integrado en el sistema.
- **API REST:** endpoints JSON para exponer telemetría y estado del sistema.
- **Dashboard Web:** interfaz HTML para observar el estado operativo desde el host.

### 💾 Almacenamiento y memoria

- **SOMAFS02:** sistema de archivos experimental sobre VirtIO-BLK.
- **Hasta 16 KiB por archivo:** límite definido por el formato actual.
- **Hasta 64 inodos dinámicos:** capacidad correspondiente a la implementación actual.
- **Espacios de trabajo:** separación lógica mediante rutas como `/workspace/` y `/mis_archivos/`.
- **KHeap:** asignador dinámico utilizado por el kernel y sus subsistemas.
- **Memoria virtual:** soporte de paginación de cuatro niveles en x86_64.

---

## 🏗️ Arquitectura

```text
┌───────────────────────────────────────────────────────────────┐
│                    SOMA BARE-METAL KERNEL                    │
│                                                               │
│  ┌───────────────────┐       ┌─────────────────────────────┐ │
│  │   Interactive CLI │◄─────►│ Autonomous ReAct            │ │
│  │   UTF-8 / COM1    │       │ Orchestrator: soma          │ │
│  └─────────┬─────────┘       └──────────────┬──────────────┘ │
│            │                                │                 │
│            │                    delegate    │                 │
│            │                                ▼                 │
│  ┌─────────▼─────────┐       ┌─────────────────────────────┐ │
│  │   SOMAFS / RamFS  │◄─────►│ Research Agent              │ │
│  │ /workspace /mem   │       │ buscador / Blackboard IPC   │ │
│  └─────────┬─────────┘       └──────────────┬──────────────┘ │
│            │                                                  │
│  ┌─────────▼───────────────────────────────────────────────┐ │
│  │       BM25 / TF-IDF + KHeap + Memory Management         │ │
│  └───────────────────────────┬─────────────────────────────┘ │
│                              │                                │
│  ┌───────────────────────────▼─────────────────────────────┐ │
│  │                 Networking / HTTPD                      │ │
│  │             TCP · UDP · IPv4 · ARP · ICMP              │ │
│  └───────────────────────────┬─────────────────────────────┘ │
│                              │                                │
│             ┌────────────────▼────────────────┐               │
│             │       VirtIO-NET / VirtIO-BLK   │               │
│             └────────────────┬────────────────┘               │
└──────────────────────────────┼────────────────────────────────┘
                               │ VirtIO Queues
                               ▼
┌───────────────────────────────────────────────────────────────┐
│                           QEMU                                │
│                 Host port forwarding :8090                   │
└──────────────────────────────┬────────────────────────────────┘
                               │ SLIRP NAT / LAN
                               ▼
┌───────────────────────────────────────────────────────────────┐
│                       LAN / AI Server                         │
│                      llama.cpp / API                          │
│                  192.168.1.200:8087                           │
└───────────────────────────────────────────────────────────────┘
```

---

## 🚀 Quickstart

SOMA utiliza Docker para proporcionar un entorno reproducible de compilación cruzada y QEMU para ejecutar el sistema virtualizado.

### 1. Clonar el repositorio
```bash
git clone https://github.com/mlalejandre/soma.git
cd soma
```

### 2. Compilar y ejecutar
```bash
python3 ejecutar.py
```
El script prepara el entorno de compilación, construye los componentes C/ASM, genera la imagen de arranque y lanza QEMU.

### 3. Abrir el Dashboard
Una vez iniciado QEMU:
```text
http://localhost:8090/
```
El puerto 8090 corresponde al forwarding configurado en el host; el servidor HTTP interno utiliza el puerto 80.

---

## 💻 Shell soma>

Una vez arrancado el sistema, SOMA proporciona una shell interactiva orientada a operaciones del kernel, almacenamiento, red y agentes.

### 🤖 Multiagente

| Comando | Descripción |
| :--- | :--- |
| `soma <misión>` | Asigna un objetivo al orquestador principal. |
| `delegate <agente> "<misión>"` | Delega una tarea a un agente especializado. |
| `bm25 <término>` | Busca información relevante en el almacenamiento persistente. |

### 🌐 Sistema y red

| Comando | Descripción |
| :--- | :--- |
| `somafetch` | Muestra información y telemetría del sistema. |
| `httpd` | Muestra el estado del servidor HTTP. |
| `ps` / `threads` | Muestra tareas y threads activos. |
| `ping <ip>` | Ejecuta una prueba ICMP. |
| `curl <url>` | Realiza una petición HTTP mediante la pila de red. |

### 💾 Sistema de archivos

| Comando | Descripción |
| :--- | :--- |
| `ls -a /workspace` | Lista el contenido del workspace. |
| `cat <archivo>` | Muestra el contenido de un archivo. |
| `nano <archivo>` | Abre el editor de texto interactivo. |

---

## 🤝 Ejemplo: coordinación autónoma

```text
soma> investiga los principios de la computación cuántica y guarda tus notas
```

### Flujo conceptual:
```text
Usuario
   │
   ▼
 soma
   │
   ├── analiza el objetivo
   │
   ├── delega ───────────────► buscador
   │                             │
   │                             ├── search
   │                             ├── curl
   │                             └── sintetiza información
   │                                      │
   │                                      ▼
   │                              Blackboard IPC
   │                                      │
   ◄──────────────────────────────────────┘
   │
   ├── lee el contexto compartido
   │
   ├── genera el informe
   │
   └── write
          │
          ▼
 /workspace/notas_quantica.txt
```

### Flujo paso a paso:
1. `soma` recibe el objetivo y determina qué información necesita.
2. El orquestador delega la investigación al agente `buscador`.
3. `buscador` utiliza las herramientas disponibles para recuperar información.
4. El agente sintetiza los resultados y los publica en el Blackboard.
5. `soma` recupera el contexto compartido.
6. El orquestador genera el resultado final.
7. El informe queda persistido en `/workspace/notas_quantica.txt`.
8. El resultado puede consultarse posteriormente mediante la shell o el Dashboard Web.

---

## 🧩 Componentes principales

```text
kernel/
├── arch/          # Arquitecturas
├── boot/          # Arranque
├── drivers/       # VirtIO y dispositivos
├── mm/            # Gestión de memoria
├── net/           # Networking
├── fs/            # SOMAFS / RamFS
├── ai/            # Agentes y orquestación
├── search/        # BM25 / TF-IDF
└── httpd/         # Servidor HTTP / API
```
La estructura es conceptual y puede variar según la versión del árbol de código.

---

## 🔐 Modelo de seguridad

SOMA ejecuta gran parte de su funcionalidad privilegiada en **Ring 0** dentro del entorno virtualizado.

Esto permite:
- Acceso directo a primitivas del kernel.
- Control sobre memoria y dispositivos virtualizados.
- Integración estrecha entre IA y sistema operativo.
- Una arquitectura con pocas capas de abstracción entre agentes y kernel.

Sin embargo:
- Ring 0 proporciona privilegios, no aislamiento de seguridad entre agentes.
- Por ello, los agentes con capacidades privilegiadas deben considerarse confiables.
- **No ejecutes agentes, modelos, código o entradas no confiables en una instancia de SOMA conectada a una red sensible.**

---

## 🧪 Entorno de desarrollo

### Requisitos
- Linux, macOS o Windows con Docker.
- Docker / Docker Compose.
- Python 3.
- QEMU.
- Toolchain compatible con x86_64 y/o AArch64.

### Integración con un modelo local
```text
llama.cpp
    │
    ▼
192.168.1.200:8087
    │
    ▼
  SOMA
```
La dirección IP anterior representa una configuración de laboratorio y puede cambiarse según el entorno.

---

## 📊 Objetivos del proyecto

```text
                 ┌────────────────────┐
                 │ Artificial          │
                 │ Intelligence        │
                 └─────────┬──────────┘
                           │
                           ▼
              ┌─────────────────────────┐
              │         SOMA            │
              │   AI-Native OS Layer    │
              └────────────┬────────────┘
                           │
             ┌─────────────┴─────────────┐
             ▼                           ▼
   ┌──────────────────┐        ┌──────────────────┐
   │ Operating        │        │ Autonomous       │
   │ Systems          │        │ Agents           │
   └──────────────────┘        └──────────────────┘
```

El proyecto investiga, entre otros temas:
- Sistemas operativos especializados para agentes.
- Tool use a nivel de sistema.
- Orquestación multiagente.
- Memoria persistente para agentes.
- Recuperación de información integrada en el sistema.
- Networking nativo para agentes autónomos.
- Interfaces humano-IA integradas en el sistema operativo.
- Arquitecturas AI-native sin depender de un sistema operativo convencional.

---

## 📌 Estado del proyecto

**SOMA v0.2 — Experimental**  
Las APIs, estructuras internas, protocolos y formatos de almacenamiento pueden cambiar entre versiones. No se garantiza compatibilidad binaria, estabilidad de ABI ni compatibilidad hacia atrás.

---

## 📄 Licencia

SOMA se distribuye bajo la licencia **MIT**. Consulta `LICENSE` para conocer los términos completos.

---

## 🤝 Contribuir

Las contribuciones son bienvenidas. Antes de abrir un Pull Request:
- Comprueba que el proyecto compila.
- Ejecuta las pruebas disponibles.
- Documenta cambios en interfaces o formatos.
- Evita introducir dependencias innecesarias en el kernel.
- Mantén separadas las capas de arquitectura, drivers, red, FS y agentes.

Para cambios importantes, documenta:
- Problema y motivación.
- Diseño propuesto.
- Impacto sobre ABI/API, memoria y rendimiento.
- Compatibilidad con x86_64 y AArch64.

---

## ⭐ Filosofía

> *Un sistema operativo no debería limitarse a ejecutar agentes.*  
> *También puede convertirse en el entorno operativo nativo de esos agentes.*  
> **SOMA explora esa posibilidad.**
