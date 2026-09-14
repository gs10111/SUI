#!/usr/bin/env python3
"""Calcula SSID e senha do ponto de acesso de atualizacao a partir do MAC da placa.

    python3 scripts/senha_ap.py 3C:71:BF:12:34:56 -a supervisora
    python3 scripts/senha_ap.py 3c71bf123457     -a sensora

SO SERVE PARA O CAMINHO DEGRADADO. Se a placa passou pelo jig, a senha dela foi SORTEADA e
gravada em NVS ("ota"/"pw"): ela nao sai do MAC, nao sai daqui, e esta na etiqueta. Este script
reproduz a derivacao que o firmware usa QUANDO NAO HA senha em NVS - hoje, todas as placas.

E POR ISSO MESMO ELE E A DEMONSTRACAO DO PROBLEMA: qualquer pessoa com este arquivo e o MAC de um
equipamento (que vai no ar, em texto claro, em toda baliza do ponto de acesso) entra nele. Ver
docs/ota.md secao 3. O conserto e o jig sortear a senha, nao esconder este script.

A derivacao tem de bater byte a byte com ota::derivedPassword() em
lib_shared/depuri_ota/include/ota_credentials.h.
"""
import argparse
import hashlib
import sys

ALFABETO = "23456789ABCDEFGHJKMNPQRSTUVWXYZ#"   # 32 simbolos, sem 0/O/1/I/L
DOMINIO = b"DIELETRONS-SUI-OTA-AP-v1"
CARACTERES = 12
PREFIXO = {"supervisora": "SUI-UR-", "ur": "SUI-UR-", "sensora": "SUI-SEN-", "sensor": "SUI-SEN-"}


def mac_de(texto):
    limpo = texto.replace(":", "").replace("-", "").replace(".", "").strip()
    if len(limpo) != 12:
        raise argparse.ArgumentTypeError("MAC tem 6 bytes, ex. 3C:71:BF:12:34:56")
    return bytes.fromhex(limpo)


def senha(mac):
    resumo = hashlib.sha256(DOMINIO + mac).digest()
    return "".join(ALFABETO[resumo[i] % len(ALFABETO)] for i in range(CARACTERES))


def ssid(mac, alvo):
    return PREFIXO[alvo] + "".join("%02X" % b for b in mac[3:])


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mac", type=mac_de, help="MAC da placa, ex. 3C:71:BF:12:34:56")
    p.add_argument("-a", "--alvo", required=True, choices=sorted(PREFIXO))
    args = p.parse_args()
    print("ssid  : %s" % ssid(args.mac, args.alvo))
    print("senha : %s" % senha(args.mac))
    print("\nSo vale se a placa NAO tiver senha gravada na producao.")
    print("A propria placa imprime as duas no console 115200 ao ligar:")
    print("  ota: ponto de acesso NO AR <ssid> senha <senha>  (DERIVADA DO MAC ...)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
