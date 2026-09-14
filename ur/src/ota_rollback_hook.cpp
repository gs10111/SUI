// DESARMA A MARCACAO AUTOMATICA DE IMAGEM VALIDA DO CORE DO ARDUINO.
//
// Conferido no core instalado (cores/esp32/esp32-hal-misc.c:203-238), com
// CONFIG_APP_ROLLBACK_ENABLE=y no sdkconfig:
//
//     bool verifyRollbackLater() __attribute__((weak));   // devolve false
//     bool verifyOta()           __attribute__((weak));   // devolve TRUE
//     void initArduino() {
//     #ifdef CONFIG_APP_ROLLBACK_ENABLE
//         if (!verifyRollbackLater()) {
//             if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
//                 if (verifyOta()) esp_ota_mark_app_valid_cancel_rollback();
//                 else             esp_ota_mark_app_invalid_rollback_and_reboot();
//
// Ou seja: DE FABRICA, o ESP32 marca como valida qualquer imagem que apenas chegue a rodar
// initArduino() - o que acontece ANTES do setup(). Uma sensora atualizada que sobe mas nao fala
// com o SCL3300, ou uma UR que sobe mas nao enxerga a sensora, ficam marcadas como boas e NAO ha
// rollback automatico. A sensora fica a 500 m dentro de um modulo: isso vira deslocamento de
// tecnico com cabo USB.
//
// Esta definicao FORTE de verifyRollbackLater() devolve true e faz o core pular o bloco inteiro.
// A decisao passa a ser nossa, com criterio explicito e prazo, em lib_shared/depuri_ota
// (ota::BootProof) - e e o laco principal que a executa depois de o equipamento provar que
// voltou a funcionar.
//
// ESTE ARQUIVO E PRE-REQUISITO DE CABO. Ele tem de estar na imagem que JA ESTA na placa quando o
// primeiro OTA chegar; nao adianta ele estar na imagem que vai subir. Sem ele, a primeira
// atualizacao remota e tambem o primeiro teste do mecanismo de recuperacao - sem mecanismo.
//
// verifyOta() NAO e sobrescrito de proposito: com verifyRollbackLater() devolvendo true, o core
// nao chega a consulta-lo, e uma segunda definicao forte so criaria duas verdades sobre o mesmo
// assunto. Quem decide e o BootProof.
#include <stdbool.h>

extern "C" bool verifyRollbackLater() {
    return true;
}
