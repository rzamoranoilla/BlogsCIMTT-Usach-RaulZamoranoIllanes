/* test.c - Version en C (Win32 + GDI, sin librerias externas) del simulador test.py */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#endif

#define PI_ 3.14159265358979323846
#define SLIDER_STEPS 10000
#define TIMER_ID 1
#define MAX_PUNTOS_MARCADOR 5000

enum { ID_BOTON_ONDA = 100, ID_SLIDER = 200, ID_CAJA = 300, ID_CAJA_CFG = 400, ID_APLICAR = 500 };

/* ============================================================
 * 1. PARAMETROS INICIALES
 * ============================================================ */
static double A = 5.0;
static double f = 5.0;
static double phi = 0.0;
static int forma_onda = 0;

static double fs = 300.0;
static int N = 300;
static int refresh_ms = 10;
static int N_Chunk_per_display = 2;
static double f_max_plot = 30.0;

#define TIEMPO_Y_MIN   (-12.0)
#define TIEMPO_Y_MAX   (12.0)
#define MAGNITUD_Y_MIN (-80.0)
#define MAGNITUD_Y_MAX (24.0)
#define FASE_Y_MIN     (-PI_)
#define FASE_Y_MAX     (PI_)
#define DB_MIN         (-80.0)
#define DB_MAX         (20.0)

static const int mostrar_fase_de_baja_magnitud = 1;

static double T_chunk, T_ventana, df;
static int nf;
static double *frecuencias;
static double tiempo_cursor = 0.0;
static double fase_portadora = 0.0;

static double *t_buf, *s_buf, *wf_buf;
static int chunks_count;
static double *mag_db, *fase_fft;
static double ax_t_x0, ax_t_x1;

static const wchar_t *FORMAS_ONDA[4] = { L"Sinusoidal", L"Cuadrada", L"Diente de sierra", L"Triangular" };
#define BOTON_ACTIVO   RGB(0xA8, 0x00, 0x00)
#define BOTON_INACTIVO RGB(0x22, 0x22, 0x22)
#define BOTON_HOVER    RGB(0xBF, 0x32, 0x32)
#define COLOR_GRID     RGB(209, 209, 209)
#define COLOR_ROJO     RGB(255, 0, 0)
#define COLOR_NEGRO    RGB(0, 0, 0)
#define COLOR_BLANCO   RGB(255, 255, 255)

typedef struct {
    const wchar_t *nombre;
    const wchar_t *etiqueta;
    double vmin, vmax, val;
    HWND slider, caja;
    int sincronizando;
} Control;

static Control controles[3] = {
    { L"phi", L"Fase φ (rad)", -PI_, PI_, 0.0, NULL, NULL, 0 },
    { L"f",   L"Frecuencia (Hz)",   0.5, 30.0, 0.0, NULL, NULL, 0 },
    { L"A",   L"Amplitud (V)",      0.0, 10.0, 0.0, NULL, NULL, 0 },
};
static wchar_t mensaje_control[256];

typedef struct { const wchar_t *clave; const wchar_t *etiqueta; } CampoCfg;
static const CampoCfg campos[5] = {
    { L"fs",                  L"fs - Muestreo (Samples/s)" },
    { L"N",                   L"N - Muestras / chunk" },
    { L"refresh_ms",          L"Refresco (ms)" },
    { L"N_Chunk_per_display", L"Chunks visibles" },
    { L"f_max_plot",          L"FFT máximo (Hz)" },
};

static HWND hwnd_main, hwnd_cfg, botones_onda[4], cajas_cfg[5], boton_aplicar;
static wchar_t mensaje_cfg[256] = L"Configuración inicial activa.";
static COLORREF color_cfg = COLOR_NEGRO;
static int ventanas_abiertas = 0;
static HFONT font_11, font_14, font_9, font_10, font_11_vert, font_11_italic;
static int dpi = 96;
static int alto_edit = 24;
static WNDPROC edit_proc_orig;

/* ============================================================
 * 2. UTILIDADES NUMERICAS Y FFT (radix-2 + Bluestein para N arbitrario)
 * ============================================================ */
static double pmod(double x, double m) { double r = fmod(x, m); return r < 0 ? r + m : r; }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int es_potencia_2(int n) { return n > 0 && (n & (n - 1)) == 0; }

static void fft_radix2(double *re, double *im, int M, int inversa)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < M; i++) {
        int bit = M >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= M; len <<= 1) {
        int half = len / 2;
        double ang = (inversa ? 2.0 : -2.0) * PI_ / len;
        for (i = 0; i < M; i += len) {
            for (k = 0; k < half; k++) {
                double cr = cos(ang * k), ci = sin(ang * k);
                int a = i + k, b = i + k + half;
                double ur = re[a], ui = im[a];
                double vr = re[b] * cr - im[b] * ci;
                double vi = re[b] * ci + im[b] * cr;
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
            }
        }
    }
    if (inversa) for (i = 0; i < M; i++) { re[i] /= M; im[i] /= M; }
}

static void rfft(const double *x, int n, double *Xre, double *Xim)
{
    int k, nout = n / 2 + 1, M;
    double *wr, *wi, *ar, *ai, *br, *bi;
    if (es_potencia_2(n)) {
        double *re = (double *)malloc(n * sizeof(double));
        double *im = (double *)calloc(n, sizeof(double));
        memcpy(re, x, n * sizeof(double));
        fft_radix2(re, im, n, 0);
        for (k = 0; k < nout; k++) { Xre[k] = re[k]; Xim[k] = im[k]; }
        free(re); free(im);
        return;
    }
    M = 1;
    while (M < 2 * n - 1) M <<= 1;
    wr = (double *)malloc(n * sizeof(double));
    wi = (double *)malloc(n * sizeof(double));
    ar = (double *)calloc(M, sizeof(double));
    ai = (double *)calloc(M, sizeof(double));
    br = (double *)calloc(M, sizeof(double));
    bi = (double *)calloc(M, sizeof(double));
    for (k = 0; k < n; k++) {
        long long q = ((long long)k * k) % (2LL * n);
        double ang = -PI_ * (double)q / n;
        wr[k] = cos(ang); wi[k] = sin(ang);
        ar[k] = x[k] * wr[k]; ai[k] = x[k] * wi[k];
    }
    br[0] = wr[0]; bi[0] = -wi[0];
    for (k = 1; k < n; k++) { br[k] = br[M - k] = wr[k]; bi[k] = bi[M - k] = -wi[k]; }
    fft_radix2(ar, ai, M, 0);
    fft_radix2(br, bi, M, 0);
    for (k = 0; k < M; k++) {
        double r = ar[k] * br[k] - ai[k] * bi[k];
        double i = ar[k] * bi[k] + ai[k] * br[k];
        ar[k] = r; ai[k] = i;
    }
    fft_radix2(ar, ai, M, 1);
    for (k = 0; k < nout; k++) {
        Xre[k] = ar[k] * wr[k] - ai[k] * wi[k];
        Xim[k] = ar[k] * wi[k] + ai[k] * wr[k];
    }
    free(wr); free(wi); free(ar); free(ai); free(br); free(bi);
}

