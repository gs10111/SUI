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
    DENTRO_DO_SCONS = True
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    DENTRO_DO_SCONS = False


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

# A conferencia do ELF e um POST-ACTION DE VERDADE, e nao codigo solto no corpo deste arquivo.
#
# POR QUE ISSO IMPORTA, e custou um build silenciosamente vazio no Windows: o corpo de um
# extra_script roda quando o SCons LE o script - antes de compilar qualquer coisa. Nesse instante
# o ELF ainda nao existe. Ate 2026-09-15 este arquivo respondia a isso com sys.exit(0), e sys.exit
# dentro do SCons NAO "pula o resto do script": derruba a leitura do SConscript inteiro. Com
# codigo 0, o PlatformIO imprimia SUCCESS sem ter compilado uma linha.
#
# No Linux o defeito ficou invisivel porque .pio/build ja tinha um ELF de builds anteriores. Num
# clone novo - que foi o caso no Windows - nao ha ELF, e o build inteiro virava no-op com cara de
# sucesso. Um build que nao constroi nada e diz SUCCESS e pior do que um build que falha.
def _nm_candidatos():
    """Onde procurar o nm, em ordem, cobrindo Windows.

    O PATH nao serve sozinho: no Windows o toolchain do PlatformIO NAO entra no PATH do sistema,
    e `nm` simplesmente nao existe.
    """
    nomes = ["xtensa-esp32-elf-nm", "nm"]
    vistos = []
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
    for nome in nomes:
        achado = shutil.which(nome)
        if achado:
            vistos.append(achado)
    return vistos


def conferir_elf(elf):
    ferramentas = _nm_candidatos()
    if not ferramentas:
        falhar("nao achei o 'nm' do toolchain para inspecionar o ELF.\n"
               "  Sem ele NAO da para provar que o linker escolheu a definicao FORTE em vez do\n"
               "  simbolo fraco do core - e essa e a conferencia que impede uma placa sem\n"
               "  rollback automatico de sair de fabrica achando que tem.\n"
               "  Procurei em <PLATFORMIO_CORE_DIR>/packages/toolchain-xtensa*/bin e no PATH.")

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
        print("check_rollback_hook: OK (definicao forte no lugar, conferida no ELF)")
        return
    falhar("achei o 'nm' mas nenhuma das tentativas conseguiu ler o ELF: " + elf)


if DENTRO_DO_SCONS:
    # Registrado para rodar DEPOIS da linkagem, que e quando o ELF existe.
    def _pos_link(source, target, env):  # noqa: ARG001
        conferir_elf(str(target[0]))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _pos_link)  # noqa: F821
    print("check_rollback_hook: fonte OK (o ELF e conferido ao fim da linkagem)")
else:
    # Execucao solta, fora do build: confere o ELF que estiver la, se houver.
    elf = None
    for raiz, _dirs, arqs in os.walk(os.path.join(PROJECT_DIR, ".pio", "build")):
        for a in arqs:
            if a.endswith(".elf"):
                elf = os.path.join(raiz, a)
                break
        if elf:
            break
    if elf is None:
        print("check_rollback_hook: fonte OK (nao ha ELF para conferir - compile antes)")
    else:
        conferir_elf(elf)
