# Silver Deesser — revisão técnica

## Resultado
A arquitetura é adequada a um de-esser de banda dividida e se aproxima funcionalmente do Oxford SuprEsser. Não há evidência suficiente para afirmar equivalência sonora. O pacote contém código-fonte, não um VST3 Windows compilado. A interface alterada ainda precisa de compilação e inspeção visual em uma DAW; os testes realizados cobrem o núcleo DSP independente de JUCE.

## Fluxo verificado no código
Entrada → trim → FIR bandpass / complemento (entrada atrasada menos banda).
Detector BAND usa a banda; detector WIDE usa entrada alinhada. O tracker recebe o complemento no primeiro caso e a entrada no segundo. O detector RMS alimenta threshold fixo ou relativo ao tracker, curva de ratio/knee e envelope attack/hold/release. A saída combina banda processada e complemento, ou aplica ganho à banda larga. Wet/dry atua no ganho do compressor. Listen seleciona mix/interior/exterior no modo de áudio BAND; áudio WIDE ignora Listen. Look-ahead de 2 ms e atraso FIR são informados ao host.

## Alterações entregues
- Paleta prateada mais clara, painéis arredondados, cores de análise mais equilibradas e contraste de textos preservado.
- Nome do cabeçalho corrigido para SILVER DEESSER; SLOPE/Q renomeado SLOPE, pois o parâmetro controla dB/oitava.
- Dicas em controles principais; componentes implementam SettableTooltipClient e a janela inclui TooltipWindow.
- Amostras do analisador em atomics lock-free: elimina a data race entre interface e áudio. É uma leitura aproximada do sinal recente, não uma captura coerente garantida de um bloco inteiro.
- Removida alocação temporária no desenho da resposta do FIR. O redesenho e FFTs ainda acontecem na thread de áudio e podem causar picos de CPU durante automação; migrar para worker com troca segura de kernels é prioridade futura.
- Suavização de wet e make-up em 5 ms para reduzir descontinuidades.
- Contador de redesenho limitado, evitando overflow em sessões longas; proteção para blocos vazios e limite de leitura do analisador.
- No GitHub, preservado o workflow existente .github/workflows/c-cpp.yml para evitar builds duplicados.

## Comparação com a referência
Fonte primária: https://dload.sonnoxplugins.com/pub/plugins/manuals/SonnoxSupMan.pdf
O manual documenta filtros complementares de fase linear, quatro combinações BAND/WIDE, três opções Listen, tracking automático e mistura wet/dry no compressor. O Silver implementa esses conceitos, mas as curvas e tempos internos são próprios.

| Item | Silver | Implicação |
|---|---|---|
| Separação | FIR complementar, kernel 2048 a 48 kHz | Reconstrução neutra validada; resposta não é uma modelagem comprovada dos filtros Oxford |
| Detector/envelope | RMS e envelope próprios | Transientes e sibilância podem receber redução diferente |
| Tracking | Janela de 24 dB e tempos assimétricos próprios | Sem teste A/B, não se pode afirmar que acompanha frases como o Sonnox |
| Resolução | Kernel cresce com sample rate | Não oferece escolha equivalente às versões standard/HR/low latency da referência |
| Banda muito estreita | Continua o mesmo desenho bandpass | Falta o modo High-Q notch específico descrito no manual |
| Look-ahead | Fixo em 2 ms | Não garante captura completa de transientes com attack longo |
| Presets | A/B temporário | Faltam presets e persistência dos dois slots A/B no projeto |

A latência medida a 48 kHz foi 1248 amostras (26 ms). Adequada para mixagem com compensação; avaliar uma versão de baixa latência para monitoramento ao vivo.

## Testes executados
Compilação C++17 com -Wall -Wextra -pedantic; execução normal e com AddressSanitizer/UndefinedBehaviorSanitizer: PASS com detecção de leaks desabilitada (LeakSanitizer não funciona sob ptrace neste ambiente).
- Reconstrução neutra, ratio 1:1, mono/estéreo em 44,1/48/96/192 kHz; erro máximo 1,49e-8.
- Latências correspondentes: 1240 / 1248 / 2368 / 4608 amostras.
- Seno 7 kHz, amplitude 0,5, threshold -30 dBFS, auto desligado, limitador, attack 0,01 ms: RMS final 0,030958 em BAND e 0,030957 em WIDE (aprox. -30,18 dBFS).
- Canais idênticos preservaram igualdade; saídas finitas nos casos testados.
Não foram realizados build do JUCE/VST3, pluginval, teste de DAW, inspeção renderizada da interface, escuta com voz real ou execução do Sonnox. Estes resultados não cobrem toda automação, frequências baixas, silêncio prolongado, todos os modos, bypass do host ou desempenho em tempo real.

## Próximas melhorias prioritárias
1. Desenho de kernel fora do callback, com buffers previamente alocados e troca sem locks na thread de áudio.
2. Fila de análise SPSC para snapshots coerentes; evitar cancelamento visual de estéreo em oposição de fase (hoje o analisador soma L+R).
3. Persistir os dois estados A/B; revisar restauração de sessão e sincronização dos indicadores.
4. Conferir curva desenhada no gráfico contra resposta FIR medida, principalmente em graves e bandas estreitas.
5. Adicionar presets próprios para voz masculina/feminina, fala, canto e pratos; considerar limite de redução e escuta do sinal removido.
6. Validar acessibilidade, foco de teclado, tooltips visíveis, contraste e legibilidade da janela na escala mínima.

## Protocolo A/B necessário
Usar o mesmo WAV seco (voz com S/CH, frases fracas/fortes e pausas) em duas pistas alinhadas pela compensação de latência. Fixar versão do Sonnox, sample rate, modo BAND/BAND e tracking desligado primeiro. Começar em 7 kHz, largura 1,4 oitava, attack 5,2 ms, hold 10 ms e release 127 ms; ajustar threshold para obter redução comparável, pois valores iguais não garantem detecção igual. Igualar loudness de saída, exportar WAV sem normalização e comparar inteligibilidade, brilho, início/fim das sibilantes, pumping e imagem estéreo em escuta cega. Repetir com tracking ligado e BAND/WIDE. Medir espectro e envelope de redução. Um null test isolado não prova qualidade ou equivalência entre algoritmos distintos.

Para conclusão sonora são necessários WAV seco e render do mesmo trecho no Sonnox, versão do plugin e ajustes usados; idealmente também render da versão compilada do Silver.

## Compilação e teste
Windows: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target SilverDesser_VST3 --parallel
DSP independente: g++ -std=c++17 -O2 tests/dsp_test.cpp -o dsp_test
Executar o binário dsp_test. JUCE 8.0.12 é obtido pelo CMake e requer acesso de rede.
