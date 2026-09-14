# Gera o .ota logo depois do firmware.bin, com o ALVO e a VERSAO ja preenchidos.
#
# POR QUE ISTO E UM PASSO DE BUILD E NAO UM COMANDO QUE ALGUEM LEMBRA DE RODAR. O campo que
# importa no cabecalho e o alvo, e ele e a unica coisa entre o operador e gravar a imagem da
# supervisora na sensora. Um empacotamento manual erra o alvo exatamente no dia corrido - e o
# erro so aparece na placa, no patio, quando ja e tarde. Aqui o alvo vem do proprio env que
# acabou de compilar: nao ha o que escolher errado.
#
# A versao vem de [common] fw_version do platformio.ini, pelo mesmo -DFW_VERSION que vai no
# binario: o que a placa diz de si mesma e o que esta no nome do arquivo.
import os
import subprocess
import sys

Import("env")  # noqa: F821

ALVO_POR_ENV = {"esp32dev": "supervisora", "pusi": "sensora"}


def empacotar(source, target, env):  # noqa: ARG001
    projeto = env["PROJECT_DIR"]
    raiz = os.path.dirname(projeto)
    script = os.path.join(raiz, "scripts", "empacota_ota.py")
    binario = str(target[0])
    alvo = ALVO_POR_ENV.get(env["PIOENV"])
    if alvo is None:
        print("empacota: env '%s' sem alvo conhecido - .ota NAO gerado" % env["PIOENV"])
        return
    if not os.path.isfile(script):
        print("empacota: %s nao existe - .ota NAO gerado" % script)
        return

    versao = "0.0.0"
    for flag in env.get("BUILD_FLAGS", []):
        if "FW_VERSION" in str(flag):
            versao = str(flag).split("=", 1)[1].strip().strip('\\"')

    saida = os.path.join(os.path.dirname(binario), "firmware-%s-%s.ota" % (alvo, versao))
    r = subprocess.run(
        [sys.executable, script, binario, "-a", alvo, "-v", versao, "-o", saida],
        capture_output=True,
        text=True,
    )
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stdout.write(r.stderr)
        # Nao derruba o build: o firmware.bin esta pronto e gravavel por cabo. O que falhou foi
        # so o empacotamento para o ponto de acesso.
        print("empacota: FALHOU - suba por cabo ou rode scripts/empacota_ota.py a mao")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", empacotar)  # noqa: F821
