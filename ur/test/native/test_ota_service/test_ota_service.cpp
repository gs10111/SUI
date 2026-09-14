// O CAMINHO INTEIRO, do primeiro byte que o portal empurra ate a particao de boot trocada, sem
// WiFi e sem flash. E o unico jeito de exercitar o caminho onde um erro vira equipamento morto a
// 500 m dentro de um modulo.
//
// O que estes testes protegem, em ordem de gravidade:
//   1. nada chega a flash antes da confirmacao no painel
//   2. a particao de boot NAO e trocada quando a imagem nao confere
//   3. o handle da flash nunca fica pendurado - senao a proxima atualizacao e recusada e o
//      equipamento so volta a aceitar depois de desligar a energia
//   4. o que foi gravado e igual ao que entrou
#include <string.h>
#include <unity.h>

#include "fakes/fake_firmware_store.h"
#include "ota_service.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint32_t kT0 = 1000u;
constexpr uint32_t kTamanho = 8192u;

struct Pacote {
    uint8_t cabecalho[ota::kHeaderBytes];
    uint8_t imagem[kTamanho];
};

void montaPacote(Pacote& p, uint16_t alvo) {
    p.imagem[0] = 0xE9u;
    uint32_t x = 0x12345678u;
    for (uint32_t i = 1; i < kTamanho; ++i) {
        x = x * 1664525u + 1013904223u;
        p.imagem[i] = static_cast<uint8_t>(x >> 24);
    }
    ota::PackageHeader h = {};
    h.versaoCabecalho = ota::kHeaderVersion;
    h.alvo = alvo;
    h.tamanhoImagem = kTamanho;
    h.crc32Imagem = ota::crc32(p.imagem, kTamanho);
    ota::Sha256 s;
    s.update(p.imagem, kTamanho);
    s.finish(h.sha256Imagem);
    h.versaoMaior = 0;
    h.versaoMenor = 3;
    h.versaoCorrecao = 0;
    ota::writeHeader(h, p.cabecalho);
}

// Leva o servico ate Gravando pelo caminho legitimo da supervisora.
void ateGravando(app::OtaService& s, const Pacote& p, uint32_t t) {
    s.service(t, false);
    TEST_ASSERT_TRUE(s.onHeader(p.cabecalho, ota::kHeaderBytes));
    s.confirmar(t);
    s.service(t, true);  // main.cpp confirma que as saidas ja estao em alarme
}

}  // namespace

// O caminho feliz inteiro, e a conferencia de que o que ficou na flash e o que entrou.
static void test_caminho_feliz_grava_a_imagem_certa_e_troca_a_particao(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);

    ateGravando(s, p, kT0);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Gravando);
    TEST_ASSERT_EQUAL_UINT32(1, flash.begins());

    for (uint32_t pos = 0; pos < kTamanho; pos += 1024u) {
        TEST_ASSERT_TRUE(s.onChunk(p.imagem + pos, 1024u));
        s.service(kT0 + pos, true);
    }
    TEST_ASSERT_EQUAL_UINT16(1000, s.progressoPorMil());

    s.onEnd();
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Concluido);
    TEST_ASSERT_TRUE(s.reinicioPendente());
    TEST_ASSERT_EQUAL_UINT32(1, flash.finishes());
    TEST_ASSERT_EQUAL_UINT32(1, flash.activates());
    TEST_ASSERT_EQUAL_UINT32(0, flash.aborts());

    // O que ficou na flash tem de ser o que entrou.
    TEST_ASSERT_EQUAL_UINT32(kTamanho, flash.escritos());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p.imagem, flash.gravado(), kTamanho);
}

// GRAVIDADE 1. Antes da confirmacao no painel, a flash nem e aberta.
static void test_nada_toca_a_flash_antes_da_confirmacao(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);

    s.service(kT0, false);
    TEST_ASSERT_TRUE(s.onHeader(p.cabecalho, ota::kHeaderBytes));
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);

    // Mesmo com main.cpp dizendo que as saidas estao em alarme, sem confirmacao nao abre.
    s.service(kT0 + 10u, true);
    TEST_ASSERT_EQUAL_UINT32(0, flash.begins());
    TEST_ASSERT_FALSE(flash.aberta());
    TEST_ASSERT_FALSE(s.onChunk(p.imagem, 1024u));
    TEST_ASSERT_EQUAL_UINT32(0, flash.escritos());
}

