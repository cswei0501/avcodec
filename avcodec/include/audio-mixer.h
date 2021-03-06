#ifndef _audio_mixer_h_
#define _audio_mixer_h_

#ifdef __cplusplus
extern "C" {
#endif

void audio_mixer_float(float* dst, const float* src, float mul, int len);

void audio_mixer_double(double* dst, const double* src, double mul, int len);

#ifdef __cplusplus
}
#endif
#endif /* !_audio_mixer_h_ */
