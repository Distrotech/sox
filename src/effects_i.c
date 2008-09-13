/* Implements a libSoX internal interface for implementing effects.
 * All public functions & data are prefixed with lsx_ .
 *
 * (c) 2005-8 Chris Bagwell and SoX contributors
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef NDEBUG /* Enable assert always. */
#undef NDEBUG /* Must undef above assert.h or other that might include it. */
#endif

#include "sox_i.h"
#include <assert.h>
#include <string.h>

#undef sox_fail
#define sox_fail sox_globals.subsystem=effp->handler.name,sox_fail

int lsx_usage(sox_effect_t * effp)
{
  if (effp->handler.usage)
    sox_fail("usage: %s", effp->handler.usage);
  else
    sox_fail("this effect takes no parameters");
  return SOX_EOF;
}

/* here for linear interp.  might be useful for other things */
unsigned lsx_gcd(unsigned a, unsigned b)
{
  if (b == 0)
    return a;
  else
    return lsx_gcd(b, a % b);
}

unsigned lsx_lcm(unsigned a, unsigned b)
{
  /* parenthesize this way to avoid unsigned overflow in product term */
  return a * (b / lsx_gcd(a, b));
}

enum_item const lsx_wave_enum[] = {
  ENUM_ITEM(SOX_WAVE_,SINE)
  ENUM_ITEM(SOX_WAVE_,TRIANGLE)
  {0, 0}};

void lsx_generate_wave_table(
    lsx_wave_t wave_type,
    sox_data_t data_type,
    void *table,
    size_t table_size,
    double min,
    double max,
    double phase)
{
  uint32_t t;
  uint32_t phase_offset = phase / M_PI / 2 * table_size + 0.5;

  for (t = 0; t < table_size; t++)
  {
    uint32_t point = (t + phase_offset) % table_size;
    double d;
    switch (wave_type)
    {
      case SOX_WAVE_SINE:
      d = (sin((double)point / table_size * 2 * M_PI) + 1) / 2;
      break;

      case SOX_WAVE_TRIANGLE:
      d = (double)point * 2 / table_size;
      switch (4 * point / table_size)
      {
        case 0:         d = d + 0.5; break;
        case 1: case 2: d = 1.5 - d; break;
        case 3:         d = d - 1.5; break;
      }
      break;

      default: /* Oops! FIXME */
        d = 0.0; /* Make sure we have a value */
      break;
    }
    d  = d * (max - min) + min;
    switch (data_type)
    {
      case SOX_FLOAT:
        {
          float *fp = (float *)table;
          *fp++ = (float)d;
          table = fp;
          continue;
        }
      case SOX_DOUBLE:
        {
          double *dp = (double *)table;
          *dp++ = d;
          table = dp;
          continue;
        }
      default: break;
    }
    d += d < 0? -0.5 : +0.5;
    switch (data_type)
    {
      case SOX_SHORT:
        {
          short *sp = table;
          *sp++ = (short)d;
          table = sp;
          continue;
        }
      case SOX_INT:
        {
          int *ip = table;
          *ip++ = (int)d;
          table = ip;
          continue;
        }
      default: break;
    }
  }
}

/*
 * lsx_parsesamples
 *
 * Parse a string for # of samples.  If string ends with a 's'
 * then the string is interpreted as a user calculated # of samples.
 * If string contains ':' or '.' or if it ends with a 't' then its
 * treated as an amount of time.  This is converted into seconds and
 * fraction of seconds and then use the sample rate to calculate
 * # of samples.
 * Returns NULL on error, pointer to next char to parse otherwise.
 */
char const * lsx_parsesamples(sox_rate_t rate, const char *str, size_t *samples, int def)
{
    int found_samples = 0, found_time = 0;
    int time = 0;
    long long_samples;
    float frac = 0;
    char const * end;
    char const * pos;
    sox_bool found_colon, found_dot;

    for (end = str; *end && strchr("0123456789:.ts", *end); ++end);
    if (end == str)
      return NULL;

    pos = strchr(str, ':');
    found_colon = pos && pos < end;

    pos = strchr(str, '.');
    found_dot = pos && pos < end;

    if (found_colon || found_dot || *(end-1) == 't')
        found_time = 1;
    else if (*(end-1) == 's')
        found_samples = 1;

    if (found_time || (def == 't' && !found_samples))
    {
        *samples = 0;

        while(1)
        {
            if (str[0] != '.' && sscanf(str, "%d", &time) != 1)
                return NULL;
            *samples += time;

            while (*str != ':' && *str != '.' && *str != 0)
                str++;

            if (*str == '.' || *str == 0)
                break;

            /* Skip past ':' */
            str++;
            *samples *= 60;
        }

        if (*str == '.')
        {
            if (sscanf(str, "%f", &frac) != 1)
                return NULL;
        }

        *samples *= rate;
        *samples += (rate * frac) + 0.5;
        return end;
    }
    if (found_samples || (def == 's' && !found_time))
    {
        if (sscanf(str, "%ld", &long_samples) != 1)
            return NULL;
        *samples = long_samples;
        return end;
    }
    return NULL;
}

