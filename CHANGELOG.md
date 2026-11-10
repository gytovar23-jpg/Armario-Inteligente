# Changelog

Segue [versionamento semântico](https://semver.org/lang/pt-BR/). A versão 1.0.0 fica reservada para depois da validação em hardware.

## 0.10.0 — 2026-11-09

- Pinagem nova, validada em bancada: quase tudo do lado do 3V3 do DevKit (detalhes no README)
- Sem sensor de porta: a sessão é uma janela de tempo depois do cartão e tranca 3 s após a última mudança de slot
- Light Sleep acorda por mudança de slot; Deep Sleep só por timer
- Configurações: sai "porta aberta, máximo"; o esquema muda e as configurações salvas voltam ao padrão na primeira inicialização

## 0.9.0 — 2026-11-03

- Dashboard simplificado: notebooks, quem retirou, horários e devoluções ao vivo
- Página Estatísticas (só administrador): médias por dia, uso por semana, quem mais usa e tempo por pessoa

## 0.8.0 — 2026-10-30

- Testes unitários da lógica pura
- README, arquitetura, roteiro de testes e checklist do TDE
- Revisão final: correções de concorrência e de atribuição de empréstimos

## 0.7.0 — 2026-10-30

- Uso de CPU por run-time stats do FreeRTOS
- Métricas de memória, flash e tasks
- Light Sleep, Deep Sleep e modo econômico
- OTA com validação e rollback

## 0.6.0 — 2026-10-30

- Páginas Dashboard, Performance, Admin, Sobre e Changelog
- Chart.js embarcado, sem CDN

## 0.5.0 — 2026-10-30

- Wi-Fi com queda para AP e portal
- NTP
- API REST, WebSocket e login

## 0.4.0 — 2026-10-30

- Usuários, eventos, histórico de 24 h e 7 dias
- Logs circulares persistentes
- Empréstimos ativos sobrevivem a reset

## 0.3.0 — 2026-10-30

- Tasks por core, filas e máquina de estados

## 0.2.0 — 2026-10-30

- Drivers de PN532, OLED, servo, LEDs, buzzer e sensores com debounce

## 0.1.0 — 2026-10-30

- Esqueleto do projeto, partições, pinagem, configurações e logger
