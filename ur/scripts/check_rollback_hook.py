# Guarda do gancho de rollback: reprova o build se a definicao FORTE de verifyRollbackLater()
# sumir do binario.
#
# POR QUE PRECISA DE GUARDA. O simbolo do core e FRACO. Se este arquivo for removido, renomeado,
# excluido do build_src_filter, ou se a assinatura divergir (C++ mangling em vez de extern "C"),
# o linker escolhe silenciosamente a versao do core - que devolve false - e o ESP32 volta a
# marcar como valida qualquer imagem que apenas suba. Nao ha erro de compilacao, nao ha aviso, e
# o defeito so aparece no dia em que uma atualizacao ruim precisava ter voltado sozinha.
#
# A conferencia e feita no ELF, e nao no codigo-fonte: o que importa e o que foi LINKADO.
import os
import shutil
import subprocess
import sys

try:
    Import("env")  # noqa: F821
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def falhar(msg):
    print("check_rollback_hook: " + msg, file=sys.stderr)
    sys.exit(1)


fonte = os.path.join(PROJECT_DIR, "src", "ota_rollback_hook.cpp")
if not os.path.exists(fonte):
    falhar("src/ota_rollback_hook.cpp sumiu. Sem ele o core do Arduino volta a marcar como "
           "valida qualquer imagem que suba, e nao existe rollback automatico.")

with open(fonte, "r", encoding="utf-8") as fp:
    texto = fp.read()
if 'extern "C" bool verifyRollbackLater()' not in texto:
    falhar('a definicao tem de ser extern "C" bool verifyRollbackLater(): com mangling de C++ o '
           "linker nao a casa com o simbolo fraco do core e escolhe o do core, em silencio.")
if "return true;" not in texto:
    falhar("verifyRollbackLater() tem de devolver true para o core pular o bloco de marcacao "
           "automatica.")

# No ELF, o simbolo tem de existir e NAO pode ser fraco (minusculas em nm = fraco/local).
elf = None
for raiz, _dirs, arqs in os.walk(os.path.join(PROJECT_DIR, ".pio", "build")):
    for a in arqs:
        if a.endswith(".elf"):
            elf = os.path.join(raiz, a)
            break
    if elf:
        break


def candidatos_nm():
    """Onde procurar o nm, em ordem, cobrindo Windows.

    O PATH nao serve sozinho: no Windows o toolchain do PlatformIO NAO entra no PATH do sistema,
    e `nm` simplesmente nao existe. Ate 2026-09-15 este script tratava isso como "tudo bem" e
    imprimia OK sem ter olhado o ELF - ou seja, a conferencia que existe para pegar o linker
    escolhendo a versao FRACA do core nunca rodava, e o build dizia que estava tudo certo.
    """
    nomes = ["xtensa-esp32-elf-nm", "nm"]
    vistos = []
    # 1. o toolchain que o proprio PlatformIO baixou - o unico lugar garantido no Windows
    raiz_pio = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(
        os.path.expanduser("~"), ".platformio")
    pacotes = os.path.join(raiz_pio, "packages")
    if os.path.isdir(pacotes):
        for pacote in sorted(os.listdir(pacotes)):
            if not pacote.startswith("toolchain-xtensa"):
                continue
            binario = os.path.join(pacotes, pacote, "bin")
            for nome in nomes:
                for ext in ("", ".exe"):
                    caminho = os.path.join(binario, nome + ext)
                    if os.path.isfile(caminho):
                        vistos.append(caminho)
    # 2. e o PATH, para quem tem o toolchain instalado por fora
    for nome in nomes:
        achado = shutil.which(nome)
        if achado:
            vistos.append(achado)
    return vistos


if elf is None:
    # Rodar sem ELF e normal: o script tambem serve como conferencia solta, antes de compilar.
    print("check_rollback_hook: fonte OK (sem ELF ainda - a conferencia do binario fica para o "
          "fim do build)")
    sys.exit(0)

ferramentas = candidatos_nm()
if not ferramentas:
    falhar("nao achei o 'nm' do toolchain para inspecionar o ELF.\n"
           "  Sem ele NAO da para provar que o linker escolheu a definicao FORTE em vez do\n"
           "  simbolo fraco do core - e essa e a conferencia que impede uma placa sem rollback\n"
           "  automatico de sair de fabrica achando que tem.\n"
           "  Procurei em <PLATFORMIO_CORE_DIR>/packages/toolchain-xtensa*/bin e no PATH.")

conferido = False
for nm in ferramentas:
    try:
        saida = subprocess.run([nm, elf], capture_output=True, text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        continue
    linhas = [l for l in saida.splitlines() if l.endswith(" verifyRollbackLater")]
    if not linhas:
        falhar("verifyRollbackLater nao aparece no ELF: " + elf)
    if not any(" T verifyRollbackLater" in l for l in linhas):
        falhar("verifyRollbackLater esta no ELF mas NAO como simbolo forte (T). O linker "
               "escolheu a versao fraca do core:\n  " + "\n  ".join(linhas))
    conferido = True
    break

if not conferido:
    falhar("achei o 'nm' mas nenhuma das tentativas conseguiu ler o ELF: " + elf)

print("check_rollback_hook: OK (definicao forte no lugar, conferida no ELF)")
