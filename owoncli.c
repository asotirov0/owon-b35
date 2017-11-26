#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#define DISP_MMODE 0x01
#define DISP_SOMETHING 0x02 // really this is just a placeholder

char help[] = " -a <address> [-t] [-o <filename>] [-d] [-q]\n"\
	"\t-h: This help\n"\
	"\t-a <address>: Set the address of the B35 meter, eg, -a 98:84:E3:CD:C0:E5\n"\
	"\t-t: Generate a text file containing current meter data (default to owon.txt)\n"\
	"\t-o <filename>: Set the filename for the meter data ( overrides 'owon.txt' )\n"\
	"\t-d: debug enabled\n"\
	"\t-q: quiet output\n"\
	"\n\n\texample: owoncli -a 98:84:E3:CD:C0:E5 -t -o obsdata.txt\n"\
	"\n";

uint8_t OLs[] = { 0x2b, 0x3f, 0x30, 0x3a, 0x3f };

char default_output[] = "owon.txt";
uint8_t sigint_pressed;

struct glb {
	uint8_t debug;
	uint8_t quiet;
	uint8_t textfile_output;
	uint16_t flags;

	char *output_filename;
	char *b35_address;

};

int init( struct glb *g ) {
	g->debug = 0;
	g->quiet = 0;
	g->flags = 0;
	g->textfile_output = 0;

	g->output_filename = default_output;
	g->b35_address = NULL;

	return 0;
}

int parse_parameters( struct glb *g, int argc, char **argv ) {

	int i;

	for (i = 0; i < argc; i++) {

		if (argv[i][0] == '-') {

			/* parameter */
			switch (argv[i][1]) {
				case 'h':
					fprintf(stdout,"Usage: %s %s", argv[0], help);
					exit(1);
					break;

				case 'a':
					/* set address of B35*/
					i++;
					if (i < argc) g->b35_address = argv[i];
					else {
						fprintf(stderr,"Insufficient parameters; -a <address>\n");
						exit(1);
					}
					break;

				case 'o':
					/* set output file for text */
					i++;
					if (i < argc) g->output_filename = argv[i];
					else {
						fprintf(stderr,"Insufficient parameters; -o <file>\n");
						exit(1);
					}
					break;

				case 't':
					g->textfile_output = 1;
					break;

				case  'd':
					g->debug = 1;
					break;

				case 'q':
					g->quiet = 1;
					/* separate output files */
					break;

				default:
					break;
			} // switch
		}
	}

	return 0;
}


void handle_sigint( int a ) {
	sigint_pressed = 1;
}

