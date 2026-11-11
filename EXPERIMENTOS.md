# Registro dos experimentos

Objetivo: observar como a **latência do jitter buffer** (`rtpjitterbuffer`) influencia a reprodução em
diferentes condições de rede, e como o receptor diagnostica perdas.

---

## 1. Ambiente e método

| Item | Valor |
|---|---|
| Sistema | Windows 10 |
| GStreamer | 1.28.6 |
| Rede | localhost (`127.0.0.1`); a perda e o atraso são **simulados** pelo elemento `netsim` dentro do receptor |
| Fonte de áudio | tom senoidal de 440 Hz (`audiotestsrc`), L16, 44.100 Hz, estéreo, `pt=96` |
| Duração | 10 s de transmissão por execução |
| Repetições | **1** por combinação |
| Script | [experimentos/rodar_experimentos.ps1](experimentos/rodar_experimentos.ps1) |
| Resultados brutos | [experimentos/resultados.csv](experimentos/resultados.csv) |

O script usa uma **cópia do receptor** em que a saída de áudio é gravada em arquivo (em vez de tocar na placa
de som) e o programa termina sozinho. O restante da pipeline é idêntico ao de `src/receptor.c`.

### O que é medido

| Coluna | Significado |
|---|---|
| Entregues / Perdidos / Atrasados | Estatísticas do `rtpjitterbuffer` (`num-pushed`, `num-lost`, `num-late`), última linha `[STATS]` |
| Eventos `[PERDA]` | Quantos eventos `GstRTPPacketLost` o receptor recebeu |
| % perdido | `perdidos / (entregues + perdidos)` |
| Áudio (s) | Duração do áudio que chegou à saída do receptor (≈ 9,8 s se nada se perdeu) |
| Cliques | Descontinuidades na onda de 440 Hz do áudio de saída (cada trecho de áudio que falta gera uma) |

Como o sinal é um seno puro, cada amostra deve seguir `x[n] = 2·cos(w)·x[n-1] − x[n-2]`. Um desvio grande
(> 20) indica uma emenda no áudio, que seria ouvida como um "clique". Vários pacotes perdidos em sequência
geram **um** clique só.

---

## 2. Parâmetros

Uso do receptor: `receptor [porta] [latencia_ms] [perda_%] [atraso_max_ms]`.

| Cenário | Perda | Atraso aleatório (uniforme) |
|---|---|---|
| Rede limpa | 0% | 0 ms |
| Perda 5% | 5% | 0 ms |
| Jitter até 80 ms | 0% | 0 a 80 ms |
| Perda 5% + jitter 80 ms | 5% | 0 a 80 ms |

Cada cenário foi executado com **4 latências**: 20 ms, 50 ms, 200 ms e 1000 ms (16 execuções).

---

## 3. Resultados

### Rede limpa

| Latência | Entregues | Perdidos | Atrasados | Eventos | % perdido | Áudio (s) | Cliques |
|---|---|---|---|---|---|---|---|
| 20 ms | 1271 | 0 | 0 | 0 | 0% | 9,84 | 0 |
| 50 ms | 1268 | 0 | 0 | 0 | 0% | 9,81 | 0 |
| 200 ms | 1266 | 0 | 0 | 0 | 0% | 9,80 | 0 |
| 1000 ms | 1266 | 0 | 0 | 0 | 0% | 9,80 | 0 |

### Perda de 5%

| Latência | Entregues | Perdidos | Atrasados | Eventos | % perdido | Áudio (s) | Cliques |
|---|---|---|---|---|---|---|---|
| 20 ms | 1199 | 59 | 0 | 59 | 4,7% | 9,28 | 58 |
| 50 ms | 1205 | 62 | 0 | 62 | 4,9% | 9,33 | 58 |
| 200 ms | 1204 | 61 | 0 | 61 | 4,8% | 9,32 | 60 |
| 1000 ms | 1204 | 63 | 0 | 63 | 5,0% | 9,32 | 62 |

### Jitter de até 80 ms

| Latência | Entregues | Perdidos | Atrasados | Eventos | % perdido | Áudio (s) | Cliques |
|---|---|---|---|---|---|---|---|
| 20 ms | 630 | 636 | 602 | 571 | 50,2% | 4,87 | 333 |
| 50 ms | 714 | 549 | 476 | 549 | 43,5% | 5,52 | 321 |
| 200 ms | 1258 | 0 | 0 | 0 | 0% | 9,74 | 0 |
| 1000 ms | 1276 | 0 | 0 | 0 | 0% | 9,88 | 0 |

### Perda de 5% + jitter de até 80 ms

| Latência | Entregues | Perdidos | Atrasados | Eventos | % perdido | Áudio (s) | Cliques |
|---|---|---|---|---|---|---|---|
| 20 ms | 615 | 649 | 589 | 594 | 51,3% | 4,76 | 342 |
| 50 ms | 664 | 602 | 470 | 602 | 47,6% | 5,14 | 326 |
| 200 ms | 1187 | 79 | 0 | 79 | 6,2% | 9,19 | 73 |
| 1000 ms | 1205 | 57 | 0 | 57 | 4,5% | 9,33 | 53 |

---

## 4. Análise

### Como o buffer influencia a reprodução

- **Rede limpa:** a latência não muda o áudio (0 perdas, 0 cliques). Ela só soma **atraso**: numa medição
  pontual feita durante o desenvolvimento, o primeiro pacote foi retido por ≈ 31, 212 e 1013 ms para as
  latências de 20, 200 e 1000 ms (a latência configurada + ~12 ms). Essa medição não faz parte do script.
