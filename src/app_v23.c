/*
 *
 * Christian Berger <christian@clarke-3.de>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief V23 application -- Emulates a v23 modem for BTX
 *
 * \author Christian Berger <christian@clarke-3.de>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include <math.h>
#include <stdint.h>

/*** DOCUMENTATION
	<application name="V23" language="en_US">
		<synopsis>
			Emulates a BTX server side modem
		</synopsis>
		<syntax />
		<description>
			<para>This emulates a V.23 modem along with the necessary link layer protocol for BTX</para>
			<para>This application does not automatically answer and should be
			preceeded by an application such as Answer() or Progress().</para>
		</description>
	</application>
 ***/

static const char app[] = "V23";
//Asterisk resamples Audio to 12kHz, makes it simpler that way
#define SRATE (12000)
#define FAST_BIT_RATE (1200)
#define FAST_BIT_LEN (SRATE/FAST_BIT_RATE)
#define SLOW_BIT_RATE (75)
#define SLOW_BIT_LEN (SRATE/SLOW_BIT_RATE)
#define FAST_FRQ_0 (2100.0)
#define FAST_FRQ_1 (1300.0)
#define SLOW_FRQ (420.0)

#define SLOW_FILTER_ORDER (5)
#define SLOW_FILTER_C (0.08)
#define FAST_AMPLITUDE (20000)


/* struct containing the demodulator state
 * contains demodulation phase, filter states as well as the 
 * position in the current octet
 */
typedef struct {
	double phi;
	int pos;
	double iq[3][2]; //iq[0] newest iq[2] oldest
	double f[SLOW_FILTER_ORDER][2]; //filter states
	double integral;
	double avg_power; //average power
	int d;
} demod_state_t;

/* Initializes a demod_state_t struct
 */
void init_demod_state(demod_state_t *state);
void init_demod_state(demod_state_t *state)
{
	int n,m;
	for (n=0;n<2;n++){
		for (m=0;m<3;m++) state->iq[m][n]=0;
		for (m=0;m<SLOW_FILTER_ORDER;m++) state->f[m][n]=0;
	}
	state->phi=0;
	state->pos=-2;
	state->integral=0;
	state->d=0;
	state->avg_power=0;
}

double sqr(double x);
double sqr(double x)
{
	return x*x;
}

/*This gets a sample in x and returns an octet or a negative number
 *  0..255 received octet
 *  -1 no data
 *  -2 no carrier
 */
int v23_demodulate(int x, demod_state_t *state);
int v23_demodulate(int x, demod_state_t *state)
{
	int n;
	int data=-1;
	//find the complex signal
	double i=x*sin(state->phi);
	double q=x*cos(state->phi);
	state->phi=state->phi+(SLOW_FRQ/SRATE*2*M_PI);
	if (state->phi>2*M_PI) state->phi=state->phi-2*M_PI;
	//filter complex signal
	state->f[0][0]=(state->f[0][0]*(1-SLOW_FILTER_C))+i*SLOW_FILTER_C;
	state->f[0][1]=(state->f[0][1]*(1-SLOW_FILTER_C))+q*SLOW_FILTER_C;
	for (n=1;n<SLOW_FILTER_ORDER;n++) {	
		state->f[n][0]=(state->f[n][0]*(1-SLOW_FILTER_C))+state->f[n-1][0]*SLOW_FILTER_C;
		state->f[n][1]=(state->f[n][1]*(1-SLOW_FILTER_C))+state->f[n-1][1]*SLOW_FILTER_C;
	}
	//demodulate
	for (n=0;n<1;n++) {
		state->iq[n+1][0]=state->iq[n][0];
		state->iq[n+1][1]=state->iq[n][1];
	}
	state->iq[0][0]=state->f[SLOW_FILTER_ORDER-1][0];
	state->iq[0][1]=state->f[SLOW_FILTER_ORDER-1][1];
	double q_=state->iq[0][1]-state->iq[2][1];
	double i_=state->iq[0][0]-state->iq[2][0];
	double f_=state->iq[1][0]*q_ -state->iq[1][1]*i_;
	double p=sqr(state->iq[1][0])+sqr(state->iq[1][1]);
	double frq=0; //Frequency
	if (p!=0) frq=-f_/p; //>0 => 1; <0 => 0
	state->avg_power=(state->avg_power*0.9)+p*0.1;
	//decode
	if (state->avg_power<100000) { //No carrier
		state->pos=-1;
		state->integral=0;
		return -2;
	}
	if (state->pos<0) {
		if (frq<0) {
			state->pos=0;
			state->integral=0;
		}
	} else {
		int bpos=state->pos/SLOW_BIT_LEN;
		state->integral=state->integral+frq;
		state->pos=state->pos+1;
		if ((state->pos%SLOW_BIT_LEN)==0) {
			if (bpos==0) { //Start Bit
				//Check Start bit
				if (state->integral>0) {
					//No real Star Bit => reset
					state->pos=-1;
				} else {
					state->d=0; //reset data bits
				}
			} else
			if (bpos<9) {
				//Data bits
				int bit=0;
				if (state->integral>0) bit=1;
					else bit=0;
				state->d=(state->d>>1)|(bit<<7);
			} else 
			if (bpos==9) {
				if (state->integral<0) {
					//framing error
					state->pos=-1;
				} else {
					//Correct framing
					state->pos=-1;
					data=state->d;
					state->d=0;
				}
			}
			//printf("%f\n",state->integral);
			state->integral=0;
		}
	}
	return data;
}


