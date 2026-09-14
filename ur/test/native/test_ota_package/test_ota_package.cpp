// O PORTAO DO PACOTE: o que precisa ser verdade antes que o primeiro byte chegue a flash.
//
// O erro que este arquivo existe para impedir nao e "arquivo corrompido" - e a imagem da OUTRA
// placa. As duas sobem o mesmo ponto de acesso, com a mesma pagina, e quem atualiza esta no patio
// com o celular na mao. Uma imagem da supervisora gravada na sensora e uma imagem valida: sobe,
// nao tem SCL3300, nao tem RS-485 escravo, e nao tem como ser atualizada de novo.
//
// Os vetores de CRC-32 e SHA-256 sao os publicados (IEEE 802.3 / FIPS 180-4). Eles prendem o
// VALOR, nao a implementacao: trocar a tabela de nibble pela de 256 entradas tem de continuar
// passando, e uma rotacao errada em SHA-256 tem de quebrar.
#include <string.h>
#include <unity.h>

#include "ota_package.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint32_t kParticaoBytes = 1310720u;  // app0/app1 de particoes_sui_4mb.csv

void hex(const char* texto, uint8_t saida[ota::kSha256Bytes]) {
    for (size_t i = 0; i < ota::kSha256Bytes; ++i) {
        auto nib = [](char c) -> uint8_t {
            return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        };
        saida[i] = static_cast<uint8_t>((nib(texto[2 * i]) << 4) | nib(texto[2 * i + 1]));
    }
}

void sha(const uint8_t* dados, size_t n, uint8_t saida[ota::kSha256Bytes]) {
    ota::Sha256 s;
    s.update(dados, n);
    s.finish(saida);
}

// Imagem de mentira, mas com tamanho de imagem de verdade e conteudo nao repetitivo.
struct Imagem {
    uint8_t* bytes;
    uint32_t n;
    Imagem(uint32_t tam) : bytes(new uint8_t[tam]), n(tam) {
        uint32_t x = 0x12345678u;
        for (uint32_t i = 0; i < tam; ++i) {
            x = x * 1664525u + 1013904223u;
            bytes[i] = static_cast<uint8_t>(x >> 24);
        }
    }
    ~Imagem() { delete[] bytes; }
};

void cabecalhoDe(const Imagem& img, uint16_t alvo, ota::PackageHeader& h) {
    h.versaoCabecalho = ota::kHeaderVersion;
    h.alvo = alvo;
    h.tamanhoImagem = img.n;
    h.crc32Imagem = ota::crc32(img.bytes, img.n);
    sha(img.bytes, img.n, h.sha256Imagem);
    h.versaoMaior = 0;
    h.versaoMenor = 2;
    h.versaoCorrecao = 0;
}

}  // namespace

// ---------------------------------------------------------------- CRC-32

static void test_crc32_bate_com_os_vetores_publicados(void) {
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, ota::crc32(nullptr, 0));
    TEST_ASSERT_EQUAL_HEX32(0xE8B7BE43u, ota::crc32(reinterpret_cast<const uint8_t*>("a"), 1));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u,
                            ota::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9));
}

// Em fluxo o arquivo chega picado pelo transporte; picar nao pode mudar o resultado.
static void test_crc32_em_pedacos_e_igual_ao_de_uma_vez(void) {
    const uint8_t* d = reinterpret_cast<const uint8_t*>("123456789");
    ota::Crc32 c;
    c.update(d, 1);
    c.update(d + 1, 5);
    c.update(d + 6, 0);
    c.update(d + 6, 3);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c.digest());
}

// ---------------------------------------------------------------- SHA-256

static void test_sha256_bate_com_os_vetores_do_fips_180_4(void) {
    uint8_t obtido[ota::kSha256Bytes];
    uint8_t esperado[ota::kSha256Bytes];

    sha(nullptr, 0, obtido);
    hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", esperado);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);

    sha(reinterpret_cast<const uint8_t*>("abc"), 3, obtido);
    hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", esperado);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);

    const char* longo = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 56 bytes
    sha(reinterpret_cast<const uint8_t*>(longo), strlen(longo), obtido);
    hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", esperado);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);
}

