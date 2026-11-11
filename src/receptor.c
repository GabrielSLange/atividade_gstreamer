/*
 * receptor.c - Receptor de áudio RTP/UDP com GStreamer.
 *
 *   udpsrc -> rtpL16depay -> audioconvert -> audioresample -> autoaudiosink
 *
 *   udpsrc       : escuta a porta UDP; as caps application/x-rtp descrevem o fluxo
 *   rtpL16depay  : remove o cabeçalho RTP e devolve o PCM
 *   audioconvert / audioresample : adaptam o PCM ao formato da placa de som
 *   autoaudiosink: reproduz o áudio
 *
 * Uso: receptor [porta]     (padrão: 5000)
 */
#include <gst/gst.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  int porta = argc > 1 ? atoi(argv[1]) : 5000;

  /* 1. Cria os elementos */
  GstElement *pipeline = gst_pipeline_new("receptor");
  GstElement *udp = gst_element_factory_make("udpsrc", "udp");
  GstElement *depay = gst_element_factory_make("rtpL16depay", "depay");
  GstElement *conv = gst_element_factory_make("audioconvert", "conv");
  GstElement *resample = gst_element_factory_make("audioresample", "resample");
  GstElement *saida = gst_element_factory_make("autoaudiosink", "saida");

  if (!pipeline || !udp || !depay || !conv || !resample || !saida) {
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

  /* 3. Adiciona à pipeline e liga os elementos */
  gst_bin_add_many(GST_BIN(pipeline), udp, depay, conv, resample, saida, NULL);
  if (!gst_element_link_many(udp, depay, conv, resample, saida, NULL)) {
    g_printerr("Erro: nao foi possivel ligar os elementos da pipeline.\n");
    gst_object_unref(pipeline);
    return 1;
  }

  /* 4. Executa e espera um erro (Ctrl+C encerra o programa) */
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Recebendo na porta UDP %d (Ctrl+C para sair)\n", porta);

  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                               GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
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