#define STABLEN (120)

//Table for modulating v23
int sinetab[STABLEN]={
      0,    1046,    2090,    3128,    4158,    5176,    6180,    7167,    8134,    9079,    9999,   10892,
  11755,   12586,   13382,   14142,   14862,   15542,   16180,   16773,   17320,   17820,   18270,   18671,
  19021,   19318,   19562,   19753,   19890,   19972,   20000,   19972,   19890,   19753,   19562,   19318,
  19021,   18671,   18270,   17820,   17320,   16773,   16180,   15542,   14862,   14142,   13382,   12586,
  11755,   10892,    9999,    9079,    8134,    7167,    6180,    5176,    4158,    3128,    2090,    1046,
      0,   -1046,   -2090,   -3128,   -4158,   -5176,   -6180,   -7167,   -8134,   -9079,  -10000,  -10892,
 -11755,  -12586,  -13382,  -14142,  -14862,  -15542,  -16180,  -16773,  -17320,  -17820,  -18270,  -18671,
 -19021,  -19318,  -19562,  -19753,  -19890,  -19972,  -20000,  -19972,  -19890,  -19753,  -19562,  -19318,
 -19021,  -18671,  -18270,  -17820,  -17320,  -16773,  -16180,  -15542,  -14862,  -14142,  -13382,  -12586,
 -11755,  -10892,  -10000,   -9079,   -8134,   -7167,   -6180,   -5176,   -4158,   -3128,   -2090,   -1046};


#define STX (0x02)
#define ITB (0x07)
#define ETB (0x17)
#define ETX (0x03)
#define EOT (0x04)
#define ENQ (0x05)
#define ACK (0x06)
#define NACK (0x15)


typedef struct {
	int phi; //phase of the modulator
	int spos; //position in samples within an octet
	int data; //Current octet in send buffer
} modstate_t;

//Modulates a single bit and outputs a sample
int v23_mod(modstate_t *state, unsigned int bit);
int v23_mod(modstate_t *state, unsigned int bit)
{
	int f;
	if ((bit&0x1)==0) f=FAST_FRQ_0/100;
	             else f=FAST_FRQ_1/100;
	state->phi=(state->phi+f)%STABLEN;
	return sinetab[state->phi];
}

void init_modstate(modstate_t *state);
void init_modstate(modstate_t *state)
{
	state->phi=0;
	state->spos=-2*SRATE;
	state->data=-1;
}

//Modulates the octet in the current outgoing buffer
//as well as start and stop bits
int v23_modulate(modstate_t *state);
int v23_modulate(modstate_t *state)
{
	//Handle idle state
	if (state->spos<-1) state->spos=state->spos+1;
	if (state->spos>=0) state->spos=state->spos+1;
	if (state->spos<0) return v23_mod(state,1); //Idle state
	//calculate bit in octet 0 1-8 9
	int bpos=state->spos/FAST_BIT_LEN;
	if (bpos==0) return v23_mod(state,0); //Start bit
	if (bpos<9)  return v23_mod(state,(state->data) >> (bpos-1)); //Data bit
	if (bpos==9) return v23_mod(state,1); //Stopp bit
	if (bpos>9) { //end of current octet
		state->spos=-1; //set idle state
		state->data=-1; //set buffer to -1
		return v23_mod(state,1);
	}
	return v23_mod(state,1); //This should never be reached
}

/* Connects to a server, parameter is "address port"
 */