// O PREENCHIMENTO e onde uma implementacao de SHA-256 erra: 55 bytes cabem no bloco, 56 obrigam
// um bloco extra, 64 sao um bloco cheio. Uma imagem de 1280 KiB atravessa os tres casos.
static void test_sha256_acerta_as_fronteiras_de_bloco(void) {
    uint8_t obtido[ota::kSha256Bytes];
    uint8_t esperado[ota::kSha256Bytes];
    uint8_t buf[64];
    for (size_t i = 0; i < sizeof(buf); ++i) {
        buf[i] = 'a';
    }

    sha(buf, 55, obtido);  // 55 x 'a'
    hex("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318", esperado);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);

    sha(buf, 56, obtido);  // 56 x 'a'
    hex("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a", esperado);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);

    sha(buf, 64, obtido);  // 64 x 'a'
    hex("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", esperado);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);
}

static void test_sha256_em_pedacos_e_igual_ao_de_uma_vez(void) {
    const char* longo = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t esperado[ota::kSha256Bytes];
    hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", esperado);

    ota::Sha256 s;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(longo);
    s.update(d, 1);
    s.update(d + 1, 0);
    s.update(d + 1, 54);
    s.update(d + 55, 1);
    uint8_t obtido[ota::kSha256Bytes];
    s.finish(obtido);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, obtido, ota::kSha256Bytes);
}

// ---------------------------------------------------------------- cabecalho

static void test_o_que_foi_escrito_e_o_que_e_lido(void) {
    Imagem img(ota::kMinImageBytes * 3u);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSupervisora, h);

    uint8_t bruto[ota::kHeaderBytes];
    ota::writeHeader(h, bruto);

    ota::PackageHeader lido;
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::Ok);
    TEST_ASSERT_EQUAL_UINT16(ota::kHeaderVersion, lido.versaoCabecalho);
    TEST_ASSERT_EQUAL_UINT16(ota::kAlvoSupervisora, lido.alvo);
    TEST_ASSERT_EQUAL_UINT32(img.n, lido.tamanhoImagem);
    TEST_ASSERT_EQUAL_HEX32(h.crc32Imagem, lido.crc32Imagem);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(h.sha256Imagem, lido.sha256Imagem, ota::kSha256Bytes);
    TEST_ASSERT_EQUAL_UINT8(0, lido.versaoMaior);
    TEST_ASSERT_EQUAL_UINT8(2, lido.versaoMenor);
    TEST_ASSERT_EQUAL_UINT8(0, lido.versaoCorrecao);
}

// O ERRO QUE ESTE MODULO EXISTE PARA IMPEDIR. Pacote perfeito, so que da outra placa.
static void test_pacote_da_outra_placa_e_recusado_nas_duas_direcoes(void) {
    Imagem img(ota::kMinImageBytes * 2u);
    uint8_t bruto[ota::kHeaderBytes];
    ota::PackageHeader h;
    ota::PackageHeader lido;

    cabecalhoDe(img, ota::kAlvoSupervisora, h);
    ota::writeHeader(h, bruto);
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSensora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::AlvoErrado);

    cabecalhoDe(img, ota::kAlvoSensora, h);
    ota::writeHeader(h, bruto);
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::AlvoErrado);
    // E o alvo certo continua passando, senao "recusa tudo" passaria neste teste.
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSensora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::Ok);
}

static void test_arquivo_que_nao_e_pacote_para_na_magica(void) {
    uint8_t naoPacote[ota::kHeaderBytes];
    for (size_t i = 0; i < sizeof(naoPacote); ++i) {
        naoPacote[i] = static_cast<uint8_t>('A' + (i % 26));
    }
    ota::PackageHeader lido;
    TEST_ASSERT_TRUE(ota::parseHeader(naoPacote, sizeof(naoPacote), ota::kAlvoSupervisora,
                                      kParticaoBytes, lido) == ota::HeaderVerdict::MagicaInvalida);
}

