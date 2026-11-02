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
        Version en Python. Dependencias: numpy y matplotlib (no requiere scipy).
        Ejecutar con:  python test.py

    test_c/test.c
        La misma aplicacion escrita en C, usando solo la API Win32 y GDI
        (sin librerias externas). Incluye una FFT propia (radix-2 + Bluestein)
        para cualquier valor de N.

    test_c/build.bat
        Compila test.c con el compilador de C de Visual Studio (cl.exe) y
        genera test.exe. Busca Visual Studio automaticamente; basta con
        ejecutarlo. Con "build.bat selftest" genera una prueba de consola
        que verifica la FFT contra una DFT directa.
        Alternativa con gcc (MSYS2 / MinGW):
            gcc -O2 -municode -mwindows test.c -o test.exe -lcomctl32 -lgdi32 -luser32
