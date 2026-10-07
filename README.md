# Atividade GStreamer – Vídeo H.264 + Áudio PCM

Aplicação em C que reproduz um vídeo H.264 (`video.mp4`) aplicando duas modificações no vídeo (5 fps e escala de cinza) e permitindo escolher entre duas configurações de áudio PCM (A ou B).

## Arquivos da pasta

| Arquivo | Função |
|---|---|
| `pipeline.c` | Código-fonte da aplicação (monta e executa a pipeline GStreamer) |
| `pipeline.exe` | Executável gerado pela compilação do `pipeline.c` |
| `executar.bat` | Script que compila e executa o programa |
| `video.mp4` | Mídia de entrada: vídeo H.264 (High Profile, 1280x720, 30 fps) + áudio AAC (44,1 kHz, estéreo), 6,2 s |
| `Sem título-2026-09-30-2133.png` | Diagrama da pipeline de vídeo |
| `pipeline_audio.excalidraw` | Diagrama da pipeline de áudio |
| `README.md` | Este arquivo |

## Como executar

```
executar.bat        -> configuração A (padrão)
executar.bat B      -> configuração B
```

Ou, depois de compilado: `pipeline.exe A` / `pipeline.exe B`.

---

## `video.mp4`

Arquivo de entrada. Contém dois fluxos dentro de um contêiner QuickTime/MP4:

- **Vídeo:** H.264 (MPEG-4 AVC), 1280x720, 30 fps. É o requisito de vídeo H.264 da atividade.
- **Áudio:** AAC, 44100 Hz, 2 canais. Não é PCM, então o GStreamer o decodifica para PCM (`audio/x-raw`) e a pipeline trabalha em cima desse PCM.

O arquivo precisa estar na mesma pasta do executável, porque o código o procura pelo nome relativo.

---

## `executar.bat`

Script de automação para Windows. Linha a linha:

| Linha | O que faz |
|---|---|
| `@echo off` | Não mostra cada comando do script no terminal |
| `cd /d "%~dp0"` | Entra na pasta onde o `.bat` está (para achar `pipeline.c` e `video.mp4` de qualquer lugar) |
| `echo ...` | Mensagens de cabeçalho |
| `set "PATH=...gstreamer...;...mingw64...;%PATH%"` | Adiciona ao PATH a pasta do GStreamer (DLLs) e a do compilador `gcc` (MinGW) |
| `taskkill /f /im pipeline.exe >nul 2>&1` | Encerra um `pipeline.exe` antigo ainda aberto, senão o `gcc` não consegue sobrescrever o arquivo. A saída é descartada |
| `gcc pipeline.c -I... -L... -l... -o pipeline.exe` | Compila. `-I` aponta onde ficam os headers do GStreamer e GLib; `-L` onde ficam as bibliotecas; `-lgstreamer-1.0 -lgobject-2.0 -lglib-2.0` liga as bibliotecas necessárias; `-o` define o nome do executável |
| `if %ERRORLEVEL% NEQ 0 (...)` | Se a compilação falhou, mostra erro, pausa e sai |
| `echo [+] Compilado...` | Mensagem de sucesso |
| `.\pipeline.exe %*` | Executa o programa repassando os argumentos do `.bat` (`%*`). É isso que permite `executar.bat B` |
| `pause` | Mantém a janela aberta no final para ler as mensagens |

---

## `pipeline.c`

### Visão geral
O programa monta **uma única pipeline** com dois ramos que saem do mesmo decodificador:

```
                     ┌─ videoconvert ! videorate(5 fps) ! videoconvert(GRAY8) ! videoconvert ! autovideosink
video.mp4 ─ uridecodebin
                     └─ queue ! audioconvert ! audioresample ! [caps PCM A/B] ! audioconvert ! audioresample ! autoaudiosink
```

Justificativa: usando uma pipeline só, o GStreamer sincroniza áudio e vídeo pelo mesmo relógio e termina os dois ao mesmo tempo (EOS).

### Linha a linha

**Linha 1 – `#include <gst/gst.h>`**
Importa a API principal do GStreamer (também traz os tipos da GLib como `gchar` e `GError`).

**Linha 3 – `int main(int argc, char *argv[])`**
Função principal. `argc`/`argv` recebem os argumentos de linha de comando (aqui, `A` ou `B`).

**Linhas 5–11 – variáveis**
- `pipeline`: a pipeline completa.
- `bus`: barramento de mensagens da pipeline (erros, fim de fluxo etc.).
- `msg`: mensagem recebida do bus.
- `error`: guarda detalhes de erros.
- `video_uri`: caminho do vídeo no formato URI (`file:///...`).
- `pipeline_str`: texto que descreve a pipeline.
- `audio_caps`: texto com a configuração PCM escolhida.

**Linha 13 – `gst_init(&argc, &argv)`**
Inicializa o GStreamer. Obrigatório antes de usar qualquer função dele. Também consome argumentos próprios do GStreamer.

**Linhas 15–19 – escolha da configuração PCM**
```c
if (argc > 1 && argv[1][0] == 'B')
    audio_caps = "audio/x-raw,rate=8000,format=U8,channels=1";
else
    audio_caps = "audio/x-raw,rate=44100,format=S16LE,channels=2";
```
Se o usuário passou `B`, usa a configuração B; senão (nada passado ou `A`), usa a A. O texto é uma *caps* (descrição de formato) do GStreamer:
- `audio/x-raw` = áudio PCM sem compressão.
- `rate` = taxa de amostragem (amostras por segundo).
- `format` = formato da amostra: `S16LE` é inteiro de 16 bits com sinal, little-endian; `U8` é inteiro de 8 bits sem sinal.
- `channels` = número de canais (2 = estéreo, 1 = mono).

