#include <stdio.h>
#include <stdint.h>

/* This programs takes demodulated samples at 16 times the bitrate and decodes them */


double t=0;

int read_sample()
{
	double x=0;
	int res=scanf("%lf%lf", &t, &x);
	if (res!=2) return -1;
	if (x<0) return 1;
	return 0;
}


int main(int argc, char** argv)
{
	char *arrow="===";
	if (argc==2) arrow=argv[1];
	while (0==0) {
		int bit=0;
		while ((bit=read_sample())==0) ;
		if (bit!=1) return 0;
		//idle
		while ((bit=read_sample())==1) ;
		if (bit!=0) return 0; 
		//start of start-bit
		int n;
		for (n=0; n<16; n++) bit=read_sample();
		//start of first bit;
		uint16_t byte=0;
		int err=0;
		for (n=0; n<8; n++){ //loop over samples
			int m;
			int sum=0;
			for (m=0; m<16; m++) {
				bit=read_sample();
				if (bit<0) return 0;
				if ( (m>=6) && (m<=8) ) {
					sum=sum+bit;
				}
			}
			if (sum==1) err=err+1; //count probably broken bits
			if (sum==2) err=err+1;
			byte=byte>>1;
			if (sum>=2) byte=byte|(1<<7);
		}	
		if (err<3) {
			if ( (byte>32) && (byte<127) ) printf("%lf\t%s\t%02x\t%c\n", t, arrow, byte, byte); 
			else printf("%lf\t%s\t%02x\n", t, arrow, byte); 
		}
	}
}
