
/**
 neum.ltc~
 */

#include "m_pd.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "math.h"
#include "ltc.h"
#include "encoder.h"
#include "decoder.h"

static t_class *ltc_class;

typedef struct _ltc {
    t_object x_obj;
    t_float x_val;
    t_outlet *msg_outlet;
    long x_connected;
    t_float fps;
    long auto_increase;
    LTCEncoder *encoder;
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


void setup_neum0x2eltc_tilde(void);
void *ltc_new(t_symbol *s, int argc, t_atom *argv);
static void ltc_dsp(t_ltc *x, t_signal **sp);
void ltc_setFps(t_ltc *x, t_floatarg value);
void ltc_setTime(t_ltc *x, t_symbol *s, long argc, t_atom *argv);
void ltc_setMilliseconds(t_ltc *x, t_floatarg f);
void ltc_free(t_ltc *x);

void setup_neum0x2eltc_tilde(void)
{
    ltc_class = class_new(gensym("neum.ltc~"), (t_newmethod)ltc_new, (t_method)ltc_free, sizeof(t_ltc), 0, A_GIMME, 0);
    CLASS_MAINSIGNALIN(ltc_class, t_ltc, x_val);
    class_addmethod(ltc_class, (t_method)ltc_dsp, gensym("dsp"), A_CANT, 0);
    class_addmethod(ltc_class, (t_method)ltc_setFps, gensym("fps"), A_DEFFLOAT, 0);
    class_addmethod(ltc_class, (t_method)ltc_setMilliseconds, gensym("milliseconds"), A_FLOAT, 0);
    class_addmethod(ltc_class, (t_method)ltc_setTime, gensym("time"), A_GIMME, 0);
}



void *ltc_new(t_symbol *s, int argc, t_atom *argv)
{
    const char *errpost = "neum.ltc~ usage:\nfirst arg (mandatory): enc/dec\nsecond arg: fps value (0-3)";
    if (argc == 0)
        error("%s", errpost);
    if ((argc > 0) && argv[0].a_type == A_SYMBOL && (argv[0].a_w.w_symbol == gensym("enc") || argv[0].a_w.w_symbol == gensym("dec")))
    {
        t_ltc *x = (t_ltc *)pd_new(ltc_class);
        outlet_new(&x->x_obj, gensym("signal"));
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
        x->encoder = ltc_encoder_create(1, 1, LTC_TV_625_50, 0);
    
        //if (argv[0].a_type == A_SYMBOL && argv[0].a_w.w_symbol == gensym("enc"))
        if (argv[0].a_w.w_symbol == gensym("enc"))
            x->auto_increase = 1;
        else x->auto_increase = 0;
        
        if (argv[1].a_type == A_FLOAT)
            ltc_setFps(x, argv[1].a_w.w_float);
        else ltc_setFps(x, 0.f);
        
        ltc_encoder_set_bufsize(x->encoder, sys_getsr(), x->fps);
        ltc_encoder_reinit(x->encoder, sys_getsr(), x->fps, x->fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
        ltc_encoder_set_filter(x->encoder, 0);
        ltc_encoder_set_filter(x->encoder, 25.0);
        ltc_encoder_set_volume(x->encoder, -3.0);
        
        int apv = sys_getsr()*1/25;
        
        x->dec_bufpos = 0;
        x->dec_buffer = (float*)getbytes(sizeof(float) * 256);
        x->decoder = ltc_decoder_create(apv, 32);
        
        return (void *)x;
    }
    
    return 0;
}

void ltc_free(t_ltc *x)
{
    ltc_decoder_free(x->decoder);
    ltc_encoder_free(x->encoder);
    freebytes(x->dec_buffer, sizeof(float) * 256);
}

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

void ltc_setTime(t_ltc *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc != 4) {
        post("time: please pass a list with four numbers");
    } else {
        if (atom_getint(&argv[3]) > x->fps) {
            post ("requested frame number higher than fps");
        } else {
            x->startTimeCode.hours = atom_getint(&argv[0]);
            x->startTimeCode.mins = atom_getint(&argv[1]);
            x->startTimeCode.secs = atom_getint(&argv[2]);
            x->startTimeCode.frame = atom_getint(&argv[3]);
            x->startTimeCodeChanged = 1;
        }
    }
}

void ltc_setMilliseconds(t_ltc *x, t_floatarg f)
{
    t_float timeInSeconds = f / 1000.;
    double intPart = 0;
    t_float subSecond = modf(timeInSeconds, &intPart);
    x->startTimeCode.hours = timeInSeconds / 360;
    x->startTimeCode.mins = (int)(timeInSeconds / 60) % 60;
    x->startTimeCode.secs = (int)(timeInSeconds) % 60;
    x->startTimeCode.frame = (int)(subSecond * x->fps);
    x->startTimeCodeChanged = 1;
}

static t_int *ltc_perform(t_int *w)
{
    t_ltc *x = (t_ltc *)(w[1]);
    t_sample *inp =  (t_sample *)(w[2]);
    t_sample *outp = (t_sample *)(w[3]);
    // check if timecode signal at input
    int ltc_input = 0;
    t_sample* input = inp;
    // search for first non-zero sample
    int n = (int)(w[4]);
    for (int i = 0; i < n; i++) {
        if ((*input++) != 0.f) {
            ltc_input = 1;
            break;
        }
    }

    if (ltc_input) {
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
            t_atom timecode_list[4];
            SETFLOAT(timecode_list,stime.hours);
            SETFLOAT(timecode_list+1,stime.mins);
            SETFLOAT(timecode_list+2,stime.secs);
            SETFLOAT(timecode_list+3,stime.frame);
            outlet_list(x->msg_outlet, 0L, 4, timecode_list);
            x->startTimeCode = stime;
            x->startTimeCodeChanged = 1;
        }
    } else {
        while (n--) {
            if (x->smpteBufferTime >= x->smpteBufferLength) {
                if (x->startTimeCodeChanged) {
                    ltc_encoder_set_timecode(x->encoder, &x->startTimeCode);
                    x->startTimeCodeChanged = 0;
                }
                else if (x->auto_increase) {
                    ltc_encoder_inc_timecode(x->encoder);
                } else {
                    //user apparently wants to keep using the same frame twice
                }

                SMPTETimecode st;
                ltc_encoder_get_timecode(x->encoder, &st);

                t_atom timecode_list[4];
                SETFLOAT(timecode_list,st.hours);
                SETFLOAT(timecode_list+1,st.mins);
                SETFLOAT(timecode_list+2,st.secs);
                SETFLOAT(timecode_list+3,st.frame);
                outlet_list(x->msg_outlet, 0L, 4, timecode_list);
                ltc_encoder_encode_frame(x->encoder);
                x->smpteBuffer = ltc_encoder_get_bufptr(x->encoder, &x->smpteBufferLength, 1);
                x->smpteBufferTime = 0;
            }

            *outp++ = x->smpteBuffer[x->smpteBufferTime] / 128. - 1.;
            x->smpteBufferTime++;
        }
    }

    return (w + 5);
}

static void ltc_dsp(t_ltc *x, t_signal **sp)
{
    dsp_add(ltc_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}
