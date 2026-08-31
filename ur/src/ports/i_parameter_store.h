// src/ports/i_parameter_store.h
// Persistencia dos parametros. A porta e um armazem de BLOB POR SLOT, com buffer
// do chamador: ela nao conhece o layout do registro, nao calcula CRC, nao decide
// qual banco e o mais novo e nao interpreta numero de sequencia. Tudo isso e
// dominio puro (ParamRecord + ParamStoreLogic), testavel em env:native com um
// slot em RAM - que e exatamente o que torna auditavel a atomicidade de dois
// bancos exigida por equipamento de seguranca.
// O manual fala em "EEPROM interna"; a placa e ESP32-WROOM-32D e a retencao e NVS
// em flash, com apagamento por setor. Dai as duas exigencias que a porta declara:
// write() so devolve kOk depois de RELER e conferir o que gravou, e existe um
// orcamento de tempo publicado (writeBudgetMs) para o chamador planejar o ciclo.
// A porta nunca chuta o watchdog e nunca chama o relogio: quem controla o ciclo
// e o dominio.
// Alvo: NvsParameterStore (src/platform/nvs_parameter_store.cpp) sobre NvsStore,
//       namespace "depuri1", chaves "par_a", "par_b", "cal_fab".
// Fake: FakeParameterStore (test/native) - tres arrays estaticos com injecao de
//       falha: escrita truncada, corrupcao de byte e Err::Storage sob demanda,
//       para reproduzir queda de energia no meio da gravacao.
// REQ:  MAN-2.1-L32 (retencao sem bateria), MAN-3-L47 (dita EEPROM interna),
//       MAN-5.4-L101/L102/L127/L128 e MAN-7-L299/L300 (momento da gravacao),
//       MAN-5.11-L233 e Tabela 2 (reset de fabrica restaura tambem a calibracao),
//       decisao 2 (commit-on-confirm com dois bancos), decisao 9 item 11.
#pragma once

#include <stdint.h>

#include "status.h"

enum class ParamSlot : uint8_t {
    BankA = 0,    // banco de parametros do usuario, copia 1
    BankB,        // banco de parametros do usuario, copia 2
    FactoryCal,   // calibracao gravada pelo jig; a IHM nunca escreve aqui
};

constexpr uint8_t kParamSlotCount = 3;

class IParameterStore {
public:
    virtual ~IParameterStore() = default;
    IParameterStore(const IParameterStore&) = delete;
    IParameterStore& operator=(const IParameterStore&) = delete;

    virtual Status begin() = 0;
    virtual bool ready() const = 0;

    // Maior blob aceito por slot. O registro do produto tem 48 bytes.
    virtual uint16_t capacityBytes() const = 0;

    // true quando o slot ja recebeu alguma escrita. NAO diz nada sobre validade
    // do conteudo: integridade e assunto do CRC do dominio.
    virtual bool exists(ParamSlot slot) const = 0;

    // Le o slot inteiro para o buffer do chamador. Err::Param se cap < tamanho
    // gravado; Err::Storage se o slot nunca foi escrito ou a midia falhou.
    virtual Status read(ParamSlot slot, void* dst, uint16_t cap, uint16_t& outLen) = 0;

    // Grava o blob inteiro (nunca campo a campo) e RELE para conferir byte a byte
    // antes de devolver kOk. Bloqueia por ate writeBudgetMs(); pode envolver
    // apagamento de setor. Err::Storage se a releitura divergir - nesse caso o
    // chamador tem de restaurar o valor anterior e avisar o operador.
    virtual Status write(ParamSlot slot, const void* src, uint16_t len) = 0;

    virtual Status erase(ParamSlot slot) = 0;

    // Orcamento de bloqueio de write(), em ms (250). O chamador usa isto para
    // garantir que a gravacao cabe entre duas avaliacoes de rele e dentro da
    // margem do watchdog.
    virtual uint32_t writeBudgetMs() const = 0;

    // Escritas concluidas desde o boot, por slot. Telemetria de desgaste da flash.
    virtual uint32_t writeCount(ParamSlot slot) const = 0;

    virtual const char* slotName(ParamSlot slot) const = 0;

protected:
    IParameterStore() = default;
};
