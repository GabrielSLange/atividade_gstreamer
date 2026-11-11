# Streaming de áudio com GStreamer — guia de estudo

Material para estudar e apresentar a atividade. Linguagem simples de propósito.

> **Situação atual:** transmissor e receptor (com jitter buffer, diagnóstico de perdas e simulação de rede),
> experimentos registrados, diagramas e [README.md](README.md) estão prontos. Falta só preencher as
> observações auditivas (ver [seção 9](#9-o-que-ainda-falta)). Este guia só afirma o que o código atual faz.

---

## 1. O que o projeto faz

Pega um áudio (tom gerado ou microfone), manda pela rede em pacotes e outro programa
recebe esses pacotes e toca o som. Como uma ligação de voz, só que feita por nós.

```
 ┌──────────────┐      rede (UDP)      ┌──────────────┐
 │ TRANSMISSOR  │ ───────────────────► │   RECEPTOR   │
 │ gera/captura │   pacotes RTP com    │ junta o áudio│
 │ o áudio      │   pedacinhos de som  │ e toca       │
 └──────────────┘                      └──────────────┘
```

Os dois podem rodar no mesmo PC (`127.0.0.1`) ou em PCs diferentes na mesma rede.

---

## 2. Conceitos que precisam estar na ponta da língua

| Termo | Explicação simples |
|---|---|
| **GStreamer** | Biblioteca que monta "linhas de produção" de mídia. |
| **Pipeline** | A linha de produção: uma cadeia de elementos, cada um faz uma tarefa. |
| **Elemento** | Uma peça da pipeline (ex.: `udpsink` envia pela rede). |
| **Pad** | A "tomada" de um elemento. Saída (`src`) de um liga na entrada (`sink`) do próximo. |
| **Caps** | A "etiqueta" que descreve o dado que passa entre dois elementos (formato, taxa, canais...). |
| **PCM** | Áudio cru, sem compressão: uma sequência de números (amostras). |
| **UDP** | Envia pacotes sem garantia: pode perder, atrasar ou trocar a ordem. É rápido. |
| **RTP** | Cabeçalho colocado em cada pacote para dar **ordem** e **tempo** ao áudio (UDP não tem). |

### Por que UDP e não TCP?
Em tempo real, **chegar tarde é pior que não chegar**. O TCP retransmite o que se perdeu e isso
atrasa tudo. No UDP, se um pedacinho some, o som só falha um instante e segue.

### Por que RTP em cima do UDP?
O UDP não diz a ordem nem o momento de cada pacote. O RTP acrescenta 12 bytes de cabeçalho com:

- **número de sequência**: permite colocar em ordem e perceber que faltou pacote;
- **timestamp**: diz quando aquele pedaço de áudio deve ser tocado;
- **payload type** (aqui, 96): diz que tipo de dado vem dentro;
- **SSRC**: identifica quem está transmitindo.

---

## 3. O formato do áudio: L16

- **L16** = PCM de 16 bits "linear", definido para RTP.
- Configuração usada: **44.100 Hz, 16 bits, 2 canais (estéreo)**.
- Taxa de dados: 44.100 × 16 × 2 = **1.411.200 bits/s** (~1,4 Mbps). É grande porque não há compressão,
  mas é simples e boa para observar perdas (cada falha fica audível).
- **S16BE**: 16 bits com sinal, *big-endian*. O RTP exige ordem de bytes "de rede" (big-endian);
  por isso o transmissor converte para S16BE antes de empacotar.

---

## 4. Transmissor — [src/transmissor.c](src/transmissor.c)

```
fonte ─► audioconvert ─► audioresample ─► capsfilter ─► rtpL16pay ─► udpsink
```

Diagrama completo, com parâmetros e legenda: [pipeline_transmissor.png](diagramas/pipeline_transmissor.png).

| Elemento | O que faz | Por quê |
|---|---|---|
| `audiotestsrc` (ou `autoaudiosrc`) | Gera um tom de 440 Hz (ou captura o microfone). | É a **fonte** do áudio. `is-live=TRUE` gera em tempo real, como um microfone. `volume=0.3` para não ficar alto. |
| `audioconvert` | Converte o formato das amostras. | O microfone pode entregar float; precisamos de inteiros de 16 bits. |
| `audioresample` | Muda a taxa de amostragem. | O microfone pode ser 48 kHz; precisamos de 44,1 kHz. |
| `capsfilter` | Obriga o formato: `S16BE, 44100 Hz, 2 canais`. | É o formato que o `rtpL16pay` aceita. |
| `rtpL16pay` | **Encapsula** o PCM em pacotes RTP (`pt=96`). | Coloca número de sequência, timestamp etc. |
| `udpsink` | **Envia** os pacotes por UDP para `host:porta`. | É a saída para a rede. |

Uso:

```
transmissor [host] [porta] [teste|mic]      (padrão: 127.0.0.1 5000 teste)
```

---

## 5. Receptor — [src/receptor.c](src/receptor.c)

```
udpsrc ─► netsim ─► rtpjitterbuffer ─► rtpL16depay ─► audioconvert ─► audioresample ─► autoaudiosink
```

Diagrama completo, com parâmetros e legenda: [pipeline_receptor.png](diagramas/pipeline_receptor.png).

| Elemento | O que faz | Por quê |
|---|---|---|
| `udpsrc` | **Recebe** pacotes na porta UDP. | Entrada da rede. |
| `netsim` | **Simula a rede**: descarta e/ou atrasa pacotes aleatoriamente. | Permite testar perda e jitter sem uma rede ruim de verdade. Com 0, não altera nada. |
| `rtpjitterbuffer` | **Segura** os pacotes por `latency` ms, põe em ordem e entrega em ritmo constante. | Compensa o jitter e a troca de ordem da rede (ver seção 9). |
| `rtpL16depay` | Tira o cabeçalho RTP e devolve o PCM. | É o inverso do `rtpL16pay`. |
| `audioconvert` / `audioresample` | Adaptam o áudio ao que a placa de som aceita. | Cada placa tem seu formato preferido. |
| `autoaudiosink` | **Toca** o áudio. | Escolhe sozinho o dispositivo de saída (no Windows, costuma ser o WASAPI). |

Uso:

```
receptor [porta] [latencia_ms] [perda_%] [atraso_max_ms]      (padrão: 5000 200 0 0)
```

### Por que o `udpsrc` precisa das caps?
O `udpsrc` entrega só **bytes**. Ele não sabe que são pacotes RTP nem qual o formato do áudio.
Por isso configuramos nele as caps:

```
application/x-rtp, media=audio, clock-rate=44100, encoding-name=L16, channels=2, payload=96
```

Elas precisam ser **iguais** às do transmissor. O `clock-rate` também é usado pelo
`rtpjitterbuffer` para calcular tempos.

### Diagnóstico de perdas (fase 4)

O receptor usa dois recursos do próprio `rtpjitterbuffer`:

1. **Evento de perda** (`do-lost=TRUE`): quando um pacote não chega a tempo, o jitter buffer envia o evento
   `GstRTPPacketLost`. Um *probe* no pad de saída dele imprime `[PERDA] pacote seq=N nao chegou a tempo`.
2. **Estatísticas** (propriedade `stats`): a cada 1 s o receptor imprime
   `[STATS] entregues=… perdidos=… atrasados=… duplicados=…`:
   - `entregues` = pacotes entregues adiante (`num-pushed`);
   - `perdidos` = pacotes dados como perdidos (`num-lost`);
   - `atrasados` = pacotes que chegaram **depois** de já terem sido dados como perdidos (`num-late`);
   - `duplicados` = pacotes repetidos (`num-duplicates`).

Para testar sem uma rede ruim, o `netsim` simula a rede dentro do receptor: o 3º argumento é a **% de
perda** e o 4º é o **atraso aleatório máximo** (ms), que cria jitter.

Resultado de um teste rápido de 8 s por cenário (tom de teste, localhost). Os experimentos completos
(16 combinações) estão em [EXPERIMENTOS.md](EXPERIMENTOS.md):

| Cenário | Entregues | Perdidos | Atrasados |
|---|---|---|---|
| A: sem perda, latência 200 ms | 1007 | 0 | 0 |
| B: 10% de perda, latência 200 ms | 897 | 108 | 0 |
| C: atraso até 100 ms, latência 20 ms | 467 | 543 | 540 |
| D: atraso até 100 ms, latência 1000 ms | 1008 | 0 | 0 |

O que isso mostra:
- **B:** perda real não volta (108 ≈ 10% de ~1005 pacotes); só é detectada e contada.
- **C x D:** o mesmo jitter vira perda com latência pequena e é totalmente absorvido com latência grande.
  É exatamente o compromisso latência × robustez.
- No cenário C houve 478 eventos `[PERDA]` para 543 perdidos. Provavelmente alguns eventos cobrem mais de um
  pacote seguido, mas isso **não foi confirmado**.

---

## 6. Como o código está organizado

Os dois arquivos seguem os mesmos 4 passos (padrão do *Basic Tutorial 2* do GStreamer):

1. **Criar** os elementos com `gst_element_factory_make`.
2. **Configurar** as propriedades com `g_object_set` (host, porta, caps, `pt`...).
3. **Adicionar e ligar** com `gst_bin_add_many` e `gst_element_link_many`.
4. **Executar**: `gst_element_set_state(pipeline, GST_STATE_PLAYING)` e esperar mensagens no *bus*.

**Tratamento de erros:**
- elemento não criado (plugin ausente) → mensagem e encerra;
- ligação falhou → mensagem e encerra;
- erro durante a execução (ex.: porta ocupada) → lido do *bus*, impresso, e o programa sai com código 1.

---

## 7. Como rodar

Compilar (precisa de GStreamer e gcc/MinGW; ajuste os caminhos no topo do `build.bat` se necessário):

```
build.bat
```

Em **dois terminais**, receptor primeiro:

```
bin\receptor.exe
bin\transmissor.exe
```

Outras formas:

```
bin\transmissor.exe 127.0.0.1 5000 mic        (microfone; cuidado com microfonia nos alto-falantes)
bin\transmissor.exe 192.168.0.20 6000         (outro PC, outra porta)
bin\receptor.exe 6000
bin\receptor.exe 5000 20                      (jitter buffer de 20 ms)
bin\receptor.exe 5000 1000                    (jitter buffer de 1000 ms)
bin\receptor.exe 5000 200 10 0                (simula 10% de perda de pacotes)
bin\receptor.exe 5000 20 0 100                (simula atraso de até 100 ms com jitter buffer de 20 ms)
```

> Em PCs diferentes, o firewall do Windows pode pedir permissão para o receptor.

**O que deve acontecer:** o receptor toca um tom contínuo de 440 Hz (um "bipe" longo) e imprime uma linha
`[STATS]` por segundo.

---

## 8. Decisões técnicas e dificuldades (ótimo para a apresentação)

- **Pipeline montada elemento a elemento**, e não com `gst_parse_launch`. O enunciado pede construção
  programática; `gst_parse_launch` seria só um texto de `gst-launch` dentro do C. Além disso, nas
  próximas fases precisamos de acesso direto ao `rtpjitterbuffer` para mudar propriedades e ler estatísticas.
- **S16BE e conversores:** o `rtpL16pay` só aceita S16BE; sem `audioconvert`/`audioresample` o microfone
  não negociaria.
- **Porta ocupada no Windows:** o `udpsrc` vem com `reuse=TRUE`, e no Windows isso deixa **dois receptores
  escutarem a mesma porta sem erro** (o segundo nunca recebe nada). Colocamos `reuse=FALSE` e a porta
  ocupada virou erro claro.
- **Caps no `udpsrc`:** sem elas, o resto da pipeline não sabe o que está recebendo.
- **Teste de verdade:** gravamos o áudio que o receptor entrega e analisamos: 440 Hz, amplitude coerente
  com `volume=0.3` e sem buracos em ~5,7 s.
- **Jitter buffer verificado:** medimos quanto tempo o `rtpjitterbuffer` segura o primeiro pacote:
  ≈ 31 ms, 212 ms e 1013 ms para latência de 20, 200 e 1000 ms (a latência configurada + ~12 ms).
  O áudio que passa por ele continua íntegro (440 Hz, sem buracos).
- **Diagnóstico de perdas verificado:** com 10% de perda simulada, o receptor contou 108 pacotes perdidos
  (de ~1005) e imprimiu 108 eventos `[PERDA]`; sem perda, zero (tabela na seção 5).
- **Simplicidade:** o código foi reduzido ao essencial (sem loop de eventos, sem contadores).
  Dá para voltar a recursos extras quando forem necessários.

**Limitações atuais (assumir com honestidade):**
- Perdas são **detectadas e contadas**, mas não **recuperadas** (o enunciado não pede recuperação).
- Cada combinação dos experimentos foi executada **uma vez** (10 s); há variação estatística entre execuções.
  As **observações auditivas** em [EXPERIMENTOS.md](EXPERIMENTOS.md) ainda precisam ser preenchidas por vocês.
- O `netsim` simula a rede dentro do receptor; ainda não testamos com uma rede real nem em dois PCs.
- A latência só muda reiniciando o receptor com outro valor (não dá para trocar com ele rodando).
- Se o host de destino não existir, o transmissor não avisa (o `udpsink` não reclama).
- Fontes suportadas: tom e microfone (não há arquivo).

---

## 9. O que ainda falta

| Fase | O que é | Status |
|---|---|---|
| 3 | `rtpjitterbuffer` no receptor com latência configurável | **feito** |
| 4 | Diagnóstico de perdas (evento de pacote perdido, estatísticas, descartes) | **feito** |
| 5 | Experimentos com ≥ 3 latências (e perda/atraso simulados com `netsim`), registrados | **feito** ([EXPERIMENTOS.md](EXPERIMENTOS.md)); faltam as observações auditivas |
| 6 | Diagramas das pipelines (Excalidraw) | **feito** (pasta [diagramas/](diagramas/)) |
| 7 | `README.md` (o registro dos experimentos e a conclusão estão no EXPERIMENTOS.md) | **feito** ([README.md](README.md)) |

### O que é o jitter buffer (já está no receptor)
Na rede, os pacotes não chegam em intervalos regulares (isso é o **jitter**) e podem chegar fora de ordem.
O **jitter buffer** segura os pacotes por um tempo (a **latência**), coloca em ordem pelo número de sequência
e entrega num ritmo constante.

- **Latência alta**: tolera mais variação da rede, mas o áudio chega mais atrasado.
- **Latência baixa**: mais responsivo, mas pacotes que chegam depois do prazo são descartados (picotes).
- **Limitação:** não recupera pacotes perdidos; só ajuda quando o pacote **chega**, mesmo que atrasado.

---

## 10. Perguntas que o professor pode fazer

**Por que UDP e não TCP?** Em tempo real, atraso é pior que perda; o TCP retransmite e atrasa tudo.

**O que o RTP acrescenta ao UDP?** Número de sequência (ordem/perdas), timestamp (tempo), payload type (tipo do dado).

**O que são caps e por que estão no `udpsrc`?** São a descrição do formato dos dados; o `udpsrc` entrega bytes
crus, então dizemos a ele que são pacotes RTP de L16 a 44,1 kHz estéreo.

**Por que S16BE?** O RTP usa ordem de bytes de rede (big-endian).

**Qual a taxa de dados?** 44.100 × 16 × 2 ≈ 1,4 Mbps (PCM sem compressão).

**Por que não usar `gst_parse_launch`?** O enunciado pede construção programática, e precisamos acessar os
elementos (jitter buffer) para configurar e consultar estatísticas.

**O que o jitter buffer faz e o que muda com a latência?** Segura os pacotes por `latency` ms, ordena pelo
número de sequência e entrega em ritmo constante. Medimos que ele retém o primeiro pacote por cerca da
latência configurada (≈ 31, 212 e 1013 ms para 20, 200 e 1000 ms). Mais latência = mais tolerância à rede,
porém mais atraso.

**O que os experimentos mostraram?** Com jitter de até 80 ms, as latências de 20 e 50 ms perderam 43% a 50% dos
pacotes (áudio picotado); com 200 e 1000 ms, zero perdas. Já uma perda real de 5% apareceu igual em todas as
latências (~60 pacotes em 10 s): o jitter buffer não recupera perdas. Detalhes em
[EXPERIMENTOS.md](EXPERIMENTOS.md).

**O que acontece se um pacote se perder?** O jitter buffer espera o pacote até o fim da latência; se não
chegar, vira uma falha no áudio. Nós **detectamos e contamos**, mas não recuperamos (o enunciado não pede).

**Como vocês detectam perdas?** De duas formas, ambas do `rtpjitterbuffer`: o evento `GstRTPPacketLost`
(`do-lost=TRUE`, capturado por um probe) e a propriedade `stats` (`num-lost`, `num-late`, `num-duplicates`),
lida a cada segundo.

**Como testaram sem uma rede ruim?** Com o elemento `netsim`, que descarta e atrasa pacotes aleatoriamente.
Com 10% de perda simulada, contamos 108 perdidos de ~1005 pacotes.

**Qual a diferença entre perdido e atrasado?** Perdido: não chegou a tempo e o jitter buffer seguiu em frente.
Atrasado: chegou depois de já ter sido dado como perdido, então foi descartado.

**Qual foi a maior dificuldade?** A porta UDP compartilhada em silêncio no Windows (`reuse`).

---

## 11. Sugestão de roteiro para a demonstração (~5 min)

1. Mostrar o diagrama geral (seção 1) e dizer o que cada programa faz.
2. Rodar `build.bat`, abrir o receptor e depois o transmissor → **ouvir o tom**.
3. Explicar a pipeline do transmissor e a do receptor (tabelas das seções 4 e 5), apontando os trechos
   de código: criação dos elementos, `g_object_set` das caps e `gst_element_link_many`.
4. Mostrar o erro tratado: abrir **dois receptores** na mesma porta e ver a mensagem clara.
5. Explicar as decisões (seção 8) e as limitações atuais.
6. Mudar a latência: reiniciar o receptor com outro valor (`receptor 5000 20`, depois `receptor 5000 1000`) e
   comentar a diferença de atraso entre o som sair e tocar.
7. Mostrar o diagnóstico de perdas: `receptor 5000 200 10 0` (linhas `[PERDA]` e `[STATS]`) e comparar
   `receptor 5000 20 0 100` com `receptor 5000 1000 0 100` (mesmo jitter, latência pequena x grande).
8. Mostrar os resultados em [EXPERIMENTOS.md](EXPERIMENTOS.md) (tabelas, análise e conclusão).
9. Mostrar os diagramas das pipelines: [transmissor](diagramas/pipeline_transmissor.png) e
   [receptor](diagramas/pipeline_receptor.png). Para editar, abrir os arquivos `.excalidraw` da mesma pasta em
   [excalidraw.com](https://excalidraw.com) (menu → Abrir, ou arrastar o arquivo para a tela).

---

## 12. Glossário rápido

**Amostra** — um número que representa o som num instante. **Taxa de amostragem** — quantas amostras por segundo.
**Canal** — fluxo independente (2 = estéreo). **Payload** — o conteúdo útil do pacote. **Bus** — canal por onde a
pipeline manda mensagens (erro, fim do fluxo). **Latência** — atraso entre o som sair e ser tocado.
**Jitter** — variação no tempo de chegada dos pacotes.