- **Jitter de até 80 ms:** há um **limiar** entre 50 e 200 ms. Com latência menor que o jitter máximo, boa
  parte dos pacotes chega depois do prazo: 43% a 50% viram perda, só metade do áudio chega à saída
  (≈ 5 s de 10 s) e há mais de 300 cliques. Com 200 ms ou mais, o jitter é absorvido por completo
  (0 perdas, 0 cliques).
- **Perda de 5%:** o resultado é praticamente o mesmo em todas as latências (59 a 63 pacotes perdidos).
  **Aumentar o buffer não recupera pacotes que nunca chegaram.**
- **Perda + jitter:** os efeitos se somam. Com 200 e 1000 ms sobram só as perdas "reais" (57 a 79 pacotes,
  perto dos 5% esperados, ≈ 63); com 20 e 50 ms o jitter domina.

### Diagnóstico de perdas

- Com perdas isoladas (cenário de 5%), há **um clique por pacote perdido** (58 a 62 cliques para 59 a 63
  perdidos), e o áudio encurta em ≈ 0,5 s (9,3 s contra 9,8 s), o que corresponde a 5% de 10 s.
- Com perdas em sequência (jitter forte), os cliques são menos que os perdidos (333 contra 636), pois uma
  sequência de pacotes perdidos gera uma emenda só.
- Os eventos `[PERDA]` nem sempre igualam os perdidos (571 contra 636 em um caso). Provavelmente alguns eventos
  cobrem mais de um pacote seguido, mas isso **não foi confirmado**.
- `atrasados` mostra os pacotes que chegaram **depois** de já terem sido dados como perdidos, e portanto foram
  descartados. Só aparece quando a latência é menor que o jitter.

### Limitações da abordagem (jitter buffer)

1. **Compromisso latência × robustez:** a latência precisa ficar acima do jitter máximo da rede, e o preço é
   atraso fixo no áudio. Com 1000 ms o áudio é robusto, mas inadequado para conversa em tempo real.
2. **Não recupera perdas:** só reordena e espera o pacote até o fim da latência.
3. **Latência fixa:** o valor é escolhido na inicialização e não se adapta à rede.
4. **Escolher o valor exige conhecer a rede:** abaixo do jitter real o áudio degrada muito (neste teste,
   de 0% para ~50% de perda).

---

## 5. Observações auditivas (preencher)

As métricas acima foram medidas no áudio que sai do receptor, **antes** da placa de som. O que se ouve de fato
precisa ser anotado por quem escutar. Para cada linha, rode o receptor real e o transmissor e descreva o que
ouviu (ex.: cortes, estalos, atraso perceptível, som contínuo).

```
bin\receptor.exe 5000 <latencia> <perda_%> <atraso_ms>
bin\transmissor.exe
```

| Configuração (`latência perda atraso`) | O que foi ouvido |
|---|---|
| `20 0 0` | _preencher_ |
| `1000 0 0` | _preencher_ |
| `200 5 0` | _preencher_ |
| `20 0 80` | _preencher_ |
| `200 0 80` | _preencher_ |
| `1000 5 80` | _preencher_ |

---

## 6. Limitações deste experimento

- **Uma repetição por combinação.** A simulação é aleatória: nos cenários de 5% de perda houve de 57 a 79
  perdidos (esperado ≈ 63), então diferenças pequenas entre latências não são significativas. Conclusões
  só se sustentam para diferenças grandes, como as do jitter.
- **Localhost com `netsim`:** não é uma rede real nem foi testado em dois computadores. O atraso simulado é
  uniforme de 0 a 80 ms; redes reais têm outras distribuições.
- **Medido antes da placa de som:** um dispositivo de áudio real pode preencher lacunas com silêncio, em vez
  de emendar o sinal como o arquivo de saída, o que muda o aspecto auditivo.
- O detector de cliques supõe o seno puro de 440 Hz e só serve para o tom de teste.

---

## 7. Conclusão

O streaming de áudio por RTP/UDP funciona bem em rede limpa, mas sem proteção o UDP expõe o áudio direto às
variações da rede. O `rtpjitterbuffer` resolve o problema do **jitter**: com latência acima do atraso máximo
(200 ms ou mais para jitter de 80 ms) o áudio ficou íntegro, enquanto com latência abaixo disso (20 e 50 ms)
cerca de metade dos pacotes foi perdida. O custo é um atraso fixo igual à latência configurada.

O buffer **não resolve a perda real de pacotes**: com 5% de perda, todas as latências perderam o mesmo número de
pacotes, e cada perda gerou uma emenda no áudio. O que o GStreamer oferece aqui é **diagnóstico**: o evento
`GstRTPPacketLost` e a propriedade `stats` do `rtpjitterbuffer` permitem identificar e contar as perdas, os
atrasos e as duplicações, como foi feito no receptor.

Na prática, a latência do jitter buffer é um compromisso: grande o suficiente para absorver o jitter da rede, e
pequena o suficiente para a aplicação continuar interativa.

---

## 8. Como reproduzir

```
build.bat
powershell -ExecutionPolicy Bypass -File experimentos\rodar_experimentos.ps1
```

Leva cerca de 4 minutos. Para um teste rápido: `-Duracao 4 -Latencias 200`. O resultado é gravado em
`experimentos\resultados.csv`.
