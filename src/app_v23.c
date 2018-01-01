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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
	int d;
} demod_state_t;

/* Initializes a demod_state_t struct
 */
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
}

double sqr(double x)
{
	return x*x;
}

/*This gets a sample in x and returns an octet or a negative number
 */
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
	//decode
	if (p<0.2) { //No carrier
		state->pos=-1;
		state->integral=0;
		return -1;
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
int v23_mod(modstate_t *state, unsigned int bit)
{
	int f;
	if ((bit&0x1)==0) f=FAST_FRQ_0/100;
	             else f=FAST_FRQ_1/100;
	state->phi=(state->phi+f)%STABLEN;
	return sinetab[state->phi];
}

void init_modstate(modstate_t *state)
{
	state->phi=0;
	state->spos=-2*SRATE;
	state->data=-1;
}

//Modulates the octet in the current outgoing buffer
//as well as start and stop bits
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
int v23_connect(char *addr)
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


#define BLEN (1024) //length of the circular buffer
#define PLEN (64) //maximum length of a packet
#define READLEN (32) 
#define T1 (12000) 
#define T1C (4)


/*
 * Buffer
 * 0----------------------------------------1024->
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
	int ack_timer; //>1 when timer is waiting; =1 send ENQ; <0 not waiting for ACK
	int ack_count; //when ack_count==1 && ack_timer==0 hang up
} linkstate_t;

void init_linkstate(linkstate_t *s)
{
	memset(s,0,sizeof(linkstate_t));
	s->last=-1;
}

int difference(int to, int from)
{
	if (to>from) return to-from;
	return to-from+BLEN;
}

/* link_layer: Handles the link layer (packed retransmission) 
 * return value:
 * 	0-255: octet sent to the terminal
 *	-1: No data
 *	-2: Hangup
*/
int link_layer(linkstate_t *s, int sock, int input, int since)
{

	//Handle input
	if (input>=0) {
		if (input==NACK) s->as=3; //NACK received
		if ( (input==ACK) && (s->as==0) ) s->as=2; else
		if ( (input==0x10) && (s->as==0) ) s->as=1; else
		if ( (input==0x30) && (s->as==1) ) s->as=2; else
		if ( (input==0x31) && (s->as==1) ) s->as=2; else {
			//send to socket
			char sc=input;
			if (send(sock,&sc,1,0)<0) return -2; //If there's an error, hang up 
		}
	}
	

	//Handle ACK/NACK responses
	if (s->as==2) { //ACK recieved
		s->as=0; //clear ACK
		s->last=-1; //drop last packet 
		s->ack_timer=-1; //Cancel sending ENQs
	}

	if ((s->as==3) && (s->last!=-1) ) { //NACK received
		//prepare retransmitting last packet
		s->current=s->last; //set start
		s->border=s->last; //set resume position
		s->last=-1;
		s->as=0;
		s->ack_timer=-1; //Cancel sending ENQs
		//abort current packet
		return ETB;
	}

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
		if (l>0) {
			int n;
			for (n=0;n<l;n++) {
				s->buffer[s->readp]=b[n];
				s->readp=(s->readp+1)%BLEN;
			}
		} else {
			if (errno==ENOTCONN) {
				return -2;
				printf("errorno==ENOTCONN\n");
			}
		}
	}

	//Handle output
	if ( (s->p<=0) && (s->current!=s->readp) ) { //Send STX
		s->p=1;
		s->crc=0;
		 //Packet starts here at current
		return STX;
	}

	if (s->p==1) { //In data-sending mode
		int end_packet=0;
		if (s->current==s->readp) end_packet=1; //No data to send
		//Calculate packet size
		int ps=(s->current-s->border); 
		if (ps<0) ps=ps+BLEN;
		if (ps>PLEN) end_packet=1;
		if ( (end_packet==0) ) { //Data to send
			int b=s->buffer[s->current]; //get byte
			s->current=(s->current+1)%BLEN;
			s->crc=crc(s->crc,b);
			return b;
		} else { //No data to send
			if (s->last>=0) return -1; //If there's been no ACK, return
			//otherwise end packet
			s->crc=crc(s->crc,ETX);
			s->p=3;
			return ETX;
		} 
	}
	if ( (s->p==3) ) { //Send CRC l
		s->p=4; //next state CRC h
		return s->crc%256;
	}
	if ( (s->p==4) ) { //Send CRC h
		s->p=-1; //Next state idle
		s->last=s->border;
		s->border=s->current;
		//Start ACK timer
		s->ack_timer=T1;
		s->ack_count=T1C;
		return s->crc/256;
	}

	//Handle ENQ
	if (s->ack_timer>0) {
		s->ack_timer=s->ack_timer-since;
		if (s->ack_timer<=0) {
			if (s->ack_count==0) return -2; //Hangup
			s->ack_count=s->ack_count-1;
			s->ack_timer=T1;
			return ENQ;	
		}
	}

	//Idle state
	s->p=-1;
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
	int read_octet=-1;
	int since=0; //samples since link_layer was called the last time;
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
				since=since+1;
				int e=v23_demodulate(d[n],&demod_state);
				
				if (e>=0) {
					char sc=e;
					read_octet=e;
				}
				d[n]=v23_modulate(&mod_state);
				if (mod_state.spos==-1) {
					int rc=link_layer(&link_state,sock,read_octet,since);
					since=0;
					read_octet=-1;
					if (rc<-2) goto end; 
					if (rc>=0) {
						mod_state.data=rc;
						mod_state.spos=0;
					} else mod_state.spos=-1;

				} 
			}
			ast_write(chan, f);
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
