/*
 * cliente.c - Interface gráfica (GTK 3) do receptor de vídeo.
 *
 * Pipeline montada elemento a elemento, compatível com o servidor (MPEG-TS sobre UDP, vídeo H.264):
 *
 *   udpsrc -> tsdemux -> h264parse -> avdec_h264 -> videoconvert -> gtksink
 *
 *   udpsrc : port, caps video/mpegts, reuse=FALSE (porta ocupada vira erro) e timeout (avisa se não chegar nada)
 *   tsdemux: seus pads só aparecem quando o fluxo é analisado, então a ligação com o h264parse é feita no
 *            sinal "pad-added"
 *   gtksink: entrega um widget GTK, que é colocado dentro da janela (sem gtksink, usa autovideosink e abre
 *            uma janela separada)
 *
 * O estado PLAYING não garante que chegou vídeo: o texto "recebendo vídeo" só aparece quando o primeiro quadro
 * decodificado passa pela pipeline.
 *
 * Compilar (terminal UCRT64 do MSYS2, ou gui.bat build):
 *   gcc src/cliente.c -o bin/cliente $(pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0)
 */
#include <gtk/gtk.h>
#include <gst/gst.h>

#define TIMEOUT_S 3 /* segundos sem pacotes até avisar */

static GtkWidget *campo_porta, *botao_iniciar, *botao_parar, *rotulo_estado, *caixa_video;
static GtkWidget *widget_video = NULL; /* widget do gtksink dentro da caixa de vídeo */
static GstElement *pipeline = NULL, *udp_src = NULL;
static guint id_bus = 0;
static gint recebendo = 0; /* vira 1 quando o primeiro quadro decodificado chega */
static int porta_atual = 0;

static void mostra_estado(const gchar *texto) {
  gchar *completo = g_strdup_printf("Estado: %s", texto);
  gtk_label_set_text(GTK_LABEL(rotulo_estado), completo);
  g_free(completo);
}

/* Para a pipeline e libera os recursos (não mexe nos widgets, para poder ser usada ao fechar a janela). */
static void liberar_pipeline(void) {
  if (id_bus) {
    g_source_remove(id_bus);
    id_bus = 0;
  }
  if (pipeline) {
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    pipeline = NULL;
    udp_src = NULL;
  }
}

static void parar(void) {
  liberar_pipeline();
  if (widget_video) {
    gtk_container_remove(GTK_CONTAINER(caixa_video), widget_video);
    widget_video = NULL;
  }
  gtk_widget_set_sensitive(botao_iniciar, TRUE);
  gtk_widget_set_sensitive(botao_parar, FALSE);
}

/* Roda no loop principal do GTK (a probe abaixo roda na thread de streaming, onde não se mexe na interface). */
static gboolean mostra_recebendo(gpointer dados) {
  if (pipeline)
    mostra_estado("recebendo vídeo");
  return G_SOURCE_REMOVE;
}

/* Probe: cada quadro decodificado que entra no videoconvert passa por aqui. */
static GstPadProbeReturn ao_chegar_quadro(GstPad *pad, GstPadProbeInfo *info, gpointer dados) {
  if (g_atomic_int_compare_and_exchange(&recebendo, 0, 1))
    g_idle_add(mostra_recebendo, NULL);
  return GST_PAD_PROBE_OK;
}

/* O tsdemux cria o pad de vídeo depois de analisar o fluxo; aqui ele é ligado ao h264parse. */
static void ao_criar_pad(GstElement *demux, GstPad *pad, gpointer parse) {
  GstPad *sink = gst_element_get_static_pad(GST_ELEMENT(parse), "sink");
  if (!gst_pad_is_linked(sink))
    gst_pad_link(pad, sink);
  gst_object_unref(sink);
}

/* GstBus: erros, avisos de timeout e mudanças de estado chegam aqui, sem bloquear a interface. */
static gboolean ao_receber_mensagem(GstBus *bus, GstMessage *msg, gpointer dados) {
  gchar *texto;

  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_ERROR: {
    GError *erro;
    gst_message_parse_error(msg, &erro, NULL);
    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(udp_src))
      texto = g_strdup_printf("erro ao escutar na porta %d (já está em uso?)", porta_atual);
    else
      texto = g_strdup_printf("erro: %s", erro->message);
    id_bus = 0; /* este watch é removido ao retornar G_SOURCE_REMOVE */
    parar();
    mostra_estado(texto);
    g_free(texto);
    g_error_free(erro);
    return G_SOURCE_REMOVE;
  }
  case GST_MESSAGE_ELEMENT: {
    /* O udpsrc posta esta mensagem quando passa TIMEOUT_S segundos sem receber nenhum pacote. */
    const GstStructure *s = gst_message_get_structure(msg);
    if (s && gst_structure_has_name(s, "GstUDPSrcTimeout")) {
      g_atomic_int_set(&recebendo, 0);
      texto = g_strdup_printf("sem pacotes há %d s na porta %d (aguardando o servidor)", TIMEOUT_S, porta_atual);
      mostra_estado(texto);
      g_free(texto);
    }
    break;
  }
  case GST_MESSAGE_STATE_CHANGED:
    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
      GstState novo;
      gst_message_parse_state_changed(msg, NULL, &novo, NULL);
      if (novo == GST_STATE_PLAYING && !g_atomic_int_get(&recebendo)) {
        texto = g_strdup_printf("aguardando vídeo na porta %d", porta_atual);
        mostra_estado(texto);
        g_free(texto);
      }
    }
    break;
  default:
    break;
  }
  return G_SOURCE_CONTINUE;
}

