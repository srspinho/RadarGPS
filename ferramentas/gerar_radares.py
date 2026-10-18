#!/usr/bin/env python3
"""
Converte o arquivo maparadar.csv no cabecalho C++ radares_db.h usado pelo sketch.

Formato de cada linha do CSV:
    longitude,latitude,Descricao - NN kmh@LIMITE
    ex.: -43.030287,-22.665031,Radar Fixo - 30 kmh@30

Os radares sao gravados ordenados por latitude, o que permite ao ESP32 achar
os candidatos proximos com busca binaria (em vez de varrer os 20 mil registros).

Uso:
    python3 gerar_radares.py ../dados/maparadar.csv ../RadarGPS/radares_db.h
"""
import sys

# A ordem aqui precisa bater com o enum TipoRadar em detector.h
TIPOS = [
    "Radar Fixo",
    "Semaforo com Radar",
    "Radar Movel",
    "Semaforo com Camera",
    "Policia Rodoviaria",
    "Pedagio",
]


def tipo_da_descricao(desc):
    for i, nome in enumerate(TIPOS):
        if desc.startswith(nome):
            return i
    raise ValueError("tipo desconhecido: %r" % desc)


def main(entrada, saida):
    radares = set()
    with open(entrada, encoding="utf-8", errors="replace") as f:
        for n, linha in enumerate(f, 1):
            linha = linha.strip()
            if not linha:
                continue
            try:
                lon, lat, resto = linha.split(",", 2)
                desc, limite = resto.rsplit("@", 1)
                lat_e6 = round(float(lat) * 1e6)
                lon_e6 = round(float(lon) * 1e6)
                radares.add((lat_e6, lon_e6, tipo_da_descricao(desc), int(limite)))
            except ValueError as e:
                print("linha %d ignorada (%s): %s" % (n, e, linha), file=sys.stderr)

    radares = sorted(radares)
    with open(saida, "w", encoding="ascii") as f:
        f.write("// Arquivo GERADO por ferramentas/gerar_radares.py - nao edite a mao.\n")
        f.write("// Fonte: %s\n" % entrada.replace("\\", "/").split("/")[-1])
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define NUM_RADARES %d\n\n" % len(radares))
        for nome, col, tipo_c in (("RADAR_LAT", 0, "int32_t"), ("RADAR_LON", 1, "int32_t"),
                                  ("RADAR_TIPO", 2, "uint8_t"), ("RADAR_LIMITE", 3, "uint8_t")):
            f.write("static const %s %s[NUM_RADARES] = {\n" % (tipo_c, nome))
            valores = [str(r[col]) for r in radares]
            por_linha = 10 if tipo_c == "int32_t" else 32
            for i in range(0, len(valores), por_linha):
                f.write("  " + ",".join(valores[i:i + por_linha]) + ",\n")
            f.write("};\n\n")

    contagem = [0] * len(TIPOS)
    for r in radares:
        contagem[r[2]] += 1
    print("%d radares gravados em %s" % (len(radares), saida))
    for nome, c in zip(TIPOS, contagem):
        print("  %-20s %6d" % (nome, c))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
