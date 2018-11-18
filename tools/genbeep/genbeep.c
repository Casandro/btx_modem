#include <stdio.h>
#include <math.h>

//This program generates a sound that is used to both disable echo cancellers on the line and make the modem dump its user data

// run this like this
// gcc genbeep.c -lm -o genbeep && ./genbeep | sox -t dat -r 8000 -c 1 - -b 16 initbeep.wav
//  mv initbeep.wav /var/lib/asterisk/sounds/

 

#define SRATE (8000.0)
#define FA (2100.0) //Binary 0
#define FZ (1300.0) //Binary 1

#define AMPLITUDE (0.5)


double phi=0; //Current phase

void addphase(double phase)
{
	phi=phi+phase;
	if (phi>M_PI*2) phi=phi-M_PI*2;
}

void print_sample(double sample)
{
	printf("0 %lf\n", sample*AMPLITUDE);
}

void modulate_sample(double frq)
{
	addphase(FA*M_PI*2.0/SRATE);
	print_sample(sin(phi));
}


void make_ans() //Generate ANS tone to turn off echo canceller
{
	int phaserev=SRATE*0.45;
	int n;
	for (n=0; n<SRATE*3.3; n++) {
		modulate_sample(FA);
		if ((n%phaserev)==0) addphase(M_PI);
	}
}

void make_tone(double dur, double frq) //Generates dur seconds of frq
{
	int n;
	for (n=0; n<SRATE*dur; n++) {
		modulate_sample(frq);
	}
}

int main(int argc, char **argv)
{
	make_ans();
	make_tone(1.6, 1300);
	make_tone(10.0/1200.0, 2100);
	make_tone(2, 1300);
}