// E mesmo confirmado, a flash so abre quando main.cpp diz que as saidas JA estao em alarme.
static void test_a_flash_so_abre_depois_de_as_saidas_irem_para_alarme(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);

    s.service(kT0, false);
    s.onHeader(p.cabecalho, ota::kHeaderBytes);
    s.confirmar(kT0);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());

    s.service(kT0 + 10u, false);  // main.cpp ainda nao aplicou
    TEST_ASSERT_EQUAL_UINT32(0, flash.begins());
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Preparando);

    s.service(kT0 + 20u, true);
    TEST_ASSERT_EQUAL_UINT32(1, flash.begins());
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Gravando);
}

// GRAVIDADE 2. Imagem que nao confere NAO pode trocar a particao de boot.
static void test_imagem_corrompida_nao_troca_a_particao(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);

    p.imagem[kTamanho / 2u] ^= 0x01u;  // um bit no meio
    for (uint32_t pos = 0; pos < kTamanho; pos += 1024u) {
        TEST_ASSERT_TRUE(s.onChunk(p.imagem + pos, 1024u));
    }
    s.onEnd();

    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::ImagemInvalida);
    TEST_ASSERT_EQUAL_UINT32(0, flash.activates());
    TEST_ASSERT_FALSE(s.reinicioPendente());
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
    TEST_ASSERT_FALSE(flash.aberta());
}

// E imagem truncada tambem nao: o navegador que perdeu a conexao deixa exatamente isto.
static void test_imagem_truncada_nao_troca_a_particao(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);

    TEST_ASSERT_TRUE(s.onChunk(p.imagem, kTamanho - 1024u));
    s.onEnd();
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_EQUAL_UINT32(0, flash.activates());
    TEST_ASSERT_FALSE(flash.aberta());
}

// E se a propria IDF reprovar a imagem gravada em finish(), tambem nao.
static void test_finish_reprovado_pela_idf_nao_troca_a_particao(void) {
    FakeFirmwareStore flash;
    flash.falharFinish(true);
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);
    s.onChunk(p.imagem, kTamanho);
    s.onEnd();

    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::FalhaDeTroca);
    TEST_ASSERT_EQUAL_UINT32(0, flash.activates());
}

// Pacote da outra placa: recusado antes de abrir a flash, e o motivo sai na pagina.
static void test_pacote_da_outra_placa_nem_abre_a_flash(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSensora);

    s.service(kT0, false);
    TEST_ASSERT_FALSE(s.onHeader(p.cabecalho, ota::kHeaderBytes));
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Recusado);
    TEST_ASSERT_TRUE(s.rejectReason() == ota::HeaderVerdict::AlvoErrado);
    TEST_ASSERT_EQUAL_UINT32(0, flash.begins());

    PortalStatus st;
    s.preencherStatusDoPortal(st);
    TEST_ASSERT_EQUAL_STRING("PACOTE DA OUTRA PLACA", st.detalhe);
    TEST_ASSERT_FALSE(st.aceitandoBytes);
}

// O tamanho e conferido contra a particao REAL, e nao contra uma constante: uma placa com
// particao menor tem de recusar o que nao cabe nela.
static void test_o_tamanho_e_conferido_contra_a_particao_desta_placa(void) {
    FakeFirmwareStore flash;
    flash.setCapacidade(kTamanho - 1u);
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);

    s.service(kT0, false);
    TEST_ASSERT_FALSE(s.onHeader(p.cabecalho, ota::kHeaderBytes));
    TEST_ASSERT_TRUE(s.rejectReason() == ota::HeaderVerdict::TamanhoInvalido);
    TEST_ASSERT_EQUAL_UINT32(0, flash.begins());
}

