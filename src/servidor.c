/*
 * servidor.c - Interface gráfica (GTK 3) do transmissor de vídeo.
 *
 * Pipeline montada elemento a elemento (a que teve os melhores resultados na Parte 1):
 *
 *   videotestsrc -> capsfilter -> videoconvert -> x264enc -> h264parse -> mpegtsmux -> udpsink
 *
 *   x264enc : tune=zerolatency, speed-preset=ultrafast, bitrate e key-int-max (= FPS: um quadro completo por segundo)
 *   h264parse: config-interval=-1     mpegtsmux: alignment=7     udpsink: sync=false
 *
 * Compilar (terminal UCRT64 do MSYS2, ou gui.bat build):
 *   gcc src/servidor.c -o bin/servidor $(pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0)
 */
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <stdio.h>

static GtkWidget *campo_ip, *campo_porta, *combo_resolucao, *combo_fps, *combo_bitrate;
static GtkWidget *botao_iniciar, *botao_parar, *rotulo_estado;
static GstElement *pipeline = NULL;
static guint id_bus = 0;
static gchar destino[64] = "";

static void mostra_estado(const gchar *texto) {
  gchar *completo = g_strdup_printf("Estado: %s", texto);
  gtk_label_set_text(GTK_LABEL(rotulo_estado), completo);
  g_free(completo);
}

/* Para a pipeline e libera os recursos (não mexe nos botões, para poder ser usada ao fechar a janela). */
static void liberar(void) {
  if (id_bus) {
    g_source_remove(id_bus);
    id_bus = 0;
  }
  if (pipeline) {
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    pipeline = NULL;
  }
}

static void parar(void) {
  liberar();
  gtk_widget_set_sensitive(botao_iniciar, TRUE);
  gtk_widget_set_sensitive(botao_parar, FALSE);
}

/* GstBus: erros e mudanças de estado chegam aqui, dentro do loop principal do GTK (sem bloquear a interface). */
static gboolean ao_receber_mensagem(GstBus *bus, GstMessage *msg, gpointer dados) {
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    GError *erro;
    gst_message_parse_error(msg, &erro, NULL);
    gchar *texto = g_strdup_printf("erro: %s", erro->message);
    id_bus = 0; /* este watch é removido ao retornar G_SOURCE_REMOVE */
    parar();
    mostra_estado(texto);
    g_free(texto);
    g_error_free(erro);
    return G_SOURCE_REMOVE;
  }
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
    GstState novo;
    gst_message_parse_state_changed(msg, NULL, &novo, NULL);
    if (novo == GST_STATE_PLAYING) {
      gchar *texto = g_strdup_printf("transmitindo para %s", destino);
      mostra_estado(texto);
      g_free(texto);
    }
  }
  return G_SOURCE_CONTINUE;
}

static void iniciar_transmissao(GtkButton *botao, gpointer dados) {
  /* 1. Recupera e valida os valores da interface */
  const gchar *ip = gtk_entry_get_text(GTK_ENTRY(campo_ip));
  int porta = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(campo_porta));
  if (!g_hostname_is_ip_address(ip)) {
    mostra_estado("IP de destino inválido");
    return;
  }
  gchar *resolucao = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_resolucao));
  gchar *fps_txt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_fps));
  gchar *bitrate_txt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_bitrate));
  int largura = 0, altura = 0;
  sscanf(resolucao, "%dx%d", &largura, &altura);
  int fps = atoi(fps_txt), bitrate = atoi(bitrate_txt);
  g_free(resolucao);
  g_free(fps_txt);
  g_free(bitrate_txt);

  /* 2. Cria os elementos */
  GstElement *p = gst_pipeline_new("servidor");
  GstElement *fonte = gst_element_factory_make("videotestsrc", NULL);
  GstElement *filtro = gst_element_factory_make("capsfilter", NULL);
  GstElement *conv = gst_element_factory_make("videoconvert", NULL);
  GstElement *enc = gst_element_factory_make("x264enc", NULL);
  GstElement *parse = gst_element_factory_make("h264parse", NULL);
  GstElement *mux = gst_element_factory_make("mpegtsmux", NULL);
  GstElement *udp = gst_element_factory_make("udpsink", NULL);
  if (!p || !fonte || !filtro || !conv || !enc || !parse || !mux || !udp) {
    mostra_estado("plugin do GStreamer ausente");
    return;
  }

  /* 3. Configura os parâmetros */
  g_object_set(fonte, "is-live", TRUE, NULL);
  gst_util_set_object_arg(G_OBJECT(fonte), "pattern", "ball");
  GstCaps *caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, largura, "height", G_TYPE_INT, altura,
                                      "framerate", GST_TYPE_FRACTION, fps, 1, NULL);
  g_object_set(filtro, "caps", caps, NULL);
  gst_caps_unref(caps);
  g_object_set(enc, "bitrate", bitrate, "key-int-max", fps, NULL);
  gst_util_set_object_arg(G_OBJECT(enc), "tune", "zerolatency");
  gst_util_set_object_arg(G_OBJECT(enc), "speed-preset", "ultrafast");
  g_object_set(parse, "config-interval", -1, NULL);
  g_object_set(mux, "alignment", 7, NULL);
  g_object_set(udp, "host", ip, "port", porta, "sync", FALSE, NULL);

  /* 4. Adiciona, liga e inicia */
  gst_bin_add_many(GST_BIN(p), fonte, filtro, conv, enc, parse, mux, udp, NULL);
  if (!gst_element_link_many(fonte, filtro, conv, enc, parse, mux, udp, NULL)) {
    mostra_estado("erro ao ligar os elementos da pipeline");
    gst_object_unref(p);
    return;
  }
  pipeline = p;
  g_snprintf(destino, sizeof(destino), "%s:%d", ip, porta);
  GstBus *bus = gst_element_get_bus(pipeline);
  id_bus = gst_bus_add_watch(bus, ao_receber_mensagem, NULL);
  gst_object_unref(bus);

  mostra_estado("iniciando...");
  gtk_widget_set_sensitive(botao_iniciar, FALSE);
  gtk_widget_set_sensitive(botao_parar, TRUE);
  gst_element_set_state(pipeline, GST_STATE_PLAYING); /* se falhar, o erro chega pelo GstBus (ao_receber_mensagem) */
}

