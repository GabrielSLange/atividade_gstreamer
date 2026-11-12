/*
 * transmissor.c - Transmissor de áudio RTP/UDP com GStreamer.
 *
 *   fonte -> audioconvert -> audioresample -> capsfilter -> rtpL16pay -> udpsink
 *
 *   fonte      : audiotestsrc (escala cromática ou tom de 440 Hz) ou autoaudiosrc (microfone)
 *   capsfilter : PCM 16 bits big-endian, 44100 Hz, estéreo (formato exigido pelo rtpL16pay)
 *   rtpL16pay  : encapsula o PCM em pacotes RTP (payload type 96)
 *   udpsink    : envia os pacotes para host:porta
 *
 * Uso: transmissor [host] [porta] [escala|teste|mic]     (padrão: 127.0.0.1 5000 escala)
 */
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>

/* Escala cromática (12 semitons) de dó4 a dó5, em Hz (afinação com lá = 440 Hz). */
static const double NOTAS[13] = {261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99,
                                 392.00, 415.30, 440.00, 466.16, 493.88, 523.25};
#define MS_POR_NOTA 400

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  const gchar *host = argc > 1 ? argv[1] : "127.0.0.1";
  int porta = argc > 2 ? atoi(argv[2]) : 5000;
  const gchar *modo = argc > 3 ? argv[3] : "escala";
  gboolean usar_mic = strcmp(modo, "mic") == 0;
  gboolean usar_escala = strcmp(modo, "escala") == 0;
  if (!usar_mic && !usar_escala && strcmp(modo, "teste") != 0) {
    g_printerr("Erro: fonte '%s' invalida. Use escala, teste ou mic.\n", modo);
    return 1;
  }

  /* 1. Cria os elementos */
  GstElement *pipeline = gst_pipeline_new("transmissor");
  GstElement *fonte = gst_element_factory_make(usar_mic ? "autoaudiosrc" : "audiotestsrc", "fonte");
  GstElement *conv = gst_element_factory_make("audioconvert", "conv");
  GstElement *resample = gst_element_factory_make("audioresample", "resample");
  GstElement *filtro = gst_element_factory_make("capsfilter", "filtro");
  GstElement *pay = gst_element_factory_make("rtpL16pay", "pay");
  GstElement *udp = gst_element_factory_make("udpsink", "udp");

  if (!pipeline || !fonte || !conv || !resample || !filtro || !pay || !udp) {
    g_printerr("Erro: nao foi possivel criar os elementos (plugins do GStreamer instalados?).\n");
    return 1;
  }

  /* 2. Configura os parâmetros */
  if (!usar_mic)
    g_object_set(fonte, "is-live", TRUE, "volume", 0.3, NULL); /* gera em tempo real */
  if (usar_escala)
    g_object_set(fonte, "freq", NOTAS[0], NULL); /* a escala começa no dó */

  GstCaps *caps = gst_caps_from_string("audio/x-raw,format=S16BE,rate=44100,channels=2");
  g_object_set(filtro, "caps", caps, NULL);
  gst_caps_unref(caps);

  g_object_set(pay, "pt", 96, NULL);
  g_object_set(udp, "host", host, "port", porta, NULL);

  /* 3. Adiciona à pipeline e liga os elementos */
  gst_bin_add_many(GST_BIN(pipeline), fonte, conv, resample, filtro, pay, udp, NULL);
  if (!gst_element_link_many(fonte, conv, resample, filtro, pay, udp, NULL)) {
    g_printerr("Erro: nao foi possivel ligar os elementos da pipeline.\n");
    gst_object_unref(pipeline);
    return 1;
  }

  /* 4. Executa e espera um erro (Ctrl+C encerra o programa) */
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Transmitindo %s para %s:%d (Ctrl+C para sair)\n",
          usar_mic ? "microfone" : (usar_escala ? "escala cromatica" : "tom de teste"), host, porta);

  /* O audiotestsrc gera um tom de uma frequência só. Para tocar a escala, a cada MS_POR_NOTA ms
   * o laço troca a propriedade "freq" pela próxima nota (sobe até o dó seguinte, desce e repete). */
  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *msg = NULL;
  int passo = 0;
  while (!msg) {
    msg = gst_bus_timed_pop_filtered(bus, usar_escala ? MS_POR_NOTA * GST_MSECOND : GST_CLOCK_TIME_NONE,
                                     GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (!msg && usar_escala) {
      passo = (passo + 1) % 24; /* 0 a 12 sobe, 13 a 23 desce */
      g_object_set(fonte, "freq", NOTAS[passo <= 12 ? passo : 24 - passo], NULL);
    }
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
