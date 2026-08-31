#!/usr/bin/env bash
# Laco de verificacao do firmware de aplicacao da UR.
#
# Existe porque "pio test" nao executa os extra_scripts do platformio.ini: a guarda de
# arquitetura precisa ser chamada explicitamente antes dos testes. O "pio run -e esp32dev" no
# fim garante que o alvo tambem compila com os flags estritos.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== guarda de arquitetura =="
python3 scripts/check_hexagonal.py

echo "== testes de dominio no host =="
pio test -e native

echo "== compilacao do alvo =="
if [ -f src/main.cpp ]; then
  pio run -e esp32dev
else
  echo "  pulado: src/main.cpp ainda nao existe (etapa 8 da ordem de entrega)."
  echo "  ate la o dominio e verificado so no host, que e o ponto do env native."
fi

echo "== tudo verde =="
