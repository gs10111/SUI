#include "ota_service.h"

namespace app {

OtaService::OtaService(IFirmwareStore& store, uint16_t meuAlvo, bool exigeConfirmacao)
    : store_(store),
      sessao_(exigeConfirmacao),
      verificador_(),
      meuAlvo_(meuAlvo),
      agoraMs_(0) {}

void OtaService::cancelar(uint32_t nowMs) {
    sessao_.cancelar(nowMs);
    fecharFlashSeAberta();
}

// A flash aberta e a unica coisa que sobrevive a uma sessao morta: fechar aqui, num lugar so,
// evita o caso em que a sessao caiu por prazo e o handle da IDF ficou pendurado - o begin()
// seguinte devolveria erro e o equipamento nao aceitaria mais atualizacao nenhuma sem desligar.
void OtaService::fecharFlashSeAberta() {
    if (store_.aberta()) {
        store_.abort();
    }
}

void OtaService::service(uint32_t nowMs, bool saidasAplicadas) {
    agoraMs_ = nowMs;
    sessao_.tick(nowMs);

    if (sessao_.phase() == ota::Phase::Preparando && saidasAplicadas) {
        // So agora a flash e aberta: ate aqui nada foi apagado e desistir nao custa nada.
        if (store_.begin(sessao_.total()).ok()) {
            verificador_.begin(sessao_.header());
            sessao_.prontoParaGravar(nowMs);
        } else {
            sessao_.noteFalhaDeGravacao(nowMs);
        }
    }

    // Uma sessao que morreu por prazo (estagnou, teto, ninguem confirmou) nao passa por onAbort():
    // nao ha ninguem do outro lado para avisar. O handle tem de ser fechado aqui.
    if (sessao_.phase() == ota::Phase::Falhou) {
        fecharFlashSeAberta();
    }
}

bool OtaService::onHeader(const uint8_t* bytes, uint32_t n) {
    if (!sessao_.aceitaPacote()) {
        return false;
    }
    ota::PackageHeader h;
    const ota::HeaderVerdict v =
        ota::parseHeader(bytes, n, meuAlvo_, store_.capacidadeBytes(), h);
    sessao_.offerHeader(v, h, agoraMs_);
    return v == ota::HeaderVerdict::Ok;
}

bool OtaService::onChunk(const uint8_t* dados, uint32_t n) {
    if (!sessao_.aceitaBytes()) {
        return false;
    }
    // Confere ANTES de gravar: um pedaco que ultrapassa o tamanho prometido nao pode chegar a
    // flash, porque e exatamente o byte que ninguem conferiu.
    if (!verificador_.feed(dados, n)) {
        sessao_.cancelar(agoraMs_);
        fecharFlashSeAberta();
        return false;
    }
    if (store_.write(dados, n).failed()) {
        sessao_.noteFalhaDeGravacao(agoraMs_);
        fecharFlashSeAberta();
        return false;
    }
    sessao_.noteGravados(verificador_.recebidos(), agoraMs_);
    return true;
}

void OtaService::onEnd() {
    if (sessao_.phase() != ota::Phase::Gravando) {
        return;
    }
    const ota::ImageVerdict v = verificador_.finish();
    sessao_.noteImagemCompleta(v, agoraMs_);
    if (sessao_.phase() != ota::Phase::Verificando) {
        fecharFlashSeAberta();
        return;
    }
    // finish() e onde a propria IDF confere a imagem gravada. So depois dela a particao de boot
    // pode ser trocada - e trocar sem conferir e o que torna uma placa irrecuperavel.
    if (store_.finish().failed()) {
        sessao_.noteTrocaDeParticao(false, agoraMs_);
        fecharFlashSeAberta();
        return;
    }
    sessao_.noteTrocaDeParticao(store_.activate().ok(), agoraMs_);
}

void OtaService::onAbort() {
    sessao_.cancelar(agoraMs_);
    fecharFlashSeAberta();
}

void OtaService::preencherStatusDoPortal(PortalStatus& st) const {
    st.fase = textoDaFase(sessao_.phase());
    st.detalhe = "";
    if (sessao_.phase() == ota::Phase::Recusado) {
        st.detalhe = textoDaRecusa(sessao_.rejectReason());
    } else if (sessao_.phase() == ota::Phase::Falhou) {
        st.detalhe = textoDaFalha(sessao_.failReason());
    }
    st.progressoPorMil = sessao_.progressoPorMil();
    st.aceitandoPacote = sessao_.aceitaPacote();
    st.aceitandoBytes = sessao_.aceitaBytes();
}

const char* textoDaFase(ota::Phase f) {
    switch (f) {
        case ota::Phase::Ocioso: return "PRONTO PARA RECEBER";
        case ota::Phase::Aguardando: return "CONFIRME NO PAINEL";
        case ota::Phase::Preparando: return "PONDO AS SAIDAS EM ALARME";
        case ota::Phase::Gravando: return "GRAVANDO";
        case ota::Phase::Verificando: return "VERIFICANDO";
        case ota::Phase::Concluido: return "CONCLUIDO - REINICIANDO";
        case ota::Phase::Recusado: return "PACOTE RECUSADO";
        case ota::Phase::Falhou: return "FALHOU";
        default: return "?";
    }
}

const char* textoDaRecusa(ota::HeaderVerdict v) {
    switch (v) {
        case ota::HeaderVerdict::Ok: return "";
        case ota::HeaderVerdict::Curto: return "ARQUIVO CURTO DEMAIS";
        case ota::HeaderVerdict::MagicaInvalida: return "NAO E UM PACOTE DESTE PRODUTO";
        case ota::HeaderVerdict::CabecalhoCorrompido: return "PACOTE CORROMPIDO";
        case ota::HeaderVerdict::VersaoDesconhecida: return "FORMATO MAIS NOVO QUE ESTE FIRMWARE";
        case ota::HeaderVerdict::AlvoErrado: return "PACOTE DA OUTRA PLACA";
        case ota::HeaderVerdict::TamanhoInvalido: return "TAMANHO NAO CABE NA PARTICAO";
        case ota::HeaderVerdict::ReservadoNaoZero: return "CAMPOS RESERVADOS EM USO";
        default: return "?";
    }
}

const char* textoDaFalha(ota::FailReason m) {
    switch (m) {
        case ota::FailReason::Nenhuma: return "";
        case ota::FailReason::NaoConfirmado: return "NINGUEM CONFIRMOU NO PAINEL";
        case ota::FailReason::Estagnou: return "O ENVIO PAROU NO MEIO";
        case ota::FailReason::TempoEsgotado: return "O ENVIO DEMOROU DEMAIS";
        case ota::FailReason::ImagemInvalida: return "IMAGEM NAO CONFERE";
        case ota::FailReason::FalhaDeGravacao: return "FALHA AO GRAVAR NA FLASH";
        case ota::FailReason::FalhaDeTroca: return "FALHA AO TROCAR A PARTICAO";
        case ota::FailReason::Cancelado: return "CANCELADO";
        default: return "?";
    }
}

}  // namespace app