| | Taxa | Formato | Canais |
|---|---|---|---|
| A | 44100 Hz | S16LE (16 bits) | 2 |
| B | 8000 Hz | U8 (8 bits) | 1 |

**Linha 22 – `gst_filename_to_uri("video.mp4", NULL)`**
Converte o nome do arquivo em URI absoluta (`file:///C:/.../video.mp4`), que é o que o `uridecodebin` exige.

**Linhas 24–32 – `g_strdup_printf(...)`: monta o texto da pipeline**
Funciona como `printf`, mas devolve uma string alocada. Os `%s` são substituídos por `video_uri` e `audio_caps`. Elementos separados por `!` são ligados em sequência.

Ramo comum e vídeo:
- `uridecodebin name=d uri="..."`: abre o arquivo, separa os fluxos (demux) e decodifica automaticamente o H.264 para vídeo bruto e o AAC para PCM. `name=d` dá um apelido para poder reutilizá-lo no ramo de áudio.
- `videoconvert`: converte formatos de pixel (necessário entre elementos de vídeo).
- `videorate ! video/x-raw,framerate=5/1`: reduz de 30 fps para 5 fps (o filtro de caps força o resultado).
- `videoconvert ! video/x-raw,format=GRAY8`: converte colorido para escala de cinza (8 bits por pixel).
- `videoconvert ! autovideosink`: converte para algo que a janela aceite e exibe o vídeo (`autovideosink` escolhe o sink adequado ao sistema).

Ramo de áudio (a parte adicionada):
- `d.`: volta ao `uridecodebin` apelidado `d` e liga o pad de áudio dele ao que vem depois (ramo paralelo ao do vídeo).
- `queue`: buffer próprio para o ramo de áudio, evitando que um ramo bloqueie o outro.
- `audioconvert`: converte o formato das amostras (ex.: float para inteiro).
- `volume volume=10`: amplifica o áudio 10 vezes (+20 dB). O áudio do `video.mp4` é muito baixo (cerca de -38 dBFS RMS, praticamente inaudível); com o ganho fica em cerca de -18 dBFS sem distorcer. Fica antes da configuração PCM para que a quantização de 8 bits (B) aja sobre um sinal com amplitude razoável.
- `audioconvert` (segundo): o `volume` só aceita alguns formatos, então este converte de novo antes do `audioresample` e das caps PCM.
- `audioresample`: converte a taxa de amostragem.
- `%s` (caps PCM): aplica a configuração A ou B. É aqui que o áudio realmente muda de taxa, profundidade e canais.
- `audioconvert ! audioresample`: segundo par, **necessário**: a placa de som no Windows (WASAPI) não aceita direto 8 bits/8 kHz nem S16LE, e sem isso a pipeline travava com erro "stream in the wrong format". Ele reconverte para o formato nativo da placa **depois** da degradação, então a diferença de qualidade da configuração continua audível.
- `autoaudiosink`: reproduz o áudio no dispositivo padrão.

**Linha 34 – `gst_parse_launch(pipeline_str, &error)`**
Transforma o texto em uma pipeline real (cria os elementos e liga). Se algo estiver errado, devolve erro em `error`.

**Linhas 36–40 – tratamento de erro na criação**
Se `error` foi preenchido (elemento inexistente, sintaxe errada etc.), imprime a mensagem em `stderr`, libera o erro e termina com código 1.

**Linha 43 – `gst_element_set_state(pipeline, GST_STATE_PLAYING)`**
Coloca a pipeline em execução. A partir daqui o GStreamer roda em threads próprias.

**Linha 44 – `g_print(...)`**
Informa qual configuração PCM está em uso.

**Linhas 47–49 – espera de mensagens**
- `gst_element_get_bus` obtém o bus da pipeline.
- `gst_bus_timed_pop_filtered(..., GST_CLOCK_TIME_NONE, ERROR | EOS)` bloqueia o programa (sem limite de tempo) até chegar uma mensagem de **erro** ou de **fim de fluxo (EOS)**. Outras mensagens são ignoradas.

**Linhas 51–57 – tratamento do resultado**
- Se a mensagem for erro: extrai o texto com `gst_message_parse_error` e o imprime.
- Caso contrário (EOS): imprime "Fim do video."

**Linha 59 – `return 0`**
Encerra o programa.

### Observação
O código original não libera `msg`, `bus` nem muda a pipeline para `GST_STATE_NULL` no final. Isso foi mantido como estava porque o programa termina logo em seguida e o sistema libera tudo; não afeta o funcionamento.

---

## Diferenças entre as configurações PCM

| | A | B |
|---|---|---|
| Taxa de amostragem | 44100 Hz | 8000 Hz |
| Profundidade | 16 bits (65.536 níveis) | 8 bits (256 níveis) |
| Canais | 2 (estéreo) | 1 (mono) |
| Taxa de dados | 44100 × 2 × 2 = 176.400 B/s | 8000 × 1 × 1 = 8.000 B/s |

- **Taxa de amostragem (Nyquist):** só é possível representar frequências até metade da taxa. A: até ~22 kHz (toda a faixa audível). B: até ~4 kHz, então perde os agudos e o som fica abafado, como em telefone.
- **Profundidade de bits:** menos bits = menos níveis de amplitude = mais ruído de quantização (chiado). B tem ruído bem mais audível.
- **Canais:** B é mono e perde a separação esquerda/direita.
- **Tamanho:** B usa ~22 vezes menos dados por segundo que A.

## Diagramas

- `Sem título-2026-09-30-2133.png`: pipeline de vídeo.
- `pipeline_audio.excalidraw`: pipeline de áudio (abrir em excalidraw.com).
