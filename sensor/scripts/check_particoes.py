# Guarda da tabela de particoes: reprova o build se o layout gravado na frota escorregar.
#
# POR QUE ESTA GUARDA EXISTE. O layout de flash nao e uma preferencia de build - ele esta
# GRAVADO em cada placa ja instalada em campo. Se o arquivo de particao mudar e o firmware novo
# for construido contra um layout diferente do que a placa tem no flash, a imagem e escrita no
# offset errado e a placa nao volta. Com OTA no produto isso deixa de ser um susto de bancada e
# passa a ser uma frota inteira que nao sobe - e nao ha OTA que conserte, porque o OTA e
# justamente o que quebrou.
#
# Ate 2026-09-14 nenhum platformio.ini declarava board_build.partitions: as tres builds herdavam
# o default.csv do framework POR OMISSAO. Um bump de versao do framework bastava para mover o
# offset da app1 em silencio. O arquivo agora e nosso; esta guarda impede que ele mude sem que
# alguem repare.
#
# Roda de dois jeitos, como o check_hexagonal.py: como pre-script do "pio run" e solto.
import hashlib
import os
import sys

try:
    Import("env")  # noqa: F821
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))

# O .csv fica na RAIZ do repositorio e serve as tres builds - uma copia por projeto poderia
# divergir, que e exatamente o modo de falha que esta guarda existe para impedir.
RAIZ = PROJECT_DIR
while not os.path.exists(os.path.join(RAIZ, "particoes_sui_4mb.csv")):
    pai = os.path.dirname(RAIZ)
    if pai == RAIZ:
        print("check_particoes: particoes_sui_4mb.csv nao encontrado", file=sys.stderr)
        sys.exit(1)
    RAIZ = pai
CSV = os.path.join(RAIZ, "particoes_sui_4mb.csv")

# O layout que a frota TEM no flash, em 2026-09-14. Mudar qualquer numero daqui exige passagem
# presencial em cada placa instalada - nao e alteracao de build.
ESPERADO = [
    ("nvs",      "data", "nvs",      0x9000,   0x5000),
    ("otadata",  "data", "ota",      0xE000,   0x2000),
    ("app0",     "app",  "ota_0",    0x10000,  0x140000),
    ("app1",     "app",  "ota_1",    0x150000, 0x140000),
    ("spiffs",   "data", "spiffs",   0x290000, 0x160000),
    ("coredump", "data", "coredump", 0x3F0000, 0x10000),
]
FLASH_BYTES = 4 * 1024 * 1024


def falhar(msg):
    print("check_particoes: " + msg, file=sys.stderr)
    sys.exit(1)


def num(txt):
    txt = txt.strip()
    return int(txt, 16) if txt.lower().startswith("0x") else int(txt)


linhas = []
with open(CSV, "r", encoding="utf-8") as fp:
    for bruta in fp:
        limpa = bruta.split("#", 1)[0].strip()
        if not limpa:
            continue
        campos = [c.strip() for c in limpa.split(",")]
        if len(campos) < 5:
            falhar("linha mal formada em particoes_sui_4mb.csv: " + limpa)
        linhas.append((campos[0], campos[1], campos[2], num(campos[3]), num(campos[4])))

if linhas != ESPERADO:
    detalhe = "\n".join(
        "  esperado %-9s %-5s %-9s 0x%06X 0x%06X" % e for e in ESPERADO
    ) + "\n" + "\n".join(
        "  lido     %-9s %-5s %-9s 0x%06X 0x%06X" % l for l in linhas
    )
    falhar(
        "a tabela de particoes DIVERGE do layout gravado na frota.\n"
        "Mudar isto quebra toda placa ja instalada - nao e alteracao de build.\n" + detalhe
    )

# A soma tem de fechar o chip de 4 MB, e nenhuma particao pode se sobrepor a outra.
cursor = linhas[0][3]
for nome, _tipo, _sub, off, tam in linhas:
    if off < cursor:
        falhar("particao %s comeca em 0x%X, dentro da anterior" % (nome, off))
    cursor = off + tam
if cursor > FLASH_BYTES:
    falhar("a tabela passa de 4 MB: termina em 0x%X" % cursor)

# O binario gerado, quando ja existe, tem de bater com o que a frota tem gravado.
SHA_FROTA = "148b959cbff1c38a"
bin_gerado = os.path.join(PROJECT_DIR, ".pio", "build")
for raiz, _dirs, arqs in os.walk(bin_gerado):
    if "partitions.bin" in arqs:
        caminho = os.path.join(raiz, "partitions.bin")
        with open(caminho, "rb") as fp:
            achado = hashlib.sha256(fp.read()).hexdigest()[:16]
        if achado != SHA_FROTA:
            falhar(
                "partitions.bin gerado (%s) difere do que a frota tem gravado (%s): %s"
                % (achado, SHA_FROTA, caminho)
            )

print("check_particoes: OK (%d particoes, layout da frota)" % len(linhas))