static void iniciar_recepcao(GtkButton *botao, gpointer dados) {
  /* 1. Recupera o valor da interface (o spin button já limita a porta a 1024..65535) */
  int porta = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(campo_porta));

  /* 2. Cria os elementos */
  GstElement *p = gst_pipeline_new("cliente");
  GstElement *udp = gst_element_factory_make("udpsrc", NULL);
  GstElement *demux = gst_element_factory_make("tsdemux", NULL);
  GstElement *parse = gst_element_factory_make("h264parse", NULL);
  GstElement *dec = gst_element_factory_make("avdec_h264", NULL);
  GstElement *conv = gst_element_factory_make("videoconvert", NULL);
  GstElement *saida = gst_element_factory_make("gtksink", NULL);
  gboolean embutido = (saida != NULL);
  if (!embutido)
    saida = gst_element_factory_make("autovideosink", NULL);
  if (!p || !udp || !demux || !parse || !dec || !conv || !saida) {
    mostra_estado("plugin do GStreamer ausente");
    return;
  }

  /* 3. Configura e liga */
  GstCaps *caps = gst_caps_from_string("video/mpegts,systemstream=true");
  g_object_set(udp, "port", porta, "caps", caps, "reuse", FALSE, "timeout", (guint64)TIMEOUT_S * GST_SECOND, NULL);
  gst_caps_unref(caps);

  gst_bin_add_many(GST_BIN(p), udp, demux, parse, dec, conv, saida, NULL);
  if (!gst_element_link(udp, demux) || !gst_element_link_many(parse, dec, conv, saida, NULL)) {
    mostra_estado("erro ao ligar os elementos da pipeline");
    gst_object_unref(p);
    return;
  }
  g_signal_connect(demux, "pad-added", G_CALLBACK(ao_criar_pad), parse);

  GstPad *pad = gst_element_get_static_pad(conv, "sink");
  gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, ao_chegar_quadro, NULL, NULL);
  gst_object_unref(pad);

  /* Com gtksink, o vídeo aparece dentro da janela: o widget dele vai para a caixa de vídeo */
  if (embutido) {
    GtkWidget *w;
    g_object_get(saida, "widget", &w, NULL);
    gtk_box_pack_start(GTK_BOX(caixa_video), w, TRUE, TRUE, 0);
    gtk_widget_show(w);
    widget_video = w;
    g_object_unref(w); /* a caixa guarda a referência dele */
  }

  /* 4. Inicia */
  pipeline = p;
  udp_src = udp;
  porta_atual = porta;
  g_atomic_int_set(&recebendo, 0);
  GstBus *bus = gst_element_get_bus(pipeline);
  id_bus = gst_bus_add_watch(bus, ao_receber_mensagem, NULL);
  gst_object_unref(bus);

  mostra_estado("iniciando...");
  gtk_widget_set_sensitive(botao_iniciar, FALSE);
  gtk_widget_set_sensitive(botao_parar, TRUE);
  gst_element_set_state(pipeline, GST_STATE_PLAYING); /* se falhar, o erro chega pelo GstBus (ao_receber_mensagem) */
}

static void interromper_recepcao(GtkButton *botao, gpointer dados) {
  parar();
  mostra_estado("parado");
}

static void ao_fechar(GtkWidget *janela, gpointer dados) {
  liberar_pipeline();
  gtk_main_quit();
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);
  gtk_init(&argc, &argv);

  GtkWidget *janela = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(janela), "Cliente Multimidia");
  gtk_window_set_default_size(GTK_WINDOW(janela), 680, 480);
  gtk_container_set_border_width(GTK_CONTAINER(janela), 12);
  g_signal_connect(janela, "destroy", G_CALLBACK(ao_fechar), NULL);

  /* Barra de cima: porta de recepção e botões */
  campo_porta = gtk_spin_button_new_with_range(1024, 65535, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(campo_porta), 5000);
  botao_iniciar = gtk_button_new_with_label("Iniciar");
  botao_parar = gtk_button_new_with_label("Interromper");
  gtk_widget_set_sensitive(botao_parar, FALSE);
  g_signal_connect(botao_iniciar, "clicked", G_CALLBACK(iniciar_recepcao), NULL);
  g_signal_connect(botao_parar, "clicked", G_CALLBACK(interromper_recepcao), NULL);

  GtkWidget *barra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(barra), gtk_label_new("Porta UDP"), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(barra), campo_porta, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(barra), botao_iniciar, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(barra), botao_parar, TRUE, TRUE, 0);

  rotulo_estado = gtk_label_new("");
  gtk_widget_set_halign(rotulo_estado, GTK_ALIGN_START);
  mostra_estado("parado");

  /* Área de reprodução: o gtksink coloca o vídeo aqui */
  caixa_video = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(caixa_video, 640, 360);

  GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_pack_start(GTK_BOX(caixa), barra, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(caixa), rotulo_estado, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(caixa), caixa_video, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(janela), caixa);

  gtk_widget_show_all(janela);
  gtk_main();
  return 0;
}
