// AS TRES REGRAS DA SESSAO DE ATUALIZACAO, e o modo de falha de campo que a terceira impede.
//
// 1. nenhum byte na flash antes da confirmacao no painel
// 2. saidas em alarme ANTES da primeira escrita, nao durante
// 3. saidas NAO ficam em alarme para sempre quando alguem desiste no meio
//
// A terceira e a que mais importa em campo: o operador confirma, comeca a subir, o celular
// bloqueia a tela, ele vai almocar. Sem relogio de estagnacao a maquina fica travada em alarme
// sem ninguem entender por que.
#include <unity.h>

#include "ota_session.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint32_t kT0 = 0xFFFFF000u;  // perto do wrap de 2^32, de proposito
constexpr uint32_t kTamanho = 400000u;

ota::PackageHeader pacote(uint16_t alvo) {
    ota::PackageHeader h = {};
    h.versaoCabecalho = ota::kHeaderVersion;
    h.alvo = alvo;
    h.tamanhoImagem = kTamanho;
    h.crc32Imagem = 0xDEADBEEFu;
    h.versaoMaior = 0;
    h.versaoMenor = 2;
    h.versaoCorrecao = 0;
    return h;
}

// Leva uma sessao da supervisora ate Gravando pelo caminho legitimo.
void ateGravando(ota::Session& s, uint32_t t) {
    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), t);
    s.confirmar(t);
    s.prontoParaGravar(t);
}

}  // namespace

// --------------------------------------------------- regra 1

static void test_nada_e_gravado_antes_da_confirmacao_no_painel(void) {
    ota::Session s(true);
    TEST_ASSERT_FALSE(s.aceitaBytes());

    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), kT0);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
    TEST_ASSERT_FALSE(s.aceitaBytes());

    // Tentar pular a confirmacao nao pode funcionar.
    s.prontoParaGravar(kT0 + 1u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
    TEST_ASSERT_FALSE(s.aceitaBytes());

    s.confirmar(kT0 + 2u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Preparando);
    TEST_ASSERT_FALSE(s.aceitaBytes());  // ainda nao: as saidas estao indo para alarme

    s.prontoParaGravar(kT0 + 3u);
    TEST_ASSERT_TRUE(s.aceitaBytes());
}

// A sensora nao tem painel nem teclado. O alarme sai do mesmo jeito porque o enlace cai e a
// supervisora declara falha por conta propria (decisao A5).
static void test_a_sensora_nao_espera_confirmacao_que_nao_tem_onde_dar(void) {
    ota::Session s(false);
    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSensora), kT0);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Preparando);
    s.prontoParaGravar(kT0 + 1u);
    TEST_ASSERT_TRUE(s.aceitaBytes());
}

// --------------------------------------------------- regra 2

static void test_as_saidas_vao_para_alarme_antes_da_primeira_escrita(void) {
    ota::Session s(true);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());

    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), kT0);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());  // so o aviso na tela; nada mudou nas saidas

    s.confirmar(kT0 + 1u);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());   // Preparando: ANTES de aceitar byte
    TEST_ASSERT_FALSE(s.aceitaBytes());

    s.prontoParaGravar(kT0 + 2u);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());
    s.noteImagemCompleta(ota::ImageVerdict::Ok, kT0 + 3u);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());   // Verificando
    s.noteTrocaDeParticao(true, kT0 + 4u);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());   // Concluido: so o reinicio tira
}

// E voltam ao normal em toda saida que NAO e o reinicio.
static void test_as_saidas_voltam_ao_normal_quando_a_sessao_morre(void) {
    const ota::FailReason kMotivos[] = {ota::FailReason::Cancelado, ota::FailReason::Estagnou,
                                        ota::FailReason::ImagemInvalida,
                                        ota::FailReason::FalhaDeGravacao};
    for (size_t i = 0; i < sizeof(kMotivos) / sizeof(kMotivos[0]); ++i) {
        ota::Session s(true);
        ateGravando(s, kT0);
        TEST_ASSERT_TRUE(s.saidasEmAlarme());

        switch (kMotivos[i]) {
            case ota::FailReason::Cancelado: s.cancelar(kT0 + 10u); break;
            case ota::FailReason::Estagnou: s.tick(kT0 + ota::kEstagnacaoMs); break;
            case ota::FailReason::ImagemInvalida:
                s.noteImagemCompleta(ota::ImageVerdict::Crc32Errado, kT0 + 10u); break;
            default: s.noteFalhaDeGravacao(kT0 + 10u); break;
        }
        TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
        TEST_ASSERT_TRUE(s.failReason() == kMotivos[i]);
        TEST_ASSERT_FALSE(s.saidasEmAlarme());
        TEST_ASSERT_FALSE(s.aceitaBytes());
    }
}

