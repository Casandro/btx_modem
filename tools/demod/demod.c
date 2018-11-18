#include <stdio.h>


/* This program takes samples in the "dat" format of sox at 4 times the medium frequency
 * The output is half of the intput sample rate.
 */

int main(int argc, char **argv)
{
	double t;
	double x;
	double i[3]={0,0,0};
	double q[3]={0,0,0};
	int n=0;
	while (scanf("%lf%lf", &t, &x)==2) {
		if (n==0) i[0]=x; else
		if (n==1) q[0]=x; else
		if (n==2) i[0]=-x; else
		if (n==3) q[0]=-x;
		if (n%2==1) {
			double i_=i[2]-i[0];
			double q_=q[2]-i[0];
			double f=i_*q[1]-q_*i[1];
			double p=i[1]*i[1]+q[1]*q[1];
			double frq=-f/p;
			if (p<2e-3) frq=0;
			printf("0 %lf\n", frq/10);
			i[2]=i[1]; i[1]=i[0];
			q[2]=q[1]; q[1]=q[0];
		}
		n=(n+1)%4;
	}

}
