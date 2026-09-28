#pragma once

/*****************************************************************************
 * FftKernel.h
 *
 * The decimation in time transform kernel, lifted out of Panel so that both
 * Panel::FFT/RealSpectrum and the background spectrogram cache can share one
 * copy. cosTab/sinTab must hold 2^(n-1) entries sampling exp(i*2*pi*j / 2^n),
 * which is what Panel::LoadTwiddles and SpectrogramCache build.
 *
 * Header only so that neither the x64 nor the x86 project needs a new
 * translation unit for it.
 *
 *****************************************************************************/

namespace winsap {

inline void FftTabled(float* z, float* y, int n, const float* cosTab, const float* sinTab)
{
    int i, j, k, p;
    int n1, n2;
    float c, s, t1, t2;

    const int size = 1 << n;

    // Bit reversal
    j = 0;
    n2 = 1 << (n - 1);
    for (i = 0; i < size - 1; i++) {
        if (i < j) {
            t1 = z[j];
            z[j] = z[i];
            z[i] = t1;
            t1 = y[j];
            y[j] = y[i];
            y[i] = t1;
        }
        k = n2;
        while (k <= j) {
            j = j - k;
            k = k >> 1;
        }
        j = j + k;
    }

    // FFT computation
    n2 = 1;
    for (i = 0; i < n; i++) {
        n1 = n2;
        n2 = n2 + n2;
        // Stage angle step is 2*pi/n2, i.e. every (size/n2)-th table entry.
        const int stride = size / n2;
        for (j = 0; j < n1; j++) {
            c = cosTab[j * stride];
            s = sinTab[j * stride];
            for (k = j; k < size; k = k + n2) {
                p = k + n1;
                t1 = c * z[p] - s * y[p];
                t2 = s * z[p] + c * y[p];
                z[p] = z[k] - t1;
                y[p] = y[k] - t2;
                z[k] = z[k] + t1;
                y[k] = y[k] + t2;
            }
        }
    }
}

} // namespace winsap
