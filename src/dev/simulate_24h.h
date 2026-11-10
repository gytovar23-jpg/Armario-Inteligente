#pragma once
// Build de demonstração (env esp32dev-sim): grava 24 h de histórico fictício no
// primeiro boot de cada gravação, para ver dashboard e gráficos preenchidos.

namespace devsim {

// Chamar depois do storage, das configurações e do fuso, e antes de usage::begin().
void run();

}  // namespace devsim