int main( int argc, char **argv ) {
	char cmd[1024];
	char minmax[128];
	char mmode[128];
	char uprefix[128];
	char units[128];
	uint8_t dps = 0;
	FILE *fp, *fo;
	struct glb g;

	fo = fp = NULL;

	sigint_pressed = 0;
	signal(SIGINT, handle_sigint); 

	init( &g );
	parse_parameters( &g, argc, argv );

	/*
	 * Sanity check our parameters
	 */
	if (g.b35_address == NULL) exit(1);

	/*
	 * All good... let's get on with the job
	 *
	 * First step, try to open a pipe to gattool so we
	 * can read the bluetooth output from the B35 via STDIN
	 *
	 */
	snprintf(cmd,sizeof(cmd), "gatttool -b %s --char-read --handle 0x2d --listen", argv[1]);
	fp = popen(cmd,"r");
	if (fp == NULL) {
		fprintf(stderr,"Error executing '%s'\n", cmd);
		exit(1);
	} else {
		if (!g.quiet) fprintf(stdout,"Success (%s)\n", cmd);
	}

	/*
	 * If required, open the text file we're going to generate the multimeter
	 * data in to, this is a single frame only data file it is NOT a log file
	 *
	 */
	if (g.textfile_output) {
		fo = fopen("owon.txt","w");
		if (fo == NULL) {
			fprintf(stderr,"Couldn't open owon.data file to write, not saving to file\n");
			g.textfile_output = 0;
		}
	}


	/*
	 * Keep reading, interpreting and converting data until someone
	 * presses ctrl-c or there's an error
	 */
	while (fgets(cmd, sizeof(cmd), fp) != NULL) {
		char *p, *q;
		uint8_t d[14];
		uint8_t i = 0;
		double v = 0.0;


		if (sigint_pressed) {
			if (fo) fclose(fo);
			if (fp) pclose(fp);
			fprintf(stdout, "Exit requested\n");
			fflush(stdout);
			exit(1);
		}

		if (g.debug) fprintf(stdout,"%s", cmd);

		p = strstr(cmd, "2e value: ");
		if (!p) continue;

		if (p) p += sizeof("2e value:");
		if (strlen(p) != 43) {
			if (!g.quiet) {
				fprintf(stdout, "\33[2K\r"); // line erase
				fprintf(stdout,"Waiting...");
			}
			continue;
		}
		while ( p && *p != '\0' && (i < 14) ) {
			d[i] = strtol(p, &q, 16);
			if (!q) break;
			p = q;
			if (g.debug)	fprintf(stdout,"[%d](%02x) ", i, d[i]);
			i++;
		}
		if (g.debug) fprintf(stdout,"\n");

		/** now decode the data in to human-readable 
		 *
		 * first byte is the sign +/-
		 * bytes 1..4 are ASCII char codes for 0000-9999
		 *
		 */
		v = ((d[1] -'0') *1000) 
			+((d[2] -'0') *100)
			+((d[3] -'0') *10)
			+((d[4] -'0') *1);


		dps = 0;
		switch (d[6]) {
			case 48: break; // no DP at all (integer) 
			case 49: v = v/1000; dps = 3; break;
			case 50: v = v/100; dps = 2; break;
			case 51: v = v/10; dps = 1; break;
					 //			case 52: v = v/10; break; // something different though depending on mode
		}

		mmode[0] = '\0';
		switch (d[7]) {
			case 0: snprintf(mmode, sizeof(mmode)," "); break; // temperature?
			case 1: snprintf(mmode, sizeof(mmode),"Manual"); break; // ohms-manual
			case 8: snprintf(mmode, sizeof(mmode),"AC-minmax"); break;
			case 9: snprintf(mmode, sizeof(mmode),"AC-manual"); break;
			case 16: snprintf(mmode, sizeof(mmode),"DC-minmax"); break;
			case 17: snprintf(mmode, sizeof(mmode),"DC-manual"); break;
			case 20: snprintf(mmode, sizeof(mmode),"Delta"); break;
			case 32: snprintf(mmode, sizeof(mmode)," "); break;
			case 33: snprintf(mmode, sizeof(mmode),"Auto"); break; // ohms-auto
			case 41: snprintf(mmode, sizeof(mmode),"AC-auto"); break;
			case 49: snprintf(mmode, sizeof(mmode),"DC-auto");
					 if (d[6] == 50) { v *= 10; dps--; }
					 break;
			case 51: snprintf(mmode, sizeof(mmode),"Hold"); break;
			default: snprintf(mmode, sizeof(mmode),"#%d)",d[7]);
		}

		minmax[0] = '\0';
		switch (d[8]) {
			case 16: snprintf(minmax, sizeof(minmax), "min"); break;
			case 32: snprintf(minmax, sizeof(minmax), "max"); break;
			default: minmax[0] = '\0';
		}

		uprefix[0] = '\0';
		switch (d[9]) {
			case 0: if (d[10] == 4) {
						v /= 10; dps++;
						snprintf(uprefix,sizeof(uprefix),"n");
					}
					break;

			case 2: snprintf(uprefix, sizeof(uprefix),"duty"); break;
			case 4: snprintf(mmode, sizeof(mmode),"Diode"); break; // DIODE test mode
			case 8: snprintf(uprefix, sizeof(uprefix)," "); break;
			case 16: snprintf(uprefix, sizeof(uprefix),"M"); break;
			case 32: snprintf(uprefix, sizeof(uprefix),"K"); break;
			case 64: snprintf(uprefix, sizeof(uprefix),"m");
					 if ((d[10] == 128)||(d[10] == 64)) {
						 v /= 10; dps++;
					 }
					 break;
			case 128: snprintf(uprefix, sizeof(uprefix),"u"); break;
			default: snprintf(uprefix, sizeof(uprefix), "#%d", d[9]);
		}

		units[0] = '\0';
		switch(d[10]) {
			case 0: snprintf(units,sizeof(units),"%%");
			case 1: snprintf(units,sizeof(units),"'F" ); break;
			case 2: snprintf(units,sizeof(units),"'C" ); break;
			case 4: snprintf(units,sizeof(units),"F"); break;
			case 8: snprintf(units,sizeof(units),"Hz"); break;
			case 16: snprintf(units,sizeof(units),"hFe"); break;
			case 32: snprintf(units,sizeof(units),"Ohm");
					 v = v/10;
					 dps++;
					 break;
			case 64: snprintf(units,sizeof(units),"A"); break;
			case 128: snprintf(units,sizeof(units),"V"); break;
			default: snprintf(units,sizeof(units),"#%d", d[10]);
		};

		if (d[0] == '-') v = -v; // polarity

		/** range checks **/
		if ( memcmp( d, OLs, 5 ) == 0 )  {
			snprintf(cmd, sizeof(cmd), "O.L %s\n%s", units, mmode );
		} else {
			if (dps < 0) dps = 0;
			if (dps > 4) dps = 4;

			switch (dps) {
				case 0: snprintf(cmd, sizeof(cmd), "%+05.0f%s%s\n%s %s",v, uprefix, units, mmode, minmax); break;
				case 1: snprintf(cmd, sizeof(cmd), "%+06.1f%s%s\n%s %s",v, uprefix, units, mmode, minmax); break;
				case 2: snprintf(cmd, sizeof(cmd), "%+06.2f%s%s\n%s %s",v, uprefix, units, mmode, minmax); break;
				case 3: snprintf(cmd, sizeof(cmd), "%+06.3f%s%s\n%s %s",v, uprefix, units, mmode, minmax); break;
				case 4: snprintf(cmd, sizeof(cmd), "%+06.4f%s%s\n%s %s",v, uprefix, units, mmode, minmax); break;
			}
		}

		if (g.textfile_output) {
			rewind(fo);
			fprintf(fo, "%s%c", cmd, 0);
			fflush(fo);
		}

		if (!g.quiet) {
			fprintf(stdout, "\33[2K\r"); // line erase
			fprintf(stdout, "\x1B[A"); // line up
			fprintf(stdout, "\33[2K\r"); // line erase
			fprintf(stdout,"%s",cmd);
			fflush(stdout);
		}

	}

	if (pclose(fp)) {
		fprintf(stdout,"Command not found, or exited with error\n");
	}
	return 0;
}