// --------------------------------------------------- regra 3

// O MODO DE FALHA DE CAMPO. Celular bloqueia a tela, operador vai almocar, a maquina fica em
// alarme. O relogio de estagnacao existe so para isto.
static void test_upload_que_para_no_meio_libera_as_saidas_sozinho(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    s.noteGravados(kTamanho / 2u, kT0 + 5000u);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());

    // Um segundo antes do prazo ainda esta gravando: nao pode abortar um upload lento.
    s.tick(kT0 + 5000u + ota::kEstagnacaoMs - 1u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Gravando);

    s.tick(kT0 + 5000u + ota::kEstagnacaoMs);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::Estagnou);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
}

// Progresso RENOVA o relogio: um upload lento porem vivo nao pode ser morto.
static void test_upload_lento_mas_vivo_nao_e_abortado(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    uint32_t t = kT0;
    for (uint32_t bytes = 1000u; bytes < kTamanho; bytes += 1000u) {
        t += ota::kEstagnacaoMs - 1000u;  // quase estagnando, mas sempre chega algo
        s.noteGravados(bytes, t);
        s.tick(t);
        if (s.phase() != ota::Phase::Gravando) {
            break;
        }
    }
    // Nao morreu por estagnacao - morreu (ou nao) pelo TETO, que e o outro relogio.
    TEST_ASSERT_TRUE(s.failReason() != ota::FailReason::Estagnou);
}

// ...e por isso existe o teto absoluto: um fluxo lento porem continuo nunca estagna.
static void test_o_teto_absoluto_mata_o_upload_eterno(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    uint32_t t = kT0;
    for (uint32_t bytes = 1u; bytes < kTamanho; ++bytes) {
        t += 1000u;
        s.noteGravados(bytes, t);
        s.tick(t);
        if (s.phase() != ota::Phase::Gravando) {
            break;
        }
    }
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::TempoEsgotado);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
    TEST_ASSERT_TRUE(ota::kGravacaoTetoMs > ota::kEstagnacaoMs);
}

// Confirmado o aviso e ninguem sobe nada: o equipamento nao pode ficar esperando para sempre.
static void test_ninguem_confirma_e_a_sessao_expira(void) {
    ota::Session s(true);
    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), kT0);
    s.tick(kT0 + ota::kConfirmacaoTimeoutMs - 1u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
    s.tick(kT0 + ota::kConfirmacaoTimeoutMs);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::NaoConfirmado);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
}

// --------------------------------------------------- pacote recusado

// Recusa NAO mexe nas saidas: nada comecou. E a mensagem sai da tela sozinha, senao o painel fica
// preso num erro de dez minutos atras.
static void test_pacote_recusado_nao_toca_nas_saidas_e_a_mensagem_expira(void) {
    ota::Session s(true);
    s.offerHeader(ota::HeaderVerdict::AlvoErrado, pacote(ota::kAlvoSensora), kT0);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Recusado);
    TEST_ASSERT_TRUE(s.rejectReason() == ota::HeaderVerdict::AlvoErrado);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
    TEST_ASSERT_FALSE(s.aceitaBytes());

    s.tick(kT0 + ota::kMensagemMs - 1u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Recusado);
    s.tick(kT0 + ota::kMensagemMs);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Ocioso);
    TEST_ASSERT_TRUE(s.rejectReason() == ota::HeaderVerdict::Ok);  // limpou
}

// Um segundo pacote no meio de uma gravacao trocaria o que esta sendo escrito pela metade.
static void test_um_segundo_pacote_no_meio_e_ignorado(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    s.noteGravados(1000u, kT0 + 10u);

    ota::PackageHeader outro = pacote(ota::kAlvoSupervisora);
    outro.tamanhoImagem = 999999u;
    s.offerHeader(ota::HeaderVerdict::Ok, outro, kT0 + 20u);

    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Gravando);
    TEST_ASSERT_EQUAL_UINT32(kTamanho, s.total());
    TEST_ASSERT_EQUAL_UINT32(1000u, s.gravados());
}

