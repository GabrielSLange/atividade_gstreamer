/*
 * receptor.c - Receptor de áudio RTP/UDP com GStreamer.
 *
 *   udpsrc -> netsim -> rtpjitterbuffer -> rtpL16depay -> audioconvert -> audioresample -> autoaudiosink
 *
 *   udpsrc         : escuta a porta UDP; as caps application/x-rtp descrevem o fluxo
 *   netsim         : SIMULA a rede (descarta pacotes e/ou atrasa aleatoriamente); com 0 não altera nada
 *   rtpjitterbuffer: segura os pacotes por "latency" ms, põe em ordem (número de sequência)
 *                    e entrega em ritmo constante, compensando o jitter da rede
 *   rtpL16depay    : remove o cabeçalho RTP e devolve o PCM
 *   audioconvert / audioresample : adaptam o PCM ao formato da placa de som
 *   autoaudiosink  : reproduz o áudio
 *
 * Diagnóstico de perdas (a partir do rtpjitterbuffer):
 *   - evento "GstRTPPacketLost" (do-lost=TRUE): uma linha [PERDA] para cada pacote perdido
 *   - propriedade "stats": uma linha [STATS] por segundo com entregues/perdidos/atrasados/duplicados
 *
 * Uso: receptor [porta] [latencia_ms] [perda_%] [atraso_max_ms]     (padrão: 5000 200 0 0)
 */
#include <gst/gst.h>
#include <stdlib.h>

/* Chamada para cada evento que sai do jitter buffer. Quando um pacote não chega a tempo,
 * ele envia o evento "GstRTPPacketLost" (só acontece com do-lost=TRUE). */
static GstPadProbeReturn ao_receber_evento(GstPad *pad, GstPadProbeInfo *info, gpointer dados) {
  const GstStructure *s = gst_event_get_structure(GST_PAD_PROBE_INFO_EVENT(info));

  if (s && gst_structure_has_name(s, "GstRTPPacketLost")) {
    guint seq = 0;
    gst_structure_get_uint(s, "seqnum", &seq);
    g_print("[PERDA] pacote seq=%u nao chegou a tempo\n", seq);
  }
  return GST_PAD_PROBE_OK;
}

/* Lê as estatísticas acumuladas do jitter buffer. */
static void imprime_estatisticas(GstElement *jitter) {
  GstStructure *stats;
  guint64 entregues = 0, perdidos = 0, atrasados = 0, duplicados = 0;

  g_object_get(jitter, "stats", &stats, NULL);
  gst_structure_get(stats, "num-pushed", G_TYPE_UINT64, &entregues, "num-lost", G_TYPE_UINT64,
                    &perdidos, "num-late", G_TYPE_UINT64, &atrasados, "num-duplicates",
                    G_TYPE_UINT64, &duplicados, NULL);
  gst_structure_free(stats);

  g_print("[STATS] entregues=%" G_GUINT64_FORMAT " perdidos=%" G_GUINT64_FORMAT
          " atrasados=%" G_GUINT64_FORMAT " duplicados=%" G_GUINT64_FORMAT "\n",
          entregues, perdidos, atrasados, duplicados);
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  int porta = argc > 1 ? atoi(argv[1]) : 5000;
  int latencia = argc > 2 ? atoi(argv[2]) : 200; /* ms de jitter buffer */
  int perda = argc > 3 ? atoi(argv[3]) : 0;      /* % de pacotes descartados (simulação) */
  int atraso = argc > 4 ? atoi(argv[4]) : 0;     /* atraso aleatório máximo em ms (simula jitter) */
  if (latencia < 0 || perda < 0 || perda > 100 || atraso < 0) {
    g_printerr("Erro: valores invalidos (latencia >= 0, perda de 0 a 100, atraso >= 0).\n");
    return 1;
  }

  /* 1. Cria os elementos */
  GstElement *pipeline = gst_pipeline_new("receptor");
  GstElement *udp = gst_element_factory_make("udpsrc", "udp");
  GstElement *rede = gst_element_factory_make("netsim", "rede");
  GstElement *jitter = gst_element_factory_make("rtpjitterbuffer", "jitter");
  GstElement *depay = gst_element_factory_make("rtpL16depay", "depay");
  GstElement *conv = gst_element_factory_make("audioconvert", "conv");
  GstElement *resample = gst_element_factory_make("audioresample", "resample");
  GstElement *saida = gst_element_factory_make("autoaudiosink", "saida");

  if (!pipeline || !udp || !rede || !jitter || !depay || !conv || !resample || !saida) {
    g_printerr("Erro: nao foi possivel criar os elementos (plugins do GStreamer instalados?).\n");
    return 1;
  }

  /* 2. Configura os parâmetros. O udpsrc entrega bytes "crus"; as caps dizem que
   *    são pacotes RTP com áudio L16 (iguais às do transmissor). reuse=FALSE faz
   *    uma porta ocupada virar erro (no Windows, por padrão, dois receptores
   *    conseguem escutar a mesma porta sem aviso). */
  GstCaps *caps = gst_caps_from_string(
      "application/x-rtp,media=audio,clock-rate=44100,encoding-name=L16,channels=2,payload=96");
  g_object_set(udp, "port", porta, "caps", caps, "reuse", FALSE, NULL);
  gst_caps_unref(caps);

  /* netsim: probabilidades de 0 a 1. Com atraso > 0, todo pacote é atrasado de 0 a "atraso" ms. */
  g_object_set(rede, "drop-probability", perda / 100.0, "delay-probability",
               atraso > 0 ? 1.0 : 0.0, "min-delay", 0, "max-delay", atraso, NULL);

  /* latency: tempo (ms) que o jitter buffer segura os pacotes antes de entregá-los.
   * do-lost: manda um evento a jusante a cada pacote declarado perdido. */
  g_object_set(jitter, "latency", latencia, "do-lost", TRUE, NULL);

  /* 3. Adiciona à pipeline e liga os elementos */
  gst_bin_add_many(GST_BIN(pipeline), udp, rede, jitter, depay, conv, resample, saida, NULL);
  if (!gst_element_link_many(udp, rede, jitter, depay, conv, resample, saida, NULL)) {
    g_printerr("Erro: nao foi possivel ligar os elementos da pipeline.\n");
    gst_object_unref(pipeline);
    return 1;
  }

  /* Observa os eventos que saem do jitter buffer (para detectar GstRTPPacketLost) */
  GstPad *pad = gst_element_get_static_pad(jitter, "src");
  gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, ao_receber_evento, NULL, NULL);
  gst_object_unref(pad);

  /* 4. Executa; a cada 1 s sem erro, imprime as estatísticas (Ctrl+C encerra o programa) */
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Recebendo na porta UDP %d | jitter buffer: %d ms | rede simulada: perda %d%%, atraso ate %d ms\n",
          porta, latencia, perda, atraso);
  g_print("(Ctrl+C para sair)\n");

  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = NULL;
  while (!msg) {
    msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (!msg)
      imprime_estatisticas(jitter);
  }

  int codigo = 0;
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    GError *erro;
    gchar *detalhes;
    gst_message_parse_error(msg, &erro, &detalhes);
    g_printerr("Erro: %s\n%s\n", erro->message, detalhes);
    g_error_free(erro);
    g_free(detalhes);
    codigo = 1;
  }

  gst_message_unref(msg);
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return codigo;
}
