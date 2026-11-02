from collections import deque

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.widgets import Button, Slider, TextBox

A = 5.0
f = 5.0
phi = 0.0
forma_onda = "Sinusoidal"

fs = 300.0
N = 300
refresh_ms = 10
N_Chunk_per_display = 2
f_max_plot = 30.0

TIEMPO_Y_MIN = -12.0
TIEMPO_Y_MAX = 12.0
MAGNITUD_Y_MIN = -80.0
MAGNITUD_Y_MAX = 24.0
FASE_Y_MIN = -np.pi
FASE_Y_MAX = np.pi

mostrar_fase_de_baja_magnitud = True

T_chunk = N / fs
T_ventana = N_Chunk_per_display * T_chunk
frecuencias = np.fft.rfftfreq(N, d=1.0 / fs)
df = fs / N

tiempo_cursor = 0.0
fase_portadora = 0.0

plt.rcParams.update({
    "font.family": "Times New Roman",
    "mathtext.fontset": "custom",
    "mathtext.rm": "Times New Roman",
    "mathtext.it": "Times New Roman:italic",
    "mathtext.bf": "Times New Roman:bold",
    "mathtext.fallback": "stix",
    "font.size": 11,
})

fig, axs = plt.subplots(2, 2, figsize=(12, 9.5), dpi=120)
ax_t, ax_mag = axs[0, 0], axs[0, 1]
ax_fase, ax_wf = axs[1, 0], axs[1, 1]
fig.suptitle("Análisis dinámico de señal por chunks", fontsize=14)
fig.subplots_adjust(left=0.09, right=0.94, bottom=0.40,
                    top=0.91, wspace=0.35, hspace=0.45)
if fig.canvas.manager is not None:
    fig.canvas.manager.set_window_title("Simulador: señal, FFT y waterfall")

(line_t,) = ax_t.plot([], [], color="red", marker="o", markersize=2,
                      linewidth=1.0, label=r"$s[n]$")
ax_t.set(title=f"Señal temporal — {forma_onda}", xlabel="Tiempo (s)",
         ylabel="Amplitud (V)")
ax_t.set_xlim(0, T_ventana)
ax_t.set_ylim(TIEMPO_Y_MIN, TIEMPO_Y_MAX)
ax_t.grid(True, color="black", alpha=0.18)
ax_t.legend(loc="upper right")

(line_mag,) = ax_mag.plot(frecuencias, np.full_like(frecuencias, MAGNITUD_Y_MIN),
                          color="red", marker="o", markersize=3, linewidth=1.0)
ax_mag.set(title="Espectro de magnitud (FFT del chunk)",
           xlabel="Frecuencia (Hz)", ylabel="Magnitud (dBV)")
ax_mag.set_xlim(0, min(f_max_plot, fs / 2))
ax_mag.set_ylim(MAGNITUD_Y_MIN, MAGNITUD_Y_MAX)
ax_mag.grid(True, color="black", alpha=0.18)

(line_fase,) = ax_fase.plot(
    frecuencias, np.full_like(frecuencias, np.nan),
    linestyle="-", linewidth=1.0, color="black",
    marker="o", markerfacecolor="red", markeredgecolor="black", markersize=4,
)
ax_fase.set(title="Espectro de fase (FFT del chunk)",
            xlabel="Frecuencia (Hz)", ylabel="Fase (rad)")
ax_fase.set_xlim(0, min(f_max_plot, fs / 2))
ax_fase.set_ylim(FASE_Y_MIN, FASE_Y_MAX)
ax_fase.set_yticks([-np.pi, -np.pi / 2, 0, np.pi / 2, np.pi])
ax_fase.set_yticklabels([r"$-\pi$", r"$-\pi/2$", "0", r"$\pi/2$", r"$\pi$"])
ax_fase.grid(True, color="black", alpha=0.18)

DB_MIN = -80.0
DB_MAX = 20.0
cmap = LinearSegmentedColormap.from_list("negro_rojo", ["black", "red"])
im_wf = ax_wf.imshow(
    np.full((N_Chunk_per_display, len(frecuencias)), DB_MIN),
    origin="lower", aspect="auto", interpolation="nearest",
    cmap=cmap, vmin=DB_MIN, vmax=DB_MAX,
    extent=[-df / 2, frecuencias[-1] + df / 2, -T_ventana, 0],
)
ax_wf.set(title="Waterfall de magnitudes", xlabel="Frecuencia (Hz)",
          ylabel="Tiempo relativo (s)")
