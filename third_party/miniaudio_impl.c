#define MINIAUDIO_IMPLEMENTATION
/* Trim what we don't need to keep the binary lean. Linux-only build. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#include "miniaudio/miniaudio.h"