static double generar_onda(double angulo, double amplitud, int tipo)
{
    double base;
    switch (tipo) {
    case 0: base = sin(angulo); break;
    case 1: base = pmod(angulo, 2 * PI_) < PI_ ? 1.0 : -1.0; break;
    case 2: base = 2.0 * pmod(angulo / (2 * PI_) + 0.5, 1.0) - 1.0; break;
    case 3: base = (2.0 / PI_) * asin(sin(angulo)); break;
    default: base = 0.0; break;
    }
    return amplitud * base;
}

/* ============================================================
 * 3. ESTADO DERIVADO Y BUFFERS CIRCULARES
 * ============================================================ */
static void recalcular_derivados(void)
{
    int k;
    T_chunk = N / fs;
    T_ventana = N_Chunk_per_display * T_chunk;
    df = fs / N;
    nf = N / 2 + 1;

    free(frecuencias); free(t_buf); free(s_buf); free(wf_buf); free(mag_db); free(fase_fft);
    frecuencias = (double *)malloc(nf * sizeof(double));
    for (k = 0; k < nf; k++) frecuencias[k] = k * df;
    t_buf = (double *)malloc((size_t)N_Chunk_per_display * N * sizeof(double));
    s_buf = (double *)malloc((size_t)N_Chunk_per_display * N * sizeof(double));
    wf_buf = (double *)malloc((size_t)N_Chunk_per_display * nf * sizeof(double));
    mag_db = (double *)malloc(nf * sizeof(double));
    fase_fft = (double *)malloc(nf * sizeof(double));
    for (k = 0; k < nf; k++) { mag_db[k] = MAGNITUD_Y_MIN; fase_fft[k] = NAN; }
    chunks_count = 0;
    ax_t_x0 = fmax(0.0, tiempo_cursor - T_ventana);
    ax_t_x1 = fmax(T_ventana, tiempo_cursor);
}

static void actualizar(void)
{
    int i, k;
    double A_actual = controles[2].val, f_actual = controles[1].val, phi_actual = controles[0].val;
    double *n, *s, *w, *Xre, *Xim, *magnitud, suelo, umbral;

    if (chunks_count == N_Chunk_per_display) {
        if (chunks_count > 1) {
            memmove(t_buf, t_buf + N, (size_t)(chunks_count - 1) * N * sizeof(double));
            memmove(s_buf, s_buf + N, (size_t)(chunks_count - 1) * N * sizeof(double));
            memmove(wf_buf, wf_buf + nf, (size_t)(chunks_count - 1) * nf * sizeof(double));
        }
        chunks_count--;
    }
    n = t_buf + (size_t)chunks_count * N;
    s = s_buf + (size_t)chunks_count * N;
    w = wf_buf + (size_t)chunks_count * nf;

    for (i = 0; i < N; i++) {
        double angulo = fase_portadora + 2 * PI_ * f_actual * i / fs;
        n[i] = tiempo_cursor + i / fs;
        s[i] = generar_onda(angulo + phi_actual, A_actual, forma_onda);
    }
    tiempo_cursor += N / fs;
    fase_portadora = pmod(fase_portadora + 2 * PI_ * f_actual * N / fs, 2 * PI_);
    chunks_count++;

    ax_t_x0 = fmax(0.0, tiempo_cursor - T_ventana);
    ax_t_x1 = fmax(T_ventana, tiempo_cursor);

    Xre = (double *)malloc(nf * sizeof(double));
    Xim = (double *)malloc(nf * sizeof(double));
    magnitud = (double *)malloc(nf * sizeof(double));
    rfft(s, N, Xre, Xim);
    for (k = 0; k < nf; k++) {
        magnitud[k] = 2.0 * hypot(Xre[k], Xim[k]) / N;
        fase_fft[k] = atan2(Xim[k], Xre[k]);
    }
    magnitud[0] /= 2.0;
    if (N % 2 == 0) magnitud[nf - 1] /= 2.0;
    if (!mostrar_fase_de_baja_magnitud) {
        double mx = 0.0;
        for (k = 0; k < nf; k++) if (magnitud[k] > mx) mx = magnitud[k];
        umbral = fmax(1e-10, 0.05 * mx);
        for (k = 0; k < nf; k++) if (magnitud[k] < umbral) fase_fft[k] = NAN;
    }
    suelo = pow(10.0, DB_MIN / 20.0);
    for (k = 0; k < nf; k++) {
        mag_db[k] = 20.0 * log10(fmax(magnitud[k], suelo));
        w[k] = clampd(mag_db[k], DB_MIN, DB_MAX);
    }
    free(Xre); free(Xim); free(magnitud);
}

#ifdef SELFTEST
/* Prueba de consola: compara rfft (radix-2 / Bluestein) contra una DFT directa. */
static void dft_directa(const double *x, int n, double *Xre, double *Xim)
{
    int k, j;
    for (k = 0; k <= n / 2; k++) {
        double sr = 0, si = 0;
        for (j = 0; j < n; j++) {
            double ang = -2 * PI_ * (double)j * k / n;
            sr += x[j] * cos(ang); si += x[j] * sin(ang);
        }
        Xre[k] = sr; Xim[k] = si;
    }
}
int main(void)
{
    int tams[] = { 300, 256, 7, 1000, 1024, 999 }, t, k;
    srand(12345);
    for (t = 0; t < 6; t++) {
        int n = tams[t], no = n / 2 + 1;
        double *x = (double *)malloc(n * sizeof(double));
        double *a = (double *)malloc(no * sizeof(double)), *b = (double *)malloc(no * sizeof(double));
        double *c = (double *)malloc(no * sizeof(double)), *d = (double *)malloc(no * sizeof(double));
        double err = 0;
        for (k = 0; k < n; k++) x[k] = (rand() / (double)RAND_MAX) * 2 - 1;
        rfft(x, n, a, b); dft_directa(x, n, c, d);
        for (k = 0; k < no; k++) err = fmax(err, fmax(fabs(a[k] - c[k]), fabs(b[k] - d[k])));
        printf("N=%5d  error maximo rfft vs DFT directa = %.3e\n", n, err);
        free(x); free(a); free(b); free(c); free(d);
    }
    controles[0].val = phi; controles[1].val = f; controles[2].val = A;
    recalcular_derivados();
    actualizar();
    printf("Sinusoidal A=5 V, f=5 Hz, fs=300, N=300:\n");
    for (k = 0; k < 8; k++) printf("  f=%5.1f Hz  mag=%8.3f dBV  fase=%8.4f rad\n", frecuencias[k], mag_db[k], fase_fft[k]);
    return 0;
}
#else

