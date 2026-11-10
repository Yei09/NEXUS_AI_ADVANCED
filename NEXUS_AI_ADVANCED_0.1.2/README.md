# NEXUS AI Advanced 0.1.2
Experimental ARM64 Android app with llama.cpp native inference. Models are imported by the user through Android's document picker and copied into app-private storage. No model is bundled.

Status: implementation in progress. GGUF Q4 support comes from llama.cpp's GGUF loader. Device inference, memory usage, thermal behavior and installation must be validated on target hardware before release.

The CI workflow installs Android platform-tools separately to avoid requesting the obsolete SDK package `tools`.
