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

#define SEED_OFFSET 0x8
#define HASH_OFFSET 0x1c
#define TYPE_OFFSET 0x3C

#define PSV_MAGIC 0x50535600

const uint8_t key[2][0x10] = {
							{0xFA, 0x72, 0xCE, 0xEF, 0x59, 0xB4, 0xD2, 0x98, 0x9F, 0x11, 0x19, 0x13, 0x28, 0x7F, 0x51, 0xC7},
							{0xAB, 0x5A, 0xBC, 0x9F, 0xC1, 0xF4, 0x9D, 0xE6, 0xA0, 0x51, 0xDB, 0xAE, 0xFA, 0x51, 0x88, 0x59}
						};
const uint8_t iv[0x10] = {0xB3, 0x0F, 0xFE, 0xED, 0xB7, 0xDC, 0x5E, 0xB7, 0x13, 0x3D, 0xA6, 0x0D, 0x1B, 0x6B, 0x2C, 0xDC};


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
	printf("INPUT FORMATS\n");
	printf(" .mcs            PS1 MCS File\n");
	printf(" .psx            PS1 AR/GS/XP PSX File\n");
	printf(" .cbs            PS2 CodeBreaker File\n");
	printf(" .max            PS2 ActionReplay Max File\n");
	printf(" .xps            PS2 Xploder/SharkPort File\n");
	printf(" .psu            PS2 EMS File (uLaunchELF)\n");
	printf(" .psv            PS3 PSV File (to PS1 .mcs/PS2 .psu)\n\n");
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