/* ============================================================
 * 4. UTILIDADES GRAFICAS (GDI)
 * ============================================================ */
typedef struct { RECT r; double x0, x1, y0, y1; } Axes;

static int ax_px(const Axes *a, double x)
{
    double w = (double)(a->r.right - a->r.left);
    return a->r.left + (int)floor((x - a->x0) / (a->x1 - a->x0) * w + 0.5);
}
static int ax_py(const Axes *a, double y)
{
    double h = (double)(a->r.bottom - a->r.top);
    return a->r.bottom - (int)floor((y - a->y0) / (a->y1 - a->y0) * h + 0.5);
}

static RECT frac_rect(int W, int H, double x, double y, double w, double h)
{
    RECT r;
    r.left = (int)(x * W + 0.5);
    r.right = (int)((x + w) * W + 0.5);
    r.top = (int)((1.0 - y - h) * H + 0.5);
    r.bottom = (int)((1.0 - y) * H + 0.5);
    return r;
}

static int pts_a_px(double puntos) { return (int)(puntos * dpi / 72.0 + 0.5); }
static int radio_marcador(double markersize) { int r = (int)(markersize * dpi / 72.0 / 2.0 + 0.5); return r < 1 ? 1 : r; }

static HFONT crear_fuente(int pt, int italic, int escapement)
{
    return CreateFontW(-MulDiv(pt, dpi, 72), 0, escapement, escapement, FW_NORMAL, (DWORD)italic, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_ROMAN, L"Times New Roman");
}

static SIZE medir_texto(HDC hdc, HFONT font, const wchar_t *s)
{
    SIZE sz = { 0, 0 };
    HFONT old = (HFONT)SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, s, (int)wcslen(s), &sz);
    SelectObject(hdc, old);
    return sz;
}

static void texto(HDC hdc, HFONT font, int x, int y, UINT align, COLORREF color, const wchar_t *s)
{
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextAlign(hdc, align);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, s, (int)wcslen(s));
    SelectObject(hdc, old);
}

static double paso_bonito(double rango, int objetivo)
{
    double crudo = rango / objetivo, mag, r;
    if (crudo <= 0) return 1.0;
    mag = pow(10.0, floor(log10(crudo)));
    r = crudo / mag;
    return (r < 1.5 ? 1.0 : r < 3.0 ? 2.0 : r < 7.0 ? 5.0 : 10.0) * mag;
}

static int calcular_ticks(double lo, double hi, double *out, int maxn, double *paso)
{
    double step = paso_bonito(hi - lo, 5), t;
    int n = 0;
    *paso = step;
    for (t = ceil(lo / step - 1e-9) * step; t <= hi + step * 1e-9 && n < maxn; t += step)
        out[n++] = fabs(t) < step * 1e-9 ? 0.0 : t;
    return n;
}

static void formatear_tick(double v, double step, wchar_t *buf, size_t cap)
{
    int dec = (int)fmax(0.0, -floor(log10(step) + 1e-9));
    swprintf(buf, cap, L"%.*f", dec, v);
}

static void dibujar_ejes(HDC hdc, const Axes *a, const wchar_t *titulo, const wchar_t *xlabel,
                         const wchar_t *ylabel, const double *yt, const wchar_t *const *ytl, int nyt, int grid)
{
    HPEN pen_grid = CreatePen(PS_SOLID, 1, COLOR_GRID);
    HPEN pen_frame = CreatePen(PS_SOLID, 1, COLOR_NEGRO);
    HGDIOBJ old_pen = SelectObject(hdc, pen_frame);
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HFONT old_font = (HFONT)SelectObject(hdc, font_11);
    TEXTMETRICW tm;
    double ticks[64], paso = 1.0;
    wchar_t buf[48];
    int i, n, th, tick = pts_a_px(3.5), maxw = 0;

    GetTextMetricsW(hdc, &tm);
    th = tm.tmHeight;
    SetTextColor(hdc, COLOR_NEGRO);

    n = calcular_ticks(a->x0, a->x1, ticks, 64, &paso);
    for (i = 0; i < n; i++) {
        int px = ax_px(a, ticks[i]);
        if (px < a->r.left || px > a->r.right) continue;
        if (grid) { SelectObject(hdc, pen_grid); MoveToEx(hdc, px, a->r.top, NULL); LineTo(hdc, px, a->r.bottom); }
        SelectObject(hdc, pen_frame);
        MoveToEx(hdc, px, a->r.bottom, NULL); LineTo(hdc, px, a->r.bottom + tick);
        formatear_tick(ticks[i], paso, buf, 48);
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        TextOutW(hdc, px, a->r.bottom + tick + 2, buf, (int)wcslen(buf));
    }

    if (yt == NULL) { n = calcular_ticks(a->y0, a->y1, ticks, 64, &paso); }
    else { n = nyt; for (i = 0; i < n; i++) ticks[i] = yt[i]; }
    for (i = 0; i < n; i++) {
        SIZE sz;
        int py = ax_py(a, ticks[i]);
        const wchar_t *lbl;
        if (py < a->r.top || py > a->r.bottom) continue;
        if (grid) { SelectObject(hdc, pen_grid); MoveToEx(hdc, a->r.left, py, NULL); LineTo(hdc, a->r.right, py); }
        SelectObject(hdc, pen_frame);
        MoveToEx(hdc, a->r.left - tick, py, NULL); LineTo(hdc, a->r.left, py);
        if (ytl) lbl = ytl[i]; else { formatear_tick(ticks[i], paso, buf, 48); lbl = buf; }
        SetTextAlign(hdc, TA_RIGHT | TA_TOP);
        TextOutW(hdc, a->r.left - tick - 3, py - th / 2, lbl, (int)wcslen(lbl));
        GetTextExtentPoint32W(hdc, lbl, (int)wcslen(lbl), &sz);
        if (sz.cx > maxw) maxw = sz.cx;
    }

    SelectObject(hdc, pen_frame);
    Rectangle(hdc, a->r.left, a->r.top, a->r.right + 1, a->r.bottom + 1);

    SetTextAlign(hdc, TA_CENTER | TA_BOTTOM);
    TextOutW(hdc, (a->r.left + a->r.right) / 2, a->r.top - 5, titulo, (int)wcslen(titulo));
    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    TextOutW(hdc, (a->r.left + a->r.right) / 2, a->r.bottom + tick + 2 + th + 2, xlabel, (int)wcslen(xlabel));

    SelectObject(hdc, font_11_vert);
    SetTextAlign(hdc, TA_CENTER | TA_BASELINE);
    TextOutW(hdc, a->r.left - tick - 3 - maxw - 6, (a->r.top + a->r.bottom) / 2, ylabel, (int)wcslen(ylabel));

    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen_grid);
    DeleteObject(pen_frame);
}

