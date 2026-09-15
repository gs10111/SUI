# Guarda de arquitetura: reprova o build se o dominio ou a camada de aplicacao passarem a
# depender de hardware. Exigido pelo criterio de aceitacao 3 do briefing.
#
# A regra vem do DIP: src/domain/ e src/app/ conhecem apenas as portas de src/ports/. Quem
# fala com o mundo e src/adapters/, e so ele. Sem esta guarda a violacao entra silenciosa e
# so aparece meses depois, quando alguem tenta rodar um teste no host.
import os
import re
import sys

# Roda de dois jeitos, porque o "pio test" do PlatformIO NAO executa extra_scripts:
#   - como pre-script do "pio run", via Import("env") do SCons;
#   - como script solto, "python3 scripts/check_hexagonal.py", que e como o scripts/check.sh
#     e a integracao continua o chamam antes de "pio test".
try:
    Import("env")  # noqa: F821  (so existe dentro do SCons do PlatformIO)
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIGIADOS = ("src/domain", "src/app", "src/ports")

# A BIBLIOTECA COMPARTILHADA DE ATUALIZACAO tem duas metades e a fronteira entre elas e o que
# mantem a decisao "esta imagem vira a particao de boot?" testavel no host. Tudo em
# lib_shared/depuri_ota e vigiado pelas MESMAS regras do dominio, MENOS os tres arquivos abaixo,
# que existem justamente para falar com a IDF e com o radio e que por isso ficam atras de
# #if !defined(HOST_BUILD).
#
# Sem esta guarda, um include de WiFi.h em ota_session.h passaria despercebido ate o dia em que
# alguem tentasse rodar o teste - e ai o caminho mais perigoso do produto estaria sem rede de
# protecao, que e exatamente o estado do qual este projeto saiu.
OTA_COMPARTILHADO = os.path.join(
    os.path.dirname(PROJECT_DIR), "lib_shared", "depuri_ota")
OTA_IMPUROS = ("esp_firmware_store.h", "esp_firmware_store.cpp", "wifi_update_portal.h",
               "wifi_update_portal.cpp", "ota_partition.h", "ota_password_store.h")

PROIBIDOS = (
    re.compile(r'^\s*#\s*include\s*[<"]Arduino\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]SPI\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]Wire\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]Preferences\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]WiFi\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]U8g2lib\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]esp_[A-Za-z0-9_/]*\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]driver/[A-Za-z0-9_/]*\.h[>"]'),
    re.compile(r'^\s*#\s*include\s*[<"]freertos/[A-Za-z0-9_/]*\.h[>"]'),
)

# float no dominio e proibido no caminho de decisao (briefing secao 7): decimos de grau em
# int16, codigo de DAC em uint16. Comentario com a palavra float continua permitido.
FLOAT = re.compile(r"(^|[^\w.])(float|double)\s")


def sem_comentario(linha):
    corte = linha.find("//")
    return linha if corte < 0 else linha[:corte]


def varrer_arquivo(caminho, rel, faltas):
    dentro_de_bloco = False
    with open(caminho, "r", encoding="utf-8") as arquivo:
        for numero, bruta in enumerate(arquivo, 1):
            linha = sem_comentario(bruta)
            if "/*" in linha:
                dentro_de_bloco = True
            if dentro_de_bloco:
                if "*/" in linha:
                    dentro_de_bloco = False
                continue
            for padrao in PROIBIDOS:
                if padrao.search(linha):
                    faltas.append(
                        "%s:%d: include de hardware no dominio: %s"
                        % (rel, numero, linha.strip())
                    )
            if FLOAT.search(linha):
                faltas.append(
                    "%s:%d: ponto flutuante no dominio: %s" % (rel, numero, linha.strip())
                )


def varrer_ota_compartilhado(faltas):
    if not os.path.isdir(OTA_COMPARTILHADO):
        faltas.append("lib_shared/depuri_ota nao existe - a guarda nao tem o que vigiar")
        return
    for pasta, _, arquivos in os.walk(OTA_COMPARTILHADO):
        for nome in sorted(arquivos):
            if not nome.endswith((".h", ".cpp")) or nome in OTA_IMPUROS:
                continue
            caminho = os.path.join(pasta, nome)
            varrer_arquivo(caminho, os.path.join("lib_shared/depuri_ota", nome), faltas)


def varrer():
    faltas = []
    varrer_ota_compartilhado(faltas)
    for vigiado in VIGIADOS:
        raiz = os.path.join(PROJECT_DIR, vigiado)
        if not os.path.isdir(raiz):
            continue
        for pasta, _, arquivos in os.walk(raiz):
            for nome in sorted(arquivos):
                if not nome.endswith((".h", ".cpp")):
                    continue
                caminho = os.path.join(pasta, nome)
                rel = os.path.relpath(caminho, PROJECT_DIR)
                varrer_arquivo(caminho, rel, faltas)
    return faltas


faltas = varrer()
if faltas:
    print("")
    print("ARQUITETURA HEXAGONAL VIOLADA - build reprovado")
    print("src/domain, src/app, src/ports e a metade PURA de lib_shared/depuri_ota nao")
    print("podem depender de hardware nem usar ponto flutuante.")
    print("")
    for falta in faltas:
        print("  " + falta)
    print("")
    sys.exit(1)