static void interromper_transmissao(GtkButton *botao, gpointer dados) {
  parar();
  mostra_estado("parado");
}

static void ao_fechar(GtkWidget *janela, gpointer dados) {
  liberar();
  gtk_main_quit();
}

static GtkWidget *novo_combo(const gchar *opcoes[], int ativo) {
  GtkWidget *combo = gtk_combo_box_text_new();
  for (int i = 0; opcoes[i]; i++)
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), opcoes[i]);
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), ativo);
  return combo;
}

static void linha(GtkWidget *grade, int n, const gchar *rotulo, GtkWidget *campo) {
  GtkWidget *r = gtk_label_new(rotulo);
  gtk_widget_set_halign(r, GTK_ALIGN_START);
  gtk_widget_set_hexpand(campo, TRUE);
  gtk_grid_attach(GTK_GRID(grade), r, 0, n, 1, 1);
  gtk_grid_attach(GTK_GRID(grade), campo, 1, n, 1, 1);
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);
  gtk_init(&argc, &argv);

  GtkWidget *janela = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(janela), "Servidor Multimidia");
  gtk_window_set_default_size(GTK_WINDOW(janela), 420, 320);
  gtk_container_set_border_width(GTK_CONTAINER(janela), 12);
  g_signal_connect(janela, "destroy", G_CALLBACK(ao_fechar), NULL);

  /* Campos: destino e três parâmetros de qualidade (resolução, quadros por segundo e bitrate) */
  campo_ip = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(campo_ip), "127.0.0.1");
  campo_porta = gtk_spin_button_new_with_range(1024, 65535, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(campo_porta), 5000);
  const gchar *resolucoes[] = {"320x240", "640x360", "1280x720", NULL};
  const gchar *taxas[] = {"15", "25", "30", NULL};
  const gchar *bitrates[] = {"200", "500", "1500", NULL};
  combo_resolucao = novo_combo(resolucoes, 1);
  combo_fps = novo_combo(taxas, 1);
  combo_bitrate = novo_combo(bitrates, 1);

  GtkWidget *grade = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grade), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grade), 12);
  linha(grade, 0, "IP de destino", campo_ip);
  linha(grade, 1, "Porta UDP", campo_porta);
  linha(grade, 2, "Resolução", combo_resolucao);
  linha(grade, 3, "Quadros por segundo", combo_fps);
  linha(grade, 4, "Bitrate (kbps)", combo_bitrate);

  botao_iniciar = gtk_button_new_with_label("Iniciar");
  botao_parar = gtk_button_new_with_label("Interromper");
  gtk_widget_set_sensitive(botao_parar, FALSE);
  g_signal_connect(botao_iniciar, "clicked", G_CALLBACK(iniciar_transmissao), NULL);
  g_signal_connect(botao_parar, "clicked", G_CALLBACK(interromper_transmissao), NULL);
  GtkWidget *botoes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(botoes), botao_iniciar, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(botoes), botao_parar, TRUE, TRUE, 0);

  rotulo_estado = gtk_label_new("");
  gtk_widget_set_halign(rotulo_estado, GTK_ALIGN_START);
  mostra_estado("parado");

  GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_pack_start(GTK_BOX(caixa), grade, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(caixa), botoes, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(caixa), rotulo_estado, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(janela), caixa);

  gtk_widget_show_all(janela);
  gtk_main();
  return 0;
}