static void dibujar_curva(HDC hdc, const Axes *a, const double *x, const double *y, int n,
                          COLORREF color_linea, int grosor, int radio, COLORREF relleno, COLORREF borde)
{
    POINT *pts;
    HPEN pen = CreatePen(PS_SOLID, grosor, color_linea);
    HGDIOBJ old_pen, old_brush;
    int i, cnt = 0;

    if (n <= 0) { DeleteObject(pen); return; }
    pts = (POINT *)malloc((size_t)n * sizeof(POINT));
    SaveDC(hdc);
    IntersectClipRect(hdc, a->r.left, a->r.top, a->r.right + 1, a->r.bottom + 1);
    old_pen = SelectObject(hdc, pen);
    for (i = 0; i <= n; i++) {
        if (i == n || isnan(x[i]) || isnan(y[i])) {
            if (cnt >= 2) Polyline(hdc, pts, cnt);
            cnt = 0;
            continue;
        }
        pts[cnt].x = ax_px(a, x[i]);
        pts[cnt].y = ax_py(a, y[i]);
        cnt++;
    }
    if (radio > 0 && n <= MAX_PUNTOS_MARCADOR) {
        HPEN pen_b = CreatePen(PS_SOLID, 1, borde);
        HBRUSH br = CreateSolidBrush(relleno);
        SelectObject(hdc, pen_b);
        old_brush = SelectObject(hdc, br);
        for (i = 0; i < n; i++) {
            int px, py;
            if (isnan(x[i]) || isnan(y[i])) continue;
            px = ax_px(a, x[i]); py = ax_py(a, y[i]);
            if (px < a->r.left - radio || px > a->r.right + radio || py < a->r.top - radio || py > a->r.bottom + radio) continue;
            Ellipse(hdc, px - radio, py - radio, px + radio + 1, py + radio + 1);
        }
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(pen_b);
        DeleteObject(br);
    } else {
        SelectObject(hdc, old_pen);
    }
    RestoreDC(hdc, -1);
    DeleteObject(pen);
    free(pts);
}

static void dibujar_leyenda(HDC hdc, const Axes *a)
{
    const wchar_t *txt = L"s[n]";
    SIZE sz = medir_texto(hdc, font_11_italic, txt);
    int pad = 6, muestra = 24, r = radio_marcador(2);
    int w = pad + muestra + 6 + sz.cx + pad, h = sz.cy + 2 * pad;
    RECT box;
    HPEN pen_borde = CreatePen(PS_SOLID, 1, RGB(204, 204, 204));
    HPEN pen_rojo = CreatePen(PS_SOLID, 1, COLOR_ROJO);
    HBRUSH br_rojo = CreateSolidBrush(COLOR_ROJO);
    HGDIOBJ old_pen, old_brush;
    int cy;

    box.right = a->r.right - 8; box.left = box.right - w;
    box.top = a->r.top + 8; box.bottom = box.top + h;
    FillRect(hdc, &box, (HBRUSH)GetStockObject(WHITE_BRUSH));
    old_pen = SelectObject(hdc, pen_borde);
    old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, box.left, box.top, box.right, box.bottom);
    cy = (box.top + box.bottom) / 2;
    SelectObject(hdc, pen_rojo);
    MoveToEx(hdc, box.left + pad, cy, NULL); LineTo(hdc, box.left + pad + muestra, cy);
    SelectObject(hdc, br_rojo);
    Ellipse(hdc, box.left + pad + muestra / 2 - r, cy - r, box.left + pad + muestra / 2 + r + 1, cy + r + 1);
    texto(hdc, font_11_italic, box.left + pad + muestra + 6, box.top + pad, TA_LEFT | TA_TOP, COLOR_NEGRO, txt);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen_borde); DeleteObject(pen_rojo); DeleteObject(br_rojo);
}

