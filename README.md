# Atividade GStreamer – Vídeo H.264 + Áudio PCM

Aplicação em C que reproduz um vídeo H.264 (`video.mp4`) aplicando três modificações no vídeo (5 fps, resolução 160x90 e escala de cinza) junto com um áudio (`audio.ogg`) convertido para PCM, permitindo escolher entre duas configurações de áudio PCM (A ou B).

São duas fontes separadas: o vídeo vem do `video.mp4` e o áudio vem do `audio.ogg`.

## Arquivos da pasta

| Arquivo | Função |
|---|---|
| `pipeline.c` | Código-fonte da aplicação (monta e executa a pipeline GStreamer) |
| `pipeline.exe` | Executável gerado pela compilação do `pipeline.c` |
| `executar.bat` | Script que compila e executa o programa |
| `video.mp4` | Fonte de vídeo: H.264 (High Profile, 1280x720, 30 fps), 6,27 s. Também tem uma faixa de áudio AAC, que a aplicação não usa |
| `audio.ogg` | Fonte de áudio: Opus, 16 kHz, mono, 8,02 s (decodificado para PCM pela pipeline) |
| `audio.wav` | Não é mais usado pela aplicação (versão anterior, gerada com `audiotestsrc`); pode ser apagado |
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

Fonte de vídeo. Contém dois fluxos dentro de um contêiner QuickTime/MP4:

- **Vídeo:** H.264 (MPEG-4 AVC), 1280x720, 30 fps. É o requisito de vídeo H.264 da atividade.
- **Áudio:** AAC, 44100 Hz, 2 canais. É muito baixo (cerca de -38 dBFS) e não é PCM, por isso a aplicação não o usa; o áudio vem do `audio.ogg`.

O arquivo precisa estar na mesma pasta do executável, porque o código o procura pelo nome relativo.

---

## `audio.ogg`

Fonte de áudio. Contêiner Ogg com áudio **Opus**, 16000 Hz, 16 bits, **mono**, 8,02 s. Opus é comprimido, então o GStreamer o decodifica para PCM (`audio/x-raw`) antes de a pipeline aplicar a configuração A ou B.

Como o áudio de origem já é de 16 kHz e mono, as configurações atuam assim sobre ele:

- **A (44100 Hz, estéreo):** o sinal é reamostrado para cima e o canal único é duplicado em dois. Não aparece informação nova, só muda a representação.
- **B (8000 Hz, 8 bits, mono):** a taxa cai pela metade (16 kHz para 8 kHz, então perde tudo acima de 4 kHz) e a profundidade cai de 16 para 8 bits.

Como o áudio (8,02 s) é mais longo que o vídeo (6,27 s), o vídeo termina primeiro e a janela fica na última imagem até o áudio acabar.

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
O programa monta **uma única pipeline** com duas cadeias independentes, uma por fonte:

```
video.mp4 ─ uridecodebin ! videoconvert ! videorate(5 fps) ! videoscale(160x90) ! videoconvert(GRAY8) ! videoconvert ! autovideosink

audio.ogg ─ filesrc ! decodebin ! audioconvert ! audioresample ! [caps PCM A/B] ! audioconvert ! audioresample ! autoaudiosink
```

Justificativa: usando uma pipeline só, o GStreamer sincroniza áudio e vídeo pelo mesmo relógio e só envia o fim de fluxo (EOS) quando os dois ramos terminam, ou seja, quando o áudio (o mais longo) acaba.

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

**Linhas 24–34 – `g_strdup_printf(...)`: monta o texto da pipeline**
Funciona como `printf`, mas devolve uma string alocada. Os `%s` são substituídos por `video_uri` e `audio_caps`. Elementos separados por `!` são ligados em sequência.

Cadeia de vídeo:
- `uridecodebin uri="..."`: abre o `video.mp4`, separa os fluxos (demux) e decodifica automaticamente o H.264 para vídeo bruto.
- `videoconvert`: converte formatos de pixel (necessário entre elementos de vídeo).
- `videorate ! video/x-raw,framerate=5/1`: reduz de 30 fps para 5 fps (o filtro de caps força o resultado).
- `videoscale ! video/x-raw,width=160,height=90`: reduz a resolução de 1280x720 para 160x90.
- `videoconvert ! video/x-raw,format=GRAY8`: converte colorido para escala de cinza (8 bits por pixel).
- `videoconvert ! autovideosink`: converte para algo que a janela aceite e exibe o vídeo (`autovideosink` escolhe o sink adequado ao sistema).

Cadeia de áudio:
- `filesrc location=audio.ogg`: lê o arquivo de áudio como bytes.
- `decodebin`: detecta o formato (Ogg com Opus), separa o contêiner e decodifica para PCM bruto (`audio/x-raw`).
- `audioconvert`: converte o formato das amostras (ex.: 16 bits para 8 bits).
- `audioresample`: converte a taxa de amostragem.
- `%s` (caps PCM): aplica a configuração A ou B. É aqui que o áudio realmente muda de taxa, profundidade e canais.
- `audioconvert ! audioresample`: segundo par, **necessário**: a placa de som no Windows (WASAPI) não aceita direto 8 bits/8 kHz nem S16LE, e sem isso a pipeline travava com erro "stream in the wrong format". Ele reconverte para o formato nativo da placa **depois** da degradação, então a diferença de qualidade da configuração continua audível.
- `autoaudiosink`: reproduz o áudio no dispositivo padrão.

**Linha 36 – `gst_parse_launch(pipeline_str, &error)`**
Transforma o texto em uma pipeline real (cria os elementos e liga). Se algo estiver errado, devolve erro em `error`.

**Linhas 38–42 – tratamento de erro na criação**
Se `error` foi preenchido (elemento inexistente, sintaxe errada etc.), imprime a mensagem em `stderr`, libera o erro e termina com código 1.

**Linha 45 – `gst_element_set_state(pipeline, GST_STATE_PLAYING)`**
Coloca a pipeline em execução. A partir daqui o GStreamer roda em threads próprias.

**Linha 46 – `g_print(...)`**
Informa as fontes e qual configuração PCM está em uso.

**Linhas 49–51 – espera de mensagens**
- `gst_element_get_bus` obtém o bus da pipeline.
- `gst_bus_timed_pop_filtered(..., GST_CLOCK_TIME_NONE, ERROR | EOS)` bloqueia o programa (sem limite de tempo) até chegar uma mensagem de **erro** ou de **fim de fluxo (EOS)**. Outras mensagens são ignoradas.

**Linhas 53–59 – tratamento do resultado**
- Se a mensagem for erro: extrai o texto com `gst_message_parse_error` e o imprime. Exemplo: se o `audio.ogg` não estiver na pasta, o `filesrc` reporta erro aqui.
- Caso contrário (EOS): imprime "Fim do video."

**Linha 61 – `return 0`**
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

- **Taxa de amostragem (Nyquist):** só é possível representar frequências até metade da taxa. A: até ~22 kHz (toda a faixa audível); o `audio.ogg` já é de 16 kHz, então A preserva tudo o que ele tem (até 8 kHz). B: até ~4 kHz, então perde os agudos e o som fica abafado, como em telefone.
- **Profundidade de bits:** menos bits = menos níveis de amplitude = mais ruído de quantização (chiado). B tem ruído bem mais audível.
- **Canais:** B é mono e perde a separação esquerda/direita.
- **Tamanho:** B usa ~22 vezes menos dados por segundo que A.

## Diagramas

- `Sem título-2026-09-30-2133.png`: pipeline de vídeo.
- `pipeline_audio.excalidraw`: pipeline de áudio (abrir em excalidraw.com).