int v23_connect(const char *addr);
int v23_connect(const char *addr)
{
	int port=0;
	char ip[strlen(addr)];
	strcpy(ip,addr);
	char *p=strchr(ip,' ');
	p[0]=0;
	if (p[1]!=0) port=atoi(&(p[1]));
	int sock=socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server;
	server.sin_addr.s_addr = inet_addr(ip);
	server.sin_family= AF_INET;
	server.sin_port=htons(port);
	int res=connect(sock, (struct sockaddr *)&server, sizeof(server));
//	printf("v23_connect: %d %s %d \n", res, strerror(errno),sock);
	int flags= fcntl(sock, F_GETFL, 0);
	flags=flags|O_NONBLOCK;
	fcntl(sock,F_SETFL, flags);
	return sock;
}

#define BUFFLEN (64)


uint16_t crc(uint16_t cr, uint8_t b);
uint16_t crc(uint16_t cr, uint8_t b)
{
	uint16_t c=cr;
	int n;  
	c=c^b;  
	for (n=0;n<8;n++) {
		if ((c&1)>0) c=(c>>1)^0xA001;
	        else c=(c>>1);
	}       
	return c;
}


#define BLEN (128) //length of the circular buffer
#define PLEN (40) //maximum length of a packet
#define READLEN (32) 
#define T1 (12000) 
#define T1C (4)


/*
 * Buffer
 * 0----------------------------------------BLEN->
 *      ^     ^   ^
 *      A     B   C
*/

typedef struct {
	uint8_t buffer[BLEN];
	uint16_t crc;
	int last; //pointer to start of previous packet in case of retransmission, -1=nothing to retransmit
	int border; //border between last and current packet (first new octet)
	int current; //pointer to the next octet to send out
	int readp; //pointer to the next octet to be read from socket
	int as; //ACK state: 1=first character received, 2=ACK received, 3=NACK received, other=idle
	int p; //State <=0: idle; 1: sent STX; 3: send CRC l; 4: send CRC h
	int ack_state; //1: $10 has been received
	int ack_count; //when ack_count==1 && ack_timer==0 hang up
	int neg_state; //state of the negotiation -1=no carrier detected, 0=ready to send
	int blocklength; //length of the block
	int last_etx;
} linkstate_t;

void init_linkstate(linkstate_t *s)
{
	memset(s,0,sizeof(linkstate_t));
	s->last=-1;
	s->neg_state=-1;
	s->current=-1;
	s->border=0;
	s->last_etx=-1;
}


int difference(int to, int from);