// --------------------------------------------------- caminho feliz e prazos

static void test_caminho_feliz_termina_em_concluido(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    TEST_ASSERT_EQUAL_UINT16(0, s.progressoPorMil());
    s.noteGravados(kTamanho / 2u, kT0 + 100u);
    TEST_ASSERT_EQUAL_UINT16(500, s.progressoPorMil());
    s.noteGravados(kTamanho, kT0 + 200u);
    TEST_ASSERT_EQUAL_UINT16(1000, s.progressoPorMil());

    s.noteImagemCompleta(ota::ImageVerdict::Ok, kT0 + 210u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Verificando);
    s.noteTrocaDeParticao(true, kT0 + 220u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Concluido);
    TEST_ASSERT_TRUE(s.saidasEmAlarme());
    TEST_ASSERT_FALSE(s.aceitaBytes());

    // Concluido nao expira: quem sai dele e o reinicio.
    s.tick(kT0 + 220u + ota::kGravacaoTetoMs * 2u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Concluido);
    s.cancelar(kT0 + 230u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Concluido);
}

// A troca de particao e o unico passo irreversivel. Se ela falhar, a placa continua na imagem
// antiga e as saidas voltam - nao adianta reiniciar.
static void test_troca_de_particao_que_falha_volta_tudo_ao_normal(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    s.noteImagemCompleta(ota::ImageVerdict::Ok, kT0 + 10u);
    s.noteTrocaDeParticao(false, kT0 + 20u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::FalhaDeTroca);
    TEST_ASSERT_FALSE(s.saidasEmAlarme());
}

// A supervisora fica energizada meses. Uma sessao que comece perto do wrap de 2^32 nao pode
// expirar na hora nem deixar de expirar nunca.
static void test_os_prazos_atravessam_o_wrap_de_2_elevado_a_32(void) {
    ota::Session s(true);
    const uint32_t t = 0xFFFFFF00u;  // somar qualquer prazo atravessa o wrap
    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), t);

    // O TIQUE LOGO DEPOIS DO MARCO e o que denuncia a comparacao errada, e nao o tique em
    // prazo-1: com "nowMs >= marco + prazo", a soma estoura para 0x0000E960 enquanto nowMs ainda
    // esta em 0xFFFFFF05, a comparacao da verdadeira, e a sessao expira 5 ms depois de comecar.
    // Em prazo-1 os dois lados ja estouraram juntos e o defeito fica invisivel. Foi um mutante
    // sobrevivente que mostrou isto.
    s.tick(t + 5u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
    s.tick(t + ota::kConfirmacaoTimeoutMs / 2u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
    s.tick(t + ota::kConfirmacaoTimeoutMs - 1u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
    s.tick(t + ota::kConfirmacaoTimeoutMs);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Falhou);

    ota::Session g(true);
    ateGravando(g, t);
    g.noteGravados(10u, t);
    g.tick(t + 5u);
    TEST_ASSERT_TRUE(g.phase() == ota::Phase::Gravando);
    g.tick(t + ota::kEstagnacaoMs / 2u);
    TEST_ASSERT_TRUE(g.phase() == ota::Phase::Gravando);
    g.tick(t + ota::kEstagnacaoMs - 1u);
    TEST_ASSERT_TRUE(g.phase() == ota::Phase::Gravando);
    g.tick(t + ota::kEstagnacaoMs);
    TEST_ASSERT_TRUE(g.phase() == ota::Phase::Falhou);
    TEST_ASSERT_TRUE(g.failReason() == ota::FailReason::Estagnou);

    // E a mensagem de erro tambem tem prazo, tambem em cima do wrap.
    const uint32_t tf = 0xFFFFFF00u + ota::kEstagnacaoMs;  // ja do outro lado
    ota::Session r(true);
    r.offerHeader(ota::HeaderVerdict::AlvoErrado, pacote(ota::kAlvoSensora), 0xFFFFFFF0u);
    r.tick(0xFFFFFFF0u + 5u);
    TEST_ASSERT_TRUE(r.phase() == ota::Phase::Recusado);
    r.tick(0xFFFFFFF0u + ota::kMensagemMs);
    TEST_ASSERT_TRUE(r.phase() == ota::Phase::Ocioso);
    (void)tf;
}

// Depois de a mensagem de erro sair da tela, o equipamento tem de aceitar outra tentativa - senao
// um upload que falhou exige ir ate la desligar a energia.
static void test_depois_de_falhar_aceita_outra_tentativa(void) {
    ota::Session s(true);
    ateGravando(s, kT0);
    s.cancelar(kT0 + 10u);
    TEST_ASSERT_FALSE(s.aceitaPacote());
    s.tick(kT0 + 10u + ota::kMensagemMs);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Ocioso);
    TEST_ASSERT_TRUE(s.aceitaPacote());
    TEST_ASSERT_TRUE(s.failReason() == ota::FailReason::Nenhuma);
    TEST_ASSERT_EQUAL_UINT32(0, s.total());

    s.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), kT0 + 99999u);
    TEST_ASSERT_TRUE(s.phase() == ota::Phase::Aguardando);
}

