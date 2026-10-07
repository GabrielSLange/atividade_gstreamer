#include <gst/gst.h>

int main(int argc, char *argv[])
{
    GstElement *pipeline;
    GstBus *bus;
    GstMessage *msg;
    GError *error = NULL;
    gchar *video_uri;
    gchar *pipeline_str;
    const gchar *audio_caps;

    gst_init(&argc, &argv);

    //Configuracao PCM escolhida na linha de comando: A (padrao) ou B
    if (argc > 1 && argv[1][0] == 'B')
        audio_caps = "audio/x-raw,rate=8000,format=U8,channels=1";          //B: 8 kHz / 8 bits / mono
    else
        audio_caps = "audio/x-raw,rate=44100,format=S16LE,channels=2";      //A: 44,1 kHz / 16 bits / estereo

    //Monta a pipeline com o caminho do video
    video_uri = gst_filename_to_uri("video.mp4", NULL);

    pipeline_str = g_strdup_printf(
        "uridecodebin name=d uri=\"%s\" ! "
        "videoconvert ! "
        "videorate ! video/x-raw,framerate=5/1 ! "   //30 FPS -> 5 FPS
        "videoscale ! video/x-raw,width=160,height=90 ! " //Modificacao 3: 1280x720 -> 160x90
        "videoconvert ! video/x-raw,format=GRAY8 ! " //Modificacao 2: colorido -> cinza
        "videoconvert ! autovideosink "
        "d. ! queue ! audioconvert ! volume volume=10 ! audioconvert ! audioresample ! %s ! "  //Audio PCM (volume: o audio do arquivo e muito baixo)
        "audioconvert ! audioresample ! autoaudiosink",      //reconverte para o formato que a placa de som aceita
        video_uri, audio_caps);

    pipeline = gst_parse_launch(pipeline_str, &error);

    if (error != NULL) {
        g_printerr("Erro ao criar a pipeline: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    //Inicia a reproducao
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Reproduzindo video.mp4 com audio PCM: %s\n", audio_caps);

    //Espera o fim do video ou um erro
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                     GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        gst_message_parse_error(msg, &error, NULL);
        g_printerr("Erro: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("Fim do video.\n");
    }

    return 0;
}
