
/**
 neum.ltc~
 */

#include "m_pd.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "math.h"
#include "ltc.h"
//#include "encoder.h"
#include "decoder.h"

static t_class *mbutombutombuto_class;

typedef struct _ltc {
    t_object x_obj;
    t_float x_val;
    t_outlet *msg_outlet;
    long x_connected;
    t_float fps;
    long auto_increase;
    //LTCEncoder *encoder;
    LTCDecoder *decoder;
    t_float length; // in seconds
    t_float sampleRate;
    ltcsnd_sample_t *smpteBuffer;
    int smpteBufferLength;
    int smpteBufferTime;
    SMPTETimecode startTimeCode;
    int startTimeCodeChanged;
    LTCFrameExt frame;
    unsigned int dec_bufpos;
    float *dec_buffer; // has to contain 256 samples...
} t_ltc;


void setup_neum0x2eltc0x2edec_tilde(void);
void *ltc_new(t_symbol *s, int argc, t_atom *argv);
static void ltc_dsp(t_ltc *x, t_signal **sp);
//void ltc_setFps(t_ltc *x, t_floatarg value);
//void ltc_setTime(t_ltc *x, t_symbol *s, long argc, t_atom *argv);
//void ltc_setMilliseconds(t_ltc *x, t_floatarg f);
void ltc_free(t_ltc *x);

void setup_neum0x2eltc0x2edec_tilde(void)
{
    mbutombutombuto_class = class_new(gensym("neum.ltc.dec~"), (t_newmethod)ltc_new, (t_method)ltc_free, sizeof(t_ltc), 0, A_GIMME, 0);
    CLASS_MAINSIGNALIN(mbutombutombuto_class, t_ltc, x_val);
    class_addmethod(mbutombutombuto_class, (t_method)ltc_dsp, gensym("dsp"), A_CANT, 0);
    //class_addmethod(mbutombutombuto_class, (t_method)ltc_setFps, gensym("fps"), A_DEFFLOAT, 0);
    //class_addmethod(mbutombutombuto_class, (t_method)ltc_setMilliseconds, gensym("milliseconds"), A_FLOAT, 0);
    //class_addmethod(mbutombutombuto_class, (t_method)ltc_setTime, gensym("time"), A_GIMME, 0);
}



void *ltc_new(t_symbol *s, int argc, t_atom *argv)
{
    t_ltc *x = (t_ltc *)pd_new(mbutombutombuto_class);
    //outlet_new(&x->x_obj, gensym("signal"));
    x->msg_outlet = outlet_new(&x->x_obj, &s_list);
    x->length = 20;
    x->startTimeCodeChanged = 1;
    x->smpteBufferTime = 0;
    x->smpteBufferLength = 0;

    const char timezone[6] = "+0100";
    strcpy(x->startTimeCode.timezone, timezone);
    x->startTimeCode.years = 0;
    x->startTimeCode.months = 0;
    x->startTimeCode.days = 0;
    x->startTimeCode.hours = 0;
    x->startTimeCode.mins = 0;
    x->startTimeCode.secs = 0;
    x->startTimeCode.frame = 0;
    //x->encoder = ltc_encoder_create(1, 1, LTC_TV_625_50, 0);

    //if (argv[0].a_type == A_SYMBOL && argv[0].a_w.w_symbol == gensym("enc"))

    /*
    // POTREBBE SERVIRE
    if (argv[0].a_type == A_FLOAT)
        ltc_setFps(x, argv[1].a_w.w_float);
    else ltc_setFps(x, 0.f);
     */

    int apv = sys_getsr()*1/25;

    x->dec_bufpos = 0;
    x->dec_buffer = (float*)getbytes(sizeof(float) * 256);
    x->decoder = ltc_decoder_create(apv, 32);

    return (void *)x;
}

void ltc_free(t_ltc *x)
{
    ltc_decoder_free(x->decoder);
    //ltc_encoder_free(x->encoder);
    freebytes(x->dec_buffer, sizeof(float) * 256);
}

/*
void ltc_setFps(t_ltc *x, t_floatarg v)
{
    short val = (short)v;

    switch (val) {
        case 0:
            x->fps = 24;
            break;
        case 1:
            x->fps = 25;
            break;
        case 2:
            x->fps = 29.97;
            break;
        case 3:
            x->fps = 30;
            break;
        default:
            break;
    }

    ltc_encoder_set_bufsize(x->encoder, sys_getsr(), x->fps);
    ltc_encoder_reinit(x->encoder, sys_getsr(), x->fps, x->fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
    post("neum.ltc~: set FPS to %.2f", x->fps);
}
 */


static t_int *ltc_perform(t_int *w)
{
    t_ltc *x = (t_ltc *)(w[1]);
    t_sample *inp =  (t_sample *)(w[2]);
    int n = (int)(w[3]);

    for (int i = 0; i < n; i++) {
        if (x->dec_bufpos > 255) {
            ltc_decoder_write_float(x->decoder, x->dec_buffer, 256, 0);
            x->dec_bufpos = 0;
        }
        x->dec_buffer[x->dec_bufpos++] = inp[i];
    }

    while (ltc_decoder_read(x->decoder, &x->frame)) {
        SMPTETimecode stime;
        ltc_frame_to_time(&stime, &x->frame.ltc, 1);

        t_int quarter = 0;
        t_float oct = 0;

        t_atom timecode_list[4];
        SETFLOAT(timecode_list,stime.hours);
        SETFLOAT(timecode_list+1,stime.mins);
        SETFLOAT(timecode_list+2,stime.secs);
        // proviamo a far uscire solo 1/8 di secondo

        //SETFLOAT(timecode_list+3,stime.frame);

        quarter = stime.frame % (25 / 8);

        if (quarter == 0) {
            oct = stime.frame / 24.f;
            SETFLOAT(timecode_list+3, oct * 1000);
            outlet_list(x->msg_outlet, 0L, 4, timecode_list);
        }
        x->startTimeCode = stime;
        x->startTimeCodeChanged = 1;
    }

    return (w + 4);
}

static void ltc_dsp(t_ltc *x, t_signal **sp)
{
    dsp_add(ltc_perform, 3, x, sp[0]->s_vec, sp[0]->s_n);
}