static void test_arquivo_curto_demais_nao_e_lido_fora_do_buffer(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSupervisora, h);
    uint8_t bruto[ota::kHeaderBytes];
    ota::writeHeader(h, bruto);

    ota::PackageHeader lido;
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, ota::kHeaderBytes - 1u, ota::kAlvoSupervisora,
                                      kParticaoBytes, lido) == ota::HeaderVerdict::Curto);
    TEST_ASSERT_TRUE(ota::parseHeader(nullptr, ota::kHeaderBytes, ota::kAlvoSupervisora,
                                      kParticaoBytes, lido) == ota::HeaderVerdict::Curto);
}

// UM BIT TROCADO EM QUALQUER CAMPO tem de virar "corrompido", e nao um alvo errado lido como
// certo. Por isso o CRC do cabecalho e conferido ANTES de qualquer campo.
static void test_um_bit_trocado_em_qualquer_lugar_do_cabecalho_reprova(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSensora, h);
    uint8_t original[ota::kHeaderBytes];
    ota::writeHeader(h, original);

    ota::PackageHeader lido;
    for (size_t byte = ota::kMagicBytes; byte < ota::kHeaderBytes; ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t bruto[ota::kHeaderBytes];
            memcpy(bruto, original, sizeof(bruto));
            bruto[byte] ^= static_cast<uint8_t>(1u << bit);
            const ota::HeaderVerdict v = ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSensora,
                                                          kParticaoBytes, lido);
            TEST_ASSERT_TRUE(v == ota::HeaderVerdict::CabecalhoCorrompido);
        }
    }
}

static void test_formato_mais_novo_e_recusado_em_vez_de_interpretado(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSupervisora, h);
    h.versaoCabecalho = ota::kHeaderVersion + 1u;
    uint8_t bruto[ota::kHeaderBytes];
    ota::writeHeader(h, bruto);  // CRC recalculado: e um pacote v2 valido, nao um corrompido

    ota::PackageHeader lido;
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::VersaoDesconhecida);
}

// A imagem tem de CABER na particao ociosa. Sem esta conferencia, esp_ota_write() falharia no
// meio, com a particao ja apagada - e o proximo boot cairia no rollback sem motivo aparente.
static void test_tamanho_fora_da_particao_e_recusado_antes_de_apagar_nada(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSupervisora, h);
    ota::PackageHeader lido;
    uint8_t bruto[ota::kHeaderBytes];

    h.tamanhoImagem = kParticaoBytes + 1u;
    ota::writeHeader(h, bruto);
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::TamanhoInvalido);

    h.tamanhoImagem = kParticaoBytes;  // exatamente cheia ainda cabe
    ota::writeHeader(h, bruto);
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::Ok);

    h.tamanhoImagem = 0;
    ota::writeHeader(h, bruto);
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::TamanhoInvalido);

    h.tamanhoImagem = ota::kMinImageBytes - 1u;
    ota::writeHeader(h, bruto);
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::TamanhoInvalido);
}

static void test_reservado_diferente_de_zero_e_recusado(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSupervisora, h);
    ota::PackageHeader lido;
    uint8_t bruto[ota::kHeaderBytes];

    // Escrito a mao: writeHeader() sempre zera os reservados, entao o pacote so pode vir de uma
    // versao futura que use esses campos - e este firmware nao sabe o que eles significam.
    ota::writeHeader(h, bruto);
    bruto[55] = 0x01u;
    ota::detail::putLe32(bruto + 60, ota::crc32(bruto, ota::kHeaderBytes - 4u));
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::ReservadoNaoZero);

    ota::writeHeader(h, bruto);
    bruto[58] = 0x80u;
    ota::detail::putLe32(bruto + 60, ota::crc32(bruto, ota::kHeaderBytes - 4u));
    TEST_ASSERT_TRUE(ota::parseHeader(bruto, sizeof(bruto), ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::ReservadoNaoZero);
}

// ---------------------------------------------------------------- imagem em fluxo

static void test_imagem_integra_passa_mesmo_picada_em_pedacos_irregulares(void) {
    Imagem img(ota::kMinImageBytes * 7u + 123u);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSensora, h);

    ota::ImageVerifier v;
    v.begin(h);
    TEST_ASSERT_EQUAL_UINT16(0, v.progressoPorMil());

    uint32_t pos = 0;
    uint32_t pedaco = 1;
    while (pos < img.n) {
        uint32_t n = pedaco;
        if (n > img.n - pos) {
            n = img.n - pos;
        }
        TEST_ASSERT_TRUE(v.feed(img.bytes + pos, n));
        pos += n;
        pedaco = (pedaco * 3u) % 1021u + 1u;
    }
    TEST_ASSERT_EQUAL_UINT16(1000, v.progressoPorMil());
    TEST_ASSERT_EQUAL_UINT32(img.n, v.recebidos());
    TEST_ASSERT_TRUE(v.finish() == ota::ImageVerdict::Ok);
}

