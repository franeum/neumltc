/**
neum.ltc~
*/

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ltc.h"
#include "encoder.h"
#include "decoder.h"

void *neumltc_class;

typedef struct _neumltc {
	t_pxobject x_obj;
	double x_val;
	void *msg_outlet;
	long x_in;
	void *x_proxy;
	long x_connected;
	double fps;
	long set_fps;
	long auto_increase;
	LTCEncoder *encoder;
	LTCDecoder *decoder;
	double length; // in seconds
	double sampleRate;
	ltcsnd_sample_t *smpteBuffer;
	int smpteBufferLength;
	int smpteBufferTime;
	SMPTETimecode startTimeCode;
	int startTimeCodeChanged;
	LTCFrameExt frame;
	unsigned int dec_bufpos;
	float *dec_buffer; // has to contain 256 samples...
} t_neumltc;


void *neumltc_new(t_symbol *s, long argc, t_atom *argv);
void neumltc_dsp64(t_neumltc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void neumltc_assist(t_neumltc *x, void *b, long m, long a, char *s);
void neumltc_setFps(t_neumltc *x, t_object *attr, long ac, t_atom *av);
void neumltc_setAutoincrease(t_neumltc *x, t_object *attr, long ac, t_atom *av);
void neumltc_setTime(t_neumltc *x, t_symbol *s, long argc, t_atom *argv);
void neumltc_setMilliseconds(t_neumltc *x, double f);
void neumltc_free(t_neumltc *x);


void ext_main(void *r)
{
	t_class *c = class_new("neum.ltc~", (method)neumltc_new, (method)neumltc_free, sizeof(t_neumltc), 0L, A_GIMME, 0);

	class_addmethod(c, (method)neumltc_dsp64, "dsp64", A_CANT, 0); 	// respond to the dsp message
	class_addmethod(c, (method)neumltc_setMilliseconds, "milliseconds", A_FLOAT, 0);
	class_addmethod(c, (method)neumltc_setTime, "time", A_GIMME, 0);
	class_addmethod(c, (method)neumltc_assist, "assist", A_CANT,0);
	class_dspinit(c);	// must call this function for MSP object classes

	CLASS_ATTR_LONG(c, "op", 0, t_neumltc, auto_increase);
    CLASS_ATTR_ACCESSORS(c, "op", NULL, neumltc_setAutoincrease);
    CLASS_ATTR_ENUMINDEX(c, "op", 0, "decoder encoder");
    CLASS_ATTR_DEFAULT(c,"op", 0, "1");
    CLASS_ATTR_LONG(c, "fps", 0, t_neumltc, set_fps);
    CLASS_ATTR_ACCESSORS(c, "fps", NULL, neumltc_setFps);
    CLASS_ATTR_FILTER_CLIP(c, "fps", 0, 3);
    CLASS_ATTR_ENUMINDEX(c, "fps", 0, "24 25 29.97 30");
    CLASS_ATTR_DEFAULT(c,"fps", 0, "0");

	class_register(CLASS_BOX, c);
	neumltc_class = c;
}

void *neumltc_new(t_symbol *s, long argc, t_atom *argv)
{
    t_neumltc *x = (t_neumltc *)object_alloc(neumltc_class);
	dsp_setup((t_pxobject *)x,1);	// set up DSP for the instance and create signal inlets
	x->x_proxy = proxy_new((t_object *)x, 1, &x->x_in);
	x->msg_outlet = outlet_new(x, NULL);            // outlet destro: messaggi
	outlet_new((t_pxobject *)x, "signal");			// outlet sinistro: segnale
	x->x_val = 0;

	x->set_fps = 0;
	x->fps = 24;
	x->length = 20;
	x->auto_increase = 1;
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

	ltc_encoder_set_bufsize(x->encoder, sys_getsr(), x->fps);
	ltc_encoder_reinit(x->encoder, sys_getsr(), x->fps, x->fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
	ltc_encoder_set_filter(x->encoder, 0);
	ltc_encoder_set_filter(x->encoder, 25.0);
	ltc_encoder_set_volume(x->encoder, -3.0);

	// decoder
	int apv = sys_getsr()*1/25;

	x->dec_bufpos = 0;
	x->dec_buffer = (float*)sysmem_newptr(sizeof(float) * 256);// allocate buffer
	x->decoder = ltc_decoder_create(apv, 32);

	attr_args_process(x, argc, argv);

	//neumltc_setAutoincrease(x, x->auto_increase);
	//neumltc_setFps(x, x->set_fps);

	return (x);
}


void neumltc_assist(t_neumltc *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) {
		switch (a) {
			case 0:
			sprintf(s,"(Signal) left");
			break;
			case 1:
			sprintf(s,"(Message) right");
			break;
		}
	} else {
		switch (a) {
			case 0:
			sprintf(s,"(Signal) left");
			break;
			case 1:
			sprintf(s,"(Message) right");
			break;
		}
	}
}

void neumltc_free(t_neumltc *x)
{
	object_free(x->x_proxy);
	ltc_decoder_free(x->decoder);
	ltc_encoder_free(x->encoder);
	sysmem_freeptr(x->dec_buffer);
}

//void neumltc_setFps(t_neumltc *x, long value)
void neumltc_setFps(t_neumltc *x, t_object *attr, long ac, t_atom *av)
{
    long val = atom_getlong(av);

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
    x->set_fps = val;
    post("neum.ltc~: FPS set %.2f", x->fps);
}

//void neumltc_setAutoincrease(t_neumltc *x, long value)
void neumltc_setAutoincrease(t_neumltc *x, t_object *attr, long ac, t_atom *av)
{
    long value = atom_getlong(av);

    if (value <= 0) {
        value = 0;
        post("neum.ltc~: set as DECODER");
    }

    if (value >= 1) {
        value = 1;
        post("neum.ltc~: set as ENCODER");
    }
	x->auto_increase = value;
}

void neumltc_setTime(t_neumltc *x, t_symbol *s, long argc, t_atom *argv)
{
	if (argc != 4) {
		post("time: please pass a list with four numbers");
	} else {
		if (atom_getlong(&argv[3]) > x->fps) {
			post ("requested frame number higher than fps");
		} else {
			x->startTimeCode.hours = atom_getlong(&argv[0]);
			x->startTimeCode.mins = atom_getlong(&argv[1]);
			x->startTimeCode.secs = atom_getlong(&argv[2]);
			x->startTimeCode.frame = atom_getlong(&argv[3]);
			x->startTimeCodeChanged = 1;
		}
	}
}

void neumltc_setMilliseconds(t_neumltc *x, double f)
{
	double timeInSeconds = f / 1000.;
	double intPart = 0;
	double subSecond = modf(timeInSeconds, &intPart);
	x->startTimeCode.hours = timeInSeconds / 360;
	x->startTimeCode.mins = (int)(timeInSeconds / 60) % 60;
	x->startTimeCode.secs = (int)(timeInSeconds) % 60;
	x->startTimeCode.frame = (int)(subSecond * x->fps);
	x->startTimeCodeChanged = 1;
}

// our perform method if both signal inlets are connected
void neumltc_perform64(t_neumltc *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long n, long flags, void *userparam)
{
	t_double *inp =  ins[0];
	t_double *outp = outs[0];
	// check if timecode signal at input
	bool ltc_input = false;
	t_double* input = inp;
	// search for first non-zero sample
	for (int i = 0; i < n; i++) {
		if ((*input++) != 0.f) {
			ltc_input = true;
			break;
		}
	}

	if (ltc_input) { // get timecode from audio signal
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
			atom_setlong(timecode_list,stime.hours);
			atom_setlong(timecode_list+1,stime.mins);
			atom_setlong(timecode_list+2,stime.secs);
			atom_setlong(timecode_list+3,stime.frame);
			outlet_list(x->msg_outlet, 0L, 4, timecode_list);
			x->startTimeCode = stime;
			x->startTimeCodeChanged = 1;
		}
	}
	else // generate timecode
	{
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
				// char timeString[256];
				// sprintf(timeString, "%02d:%02d:%02d:%02d", st.hours, st.mins, st.secs, st.frame);

				t_atom timecode_list[4];
				atom_setlong(timecode_list,st.hours);
				atom_setlong(timecode_list+1,st.mins);
				atom_setlong(timecode_list+2,st.secs);
				atom_setlong(timecode_list+3,st.frame);
				outlet_list(x->msg_outlet, 0L, 4, timecode_list);
				ltc_encoder_encode_frame(x->encoder);
				x->smpteBuffer = ltc_encoder_get_bufptr(x->encoder, &x->smpteBufferLength, 1);
				x->smpteBufferTime = 0;
			}

			*outp++ = x->smpteBuffer[x->smpteBufferTime] / 128. - 1.;
			x->smpteBufferTime++;
		}
	}
}

// method called when dsp is turned on
void neumltc_dsp64(t_neumltc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	object_method(dsp64, gensym("dsp_add64"), x, neumltc_perform64, 0, NULL);
}
