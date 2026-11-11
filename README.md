# Streaming de áudio com GStreamer (RTP/UDP)

Disciplina: Sistemas Multimídia (CMP 1103) — PUC Goiás.

Aplicação em C com dois programas: um **transmissor**, que envia áudio por UDP/RTP, e um **receptor**, que
recebe, compensa o jitter com `rtpjitterbuffer`, diagnostica perdas de pacotes e toca o som. As pipelines são
montadas elemento a elemento (`gst_element_factory_make` + `gst_element_link_many`), sem `gst_parse_launch`.

```
 TRANSMISSOR                                                RECEPTOR
 fonte ─► audioconvert ─► audioresample ─► capsfilter       udpsrc ─► netsim ─► rtpjitterbuffer ─► rtpL16depay
       ─► rtpL16pay ─► udpsink  ═══ UDP / RTP ═══►                ─► audioconvert ─► audioresample ─► autoaudiosink
```

## Estrutura do projeto

```
src/transmissor.c          código do transmissor
src/receptor.c             código do receptor (jitter buffer, diagnóstico de perdas, simulação de rede)
build.bat                  compila os dois programas para bin/
bin/                       executáveis gerados (transmissor.exe e receptor.exe)
diagramas/                 diagramas das pipelines (.excalidraw editável e .png)
experimentos/              script dos experimentos e resultados.csv
EXPERIMENTOS.md            registro dos experimentos, análise e conclusão
ESTUDO.md                  guia de estudo para a apresentação
```

## Requisitos

- **Windows** com o **GStreamer 1.x MinGW 64-bit** instalado (testado com a 1.28.6, instalação completa, que traz os
  plugins `rtpmanager` e `netsim`) e **gcc** (MinGW-w64).
- O instalador do GStreamer adiciona a pasta `bin` ao `PATH`. Se um `.exe` reclamar de DLL não encontrada, abra um
  terminal novo (ou adicione `C:\Program Files\gstreamer\1.0\mingw_x86_64\bin` ao `PATH`).
- Para conferir se os elementos existem: `gst-inspect-1.0 rtpjitterbuffer` (e o mesmo para `netsim`).

## Compilação

```
.\build.bat
```

Gera `bin\transmissor.exe` e `bin\receptor.exe`. Se o GStreamer ou o MinGW estiverem em outra pasta, edite as
variáveis `GST_DIR` e `MINGW_DIR` no topo do `build.bat`.

> Linux/macOS: **não testado**. Em princípio basta
> `gcc src/receptor.c -o bin/receptor $(pkg-config --cflags --libs gstreamer-1.0)` (e o mesmo para o transmissor).

## Execução

Abra **dois terminais** na pasta do projeto e inicie **o receptor primeiro**.

```
bin\receptor.exe
bin\transmissor.exe
```

O receptor toca um tom contínuo de 440 Hz. `Ctrl+C` encerra qualquer um dos programas.

### Argumentos

| Programa | Uso | Padrão |
|---|---|---|
| Transmissor | `transmissor [host] [porta] [teste\|mic]` | `127.0.0.1 5000 teste` |
| Receptor | `receptor [porta] [latencia_ms] [perda_%] [atraso_max_ms]` | `5000 200 0 0` |

- **`teste`**: tom senoidal de 440 Hz gerado pelo GStreamer. **`mic`**: microfone padrão do sistema.
  (Com `mic` no mesmo computador, use fones para evitar microfonia.)
- **`latencia_ms`**: latência do `rtpjitterbuffer`, ou seja, quanto tempo ele segura os pacotes.
- **`perda_%`** e **`atraso_max_ms`**: simulam uma rede ruim com o elemento `netsim` (descarta essa porcentagem
  dos pacotes e atrasa cada pacote de 0 a `atraso_max_ms`). Com `0 0`, a rede não é alterada.

### Exemplos

```
bin\receptor.exe 5000 20            jitter buffer de 20 ms
bin\receptor.exe 5000 1000          jitter buffer de 1000 ms
bin\receptor.exe 5000 200 10 0      simula 10% de perda de pacotes
bin\receptor.exe 5000 20 0 100      simula atraso de até 100 ms com buffer pequeno (vira perda)
bin\receptor.exe 5000 200 0 100     o mesmo jitter, absorvido pelo buffer maior
bin\transmissor.exe 127.0.0.1 5000 mic
```

### Em dois computadores

1. No receptor, descubra o IP (`ipconfig`) e inicie `bin\receptor.exe`. Permita a porta no Firewall do Windows se
   ele perguntar.
2. No transmissor, passe esse IP: `bin\transmissor.exe 192.168.0.20 5000`.

(Não testado em dois computadores: todos os testes foram em `127.0.0.1`.)

### O que aparece no console

Transmissor:

```
Transmitindo tom de teste para 127.0.0.1:5000 (Ctrl+C para sair)
```

Receptor (uma linha `[STATS]` por segundo; uma linha `[PERDA]` para cada pacote perdido):