int difference(int to, int from)
{
	if (to>from) return to-from;


int ll_get_data(linkstate_t *s, int sock);
int ll_get_data(linkstate_t *s, int sock)
{
	//Read from socket
	int lb=s->last; //find out last octet to keep
	int f=0;
	if (lb<0) lb=s->border;
	if (lb==s->readp) f=BLEN; else
	if (lb>s->readp) f=(lb-s->readp); else
	if (lb<s->readp) f=BLEN-(s->readp-lb);
	if (f>READLEN*2) {
	//	printf("<READ>\n");
		uint8_t b[READLEN];
		int l=recv(sock,b,READLEN,0);
		printf("read %d\n", l);
		if (l>0) {
			int n;
			for (n=0;n<l;n++) {
				s->buffer[s->readp]=b[n];
				s->readp=(s->readp+1)%BLEN;
			}
		} else {
			if (errno==ENOTCONN) {
				printf("errorno==ENOTCONN\n");
				return -2;
			}
		}
	}
	return 0;
}

int link_layer(linkstate_t *s, int sock, int input, int time);
/* link_layer: Handles the link layer (packed retransmission) 
 * return value:
 * 	0-255: octet sent to the terminal
 *	-1: No data
 *	-2: Hangup
*/
int link_layer(linkstate_t *s, int sock, int input, int time)
{

	//Handle input
	if (input==-2) { //No carrier from the terminal, reset everything
		s->neg_state=-1; //no carrier detected
	} else {
		if( s->neg_state==-1) {
			s->neg_state=1;
		}
	};
	ll_get_data(s, sock);
	if (s->neg_state>0) {
		s->neg_state=s->neg_state+1;
		if (s->neg_state==6000) return 0; //NUL Byte to cause modem to identify
		if (s->neg_state>40000) s->neg_state=0; //Give connection
	}
	if ((s->ack_state==1) && ( (input==0x30) || (input==0x31) || (input==0x3f))) {
		printf("ack_state=1 input=0x%02x\n", input);
		//erase previous frame
		s->last=-1;
		s->last_etx=-1; 
		s->ack_state=-1;
		return -1; //Return, since it'll be called again on next sample
	} else
	if (input==0x10) {
		s->ack_state=1;
	} else
	if (input==ACK) {
		//erase previous frame
		s->last=-1;
		s->last_etx=-1; //stop sending ENQ
	} else
	if (input==NACK) {
		s->last_etx=-1; //stop sending ENQ
		//repeat last frame
		if (s->last>=0) {
			s->border=s->last;
			s->last=-1;
			if (s->current<0) return -1;
			s->current=-1; //Send STX next time
			return EOT; //send EOT
		} //If no previous block, just continue
	} else if (input>=0) { //Normal octet from modem, pass through
		char sc=input;
		if (send(sock, &sc, 1,0)<0) return -2; //Send octet and hang up if there's an error
	}


	if (s->neg_state!=0) { //if there is no carrier, return -1 => idle
		return -1;
	}
	
//	printf("neg_state=%d current=%d last=%d border=%d readp=%d\n", s->neg_state, s->current, s->last, s->border, s->readp);

	if ((s->current==-1) && (s->border>=0) /*&& (s->last<0)*/) { //Send STX as a new block starts
		printf("neg_state=%d current=%d last=%d border=%d readp=%d\n", s->neg_state, s->current, s->last, s->border, s->readp);
		s->current=s->border; //send first character next time
		s->crc=0; //Reset CRC
		s->blocklength=0;
		return STX;
	}
	if ((s->current>=0)) {
		int ch=s->buffer[s->current];
//		printf("ch=%d\n",ch);
		s->current=(s->current+1)%BLEN;
		int end=0;
		if (s->current==s->readp) end=1;
		if (s->blocklength>=BLEN) end=1;
		if (end!=0) { //if all octets have been sent
			printf("end==1\n");
			s->last=s->border;
			s->border=s->current; //
			s->current=-2; //send ETX next time
			s->last_etx=time;
		}
		s->crc=crc(s->crc, ch);
		s->blocklength=s->blocklength+1;
		return ch;
	}
	if (s->current==-2) { //ETX
		s->crc=crc(s->crc,ETX);
		s->current=-3; //Send CRC low next time
		return ETX;
	}
	if (s->current==-3) { //CRC low
		s->current=-4; //next octet CRC high
		return s->crc%256;
	}
	if (s->current==-4) { //CRC hight
		s->current=-1;
		return s->crc/256;
	}

	if (s->last_etx+1000<time) {
		s->last_etx=time;
		return ENQ;
	}

	return -1;
}

/* This is the main function
 * It reads a block of samples from Asterisk
 * overwrites the data and
 * sends it back
 */
static int v23_exec(struct ast_channel *chan, const char *data)
{
	ast_set_read_format(chan, ast_format_slin12);
	ast_set_write_format(chan, ast_format_slin12);
	int res = -1;
	int sock=v23_connect(data); //connect to the data
	demod_state_t demod_state;
	init_demod_state(&demod_state);
	modstate_t mod_state;
	init_modstate(&mod_state);
	linkstate_t link_state;
	init_linkstate(&link_state);
	int time_ms=0; //ms since connection started
	int read_byte=-4;
	while (ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f = ast_read(chan);
		if (!f) {
			break;
		}
		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;
		if (f->frametype == AST_FRAME_VOICE) {
			int n;
			int16_t *d=&(f->data.pad[f->offset*2]); 
			//modulate outgoing audio
			for (n=0; n<f->samples; n++) {
				int e=v23_demodulate(d[n],&demod_state);
				if (e==-2) read_byte=-2;
			       	if (e>=0) {
					read_byte=e;
					printf("read_byte=%d\n", read_byte);
				}
				d[n]=v23_modulate(&mod_state);
				if (mod_state.spos==-1) {
					int rc=link_layer(&link_state,sock,read_byte,time_ms+n/12);
					read_byte=-1;
					//if (rc!=-1) printf("rc=%d %c\n", rc, rc);
					if (rc<-2) goto end; 
					if (rc>=0) {
						mod_state.data=rc;
						mod_state.spos=0;
					} else mod_state.spos=-1;

				} 
			}
			ast_write(chan, f);
			time_ms=time_ms+(f->samples/12);
		} else
		if (f->frametype != AST_FRAME_CONTROL
			&& f->frametype != AST_FRAME_MODEM
			&& f->frametype != AST_FRAME_NULL
			&& ast_write(chan, f)) {
			ast_frfree(f);
			goto end;
		}
		ast_frfree(f);
	}
end:
	close (sock);
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, v23_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Simple V23 Modem");
