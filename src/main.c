/*
*
*	PSV Save Converter - (c) 2020 by Bucanero - www.bucanero.com.ar
*
* This tool is based on the ps3-psvresigner by @dots_tb (https://github.com/dots-tb/ps3-psvresigner)
*
* PS2 Save format code from:
*	- https://github.com/PMStanley/PSV-Exporter
*	- https://github.com/root670/CheatDevicePS2
*
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#include "aes.h"
#include "sha1.h"
#include "ps2mc.h"


int extractPSU(const char *save);

char* endsWith(const char * a, const char * b)
{
	int al = strlen(a), bl = strlen(b);
    
	if (al < bl)
		return NULL;

	a += (al - bl);
	while (*a)
		if (toupper(*a++) != toupper(*b++)) return NULL;

	return (char*) (a - bl);
}

static void usage(char *argv[])
{
	printf("This tool converts and resigns PS1/PS2 saves to PS3 .PSV savegame format.\n\n");
	printf("USAGE: %s <filename>\n\n", argv[0]);
	printf("INPUT FORMAT\n");
	printf(" .psu            PS2 EMS File (uLaunchELF)\n\n");
	return;
}

int main(int argc, char **argv)
{
	printf("\n PSV Save Converter v1.2.1 - (c) 2020 by Bucanero\n\n");

	if (argc != 2) {
		usage(argv);
		return 1;
	}

	if (endsWith(argv[1], ".psu"))
		extractPSU(argv[1]);

	else
		usage(argv);

	return 0;
}
