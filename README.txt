BlogsCIMTT-Usach-RaulZamoranoIllanes
====================================

Codigo de los blogs del CIMTT (Universidad de Santiago de Chile) escritos por
Raul Zamorano Illanes. Cada blog tiene su propia carpeta.

Contenido
---------

Blog_1/
    Simulador de senales en tiempo real: cuatro formas de onda (sinusoidal,
    cuadrada, diente de sierra y triangular), espectro de magnitud en dBV,
    espectro de fase y waterfall, con controles de fase, frecuencia y amplitud
    y una segunda ventana de configuracion (fs, N, refresco, chunks visibles,
    FFT maximo).

    test.py
        Version en Python, con el codigo fuente. Dependencias: numpy y
        matplotlib (no requiere scipy).
        Ejecutar con:  python test.py

    test.exe
        La misma aplicacion compilada en C para Windows (64 bits). Usa solo
        la API Win32 y GDI, por lo que no necesita instalar nada: basta con
        hacer doble clic. Solo se distribuye el ejecutable, no el codigo fuente.
