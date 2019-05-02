/**
neum.ltc_decoder~
*/

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "../../../libltc/ltc.h"
#include "../../../libltc/decoder.h"

void *neumltc_class;

typedef struct _neumltc {
	t_pxobject      x_obj;
	double          x_val;
	void            *msg_outlet;
	LTCDecoder      *decoder;
	double          length; // in seconds
	double          sampleRate;
	SMPTETimecode   startTimeCode;
	int             startTimeCodeChanged;
	LTCFrameExt     frame;
	unsigned int    dec_bufpos;
	float           *dec_buffer; // has to contain 256 samples...
} t_neumltc;

void *neumltc_new(t_symbol *s, long argc, t_atom *argv);
void neumltc_dsp64(t_neumltc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void neumltc_assist(t_neumltc *x, void *b, long m, long a, char *s);
void neumltc_free(t_neumltc *x);

void ext_main(void *r)
{
	t_class *c = class_new("neum.ltc.dec~", (method)neumltc_new, (method)neumltc_free, sizeof(t_neumltc), 0L, A_GIMME, 0);
	class_addmethod(c, (method)neumltc_dsp64, "dsp64", A_CANT, 0); 	// respond to the dsp message
	class_addmethod(c, (method)neumltc_assist, "assist", A_CANT,0);
	class_dspinit(c);	// must call this function for MSP object classes
	class_register(CLASS_BOX, c);
	neumltc_class = c;
}

void *neumltc_new(t_symbol *s, long argc, t_atom *argv)
{
    t_neumltc *x = (t_neumltc *)object_alloc(neumltc_class);
	dsp_setup((t_pxobject *)x,1);	        // set up DSP for the instance and create signal inlets
	x->msg_outlet = outlet_new(x, NULL);
    x->x_val = 0;
	x->startTimeCodeChanged = 1;

	const char timezone[6] = "+0100";
	strcpy(x->startTimeCode.timezone, timezone);
	x->startTimeCode.years = 0;
	x->startTimeCode.months = 0;
	x->startTimeCode.days = 0;
	x->startTimeCode.hours = 0;
	x->startTimeCode.mins = 0;
	x->startTimeCode.secs = 0;
	x->startTimeCode.frame = 0;
	
	// decoder
	int apv = sys_getsr()*1/25;
	x->dec_bufpos = 0;
	x->dec_buffer = (float*)sysmem_newptr(sizeof(float) * 256);// allocate buffer
	x->decoder = ltc_decoder_create(apv, 32);

	return (x);
}


void neumltc_assist(t_neumltc *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) {
		switch (a) {
			case 0:
                sprintf(s,"(Signal) timecode");
                break;
		}
	} else {
		switch (a) {
			case 0:
                sprintf(s,"list: hr min sec frame");
                break;
		}
	}
}

void neumltc_free(t_neumltc *x)
{
	ltc_decoder_free(x->decoder);
	sysmem_freeptr(x->dec_buffer);
}


void neumltc_perform64(t_neumltc *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long n, long flags, void *userparam)
{
	t_double *inp =  ins[0];
    
    unsigned int bufpos = x->dec_bufpos;
    
    for (int i = 0; i < n; i++) {
        if (bufpos > 255) {
            ltc_decoder_write_float(x->decoder, x->dec_buffer, 256, 0);
            bufpos = 0;
        }
        x->dec_buffer[bufpos++] = inp[i];
    }
    
    x->dec_bufpos = bufpos;
    
    while (ltc_decoder_read(x->decoder, &x->frame)) {
        SMPTETimecode stime;
        ltc_frame_to_time(&stime, &x->frame.ltc, 1);
        
        t_atom timecode_list[4];
        
        atom_setlong(timecode_list,stime.hours);
        atom_setlong(timecode_list + 1,stime.mins);
        atom_setlong(timecode_list + 2,stime.secs);
        atom_setlong(timecode_list + 3,stime.frame);
        outlet_list(x->msg_outlet, 0L, 4, timecode_list);
        
        x->startTimeCode = stime;
        x->startTimeCodeChanged = 1;
    }
}

void neumltc_dsp64(t_neumltc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	object_method(dsp64, gensym("dsp_add64"), x, neumltc_perform64, 0, NULL);
}