/* a note is given as an int,
 * 0   => 440 Hz = A
 * >0  => number of half notes 'up',
 * <0  => number of half notes down,
 * example 12 => A of next octave, 880Hz
 *
 * calculated by freq = 440Hz * 2**(note/12)
 */
static double calc_note_freq(double note)
{
  return 440.0 * pow(2.0, note / 12.0);
}

/* Read string 'text' and convert to frequency.
 * 'text' can be a positive number which is the frequency in Hz.
 * If 'text' starts with a hash '%' and a following number the corresponding
 * note is calculated.
 * Return -1 on error.
 */
double lsx_parse_frequency(char const * text, char * * end_ptr)
{
  double result;

  if (*text == '%') {
    result = strtod(text + 1, end_ptr);
    if (*end_ptr == text + 1)
      return -1;
    return calc_note_freq(result);
  }
  result = strtod(text, end_ptr);
  if (end_ptr) {
    if (*end_ptr == text)
      return -1;
    if (**end_ptr == 'k') {
      result *= 1000;
      ++*end_ptr;
    }
  }
  return result < 0 ? -1 : result;
}

double lsx_bessel_I_0(double x)
{
  double term = 1, sum = 1, last_sum, x2 = x / 2;
  int i = 1;
  do {
    double y = x2 / i++;
    last_sum = sum, sum += term *= y * y;
  } while (sum != last_sum);
  return sum;
}

#include "fft4g.h"
int * lsx_fft_br;
double * lsx_fft_sc;

static void update_fft_cache(int len)
{
  static int n;

  if (!len) {
    free(lsx_fft_br);
    free(lsx_fft_sc);
    lsx_fft_sc = NULL;
    lsx_fft_br = NULL;
    n = 0;
    return;
  }
  if (len > n) {
    int old_n = n;
    n = len;
    lsx_fft_br = lsx_realloc(lsx_fft_br, dft_br_len(n) * sizeof(*lsx_fft_br));
    lsx_fft_sc = lsx_realloc(lsx_fft_sc, dft_sc_len(n) * sizeof(*lsx_fft_sc));
    if (!old_n)
      lsx_fft_br[0] = 0;
  }
}

static sox_bool is_power_of_2(int x)
{
  return !(x < 2 || (x & (x - 1)));
}

void lsx_safe_rdft(int len, int type, double * d)
{
  assert(is_power_of_2(len));
  update_fft_cache(len);
  lsx_rdft(len, type, d, lsx_fft_br, lsx_fft_sc);
}

void lsx_safe_cdft(int len, int type, double * d)
{
  assert(is_power_of_2(len));
  update_fft_cache(len);
  lsx_cdft(len, type, d, lsx_fft_br, lsx_fft_sc);
}

void lsx_power_spectrum(int n, double const * in, double * out)
{
  int i;
  double * work = lsx_memdup(in, n * sizeof(*work));
  lsx_safe_rdft(n, 1, work);
  out[0] = sqr(work[0]);
  for (i = 2; i < n; i += 2)
    out[i >> 1] = sqr(work[i]) + sqr(work[i + 1]);
  out[i >> 1] = sqr(work[1]);
  free(work);
}

void lsx_power_spectrum_f(int n, float const * in, float * out)
{
  int i;
  double * work = lsx_malloc(n * sizeof(*work));
  for (i = 0; i< n; ++i) work[i] = in[i];
  lsx_safe_rdft(n, 1, work);
  out[0] = sqr(work[0]);
  for (i = 2; i < n; i += 2)
    out[i >> 1] = sqr(work[i]) + sqr(work[i + 1]);
  out[i >> 1] = sqr(work[1]);
  free(work);
}

void lsx_apply_hann_f(float h[], const int num_points)
{
  int i, m = num_points - 1;
  for (i = 0; i < num_points; ++i) {
    double x = 2 * M_PI * i / m;
    h[i] *= .5 - .5 * cos(x);
  }
}

void lsx_apply_hann(double h[], const int num_points)
{
  int i, m = num_points - 1;
  for (i = 0; i < num_points; ++i) {
    double x = 2 * M_PI * i / m;
    h[i] *= .5 - .5 * cos(x);
  }
}

void lsx_apply_hamming(double h[], const int num_points)
{
  int i, m = num_points - 1;
  for (i = 0; i < num_points; ++i) {
    double x = 2 * M_PI * i / m;
    h[i] *= .53836 - .46164 * cos(x);
  }
}

void lsx_apply_bartlett(double h[], const int num_points)
{
  int i, m = num_points - 1;
  for (i = 0; i < num_points; ++i) {
    h[i] *= 2. / m * (m / 2. - fabs(i - m / 2.));
  }
}

double lsx_kaiser_beta(double att)
{
  if (att > 100  ) return .1117 * att - 1.11;
  if (att > 50   ) return .1102 * (att - 8.7);
  if (att > 20.96) return .58417 * pow(att -20.96, .4) + .07886 * (att - 20.96);
  return 0;
}

void lsx_apply_kaiser(double h[], const int num_points, double beta)
{
  int i, m = num_points - 1;
  for (i = 0; i <= m; ++i) {
    double x = 2. * i / m - 1;
    h[i] *= lsx_bessel_I_0(beta * sqrt(1 - x * x)) / lsx_bessel_I_0(beta);
  }
}

