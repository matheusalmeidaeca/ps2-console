# PS2 Console (esqueleto)
App Kotlin (biblioteca/BIOS/ISO/controles) + ponte JNI em C++.
O núcleo PS2 (ex.: PCSX2 libretro, GPL) entra como `libpcsx2_libretro.so` em `app/src/main/jniLibs/arm64-v8a/`.
Use só BIOS e jogos do seu próprio console. Host libretro pronto (software video). Pendente: vídeo HW (EGL/Vulkan), áudio e compilar o núcleo PCSX2 para arm64.

## Núcleo
Workflow `Build núcleo PS2`: compila o libretro (padrão: ARMSX2, GPL) e gera o .so como artifact.
Coloque o .so em `app/src/main/jniLibs/arm64-v8a/libpcsx2_libretro.so` (renomeie) e faça novo push.
BIOS vai em `system/pcsx2/bios`; a pasta `resources` do núcleo deve ir em `system/pcsx2/resources`.
