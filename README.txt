BlogsCIMTT-Usach-RaulZamoranoIllanes
====================================

Blogs del CIMTT (Universidad de Santiago de Chile) escritos por
Raul Zamorano Illanes. Cada blog tiene su propia carpeta (BLOG1, BLOG2, ...)
y dentro de ella siempre hay dos subcarpetas:

    CODE/      Los programas del blog (codigo fuente y/o ejecutables).
    BLOG_MD/   El texto del blog en formato Markdown, junto con las
               imagenes que utiliza, para leerlo directamente en GitHub.

Contenido
---------

BLOG1/
    Simulacion de senales en tiempo real usando Python: digitalizacion,
    espacios muestrales y frecuencia de muestreo.

    BLOG_MD/RealTimeSignalsWithPython.md
        Texto del blog (en espanol), con las figuras PLOT1.png, PLOT2.png
        y PLOT3.png.

    CODE/blog1.py
        Simulador de senales en tiempo real escrito en Python, con el
        codigo fuente: cuatro formas de onda (sinusoidal, cuadrada, diente
        de sierra y triangular), espectro de magnitud en dBV, espectro de
        fase y waterfall, con controles de fase, frecuencia y amplitud y una
        segunda ventana de configuracion (fs, N, refresco, chunks visibles,
        FFT maximo). Dependencias: numpy y matplotlib (no requiere scipy).
        Ejecutar con:  python blog1.py

    CODE/blog1.exe
        La misma aplicacion compilada en C para Windows (64 bits). Usa solo
        la API Win32 y GDI, por lo que no necesita instalar nada: basta con
        hacer doble clic. Solo se distribuye el ejecutable, no el codigo fuente.