static void dibujar_waterfall(HDC hdc, const Axes *a)
{
    int rows = N_Chunk_per_display, cols = nf, r, k, x0, x1;
    BITMAPINFO bmi;
    DWORD *bits = (DWORD *)malloc((size_t)rows * cols * sizeof(DWORD));

    for (r = 0; r < rows; r++) {
        int j = r - (rows - chunks_count);
        for (k = 0; k < cols; k++) {
            double v = (j < 0) ? DB_MIN : wf_buf[(size_t)j * nf + k];
            double t = clampd((v - DB_MIN) / (DB_MAX - DB_MIN), 0.0, 1.0);
            bits[(size_t)r * cols + k] = ((DWORD)(t * 255.0 + 0.5)) << 16;
        }
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cols;
    bmi.bmiHeader.biHeight = rows;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    x0 = ax_px(a, -df / 2);
    x1 = ax_px(a, frecuencias[nf - 1] + df / 2);
    SaveDC(hdc);
    IntersectClipRect(hdc, a->r.left, a->r.top, a->r.right, a->r.bottom);
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, x0, a->r.top, x1 - x0, a->r.bottom - a->r.top, 0, 0, cols, rows,
                  bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
    RestoreDC(hdc, -1);
    free(bits);
}

static void dibujar_colorbar(HDC hdc, const RECT *rwf, int cb_left, int cb_w)
{
    RECT cb;
    int rows, r, i, n, tick = pts_a_px(3.5), maxw = 0, th;
    BITMAPINFO bmi;
    DWORD *bits;
    HPEN pen = CreatePen(PS_SOLID, 1, COLOR_NEGRO);
    HGDIOBJ old_pen, old_brush;
    HFONT old_font;
    TEXTMETRICW tm;
    double ticks[16], paso;
    wchar_t buf[32];

    cb.left = cb_left; cb.right = cb_left + cb_w; cb.top = rwf->top; cb.bottom = rwf->bottom;
    rows = cb.bottom - cb.top;
    if (rows <= 0 || cb_w <= 0) { DeleteObject(pen); return; }
    bits = (DWORD *)malloc((size_t)rows * sizeof(DWORD));
    for (r = 0; r < rows; r++) {
        double t = rows > 1 ? (double)r / (rows - 1) : 1.0;
        bits[r] = ((DWORD)(t * 255.0 + 0.5)) << 16;
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 1;
    bmi.bmiHeader.biHeight = rows;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, cb.left, cb.top, cb_w, rows, 0, 0, 1, rows, bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
    free(bits);

    old_pen = SelectObject(hdc, pen);
    old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    old_font = (HFONT)SelectObject(hdc, font_11);
    GetTextMetricsW(hdc, &tm);
    th = tm.tmHeight;
    Rectangle(hdc, cb.left, cb.top, cb.right + 1, cb.bottom + 1);
    SetTextColor(hdc, COLOR_NEGRO);
    n = calcular_ticks(DB_MIN, DB_MAX, ticks, 16, &paso);
    for (i = 0; i < n; i++) {
        SIZE sz;
        int py = cb.bottom - (int)floor((ticks[i] - DB_MIN) / (DB_MAX - DB_MIN) * rows + 0.5);
        MoveToEx(hdc, cb.right, py, NULL); LineTo(hdc, cb.right + tick, py);
        formatear_tick(ticks[i], paso, buf, 32);
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        TextOutW(hdc, cb.right + tick + 3, py - th / 2, buf, (int)wcslen(buf));
        GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
        if (sz.cx > maxw) maxw = sz.cx;
    }
    SelectObject(hdc, font_11_vert);
    SetTextAlign(hdc, TA_CENTER | TA_BASELINE);
    TextOutW(hdc, cb.right + tick + 3 + maxw + 6 + tm.tmAscent, (cb.top + cb.bottom) / 2, L"Magnitud (dBV)", 14);

    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
}

/* ============================================================
 * 5. VENTANA PRINCIPAL: DISPOSICION Y PINTADO
 * ============================================================ */
static void calcular_rects_ejes(int W, int H, RECT *rt, RECT *rmag, RECT *rfase, RECT *rwf, int *cb_left, int *cb_w)
{
    const double left = 0.09, right = 0.94, bottom = 0.40, top = 0.91, wspace = 0.35, hspace = 0.45;
    double aw = (right - left) / (2 + wspace), gw = aw * wspace;
    double ah = (top - bottom) / (2 + hspace);
    *rt = frac_rect(W, H, left, top - ah, aw, ah);
    *rmag = frac_rect(W, H, left + aw + gw, top - ah, aw, ah);
    *rfase = frac_rect(W, H, left, bottom, aw, ah);
    *rwf = frac_rect(W, H, left + aw + gw, bottom, aw * (1.0 - 0.046 - 0.04 - 0.03), ah);
    *cb_left = rwf->right + (int)(0.04 * aw * W);
    *cb_w = (int)(0.046 * aw * W);
}

static RECT rect_boton(int W, int H, int i) { return frac_rect(W, H, 0.135 + i * 0.195, 0.287, 0.175, 0.047); }
static RECT rect_slider(int W, int H, int i) { return frac_rect(W, H, 0.24, 0.210 - 0.06 * i, 0.49, 0.027); }
static RECT rect_caja(int W, int H, int i) { return frac_rect(W, H, 0.79, 0.200 - 0.06 * i, 0.105, 0.048); }

static void posicionar_controles_principal(int W, int H)
{
    int i;
    for (i = 0; i < 4; i++) {
        RECT r = rect_boton(W, H, i);
        MoveWindow(botones_onda[i], r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    }
    for (i = 0; i < 3; i++) {
        RECT rs = rect_slider(W, H, i), rc = rect_caja(W, H, i);
        int top = (rc.top + rc.bottom) / 2 - alto_edit / 2;
        MoveWindow(controles[i].slider, rs.left, rs.top, rs.right - rs.left, rs.bottom - rs.top, TRUE);
        MoveWindow(controles[i].caja, rc.left, top, rc.right - rc.left, alto_edit, TRUE);
    }
}

static void pintar_principal(HDC hdc, int W, int H)
{
    RECT all = { 0, 0, W, H }, rt, rmag, rfase, rwf;
    int cb_left, cb_w, i;
    double lim_x = fmin(f_max_plot, fs / 2);
    wchar_t titulo[160];
    static const double yt_fase[5] = { -PI_, -PI_ / 2, 0.0, PI_ / 2, PI_ };
    static const wchar_t *const ytl_fase[5] = { L"-π", L"-π/2", L"0", L"π/2", L"π" };
    Axes at, am, af, aw;
    SIZE sz;

    FillRect(hdc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    texto(hdc, font_14, W / 2, (int)(0.02 * H), TA_CENTER | TA_TOP, COLOR_NEGRO,
          L"Análisis dinámico de señal por chunks");

    calcular_rects_ejes(W, H, &rt, &rmag, &rfase, &rwf, &cb_left, &cb_w);

    at.r = rt; at.x0 = ax_t_x0; at.x1 = ax_t_x1; at.y0 = TIEMPO_Y_MIN; at.y1 = TIEMPO_Y_MAX;
    swprintf(titulo, 160, L"Señal temporal — %ls", FORMAS_ONDA[forma_onda]);
    dibujar_ejes(hdc, &at, titulo, L"Tiempo (s)", L"Amplitud (V)", NULL, NULL, 0, 1);
    dibujar_curva(hdc, &at, t_buf, s_buf, chunks_count * N, COLOR_ROJO, 1, radio_marcador(2), COLOR_ROJO, COLOR_ROJO);
    dibujar_leyenda(hdc, &at);

    am.r = rmag; am.x0 = 0; am.x1 = lim_x; am.y0 = MAGNITUD_Y_MIN; am.y1 = MAGNITUD_Y_MAX;
    dibujar_ejes(hdc, &am, L"Espectro de magnitud (FFT del chunk)", L"Frecuencia (Hz)", L"Magnitud (dBV)", NULL, NULL, 0, 1);
    dibujar_curva(hdc, &am, frecuencias, mag_db, nf, COLOR_ROJO, 1, radio_marcador(3), COLOR_ROJO, COLOR_ROJO);

    af.r = rfase; af.x0 = 0; af.x1 = lim_x; af.y0 = FASE_Y_MIN; af.y1 = FASE_Y_MAX;
    dibujar_ejes(hdc, &af, L"Espectro de fase (FFT del chunk)", L"Frecuencia (Hz)", L"Fase (rad)", yt_fase, ytl_fase, 5, 1);
    dibujar_curva(hdc, &af, frecuencias, fase_fft, nf, COLOR_NEGRO, 1, radio_marcador(4), COLOR_ROJO, COLOR_NEGRO);

    aw.r = rwf; aw.x0 = 0; aw.x1 = lim_x; aw.y0 = -T_ventana; aw.y1 = 0;
    dibujar_waterfall(hdc, &aw);
    dibujar_ejes(hdc, &aw, L"Waterfall de magnitudes", L"Frecuencia (Hz)", L"Tiempo relativo (s)", NULL, NULL, 0, 0);
    dibujar_colorbar(hdc, &rwf, cb_left, cb_w);

    for (i = 0; i < 3; i++) {
        RECT rs = rect_slider(W, H, i);
        sz = medir_texto(hdc, font_11, controles[i].etiqueta);
        texto(hdc, font_11, rs.left - (int)(0.02 * W), (rs.top + rs.bottom) / 2 - sz.cy / 2,
              TA_RIGHT | TA_TOP, COLOR_NEGRO, controles[i].etiqueta);
    }
    if (mensaje_control[0])
        texto(hdc, font_9, (int)(0.24 * W), (int)((1.0 - 0.033) * H), TA_LEFT | TA_BOTTOM, COLOR_ROJO, mensaje_control);
}

/* ============================================================
 * 6. SELECCION DE FORMA DE ONDA, SLIDERS Y CUADROS DE TEXTO
 * ============================================================ */
static void seleccionar_onda(int tipo)
{
    int i;
    forma_onda = tipo;
    for (i = 0; i < 4; i++) InvalidateRect(botones_onda[i], NULL, TRUE);
    InvalidateRect(hwnd_main, NULL, FALSE);
}

static int parsear_double(const wchar_t *texto_in, int coma_decimal, double *out)
{
    wchar_t buf[128], *end;
    size_t i, j = 0, len = wcslen(texto_in);
    for (i = 0; i < len && j < 127; i++) {
        wchar_t c = texto_in[i];
        if (coma_decimal && c == L',') c = L'.';
        buf[j++] = c;
    }
    buf[j] = 0;
    i = 0;
    while (iswspace(buf[i])) i++;
    if (buf[i] == 0) return 0;
    *out = wcstod(buf + i, &end);
    if (end == buf + i) return 0;
    while (iswspace(*end)) end++;
    return *end == 0;
}

static int pos_slider(const Control *c) { return (int)floor((c->val - c->vmin) / (c->vmax - c->vmin) * SLIDER_STEPS + 0.5); }

static void sincronizar_desde_slider(int idx)
{
    Control *c = &controles[idx];
    wchar_t buf[64];
    int pos;
    if (c->sincronizando) return;
    pos = (int)SendMessageW(c->slider, TBM_GETPOS, 0, 0);
    c->val = c->vmin + (c->vmax - c->vmin) * pos / SLIDER_STEPS;
    c->sincronizando = 1;
    swprintf(buf, 64, L"%.8g", c->val);
    SetWindowTextW(c->caja, buf);
    mensaje_control[0] = 0;
    c->sincronizando = 0;
    InvalidateRect(hwnd_main, NULL, FALSE);
}

static void sincronizar_desde_caja(int idx)
{
    Control *c = &controles[idx];
    wchar_t texto_caja[128], buf[64];
    double valor;
    if (c->sincronizando) return;
    GetWindowTextW(c->caja, texto_caja, 128);
    if (!parsear_double(texto_caja, 1, &valor) || !isfinite(valor) || valor < c->vmin || valor > c->vmax) {
        swprintf(mensaje_control, 256, L"Valor no válido para %ls. Rango: %g a %g.", c->nombre, c->vmin, c->vmax);
        c->sincronizando = 1;
        swprintf(buf, 64, L"%.8g", c->val);
        SetWindowTextW(c->caja, buf);
        c->sincronizando = 0;
        InvalidateRect(hwnd_main, NULL, FALSE);
        return;
    }
    c->val = valor;
    c->sincronizando = 1;
    SendMessageW(c->slider, TBM_SETPOS, TRUE, pos_slider(c));
    swprintf(buf, 64, L"%.8g", c->val);
    SetWindowTextW(c->caja, buf);
    mensaje_control[0] = 0;
    c->sincronizando = 0;
    InvalidateRect(hwnd_main, NULL, FALSE);
}

static LRESULT CALLBACK EditSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        int idx = (int)GetWindowLongPtrW(h, GWLP_ID) - ID_CAJA;
        if (idx >= 0 && idx < 3) sincronizar_desde_caja(idx);
        return 0;
    }
    if (msg == WM_CHAR && wp == VK_RETURN) return 0;
    return CallWindowProcW(edit_proc_orig, h, msg, wp, lp);
}

/* ============================================================
 * 7. SEGUNDA VENTANA: CONFIGURACION GLOBAL
 * ============================================================ */
static int entero_positivo(const wchar_t *texto_in, const wchar_t *nombre, int minimo, int *out, wchar_t *err, size_t errcap)
{
    double v;
    if (!parsear_double(texto_in, 0, &v)) {
        swprintf(err, errcap, L"Valor no numérico para %ls: '%ls'.", nombre, texto_in);
        return 0;
    }
    if (!isfinite(v) || v != floor(v) || v < minimo) {
        swprintf(err, errcap, L"%ls debe ser un entero >= %d.", nombre, minimo);
        return 0;
    }
    *out = (int)v;
    return 1;
}

static void aplicar_configuracion(void)
{
    wchar_t t[5][128], err[256];
    double fs_nuevo = 0, fmax_nuevo = 0;
    int N_nuevo = 0, refresh_nuevo = 0, chunks_nuevos = 0, i, ok = 1;

    for (i = 0; i < 5; i++) GetWindowTextW(cajas_cfg[i], t[i], 128);

    if (!parsear_double(t[0], 0, &fs_nuevo)) { swprintf(err, 256, L"Valor no numérico para fs: '%ls'.", t[0]); ok = 0; }
    else if (!entero_positivo(t[1], L"N", 2, &N_nuevo, err, 256)) ok = 0;
    else if (!entero_positivo(t[2], L"Refresco", 1, &refresh_nuevo, err, 256)) ok = 0;
    else if (!entero_positivo(t[3], L"Chunks visibles", 1, &chunks_nuevos, err, 256)) ok = 0;
    else if (!parsear_double(t[4], 0, &fmax_nuevo)) { swprintf(err, 256, L"Valor no numérico para FFT máximo: '%ls'.", t[4]); ok = 0; }
    else if (!isfinite(fs_nuevo) || fs_nuevo <= 0) { swprintf(err, 256, L"fs debe ser un número positivo."); ok = 0; }
    else if (!isfinite(fmax_nuevo) || fmax_nuevo <= 0) { swprintf(err, 256, L"FFT máximo debe ser positivo."); ok = 0; }
    else if ((long long)N_nuevo * chunks_nuevos > 1000000LL) { swprintf(err, 256, L"Demasiadas muestras visibles (> 1 000 000)."); ok = 0; }
    else if (N_nuevo > 100000 || chunks_nuevos > 500 || refresh_nuevo > 10000) { swprintf(err, 256, L"Parámetros demasiado grandes para esta interfaz."); ok = 0; }

    if (!ok) {
        swprintf(mensaje_cfg, 256, L"Error: %ls", err);
        color_cfg = COLOR_ROJO;
        InvalidateRect(hwnd_cfg, NULL, FALSE);
        return;
    }

    fs = fs_nuevo; N = N_nuevo; refresh_ms = refresh_nuevo;
    N_Chunk_per_display = chunks_nuevos; f_max_plot = fmax_nuevo;
    recalcular_derivados();
    if (hwnd_main) SetTimer(hwnd_main, TIMER_ID, (UINT)refresh_ms, NULL);

    swprintf(mensaje_cfg, 256, L"Aplicado: fs=%g Samples/s; N=%d; chunk=%.3g s; ventana=%.3g s", fs, N, T_chunk, T_ventana);
    if (controles[1].val >= fs / 2) wcsncat(mensaje_cfg, L"  |  Aviso: posible aliasing", 255 - wcslen(mensaje_cfg));
    color_cfg = COLOR_NEGRO;
    InvalidateRect(hwnd_cfg, NULL, FALSE);
    if (hwnd_main) InvalidateRect(hwnd_main, NULL, FALSE);
}

static RECT rect_caja_cfg(int W, int H, int i) { return frac_rect(W, H, 0.56, 0.735 - 0.122 * i, 0.28, 0.062); }

static void posicionar_controles_cfg(int W, int H)
{
    int i;
    RECT ra = frac_rect(W, H, 0.59, 0.105, 0.22, 0.083);
    for (i = 0; i < 5; i++) {
        RECT r = rect_caja_cfg(W, H, i);
        int top = (r.top + r.bottom) / 2 - alto_edit / 2;
        MoveWindow(cajas_cfg[i], r.left, top, r.right - r.left, alto_edit, TRUE);
    }
    MoveWindow(boton_aplicar, ra.left, ra.top, ra.right - ra.left, ra.bottom - ra.top, TRUE);
}

static void pintar_cfg(HDC hdc, int W, int H)
{
    RECT all = { 0, 0, W, H };
    SIZE sz;
    int i, y;
    FillRect(hdc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    texto(hdc, font_14, W / 2, (int)(0.02 * H), TA_CENTER | TA_TOP, COLOR_NEGRO,
          L"Configuración de adquisición y visualización");
    y = (int)((1.0 - 0.87) * H);
    sz = medir_texto(hdc, font_10, L"X");
    texto(hdc, font_10, (int)(0.075 * W), y, TA_LEFT | TA_TOP, COLOR_NEGRO, L"Modifica los parámetros y pulsa Aplicar.");
    texto(hdc, font_10, (int)(0.075 * W), y + sz.cy + 2, TA_LEFT | TA_TOP, COLOR_NEGRO, L"Los cambios no se ejecutan hasta pulsar el botón.");
    for (i = 0; i < 5; i++) {
        RECT r = rect_caja_cfg(W, H, i);
        sz = medir_texto(hdc, font_11, campos[i].etiqueta);
        texto(hdc, font_11, r.left - (int)(0.02 * W), (r.top + r.bottom) / 2 - sz.cy / 2, TA_RIGHT | TA_TOP, COLOR_NEGRO, campos[i].etiqueta);
    }
    sz = medir_texto(hdc, font_10, mensaje_cfg);
    texto(hdc, font_10, (int)(0.08 * W), (int)((1.0 - 0.03) * H) - sz.cy / 2, TA_LEFT | TA_TOP, color_cfg, mensaje_cfg);
}

/* ============================================================
 * 8. PROCEDIMIENTOS DE VENTANA
 * ============================================================ */
static void pintar_doble_buffer(HWND hwnd, void (*pintar)(HDC, int, int))
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC hdc = BeginPaint(hwnd, &ps), mem;
    HBITMAP bmp, oldb;
    int W, H;
    GetClientRect(hwnd, &rc);
    W = rc.right - rc.left; H = rc.bottom - rc.top;
    if (W > 0 && H > 0) {
        mem = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, W, H);
        oldb = (HBITMAP)SelectObject(mem, bmp);
        pintar(mem, W, H);
        BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldb);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProcPrincipal(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = ((LPCREATESTRUCT)lp)->hInstance;
        wchar_t buf[64];
        hwnd_main = hwnd;
        for (i = 0; i < 4; i++)
            botones_onda[i] = CreateWindowW(L"BUTTON", FORMAS_ONDA[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_BOTON_ONDA + i), inst, NULL);
        for (i = 0; i < 3; i++) {
            Control *c = &controles[i];
            c->slider = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_SLIDER + i), inst, NULL);
            SendMessageW(c->slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, SLIDER_STEPS));
            SendMessageW(c->slider, TBM_SETPOS, TRUE, pos_slider(c));
            swprintf(buf, 64, L"%.8g", c->val);
            c->caja = CreateWindowW(L"EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_CAJA + i), inst, NULL);
            SendMessageW(c->caja, WM_SETFONT, (WPARAM)font_11, TRUE);
            edit_proc_orig = (WNDPROC)SetWindowLongPtrW(c->caja, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        }
        SetTimer(hwnd, TIMER_ID, (UINT)refresh_ms, NULL);
        ventanas_abiertas++;
        return 0;
    }
    case WM_SIZE:
        posicionar_controles_principal(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_ID) { actualizar(); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, COLOR_BLANCO);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_PAINT:
        pintar_doble_buffer(hwnd, pintar_principal);
        return 0;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_BUTTON && d->CtlID >= ID_BOTON_ONDA && d->CtlID < ID_BOTON_ONDA + 4) {
            int idx = (int)d->CtlID - ID_BOTON_ONDA;
            COLORREF c = (idx == forma_onda) ? BOTON_ACTIVO : BOTON_INACTIVO;
            HBRUSH br;
            HFONT oldf;
            RECT r = d->rcItem;
            if (d->itemState & ODS_SELECTED) c = BOTON_HOVER;
            br = CreateSolidBrush(c);
            FillRect(d->hDC, &r, br);
            DeleteObject(br);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, COLOR_BLANCO);
            oldf = (HFONT)SelectObject(d->hDC, font_10);
            DrawTextW(d->hDC, FORMAS_ONDA[idx], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(d->hDC, oldf);
            return TRUE;
        }
        break;
    }
    case WM_HSCROLL:
        for (i = 0; i < 3; i++)
            if ((HWND)lp == controles[i].slider) { sincronizar_desde_slider(i); break; }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= ID_BOTON_ONDA && id < ID_BOTON_ONDA + 4 && code == BN_CLICKED) { seleccionar_onda(id - ID_BOTON_ONDA); return 0; }
        if (id >= ID_CAJA && id < ID_CAJA + 3 && code == EN_KILLFOCUS) { sincronizar_desde_caja(id - ID_CAJA); return 0; }
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        hwnd_main = NULL;
        if (--ventanas_abiertas <= 0) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcCfg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = ((LPCREATESTRUCT)lp)->hInstance;
        wchar_t buf[64];
        hwnd_cfg = hwnd;
        for (i = 0; i < 5; i++) {
            switch (i) {
            case 0: swprintf(buf, 64, L"%g", fs); break;
            case 1: swprintf(buf, 64, L"%d", N); break;
            case 2: swprintf(buf, 64, L"%d", refresh_ms); break;
            case 3: swprintf(buf, 64, L"%d", N_Chunk_per_display); break;
            default: swprintf(buf, 64, L"%g", f_max_plot); break;
            }
            cajas_cfg[i] = CreateWindowW(L"EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
                                         0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_CAJA_CFG + i), inst, NULL);
            SendMessageW(cajas_cfg[i], WM_SETFONT, (WPARAM)font_11, TRUE);
        }
        boton_aplicar = CreateWindowW(L"BUTTON", L"Aplicar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_APLICAR, inst, NULL);
        SendMessageW(boton_aplicar, WM_SETFONT, (WPARAM)font_11, TRUE);
        ventanas_abiertas++;
        return 0;
    }
    case WM_SIZE:
        posicionar_controles_cfg(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, COLOR_BLANCO);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_PAINT:
        pintar_doble_buffer(hwnd, pintar_cfg);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_APLICAR && HIWORD(wp) == BN_CLICKED) { aplicar_configuracion(); return 0; }
        break;
    case WM_DESTROY:
        hwnd_cfg = NULL;
        if (--ventanas_abiertas <= 0) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND crear_ventana(HINSTANCE inst, const wchar_t *clase, const wchar_t *titulo, int cw, int ch, int x, int y)
{
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return CreateWindowExW(0, clase, titulo, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSW wc;
    HDC sdc;
    MSG msg;
    HWND w1, w2;
    SIZE sz;
    (void)prev; (void)cmd;

    SetProcessDPIAware();
    icc.dwSize = sizeof(icc); icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    sdc = GetDC(NULL);
    dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    font_11 = crear_fuente(11, 0, 0);
    font_14 = crear_fuente(14, 0, 0);
    font_9 = crear_fuente(9, 0, 0);
    font_10 = crear_fuente(10, 0, 0);
    font_11_vert = crear_fuente(11, 0, 900);
    font_11_italic = crear_fuente(11, 1, 0);
    sz = medir_texto(sdc, font_11, L"0123456789");
    alto_edit = sz.cy + 8;
    ReleaseDC(NULL, sdc);

    controles[0].val = phi; controles[1].val = f; controles[2].val = A;
    recalcular_derivados();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProcPrincipal;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"SimuladorPrincipal";
    RegisterClassW(&wc);
    wc.lpfnWndProc = WndProcCfg;
    wc.lpszClassName = L"SimuladorConfig";
    RegisterClassW(&wc);

    {
        RECT area = { 0, 0, 1920, 1080 }, marco = { 0, 0, 0, 0 };
        int w_main = MulDiv(1200, dpi, 96), h_main = MulDiv(950, dpi, 96);
        int w_cfg = MulDiv(781, dpi, 96), h_cfg = MulDiv(671, dpi, 96);
        int ancho_area, alto_area, x_cfg;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        AdjustWindowRectEx(&marco, WS_OVERLAPPEDWINDOW, FALSE, 0);
        ancho_area = area.right - area.left - (marco.right - marco.left);
        alto_area = area.bottom - area.top - (marco.bottom - marco.top);
        if (w_main > ancho_area - 20) w_main = ancho_area - 20;
        if (h_main > alto_area - 20) h_main = alto_area - 20;
        if (w_cfg > ancho_area - 20) w_cfg = ancho_area - 20;
        if (h_cfg > alto_area - 20) h_cfg = alto_area - 20;
        x_cfg = area.left + 20 + w_main + 60;
        if (x_cfg + w_cfg > area.right - 10) x_cfg = area.right - 10 - w_cfg - (marco.right - marco.left);
        w1 = crear_ventana(inst, L"SimuladorPrincipal", L"Simulador: señal, FFT y waterfall",
                           w_main, h_main, area.left + 20, area.top + 20);
        w2 = crear_ventana(inst, L"SimuladorConfig", L"Configuración del simulador",
                           w_cfg, h_cfg, x_cfg, area.top + 20);
    }
    if (!w1 || !w2) return 1;
    ShowWindow(w1, show);
    ShowWindow(w2, SW_SHOWNOACTIVATE);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
#endif
