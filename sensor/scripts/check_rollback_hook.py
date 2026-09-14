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

if elf is not None:
    for nm in ("xtensa-esp32-elf-nm", "nm"):
        try:
            saida = subprocess.run([nm, elf], capture_output=True, text=True, timeout=120).stdout
        except (FileNotFoundError, subprocess.SubprocessError):
            continue
        linhas = [l for l in saida.splitlines() if l.endswith(" verifyRollbackLater")]
        if not linhas:
            falhar("verifyRollbackLater nao aparece no ELF: " + elf)
        if not any(" T verifyRollbackLater" in l for l in linhas):
            falhar("verifyRollbackLater esta no ELF mas NAO como simbolo forte (T). O linker "
                   "escolheu a versao fraca do core:\n  " + "\n  ".join(linhas))
        break

print("check_rollback_hook: OK (definicao forte no lugar)")