ax_wf.set_xlim(0, min(f_max_plot, fs / 2))
fig.colorbar(im_wf, ax=ax_wf, label="Magnitud (dBV)", fraction=0.046, pad=0.04)

t_chunks = deque(maxlen=N_Chunk_per_display)
s_chunks = deque(maxlen=N_Chunk_per_display)
espectros_db = deque(maxlen=N_Chunk_per_display)


FORMAS_ONDA = ("Sinusoidal", "Cuadrada", "Diente de sierra", "Triangular")
BOTON_ACTIVO = "#a80000"
BOTON_INACTIVO = "#222222"


def generar_onda(angulos, amplitud, tipo):
    if tipo == "Sinusoidal":
        base = np.sin(angulos)
    elif tipo == "Cuadrada":
        base = np.where(np.mod(angulos, 2 * np.pi) < np.pi, 1.0, -1.0)
    elif tipo == "Diente de sierra":
        fraccion = np.mod(angulos / (2 * np.pi) + 0.5, 1.0)
        base = 2.0 * fraccion - 1.0
    elif tipo == "Triangular":
        base = (2.0 / np.pi) * np.arcsin(np.sin(angulos))
    else:
        raise ValueError(f"Forma de onda no reconocida: {tipo}")
    return amplitud * base


botones_onda = {}
for i, nombre in enumerate(FORMAS_ONDA):
    ax_boton = fig.add_axes([0.135 + i * 0.195, 0.287, 0.175, 0.047])
    boton = Button(
        ax_boton, nombre,
        color=BOTON_ACTIVO if nombre == forma_onda else BOTON_INACTIVO,
        hovercolor="#bf3232",
    )
    boton.label.set_color("white")
    boton.label.set_fontsize(10)
    botones_onda[nombre] = boton


def seleccionar_onda(tipo):
    global forma_onda
    forma_onda = tipo
    for nombre, boton in botones_onda.items():
        color = BOTON_ACTIVO if nombre == tipo else BOTON_INACTIVO
        boton.color = color
        boton.ax.set_facecolor(color)
    ax_t.set_title(f"Señal temporal — {tipo}")
    fig.canvas.draw_idle()


for nombre, boton in botones_onda.items():
    boton.on_clicked(lambda _event, tipo=nombre: seleccionar_onda(tipo))

ax_slider_phi = fig.add_axes([0.24, 0.210, 0.49, 0.027])
ax_slider_f = fig.add_axes([0.24, 0.150, 0.49, 0.027])
ax_slider_A = fig.add_axes([0.24, 0.090, 0.49, 0.027])

slider_phi = Slider(ax_slider_phi, r"Fase $\phi$ (rad)", -np.pi, np.pi,
                    valinit=phi, valfmt="%.3f", color="red")
slider_f = Slider(ax_slider_f, "Frecuencia (Hz)", 0.5, 30.0,
                  valinit=f, valfmt="%.3f", color="red")
slider_A = Slider(ax_slider_A, "Amplitud (V)", 0.0, 10.0,
                  valinit=A, valfmt="%.3f", color="red")

for slider in (slider_phi, slider_f, slider_A):
    slider.valtext.set_visible(False)

ax_text_phi = fig.add_axes([0.79, 0.200, 0.105, 0.048])
ax_text_f = fig.add_axes([0.79, 0.140, 0.105, 0.048])
ax_text_A = fig.add_axes([0.79, 0.080, 0.105, 0.048])

caja_phi = TextBox(ax_text_phi, "", initial=f"{phi:.8g}", textalignment="center")
caja_f = TextBox(ax_text_f, "", initial=f"{f:.8g}", textalignment="center")
caja_A = TextBox(ax_text_A, "", initial=f"{A:.8g}", textalignment="center")

controles = {
    "phi": (slider_phi, caja_phi),
    "f": (slider_f, caja_f),
    "A": (slider_A, caja_A),
}

sincronizando = {nombre: False for nombre in controles}
mensaje_control = fig.text(0.24, 0.033, "", fontsize=9, color="red")


def sincronizar_desde_slider(valor, nombre):
    if sincronizando[nombre]:
        return
    _, caja = controles[nombre]
    sincronizando[nombre] = True
    try:
        caja.set_val(f"{valor:.8g}")
        mensaje_control.set_text("")
    finally:
        sincronizando[nombre] = False