// TRUNCADA e o caso comum de verdade: rede que cai, celular que bloqueia a tela. Nao pode virar
// "Ok" nem "corrompida" - e incompleta, e a mensagem para quem esta no patio e outra.
static void test_imagem_truncada_fica_incompleta_e_nao_ok(void) {
    Imagem img(ota::kMinImageBytes * 2u);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSensora, h);

    ota::ImageVerifier v;
    v.begin(h);
    TEST_ASSERT_TRUE(v.feed(img.bytes, img.n - 1u));
    TEST_ASSERT_TRUE(v.finish() == ota::ImageVerdict::Incompleta);
    TEST_ASSERT_TRUE(v.progressoPorMil() < 1000);
}

// EXCESSO e detectado NA HORA: aceitar e so reclamar no fim seria gravar bytes que ninguem
// conferiu.
static void test_byte_a_mais_e_recusado_na_hora(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSensora, h);

    ota::ImageVerifier v;
    v.begin(h);
    TEST_ASSERT_TRUE(v.feed(img.bytes, img.n));
    const uint8_t sobra = 0x5Au;
    TEST_ASSERT_FALSE(v.feed(&sobra, 1));
    TEST_ASSERT_TRUE(v.finish() == ota::ImageVerdict::TamanhoErrado);
}

// UM BIT TROCADO no meio da imagem. O CRC-32 pega antes do SHA-256 (e mais barato), mas o que
// importa e que NAO passe.
static void test_um_bit_trocado_no_meio_da_imagem_reprova(void) {
    Imagem img(ota::kMinImageBytes * 2u);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSensora, h);

    img.bytes[img.n / 2u] ^= 0x01u;
    ota::ImageVerifier v;
    v.begin(h);
    TEST_ASSERT_TRUE(v.feed(img.bytes, img.n));
    TEST_ASSERT_TRUE(v.finish() == ota::ImageVerdict::Crc32Errado);
}

// E o SHA-256 tem de ser conferido de verdade, e nao so guardado: uma imagem com o CRC-32 certo e
// o resumo errado e exatamente o que um CRC nao distingue.
static void test_sha256_errado_reprova_mesmo_com_crc32_certo(void) {
    Imagem img(ota::kMinImageBytes);
    ota::PackageHeader h;
    cabecalhoDe(img, ota::kAlvoSensora, h);
    h.sha256Imagem[31] ^= 0x01u;

    ota::ImageVerifier v;
    v.begin(h);
    TEST_ASSERT_TRUE(v.feed(img.bytes, img.n));
    TEST_ASSERT_TRUE(v.finish() == ota::ImageVerdict::Sha256Errado);
}

// ---------------------------------------------------------------- vetor dourado

