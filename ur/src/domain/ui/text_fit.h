// Escolha de fonte POR MEDICAO, e nao por tabela escrita a mao.
//
// O painel tem 256 px e a IHM tem textos de 14 a 36 caracteres. Escolher a fonte tela por tela
// produziu, nas duas primeiras rodadas, exatamente o defeito que o teste geometrico depois
// pegou: uma linha legitima estourando a borda em silencio, porque IDisplay nao recorta e
// ninguem percebe olhando o codigo. A regra aqui e uma so e vale para toda tela de conteudo:
//
//   use a MAIOR fonte em que a string INTEIRA cabe na largura disponivel.
//
// Com isso "Ajuste 0Vcc:0000" sobe para Medium e "Angulo fim de escala X(graus):+045,0" desce
// sozinho para Small, sem que nenhuma das duas precise estar escrita numa lista - e uma string
// que mudar de tamanho amanha reencontra a fonte certa sem que ninguem se lembre de revisar.
//
// NAO decide altura. Quem empilha linhas continua responsavel por caber no painel; esta funcao
// so responde sobre a largura de UMA string.
#pragma once

#include <stdint.h>

#include "ports/i_display.h"

namespace domain {
namespace ui {

// Ordem de preferencia: Large, Medium, Small. Devolve Small quando nem Small cabe - recortar
// nao e opcao desta camada, e uma linha grande demais tem de aparecer grande demais no teste
// geometrico em vez de sumir aqui.
TextFont fontThatFits(const IDisplay& display, const char* text, int16_t availableWidth,
                      TextFont largest = TextFont::Medium);

}  // namespace ui
}  // namespace domain
