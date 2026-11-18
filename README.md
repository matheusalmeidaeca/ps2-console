# PS2 Console (esqueleto)
App Kotlin (biblioteca/BIOS/ISO/controles) + ponte JNI em C++.
O núcleo PS2 (ex.: PCSX2 libretro, GPL) entra como `libpcsx2_libretro.so` em `app/src/main/jniLibs/arm64-v8a/`.
Use só BIOS e jogos do seu próprio console. Próximo passo: integrar libretro (retro_load_game/retro_run) em `native_bridge.cpp`.