// GRAVIDADE 3. O handle nao pode ficar pendurado em NENHUM caminho de morte - senao o
// esp_ota_begin() seguinte devolve erro e o equipamento so volta a aceitar atualizacao depois de
// desligar a energia. Este e o defeito que transforma "uma tentativa falhou" em "viagem ate la".
static void test_a_flash_nunca_fica_aberta_depois_de_a_sessao_morrer(void) {
    enum Caminho { kCancelado, kEstagnou, kTeto, kFalhaDeIo, kImagemRuim, kAbortadoPeloPortal };
    const Caminho kCaminhos[] = {kCancelado, kEstagnou, kTeto, kFalhaDeIo, kImagemRuim,
                                 kAbortadoPeloPortal};

    for (size_t i = 0; i < sizeof(kCaminhos) / sizeof(kCaminhos[0]); ++i) {
        FakeFirmwareStore flash;
        app::OtaService s(flash, ota::kAlvoSupervisora, true);
        Pacote p;
        montaPacote(p, ota::kAlvoSupervisora);
        if (kCaminhos[i] == kFalhaDeIo) {
            flash.falharWriteAposBytes(2048u);
        }
        ateGravando(s, p, kT0);
        TEST_ASSERT_TRUE(flash.aberta());

        uint32_t t = kT0;
        switch (kCaminhos[i]) {
            case kCancelado:
                s.cancelar(t);
                break;
            case kEstagnou:
                s.onChunk(p.imagem, 1024u);
                t = kT0 + ota::kEstagnacaoMs + 1u;
                s.service(t, true);
                break;
            case kTeto:
                // Fluxo lento porem CONTINUO: nunca estagna, entao so o teto absoluto o mata.
                // Pedacos pequenos de proposito - a imagem tem de durar mais que o teto.
                for (uint32_t k = 0; k < ota::kGravacaoTetoMs / (ota::kEstagnacaoMs / 2u) + 4u;
                     ++k) {
                    t += ota::kEstagnacaoMs / 2u;
                    s.onChunk(p.imagem + k * 8u, 8u);
                    s.service(t, true);
                    if (s.phase() != ota::Phase::Gravando) {
                        break;
                    }
                }
                TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::TempoEsgotado);
                break;
            case kFalhaDeIo:
                s.onChunk(p.imagem, 1024u);
                TEST_ASSERT_FALSE(s.onChunk(p.imagem + 1024u, 4096u));
                break;
            case kImagemRuim:
                s.onChunk(p.imagem, kTamanho - 512u);
                s.onEnd();
                break;
            default:
                s.onAbort();
                break;
        }

        TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
        TEST_ASSERT_FALSE(flash.aberta());
        TEST_ASSERT_TRUE(flash.aborts() >= 1u);
        TEST_ASSERT_FALSE(s.saidasEmAlarme());
    }
}

// E depois de a mensagem sair da tela, outra tentativa tem de funcionar inteira. Sem isto, um
// upload que falhou exige ir ate o equipamento.
static void test_depois_de_falhar_a_proxima_tentativa_funciona_inteira(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);

    ateGravando(s, p, kT0);
    s.cancelar(kT0 + 10u);
    s.service(kT0 + 10u + ota::kMensagemMs, false);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Ocioso);

    const uint32_t t2 = kT0 + 100000u;
    ateGravando(s, p, t2);
    TEST_ASSERT_TRUE(s.onChunk(p.imagem, kTamanho));
    s.onEnd();
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Concluido);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p.imagem, flash.gravado(), kTamanho);
}

// Byte a mais nao pode chegar a flash: e exatamente o byte que ninguem conferiu.
static void test_byte_excedente_nao_chega_a_flash(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);

    TEST_ASSERT_TRUE(s.onChunk(p.imagem, kTamanho));
    const uint32_t antes = flash.escritos();
    const uint8_t sobra[4] = {0, 0, 0, 0};
    TEST_ASSERT_FALSE(s.onChunk(sobra, sizeof(sobra)));
    TEST_ASSERT_EQUAL_UINT32(antes, flash.escritos());
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
}

