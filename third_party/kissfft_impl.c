/* Compile both kiss_fft and kiss_fftr in float mode (default) into our
 * thirdparty static lib. The visualizer uses kiss_fftr since audio frames
 * are real-valued.
 */
#include "kissfft/kiss_fft.c"
#include "kissfft/kiss_fftr.c"
