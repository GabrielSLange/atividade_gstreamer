# Roteiro da apresentação — 4 partes

Cada integrante fala cerca de **3 minutos** (total ≈ 12 min). Escrevam os nomes:

| Parte | Tema | Quem fala |
|---|---|---|
| 1 | Visão geral e demonstração ao vivo | _______________ |
| 2 | Transmissor | _______________ |
| 3 | Receptor e jitter buffer | _______________ |
| 4 | Perdas, experimentos e conclusão | _______________ |

**Regra da demonstração:** o **transmissor fica rodando o tempo todo** (Terminal B). Só o **receptor** (Terminal A)
é reiniciado com `Ctrl+C` e um novo comando a cada parte. O UDP não tem conexão, então isso funciona.

---

## Antes de começar (todos)

- [ ] `build.bat` rodou sem erro e a pasta `bin\` está atualizada.
- [ ] **Testar o som antes**: iniciar receptor e transmissor e ouvir a escala cromática. Isso ainda **não foi ouvido** por ninguém.
- [ ] Dois terminais abertos na pasta do projeto: **A** (receptor) e **B** (transmissor). Um terceiro (**C**) só se forem fazer a demonstração **opcional** de porta ocupada (parte 3).
- [ ] Volume do PC moderado e nada mais usando a porta 5000.
- [ ] Abertos e prontos: `diagramas/pipeline_transmissor.png`, `diagramas/pipeline_receptor.png`, `EXPERIMENTOS.md` e os arquivos `src/transmissor.c` e `src/receptor.c` no editor.
- [ ] **Preencher as observações auditivas** da seção 5 do `EXPERIMENTOS.md` (só quem ouve consegue).

### Onde cada item da demonstração do enunciado é coberto

| Item pedido pelo professor | Parte |
|---|---|
| 1. A aplicação transmitindo e reproduzindo áudio | 1 |
| 2. A execução do transmissor e do receptor | 1 |
| 3. A alteração dos parâmetros de latência | 3 |
| 4. Os mecanismos para identificar ou tratar perdas | 4 |
| 5. Os diagramas das pipelines | 2 e 3 |
| 6. Os principais trechos de código | 2, 3 e 4 |

---

## Parte 1 — Visão geral e demonstração ao vivo

**Objetivo:** explicar o que o projeto faz e mostrar que funciona.

**O que falar**
- O projeto manda áudio de um programa para outro pela rede: um **transmissor** e um **receptor**, ambos em C com GStreamer.
- Usamos **UDP** (e não TCP) porque em tempo real chegar tarde é pior que perder um pedacinho: o TCP retransmite e atrasa tudo.
- O UDP não dá ordem nem tempo aos pacotes; por isso usamos **RTP** por cima, com número de sequência, timestamp e payload type (96).
- O áudio é **L16**: PCM sem compressão, 44.100 Hz, 16 bits, estéreo ≈ 1,4 Mbps.

**Demonstração**
1. Terminal A: `bin\receptor.exe`
2. Terminal B: `bin\transmissor.exe` (deixar rodando até o fim)
3. Todos ouvem a escala cromática (do dó ao dó agudo e de volta). Mostrar que o receptor imprime `[STATS]` a cada segundo, com perdidos = 0.

**Passa a palavra:** "Agora o [nome] vai mostrar como o transmissor está montado."

**Prováveis perguntas:** por que UDP e não TCP? O que o RTP acrescenta? Qual a taxa de dados?

---

## Parte 2 — Transmissor

**Objetivo:** mostrar a pipeline e o código do transmissor e justificar as decisões.

**Mostrar:** `diagramas/pipeline_transmissor.png` e [src/transmissor.c](src/transmissor.c).

**O que falar**
- Pipeline: `audiotestsrc → audioconvert → audioresample → capsfilter → rtpL16pay → udpsink`.
- `audiotestsrc` gera o som: a **escala cromática** (o programa troca a propriedade `freq` a cada 0,4 s) ou um tom fixo de 440 Hz; `autoaudiosrc` pega o microfone. `is-live=TRUE` gera em tempo real, como um microfone.
- `audioconvert` e `audioresample` adaptam qualquer fonte ao formato certo. O `capsfilter` força **S16BE, 44.100 Hz, 2 canais**: o RTP usa a ordem de bytes de rede (big-endian) e o `rtpL16pay` só aceita esse formato.
- `rtpL16pay` (pt=96) encapsula em pacotes RTP e `udpsink` envia para `host:porta`, que são argumentos do programa.
- Montamos as pipelines **elemento a elemento**, e não com `gst_parse_launch`, porque o enunciado pede construção programática e porque precisamos de acesso aos elementos (isso pesa na parte 3).

**Trechos de código para mostrar** (em `src/transmissor.c`)

| Linhas | O que é |
|---|---|
| 35–42 | Cria os elementos (`gst_element_factory_make`) |
| 49–60 | Configura fonte, caps, `pt`, `host` e `port` (`g_object_set`) |
| 62–69 | Adiciona e liga (`gst_element_link_many`) |
| 70–87 | Executa (`PLAYING`), espera erros no *bus* e troca a nota (`freq`) a cada 400 ms |

**Dificuldade para comentar:** o `udpsink` não avisa se o host não existe (ele só não envia nada); o programa tem que conferir o IP.

**Passa a palavra:** "Agora o [nome] mostra o receptor e o jitter buffer."

**Prováveis perguntas:** por que S16BE? por que o `capsfilter`? por que `audioconvert` e `audioresample`? por que `pt=96`?

---

## Parte 3 — Receptor e jitter buffer

**Objetivo:** explicar o receptor, o jitter buffer e **demonstrar a mudança de latência**.

**Mostrar:** `diagramas/pipeline_receptor.png` e [src/receptor.c](src/receptor.c).

**O que falar**
- Pipeline: `udpsrc → netsim → rtpjitterbuffer → rtpL16depay → audioconvert → audioresample → autoaudiosink`.
- O `udpsrc` entrega só bytes. As **caps** (`application/x-rtp, clock-rate=44100, encoding-name=L16, channels=2, payload=96`) dizem que são pacotes RTP de L16; precisam ser iguais às do transmissor.
- **Jitter** é a variação no tempo de chegada dos pacotes. O `rtpjitterbuffer` segura os pacotes por `latency` ms, põe em ordem pelo número de sequência e entrega em ritmo constante.
- O `netsim` simula perda e atraso para testarmos (com 0, não altera nada); ele aparece na parte 4.
- **Dificuldade:** no Windows, o `udpsrc` por padrão deixa **dois receptores escutarem a mesma porta sem erro**, e o segundo nunca recebe nada. Colocamos `reuse=FALSE` e a porta ocupada virou um erro claro.

**Trechos de código para mostrar** (em `src/receptor.c`)

| Linhas | O que é |
|---|---|
| 79–85 | Caps do `udpsrc` e `reuse=FALSE` |
| 94 | `latency` e `do-lost` do jitter buffer |
| 98 | Ligação dos elementos |

**Demonstração (item 3 do enunciado)**
1. Terminal A: `Ctrl+C` e depois `bin\receptor.exe 5000 20`
2. `Ctrl+C` e depois `bin\receptor.exe 5000 1000`
3. O que observar: com 20 ms o som começa quase na hora; com 1000 ms ele **demora cerca de 1 s para começar**. Medimos que o jitter buffer retém o primeiro pacote por ≈ 31, 212 e 1013 ms para as latências 20, 200 e 1000 ms.
4. (Opcional, tratamento de erros) Terminal C: com o receptor rodando, abrir outro `bin\receptor.exe`. Deve dar o erro de porta ocupada, com código 1.

**Passa a palavra:** "Mas com a rede limpa as duas latências soam iguais. O [nome] vai mostrar o que muda quando a rede é ruim."

**Prováveis perguntas:** o que é jitter? o que é `latency`? o que acontece se a latência for menor que o jitter? por que `reuse=FALSE`?

---

## Parte 4 — Perdas, experimentos e conclusão

**Objetivo:** mostrar o diagnóstico de perdas, os experimentos e fechar.

**Mostrar:** [src/receptor.c](src/receptor.c) e [EXPERIMENTOS.md](EXPERIMENTOS.md).

**O que falar**
- Usamos **dois mecanismos do `rtpjitterbuffer`**:
  - o evento `GstRTPPacketLost` (com `do-lost=TRUE`), capturado por um *probe*, que imprime `[PERDA]`;
  - a propriedade `stats`, lida a cada segundo, que imprime `[STATS]` com entregues, perdidos, atrasados e duplicados.
- **Perdido** = não chegou a tempo. **Atrasado** = chegou depois de já ter sido dado como perdido e foi descartado.
- Para testar sem uma rede ruim, o `netsim` descarta e atrasa pacotes: `receptor [porta] [latencia] [perda_%] [atraso_max_ms]`.
- Não implementamos recuperação de pacotes, como o enunciado permite; só detectamos e contamos.

**Trechos de código para mostrar** (em `src/receptor.c`)

| Linhas | O que é |
|---|---|
| 24–35 | Probe que detecta o evento `GstRTPPacketLost` |
| 37 em diante | `imprime_estatisticas`: lê a propriedade `stats` |
| 89–94 | Configuração do `netsim` e do jitter buffer |
| 104–121 | Registro do probe e laço que imprime as estatísticas a cada 1 s |

**Demonstração**
1. `bin\receptor.exe 5000 200 10 0`: 10% de perda. Aparecem linhas `[PERDA]` e o `[STATS]` mostra perdidos crescendo (≈ 13 por segundo); o som fica picotado.
2. `bin\receptor.exe 5000 20 0 100`: jitter de até 100 ms com buffer de 20 ms. Muitos pacotes viram perda.
3. `bin\receptor.exe 5000 200 0 100`: o **mesmo jitter** com buffer de 200 ms. Perdidos = 0.

**Resultados para citar** (do `EXPERIMENTOS.md`, 10 s por execução, jitter de até 80 ms)

| Latência | Perdidos | % perdido |
|---|---|---|
| 20 ms | 636 | 50,2% |
| 50 ms | 549 | 43,5% |
| 200 ms | 0 | 0% |
| 1000 ms | 0 | 0% |

Com **5% de perda**, todas as latências perderam o mesmo (≈ 60 pacotes): **o jitter buffer não recupera pacotes que nunca chegaram.**

**Conclusão (frase final):** o jitter buffer resolve o *jitter*, ao custo de um atraso igual à latência; ele não resolve a *perda*, que o GStreamer só nos deixa diagnosticar. A latência é um compromisso entre robustez e interatividade.

**Limitações a assumir com honestidade**
- Cada combinação foi executada **uma vez**; a simulação é aleatória e só as diferenças grandes são conclusivas.
- Tudo foi em `127.0.0.1` com o `netsim`: **não testamos em dois computadores** nem em rede real.
- Os números foram medidos no áudio antes da placa de som; o que se ouve está nas observações auditivas.

**Prováveis perguntas:** como detectam perdas? qual a diferença entre perdido e atrasado? como testaram sem rede ruim? por que a latência não resolve a perda?

---

## Se algo der errado

| Problema | O que fazer |
|---|---|
| Sem som | Continuar mostrando o `[STATS]` e os diagramas; verificar o dispositivo de saída padrão do Windows. |
| Erro de porta ocupada | Fechar o receptor antigo (ou usar outra porta nos dois programas). |
| Receptor mudo | Conferir se o transmissor está rodando e se a porta é a mesma. |
| `.exe` não abre | Rodar `build.bat` de novo. |

Mais perguntas e respostas prováveis do professor estão na seção 10 do [ESTUDO.md](ESTUDO.md).