// Se ate o begin() da flash falhar, a sessao morre limpa e as saidas voltam.
static void test_begin_da_flash_que_falha_mata_a_sessao_e_libera_as_saidas(void) {
    FakeFirmwareStore flash;
    flash.falharBegin(true);
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);

    s.service(kT0, false);
    s.onHeader(p.cabecalho, ota::kHeaderBytes);
    s.confirmar(kT0);
    s.service(kT0 + 10u, true);

    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::FalhaDeGravacao);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
    TEST_ASSERT_FALSE(flash.aberta());
}

// A sensora nao tem painel: aceita e ja abre, assim que main.cpp aplicar as saidas.
static void test_na_sensora_nao_ha_confirmacao_a_dar(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSensora, false);
    Pacote p;
    montaPacote(p, ota::kAlvoSensora);

    s.service(kT0, false);
    TEST_ASSERT_TRUE(s.onHeader(p.cabecalho, ota::kHeaderBytes));
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Preparando);
    s.service(kT0 + 10u, true);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Gravando);
}

// O texto que sai na tela e na pagina tem de existir para TODA fase e TODO motivo: um "?" no
// painel de um equipamento parado nao diz a ninguem o que fazer.
static void test_toda_fase_e_todo_motivo_tem_texto(void) {
    const ota::Phase kFases[] = {ota::Phase::Ocioso,      ota::Phase::Aguardando,
                                 ota::Phase::Preparando,  ota::Phase::Gravando,
                                 ota::Phase::Verificando, ota::Phase::Concluido,
                                 ota::Phase::Recusado,    ota::Phase::Falhou};
    for (size_t i = 0; i < sizeof(kFases) / sizeof(kFases[0]); ++i) {
        const char* t = app::textoDaFase(kFases[i]);
        TEST_ASSERT_TRUE(strlen(t) > 0);
        TEST_ASSERT_TRUE(strcmp(t, "?") != 0);
    }
    const ota::HeaderVerdict kRecusas[] = {
        ota::HeaderVerdict::Curto,          ota::HeaderVerdict::MagicaInvalida,
        ota::HeaderVerdict::CabecalhoCorrompido, ota::HeaderVerdict::VersaoDesconhecida,
        ota::HeaderVerdict::AlvoErrado,     ota::HeaderVerdict::TamanhoInvalido,
        ota::HeaderVerdict::ReservadoNaoZero};
    for (size_t i = 0; i < sizeof(kRecusas) / sizeof(kRecusas[0]); ++i) {
        const char* t = app::textoDaRecusa(kRecusas[i]);
        TEST_ASSERT_TRUE(strlen(t) > 0);
        TEST_ASSERT_TRUE(strcmp(t, "?") != 0);
    }
    const ota::FailReason kMotivos[] = {
        ota::FailReason::NaoConfirmado, ota::FailReason::Estagnou,
        ota::FailReason::TempoEsgotado, ota::FailReason::ImagemInvalida,
        ota::FailReason::FalhaDeGravacao, ota::FailReason::FalhaDeTroca,
        ota::FailReason::Cancelado};
    for (size_t i = 0; i < sizeof(kMotivos) / sizeof(kMotivos[0]); ++i) {
        const char* t = app::textoDaFalha(kMotivos[i]);
        TEST_ASSERT_TRUE(strlen(t) > 0);
        TEST_ASSERT_TRUE(strcmp(t, "?") != 0);
    }
}

// Um pedaco que chega ATRASADO, depois de a sessao ja ter morrido por prazo, nao pode nem BATER
// na porta da flash. Devolver erro depois de tentar nao e a mesma coisa que nao tentar: o handle
// da IDF ja foi abortado, e escrever num handle abortado e comportamento que ninguem garantiu.
static void test_pedaco_atrasado_nem_tenta_escrever_na_flash(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);
    TEST_ASSERT_TRUE(s.onChunk(p.imagem, 1024u));
    const uint32_t tentativasAntes = flash.tentativasDeEscrita();

    s.service(kT0 + ota::kEstagnacaoMs + 1u, true);  // morreu por estagnacao
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);

    TEST_ASSERT_FALSE(s.onChunk(p.imagem + 1024u, 1024u));
    TEST_ASSERT_EQUAL_UINT32(tentativasAntes, flash.tentativasDeEscrita());
}