def sincronizar_desde_caja(texto, nombre):
    if sincronizando[nombre]:
        return
    slider, caja = controles[nombre]
    try:
        valor = float(texto.strip().replace(",", "."))
        if not np.isfinite(valor):
            raise ValueError("valor no finito")
        if not slider.valmin <= valor <= slider.valmax:
            raise ValueError("fuera de rango")
    except ValueError:
        mensaje_control.set_text(
            f"Valor no válido para {nombre}. Rango: "
            f"{slider.valmin:g} a {slider.valmax:g}."
        )
        sincronizando[nombre] = True
        try:
            caja.set_val(f"{slider.val:.8g}")
        finally:
            sincronizando[nombre] = False
        fig.canvas.draw_idle()
        return

    slider.set_val(valor)
    sincronizar_desde_slider(slider.val, nombre)


for nombre, (slider, caja) in controles.items():
    slider.on_changed(lambda valor, clave=nombre: sincronizar_desde_slider(valor, clave))
    caja.on_submit(lambda texto, clave=nombre: sincronizar_desde_caja(texto, clave))


fig_cfg = plt.figure(figsize=(7.1, 6.1), dpi=110)
fig_cfg.suptitle("Configuración de adquisición y visualización", fontsize=14)
if fig_cfg.canvas.manager is not None:
    fig_cfg.canvas.manager.set_window_title("Configuración del simulador")

fig_cfg.text(0.075, 0.87,
             "Modifica los parámetros y pulsa Aplicar.\n"
             "Los cambios no se ejecutan hasta pulsar el botón.",
             fontsize=10, va="top")

campos = [
    ("fs", "fs - Muestreo (Samples/s)", f"{fs:g}"),
    ("N", "N - Muestras / chunk", str(N)),
    ("refresh_ms", "Refresco (ms)", str(refresh_ms)),
    ("N_Chunk_per_display", "Chunks visibles", str(N_Chunk_per_display)),
    ("f_max_plot", "FFT máximo (Hz)", f"{f_max_plot:g}"),
]
cajas = {}
for i, (clave, etiqueta, valor) in enumerate(campos):
    ypos = 0.735 - 0.122 * i
    caja_ax = fig_cfg.add_axes([0.56, ypos, 0.28, 0.062])
    cajas[clave] = TextBox(caja_ax, etiqueta, initial=valor, textalignment="center")

ax_apply = fig_cfg.add_axes([0.59, 0.105, 0.22, 0.083])
boton_aplicar = Button(ax_apply, "Aplicar", color="0.87", hovercolor="0.76")
mensaje_cfg = fig_cfg.text(0.08, 0.03, "Configuración inicial activa.",
                           fontsize=10, color="black", va="center")


def entero_positivo(texto, nombre, minimo=1):
    valor = float(texto.strip())
    if not np.isfinite(valor) or not valor.is_integer() or valor < minimo:
        raise ValueError(f"{nombre} debe ser un entero >= {minimo}.")
    return int(valor)


