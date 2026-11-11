# NEXUS AI Advanced 0.1.3 — candidata técnica

Esta rama independiente prepara el APK ARM64 de 0.1.3 a partir de la integración nativa de llama.cpp de 0.1.2. El motor llama.cpp y el selector/importador GGUF se heredan y se compilan en esta versión; el cambio de versión es explícito (versionCode 13, versionName 0.1.3).

**Estado:** candidata técnica, no versión final completa. El código actual aún no implementa streaming, cancelación, historial/contexto multi-turno, memoria cifrada, UI Gemini, ni herramientas Android. La compilación CI no demuestra inferencia real ni instalación en el Redmi Note 14 5G. No se debe presentar como versión final hasta cerrar esas pruebas y funciones.

Modelo GGUF no incluido en el APK. Importación local mediante el selector de documentos. Sin permiso INTERNET declarado en el manifest.

Workflow de CI configurado en `.github/workflows/build-0.1.3.yml`.