```
Recebendo na porta UDP 5000 | jitter buffer: 200 ms | rede simulada: perda 10%, atraso ate 0 ms
[PERDA] pacote seq=4091 nao chegou a tempo
[STATS] entregues=897 perdidos=108 atrasados=0 duplicados=0
```

| Campo | Significado |
|---|---|
| `entregues` | pacotes que o jitter buffer entregou adiante |
| `perdidos` | pacotes dados como perdidos (não chegaram a tempo) |
| `atrasados` | pacotes que chegaram **depois** de já terem sido dados como perdidos (descartados) |
| `duplicados` | pacotes repetidos |

## Como funciona

Os diagramas completos, com os parâmetros de cada elemento, estão em [diagramas/](diagramas/):
[transmissor](diagramas/pipeline_transmissor.png) e [receptor](diagramas/pipeline_receptor.png).

**Transmissor** (`src/transmissor.c`): `audiotestsrc` (ou `autoaudiosrc`) → `audioconvert` → `audioresample` →
`capsfilter` (`audio/x-raw, S16BE, 44100 Hz, 2 canais`) → `rtpL16pay` (`pt=96`) → `udpsink` (`host`, `port`).

**Receptor** (`src/receptor.c`): `udpsrc` (com as caps `application/x-rtp, clock-rate=44100, encoding-name=L16,
channels=2, payload=96`) → `netsim` → `rtpjitterbuffer` (`latency`, `do-lost=TRUE`) → `rtpL16depay` →
`audioconvert` → `audioresample` → `autoaudiosink`.

**Jitter e latência:** o `rtpjitterbuffer` segura os pacotes por `latency` ms, coloca-os em ordem pelo número de
sequência e os entrega em ritmo constante. Latência maior tolera mais variação da rede, mas adiciona atraso.

**Diagnóstico de perdas** (a partir do `rtpjitterbuffer`):
- o evento `GstRTPPacketLost` (habilitado por `do-lost=TRUE`), capturado por um probe no pad de saída, gera as
  linhas `[PERDA]`;
- a propriedade `stats` (`num-pushed`, `num-lost`, `num-late`, `num-duplicates`) é lida a cada 1 s e gera as
  linhas `[STATS]`.

Não há algoritmo próprio de recuperação de pacotes: o objetivo é investigar e usar o que o GStreamer oferece.

## Experimentos

O registro completo (parâmetros, 16 execuções, análise, limitações e conclusão) está em
[EXPERIMENTOS.md](EXPERIMENTOS.md). Para repetir:

```
powershell -ExecutionPolicy Bypass -File experimentos\rodar_experimentos.ps1
```

Leva cerca de 4 minutos e grava `experimentos\resultados.csv`. Resumo: com jitter de até 80 ms, latências de 20 e
50 ms perderam de 43% a 50% dos pacotes e as de 200 e 1000 ms não perderam nenhum; já a perda real de 5% apareceu
igual em todas as latências, pois o jitter buffer não recupera pacotes perdidos.

## Pesquisa e validação

Antes de implementar em C, as pipelines foram investigadas com `gst-inspect-1.0` (propriedades do
`rtpjitterbuffer`, `netsim`, `rtpL16pay`, `udpsrc`) e validadas com `gst-launch-1.0`. Equivalentes de linha de
comando, apenas como referência (a entrega são os programas em C):

```
gst-launch-1.0 audiotestsrc is-live=true volume=0.3 ! audioconvert ! audioresample ! "audio/x-raw,format=S16BE,rate=44100,channels=2" ! rtpL16pay pt=96 ! udpsink host=127.0.0.1 port=5000

gst-launch-1.0 udpsrc port=5000 caps="application/x-rtp,media=audio,clock-rate=44100,encoding-name=L16,channels=2,payload=96" ! rtpjitterbuffer latency=200 do-lost=true ! rtpL16depay ! audioconvert ! audioresample ! autoaudiosink
```

## Problemas comuns

| Sintoma | Causa e solução |
|---|---|
| `bind failed ... uma utilização de cada endereço` | Já há um receptor (ou outro programa) usando a porta. Feche-o ou use outra porta nos dois programas. |
| Receptor mudo | Inicie o receptor **antes** do transmissor e confira se a porta e o IP são os mesmos nos dois. |
| `nao foi possivel criar os elementos` | Falta um plugin do GStreamer. Confira com `gst-inspect-1.0 <elemento>`. |
| DLL não encontrada ao abrir o `.exe` | Abra um terminal novo ou adicione a pasta `bin` do GStreamer ao `PATH`. |
| Transmissor "transmite" mas nada chega | O `udpsink` não reclama de host inexistente: confira o IP digitado. |

## Limitações

- Testado apenas no Windows e em `127.0.0.1`; o atraso e a perda dos experimentos são **simulados** (`netsim`).
- Codec único (L16, sem compressão, ~1,4 Mbps); a latência do jitter buffer é fixa e só muda reiniciando o receptor.
- O transmissor não tem fonte de arquivo; só tom de teste e microfone.