def aplicar_configuracion(event):
    global fs, N, refresh_ms, N_Chunk_per_display, f_max_plot
    global T_chunk, T_ventana, frecuencias, df
    global t_chunks, s_chunks, espectros_db

    try:
        fs_nuevo = float(cajas["fs"].text.strip())
        N_nuevo = entero_positivo(cajas["N"].text, "N", minimo=2)
        refresh_nuevo = entero_positivo(
            cajas["refresh_ms"].text, "Refresco", minimo=1
        )
        chunks_nuevos = entero_positivo(
            cajas["N_Chunk_per_display"].text, "Chunks visibles", minimo=1
        )
        fmax_nuevo = float(cajas["f_max_plot"].text.strip())
        if not np.isfinite(fs_nuevo) or fs_nuevo <= 0:
            raise ValueError("fs debe ser un número positivo.")
        if not np.isfinite(fmax_nuevo) or fmax_nuevo <= 0:
            raise ValueError("FFT máximo debe ser positivo.")
        if N_nuevo * chunks_nuevos > 1_000_000:
            raise ValueError("Demasiadas muestras visibles (> 1 000 000).")
        if N_nuevo > 100_000 or chunks_nuevos > 500 or refresh_nuevo > 10_000:
            raise ValueError("Parámetros demasiado grandes para esta interfaz.")
    except ValueError as error:
        mensaje_cfg.set_text("Error: " + str(error))
        mensaje_cfg.set_color("red")
        fig_cfg.canvas.draw_idle()
        return

    fs, N, refresh_ms = fs_nuevo, N_nuevo, refresh_nuevo
    N_Chunk_per_display, f_max_plot = chunks_nuevos, fmax_nuevo
    T_chunk = N / fs
    T_ventana = N_Chunk_per_display * T_chunk
    frecuencias = np.fft.rfftfreq(N, d=1.0 / fs)
    df = fs / N
    limite_fft_x = min(f_max_plot, fs / 2)

    t_chunks = deque(maxlen=N_Chunk_per_display)
    s_chunks = deque(maxlen=N_Chunk_per_display)
    espectros_db = deque(maxlen=N_Chunk_per_display)

    line_t.set_data([], [])
    ax_t.set_xlim(max(0, tiempo_cursor - T_ventana),
                  max(T_ventana, tiempo_cursor))
    line_mag.set_data(frecuencias, np.full_like(frecuencias, MAGNITUD_Y_MIN))
    line_fase.set_data(frecuencias, np.full_like(frecuencias, np.nan))
    ax_mag.set_xlim(0, limite_fft_x)
    ax_fase.set_xlim(0, limite_fft_x)
    im_wf.set_data(np.full((N_Chunk_per_display, len(frecuencias)), DB_MIN))
    im_wf.set_extent([-df / 2, frecuencias[-1] + df / 2, -T_ventana, 0])
    ax_wf.set_xlim(0, limite_fft_x)
    ax_wf.set_ylim(-T_ventana, 0)

    ani.event_source.interval = refresh_ms

    aviso = (f"Aplicado: fs={fs:g} Samples/s; N={N}; "
             f"chunk={T_chunk:.3g} s; ventana={T_ventana:.3g} s")
    if slider_f.val >= fs / 2:
        aviso += "  |  Aviso: posible aliasing"
    mensaje_cfg.set_text(aviso)
    mensaje_cfg.set_color("black")
    fig_cfg.canvas.draw_idle()
    fig.canvas.draw_idle()


boton_aplicar.on_clicked(aplicar_configuracion)

def actualizar(_frame):
    global tiempo_cursor, fase_portadora

    A_actual = slider_A.val
    f_actual = slider_f.val
    phi_actual = slider_phi.val

    n = tiempo_cursor + np.arange(N) / fs
    tiempo_cursor += N / fs

    angulos = fase_portadora + 2 * np.pi * f_actual * np.arange(N) / fs
    s = generar_onda(angulos + phi_actual, A_actual, forma_onda)
    fase_portadora = (fase_portadora + 2 * np.pi * f_actual * N / fs) % (2 * np.pi)

    t_chunks.append(n)
    s_chunks.append(s)
    line_t.set_data(np.concatenate(t_chunks), np.concatenate(s_chunks))
    ax_t.set_xlim(max(0, tiempo_cursor - T_ventana),
                  max(T_ventana, tiempo_cursor))

    X = np.fft.rfft(s)
    magnitud = 2.0 * np.abs(X) / N
    magnitud[0] /= 2.0
    if N % 2 == 0:
        magnitud[-1] /= 2.0
    fase_fft = np.angle(X)
    if not mostrar_fase_de_baja_magnitud:
        umbral = max(1e-10, 0.05 * np.max(magnitud))
        fase_fft[magnitud < umbral] = np.nan

    magnitud_db = 20.0 * np.log10(np.maximum(magnitud, 10 ** (DB_MIN / 20)))
    line_mag.set_data(frecuencias, magnitud_db)
    line_fase.set_data(frecuencias, fase_fft)

    espectros_db.append(np.clip(magnitud_db, DB_MIN, DB_MAX))
    waterfall = np.full((N_Chunk_per_display, len(frecuencias)), DB_MIN)
    waterfall[-len(espectros_db):, :] = np.asarray(espectros_db)
    im_wf.set_data(waterfall)

    return line_t, line_mag, line_fase, im_wf


def iniciar():
    return line_t, line_mag, line_fase, im_wf


ani = FuncAnimation(fig, actualizar, init_func=iniciar,
                    interval=refresh_ms, blit=False, cache_frame_data=False)

plt.show()
