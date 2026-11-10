# Roteiro de testes

Preencha **Resultado** e **Evidência** (print, foto ou medição). A coluna Diagnóstico indica o botão em Admin → Controle.

## Testes automatizados

| Teste | Comando | Resultado |
|---|---|---|
| 59 testes da lógica pura | `pio test -e native` | Passou em 2026-10-30 (no PC) |
| Compilação | `pio run` | Passou em 2026-10-30 |

## Testes isolados

| # | Item | Procedimento | Esperado | Diagnóstico | Resultado | Evidência |
|---|---|---|---|---|---|---|
| I1 | PN532 | Aproximar cartão | Contador de leituras sobe; firmware informado | NFC | | |
| I2 | OLED | Observar no boot | Tela "Aproxime o cartao" | OLED | | |
| I3 | Servo | Acionar teste | Destrava e volta em 1,5 s | Servo | | |
| I4 | Buzzer | Acionar teste | Bipe de 400 ms | Buzzer | | |
| I5 | LED verde / vermelho | Acionar teste | Acende por 1 s | LED verde, LED vermelho | | |
| I7 | Slots 1 e 2 | Pressionar e soltar | Estado muda sem repetir | Sensores | | |
| I8 | Debounce | Tocar a chave rapidamente | Interrupções > mudanças de estado | Sensores | | |
| I9 | Segundo atuador | Ligar e desligar | Relé comuta | Atuador 2 | | |
| I10 | Flash | Acionar teste | `ok: true`, sem erros de escrita | Flash | | |

## Integração: sensores e firmware

| # | Cenário | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| S1 | Cartão desconhecido | LED vermelho, 2 bipes, "Acesso negado", log WARN | | |
| S2 | Cartão autorizado | LED verde, 1 bipe, destranca, nome no OLED | | |
| S3 | Autorizado sem mexer nos slots | Retranca após o tempo configurado, sem evento | | |
| S4 | Retirada completa | Tranca 3 s depois de tirar o notebook; evento gravado ao trancar | | |
| S5 | Devolução | Evento com duração do empréstimo | | |
| S6 | Tirar e repor na mesma sessão | Nenhum evento de empréstimo | | |
| S7 | Dois cartões seguidos na mesma sessão | Retirada do primeiro gravada no nome dele; a do segundo, no dele | | |
| S9 | Slot muda com o armário trancado | Alarme de violação, log ERROR | | |
| S10 | Cartão comum durante alarme | Alarme continua tocando | | |
| S11 | Cartão admin durante alarme | Silencia | | |
| S12 | Cadastro de cartão | UID aparece no formulário | | |

## Integração: firmware e web

| # | Cenário | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| W1 | Primeiro acesso em AP | Portal abre; troca de senha é obrigatória | | |
| W2 | Trocar Wi-Fi pela web | Conecta sem regravar | | |
| W3 | Senha de Wi-Fi errada | Cai em AP `Armario-Setup` | | |
| W4 | Dashboard ao vivo | Aviso de armário destrancado muda na tela sem recarregar; retirada aparece com nome, notebook e horário | | |
| W4b | Devolver com o cartão e esperar trancar | Linha passa a "Devolvido" com horário, sem recarregar | | |
| W4c | Estatísticas sem login / com login | Sem login pede para entrar no Admin; com login mostra médias, semanas e ranking | | |
| W5 | Performance | CPU, memória, tasks e funções atualizam | | |
| W6 | Sem internet (modo AP) | Gráficos funcionam | | |
| W7 | Alterar leitura NFC e tempos | Vale sem reiniciar | | |
| W8 | Exportar logs CSV e JSON | Arquivos abrem corretamente | | |
| W9 | Backup e restauração | Usuários e configurações voltam | | |
| W10 | Rotas admin sem login | Respondem 401 | | |
| W11 | OTA com `firmware.bin` | Barra de progresso; versão nova após reiniciar | | |
| W12 | OTA com arquivo inválido | Recusado; firmware segue igual | | |
| W13 | OTA com imagem que trava no boot | Volta à versão anterior | | |

## Persistência

| # | Cenário | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| P1 | Reiniciar pela web | Logs, histórico e usuários permanecem | | |
| P2 | Cortar energia com notebook emprestado | Empréstimo continua ativo | | |
| P3 | Gerar mais de 1125 logs | Mantém ao menos 1000; mais antigos saem | | |
| P4 | Deixar ligado por 24 h | Gráficos com 24 h completas | | |
| P5 | Cortar energia durante uso intenso | Boot sem corrupção | | |

## Consumo por modo de energia

Meça a corrente com multímetro em série na alimentação de 5 V, sem o servo em movimento.

| # | Modo | Corrente (mA) | Evidência |
|---|---|---|---|
| E1 | Normal, 240 MHz, Wi-Fi conectado | | |
| E2 | 160 MHz | | |
| E3 | 80 MHz | | |
| E4 | 240 MHz com modem sleep | | |
| E5 | Light Sleep | | |
| E6 | Deep Sleep | | |
| E7 | Light Sleep: acordar por timer e por slot; motivo correto no log | | |

## Medições para o relatório

| Métrica | Valor | Condição |
|---|---|---|
| CPU core 0 / core 1 em repouso | | |
| CPU com página Performance aberta | | |
| Heap mínimo após 24 h | | |
| Menor pilha livre e task correspondente | | |
| Tempo médio e máximo de cada função | | |
| Latência do cartão até o destrave | | |
| LittleFS usado | | |
