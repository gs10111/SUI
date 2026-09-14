#!/usr/bin/env python3
"""Empacota um firmware.bin no formato .ota do SUI-DI141388XY.

    python3 scripts/empacota_ota.py ur/.pio/build/esp32dev/firmware.bin -a supervisora
    python3 scripts/empacota_ota.py sensor/.pio/build/pusi/firmware.bin -a sensora

O arquivo gerado e o cabecalho de 64 bytes seguido da imagem, sem mais nada. E o arquivo que
alguem no patio sobe pela pagina do ponto de acesso.

POR QUE ISTO EXISTE EM VEZ DE SUBIR O firmware.bin DIRETO. O firmware.bin nao diz de que placa e.
As duas placas do produto sobem o mesmo ponto de acesso, com a mesma pagina. Uma imagem da
supervisora gravada na sensora e uma imagem valida que sobe e nao tem como ser atualizada de
novo. O campo `alvo` deste cabecalho e a unica coisa entre o operador e esse erro.

O LAYOUT ESTA DESCRITO EM lib_shared/depuri_ota/include/ota_package.h e os dois lados nao podem
divergir: o teste test_pacote_dourado_gerado_pelo_empacotador (ur/test/native/test_ota_package)
le um cabecalho gerado por ESTE script e quebra se o layout mudar de um lado so.
"""
import argparse
import binascii
import hashlib
import struct
import sys

MAGICA = b"DEPURIOT"
VERSAO_CABECALHO = 1
ALVOS = {"supervisora": 1, "ur": 1, "sensora": 2, "sensor": 2}
TAMANHO_CABECALHO = 64
MIN_IMAGEM = 4096
PARTICAO_BYTES = 1310720  # app0/app1 de particoes_sui_4mb.csv


def monta_cabecalho(imagem: bytes, alvo: int, versao: tuple) -> bytes:
    maior, menor, correcao = versao
    c = bytearray(TAMANHO_CABECALHO)
    c[0:8] = MAGICA
    struct.pack_into("<H", c, 8, VERSAO_CABECALHO)
    struct.pack_into("<H", c, 10, alvo)
    struct.pack_into("<I", c, 12, len(imagem))
    struct.pack_into("<I", c, 16, binascii.crc32(imagem) & 0xFFFFFFFF)
    c[20:52] = hashlib.sha256(imagem).digest()
    c[52] = maior
    c[53] = menor
    c[54] = correcao
    # 55 e 56..59 ficam em zero: o firmware recusa o pacote se nao estiverem.
    struct.pack_into("<I", c, 60, binascii.crc32(bytes(c[:60])) & 0xFFFFFFFF)
    return bytes(c)


def versao_de(texto: str) -> tuple:
    partes = texto.split(".")
    if len(partes) != 3:
        raise argparse.ArgumentTypeError("versao tem de ser maior.menor.correcao, ex. 0.2.0")
    return tuple(min(255, int(p)) for p in partes)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("firmware", help="caminho do firmware.bin")
    p.add_argument("-a", "--alvo", required=True, choices=sorted(ALVOS),
                   help="placa de destino")
    p.add_argument("-v", "--versao", type=versao_de, default=(0, 0, 0),
                   help="versao do firmware, ex. 0.2.0")
    p.add_argument("-o", "--saida", help="arquivo .ota de saida")
    args = p.parse_args()

    with open(args.firmware, "rb") as f:
        imagem = f.read()

    if len(imagem) < MIN_IMAGEM:
        print(f"ERRO: {len(imagem)} bytes e pequeno demais para ser firmware", file=sys.stderr)
        return 1
    if len(imagem) > PARTICAO_BYTES:
        print(f"ERRO: {len(imagem)} bytes nao cabe na particao de {PARTICAO_BYTES}",
              file=sys.stderr)
        return 1
    # O primeiro byte de uma imagem de aplicacao do ESP32 e a magica 0xE9. Sem esta conferencia,
    # empacotar o arquivo errado (um .elf, um partitions.bin) so seria descoberto na placa.
    if imagem[0] != 0xE9:
        print(f"ERRO: o arquivo nao comeca com 0xE9 - isto nao e uma imagem de aplicacao do "
              f"ESP32 (primeiro byte: 0x{imagem[0]:02X})", file=sys.stderr)
        return 1

    alvo = ALVOS[args.alvo]
    cabecalho = monta_cabecalho(imagem, alvo, args.versao)
    saida = args.saida or (args.firmware.rsplit(".", 1)[0] +
                           f"-{args.alvo}-{'.'.join(str(x) for x in args.versao)}.ota")
    with open(saida, "wb") as f:
        f.write(cabecalho)
        f.write(imagem)

    print(f"pacote : {saida}")
    print(f"alvo   : {args.alvo} ({alvo})")
    print(f"versao : {'.'.join(str(x) for x in args.versao)}")
    print(f"imagem : {len(imagem)} bytes")
    print(f"crc32  : 0x{binascii.crc32(imagem) & 0xFFFFFFFF:08X}")
    print(f"sha256 : {hashlib.sha256(imagem).hexdigest()}")
    print("\nO sha256 identifica exatamente este arquivo. A placa registra o dela no console\nde bancada assim que aceita o cabecalho - e assim que se confere que o que subiu foi este.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
