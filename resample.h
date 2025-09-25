#include <stdint.h>

typedef struct Resample {
  float*buf;
  float**filters;
  double offset;
  int32_t index;
  uint32_t pin,pout;
  uint16_t ntap,nsam,nfil;
  uint8_t flag;
} Resample;
/*
  This structure is used for the resampling state, and the values that are
  precalculated when initialized. Most fields are used internally and you
  probably should not touch them, but see the description of the resampling
  process functions for the use of the "pin" and "pout" fields.
*/

#define RESAMPLE_LOWPASS 1
#define RESAMPLE_BLACKMAN_HARRIS 2
#define RESAMPLE_SUBSAMPLE_INTERPOLATE 4
#define RESAMPLE_SHARE 8

#define RESAMPLE_OK (0)
#define RESAMPLE_ERROR_ARGUMENT (-1)
#define RESAMPLE_ERROR_MEMORY (-2)

int resample_init(Resample*obj,int ntap,int nfil,double lpratio,uint8_t flag);
/*
  Initialize the Resample object. It must already be allocated (static or
  dynamic), but the fields do not need to be filled in yet; it does not
  matter what it contains unless the RESAMPLE_SHARE flag is specified.

  ntap = Number of taps, from 4 to 1024; must be a multiple of four.

  nfil = Number of filters, from 1 to 1024.

  lpratio = Low pass filter ratio; specify 1.0 if not using low pass. This
  is recommended for downsampling in order to avoid aliasing, but can be
  used whether or not you are doing downsampling.

  flags = Bitwise OR of the following:

  RESAMPLE_LOWPASS = Enable lowpass filter. The lpratio should be greater
  than 0.0 and less than 1.0 if using the lowpass filter.

  RESAMPLE_BLACKMAN_HARRIS = Use Blackman Harris instead of Hann window.

  RESAMPLE_SUBSAMPLE_INTERPOLATE = Enable subsample interpolation.

  RESAMPLE_SHARE = If set, then it is expected to be a Resample object
  copied from an existing one; the parameters are copied from the original
  but it is otherwise independent. The original one should not be freed if
  any of the copies are still in use, since the copies share the same
  memory for the filters.

  Returns RESAMPLE_OK if OK, RESAMPLE_ERROR_ARGUMENT if the arguments are
  wrong, or RESAMPLE_ERROR_MEMORY in case memory allocation fails. If the
  allocation fails, some things might have already been allocated; you can
  still use resample_uninit to free them.
*/

void resample_uninit(Resample*obj);
/*
  Uninitialize Resample object; free memory other than Resample itself. If
  it has the RESAMPLE_SHARE flag, then only the buffer is freed, and it can
  be initialized again with the RESAMPLE_SHARE flag later if desired.

  After calling this (whether or not the RESAMPLE_SHARE flag is used), you
  should not use this Resample object again until it has been initialized.
*/

void resample_reset(Resample*obj);
/*
  Reset the Resample object to its initial state, causing previous input
  samples to not affect any further output, and forces any further output
  to be aligned.
*/

void resample_process_int16(Resample*obj,int16_t*in,uint32_t nin,int16_t*out,uint32_t nout,double ratio);
/*
  Do resampling with signed 16-bit integers.

  in = Input buffer.

  nin = Size of input buffer.

  out = Output buffer.

  nout = Size of output buffer.

  ratio = Ratio of resampling; the number of output samples is
  approximately equal to this ratio multiplied by the number of
  input samples.

  It will increase obj->pin by the number of input samples used and will
  increase obj->pout by the number of output samples used; those fields
  have no other use, so you can initialize them to values that you will
  use to keep track of the input/output positions for your own use.
*/

void resample_process_int16_to_float_mix(Resample*obj,int16_t*in,uint32_t nin,float*out,uint32_t nout,float vol,double ratio);
/*
  Like above, but the output is floating point format (with no inherent
  scaling; 32767 corresponds directly to 32767.0), and it is mixed with
  existing audio in the output buffer, and you can specify the volume to
  multiply the resampled data by.
*/