// O EMPACOTADOR E O FIRMWARE NAO PODEM DIVERGIR. scripts/empacota_ota.py monta o cabecalho em
// Python; ota_package.h le em C++. Sao duas implementacoes do mesmo layout, e nada alem deste
// teste impede que uma mude sem a outra - um campo deslocado por dois bytes produz um pacote que
// o empacotador jura bom e a placa recusa, ou pior, aceita lendo o alvo errado.
//
// Os bytes abaixo sairam de scripts/empacota_ota.py sobre a imagem construida em imagemDourada():
// 4096 bytes, primeiro byte 0xE9 (magica de aplicacao do ESP32), o resto um congruente linear com
// semente 0x12345678. Para regerar, ver o cabecalho do proprio script.
static void test_pacote_dourado_gerado_pelo_empacotador(void) {
    static const uint8_t kCabecalhoDourado[ota::kHeaderBytes] = {
        0x44, 0x45, 0x50, 0x55, 0x52, 0x49, 0x4F, 0x54, 0x01, 0x00, 0x02, 0x00, 0x00, 0x10, 0x00,
        0x00, 0xEF, 0x9C, 0x59, 0x47, 0x9D, 0xC0, 0x11, 0x3F, 0x58, 0x8A, 0xF8, 0x16, 0xEA, 0xE6,
        0x86, 0xA4, 0xC4, 0xF6, 0x0A, 0xA1, 0xA9, 0x05, 0x02, 0xC9, 0x23, 0x18, 0x98, 0x19, 0xB1,
        0xA7, 0x19, 0x79, 0x51, 0x38, 0x69, 0x9C, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x19, 0x28, 0x0F, 0x93};

    uint8_t imagem[4096];
    imagem[0] = 0xE9u;  // magica de imagem de aplicacao do ESP32
    uint32_t x = 0x12345678u;
    for (size_t i = 1; i < sizeof(imagem); ++i) {
        x = x * 1664525u + 1013904223u;
        imagem[i] = static_cast<uint8_t>(x >> 24);
    }

    ota::PackageHeader lido;
    TEST_ASSERT_TRUE(ota::parseHeader(kCabecalhoDourado, sizeof(kCabecalhoDourado),
                                      ota::kAlvoSensora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::Ok);
    TEST_ASSERT_EQUAL_UINT16(ota::kAlvoSensora, lido.alvo);
    TEST_ASSERT_EQUAL_UINT32(sizeof(imagem), lido.tamanhoImagem);
    TEST_ASSERT_EQUAL_UINT8(0, lido.versaoMaior);
    TEST_ASSERT_EQUAL_UINT8(2, lido.versaoMenor);
    TEST_ASSERT_EQUAL_UINT8(0, lido.versaoCorrecao);

    // E o CRC-32 e o SHA-256 do Python tem de bater com os daqui sobre os MESMOS bytes.
    ota::ImageVerifier v;
    v.begin(lido);
    TEST_ASSERT_TRUE(v.feed(imagem, sizeof(imagem)));
    TEST_ASSERT_TRUE(v.finish() == ota::ImageVerdict::Ok);

    // E o mesmo cabecalho tem de ser recusado pela supervisora: o dourado tambem prende o alvo.
    TEST_ASSERT_TRUE(ota::parseHeader(kCabecalhoDourado, sizeof(kCabecalhoDourado),
                                      ota::kAlvoSupervisora, kParticaoBytes,
                                      lido) == ota::HeaderVerdict::AlvoErrado);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_bate_com_os_vetores_publicados);
    RUN_TEST(test_crc32_em_pedacos_e_igual_ao_de_uma_vez);
    RUN_TEST(test_sha256_bate_com_os_vetores_do_fips_180_4);
    RUN_TEST(test_sha256_acerta_as_fronteiras_de_bloco);
    RUN_TEST(test_sha256_em_pedacos_e_igual_ao_de_uma_vez);
    RUN_TEST(test_o_que_foi_escrito_e_o_que_e_lido);
    RUN_TEST(test_pacote_da_outra_placa_e_recusado_nas_duas_direcoes);
    RUN_TEST(test_arquivo_que_nao_e_pacote_para_na_magica);
    RUN_TEST(test_arquivo_curto_demais_nao_e_lido_fora_do_buffer);
    RUN_TEST(test_um_bit_trocado_em_qualquer_lugar_do_cabecalho_reprova);
    RUN_TEST(test_formato_mais_novo_e_recusado_em_vez_de_interpretado);
    RUN_TEST(test_tamanho_fora_da_particao_e_recusado_antes_de_apagar_nada);
    RUN_TEST(test_reservado_diferente_de_zero_e_recusado);
    RUN_TEST(test_imagem_integra_passa_mesmo_picada_em_pedacos_irregulares);
    RUN_TEST(test_imagem_truncada_fica_incompleta_e_nao_ok);
    RUN_TEST(test_byte_a_mais_e_recusado_na_hora);
    RUN_TEST(test_um_bit_trocado_no_meio_da_imagem_reprova);
    RUN_TEST(test_sha256_errado_reprova_mesmo_com_crc32_certo);
    RUN_TEST(test_pacote_dourado_gerado_pelo_empacotador);
    return UNITY_END();
}