// INVARIANTE GERAL: aceitaBytes() e verdadeiro em UMA fase so. Se alguem acrescentar uma fase e
// esquecer disto, e byte indo para a flash fora de hora.
static void test_aceita_bytes_so_em_gravando(void) {
    struct Caso { ota::Phase fase; ota::Session (*monta)(); };
    ota::Session ocioso(true);
    TEST_ASSERT_FALSE(ocioso.aceitaBytes());

    ota::Session aguardando(true);
    aguardando.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), kT0);
    TEST_ASSERT_FALSE(aguardando.aceitaBytes());

    ota::Session preparando(true);
    preparando.offerHeader(ota::HeaderVerdict::Ok, pacote(ota::kAlvoSupervisora), kT0);
    preparando.confirmar(kT0);
    TEST_ASSERT_FALSE(preparando.aceitaBytes());

    ota::Session gravando(true);
    ateGravando(gravando, kT0);
    TEST_ASSERT_TRUE(gravando.aceitaBytes());

    ota::Session verificando(true);
    ateGravando(verificando, kT0);
    verificando.noteImagemCompleta(ota::ImageVerdict::Ok, kT0);
    TEST_ASSERT_FALSE(verificando.aceitaBytes());

    ota::Session concluido(true);
    ateGravando(concluido, kT0);
    concluido.noteImagemCompleta(ota::ImageVerdict::Ok, kT0);
    concluido.noteTrocaDeParticao(true, kT0);
    TEST_ASSERT_FALSE(concluido.aceitaBytes());

    ota::Session recusado(true);
    recusado.offerHeader(ota::HeaderVerdict::AlvoErrado, pacote(ota::kAlvoSensora), kT0);
    TEST_ASSERT_FALSE(recusado.aceitaBytes());

    ota::Session falhou(true);
    ateGravando(falhou, kT0);
    falhou.cancelar(kT0);
    TEST_ASSERT_FALSE(falhou.aceitaBytes());

    (void)sizeof(Caso);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_nada_e_gravado_antes_da_confirmacao_no_painel);
    RUN_TEST(test_a_sensora_nao_espera_confirmacao_que_nao_tem_onde_dar);
    RUN_TEST(test_as_saidas_vao_para_alarme_antes_da_primeira_escrita);
    RUN_TEST(test_as_saidas_voltam_ao_normal_quando_a_sessao_morre);
    RUN_TEST(test_upload_que_para_no_meio_libera_as_saidas_sozinho);
    RUN_TEST(test_upload_lento_mas_vivo_nao_e_abortado);
    RUN_TEST(test_o_teto_absoluto_mata_o_upload_eterno);
    RUN_TEST(test_ninguem_confirma_e_a_sessao_expira);
    RUN_TEST(test_pacote_recusado_nao_toca_nas_saidas_e_a_mensagem_expira);
    RUN_TEST(test_um_segundo_pacote_no_meio_e_ignorado);
    RUN_TEST(test_caminho_feliz_termina_em_concluido);
    RUN_TEST(test_troca_de_particao_que_falha_volta_tudo_ao_normal);
    RUN_TEST(test_os_prazos_atravessam_o_wrap_de_2_elevado_a_32);
    RUN_TEST(test_depois_de_falhar_aceita_outra_tentativa);
    RUN_TEST(test_aceita_bytes_so_em_gravando);
    return UNITY_END();
}