// E um pedaco que chega ANTES da confirmacao tambem nao.
static void test_pedaco_antes_da_confirmacao_nem_tenta_escrever(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    s.service(kT0, false);
    s.onHeader(p.cabecalho, ota::kHeaderBytes);

    TEST_ASSERT_FALSE(s.onChunk(p.imagem, 1024u));
    TEST_ASSERT_EQUAL_UINT32(0, flash.tentativasDeEscrita());
}

// Um segundo pacote no meio de uma gravacao tem de ser RECUSADO na cara do portal - devolver
// "aceito" enquanto nada acontece faz a pagina mandar a imagem nova por cima da que esta indo.
static void test_segundo_pacote_no_meio_e_recusado_para_o_portal(void) {
    FakeFirmwareStore flash;
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);
    s.onChunk(p.imagem, 1024u);

    TEST_ASSERT_FALSE(s.onHeader(p.cabecalho, ota::kHeaderBytes));
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Gravando);
    TEST_ASSERT_EQUAL_UINT32(1, flash.begins());

    PortalStatus st;
    s.preencherStatusDoPortal(st);
    TEST_ASSERT_FALSE(st.aceitandoPacote);
}

// A TROCA DE PARTICAO E O UNICO PASSO IRREVERSIVEL. Se ela falhar, a placa continua na imagem
// antiga: dizer "concluido" e reiniciar faria a placa subir no firmware velho mostrando que
// atualizou - e a proxima tentativa seria feita achando que a anterior deu certo.
static void test_activate_que_falha_nao_vira_concluido(void) {
    FakeFirmwareStore flash;
    flash.falharActivate(true);
    app::OtaService s(flash, ota::kAlvoSupervisora, true);
    Pacote p;
    montaPacote(p, ota::kAlvoSupervisora);
    ateGravando(s, p, kT0);
    TEST_ASSERT_TRUE(s.onChunk(p.imagem, kTamanho));
    s.onEnd();

    TEST_ASSERT_EQUAL_UINT32(1, flash.activates());
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::FalhaDeTroca);
    TEST_ASSERT_FALSE(s.reinicioPendente());
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_caminho_feliz_grava_a_imagem_certa_e_troca_a_particao);
    RUN_TEST(test_nada_toca_a_flash_antes_da_confirmacao);
    RUN_TEST(test_a_flash_so_abre_depois_de_as_saidas_irem_para_alarme);
    RUN_TEST(test_imagem_corrompida_nao_troca_a_particao);
    RUN_TEST(test_imagem_truncada_nao_troca_a_particao);
    RUN_TEST(test_finish_reprovado_pela_idf_nao_troca_a_particao);
    RUN_TEST(test_pacote_da_outra_placa_nem_abre_a_flash);
    RUN_TEST(test_o_tamanho_e_conferido_contra_a_particao_desta_placa);
    RUN_TEST(test_a_flash_nunca_fica_aberta_depois_de_a_sessao_morrer);
    RUN_TEST(test_depois_de_falhar_a_proxima_tentativa_funciona_inteira);
    RUN_TEST(test_byte_excedente_nao_chega_a_flash);
    RUN_TEST(test_begin_da_flash_que_falha_mata_a_sessao_e_libera_as_saidas);
    RUN_TEST(test_na_sensora_nao_ha_confirmacao_a_dar);
    RUN_TEST(test_pedaco_atrasado_nem_tenta_escrever_na_flash);
    RUN_TEST(test_pedaco_antes_da_confirmacao_nem_tenta_escrever);
    RUN_TEST(test_segundo_pacote_no_meio_e_recusado_para_o_portal);
    RUN_TEST(test_activate_que_falha_nao_vira_concluido);
    RUN_TEST(test_toda_fase_e_todo_motivo_tem_texto);
    return UNITY_END();
}
