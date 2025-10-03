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
int extractMAX(const char *save);
int extractMCS(const char *save);
int extractPSX(const char *save);
int extractCBS(const char *save);
int extractXPS(const char *save);

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

void XorWithByte(uint8_t* buf, uint8_t byte, int length)
{
	for (int i = 0; i < length; ++i) {
    	buf[i] ^= byte;
	}
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

void generateHash(uint8_t *input, size_t sz, int type) {
	struct AES_ctx aes_ctx;

	uint8_t salt[0x40];
	uint8_t work_buf[0x14];
		
	uint8_t *salt_seed = input + SEED_OFFSET;
	memset(salt , 0, sizeof(salt));
	
	printf("Type detected: PS%x\n", type);
	if(type == 1) {//PS1
		//idk why the normal cbc doesn't work.
		AES_init_ctx_iv(&aes_ctx, key[1], iv);
		memcpy(work_buf, salt_seed, 0x10);
		AES_ECB_decrypt(&aes_ctx, work_buf);
		memcpy(salt, work_buf, 0x10);

		memcpy(work_buf, salt_seed, 0x10);
		AES_ECB_encrypt(&aes_ctx, work_buf);
		memcpy(salt + 0x10, work_buf, 0x10);

		XorWithIv(salt, iv);
			
		memset(work_buf, 0xFF, sizeof(work_buf));
		memcpy(work_buf, salt_seed + 0x10, 0x4);
		XorWithIv(salt + 0x10, work_buf);
		
	} else if(type == 2) {//PS2
		uint8_t laid_paid[16]  = {	
			0x10, 0x70, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x10, 0x70, 0x00, 0x03, 0xFF, 0x00, 0x00, 0x01 };

		memcpy(salt, salt_seed, 0x14);
		XorWithIv(laid_paid, key[0]);
		AES_init_ctx_iv(&aes_ctx, laid_paid, iv);
		AES_CBC_decrypt_buffer(&aes_ctx, salt, 0x40);
	} else {
		printf("Unsupported .psv type\n");
		return;
	}
	
	memset(salt + 0x14, 0, sizeof(salt) - 0x14);
	
	XorWithByte(salt, 0x36, 0x40);
		
	SHA1_CTX sha1_ctx_1;	
	SHA1Init(&sha1_ctx_1);
	
	SHA1Update(&sha1_ctx_1, salt, 0x40);

	memset(input + HASH_OFFSET, 0, 0x14);
	SHA1Update(&sha1_ctx_1, input, sz);
				
	XorWithByte(salt, 0x6A, 0x40);

	SHA1Final(work_buf, &sha1_ctx_1);

	SHA1_CTX sha1_ctx_2;
	SHA1Init(&sha1_ctx_2);
	SHA1Update(&sha1_ctx_2, salt, 0x40);
	SHA1Update(&sha1_ctx_2, work_buf, 0x14);

	SHA1Final(input + HASH_OFFSET, &sha1_ctx_2);
}

void psv_resign(const char* src_file)
{
	FILE *fin = 0, *fout = 0;
	fin = fopen(src_file, "rb");
	if (!fin) {
		perror("Failed to open input file");
		goto error;
	}

	fseek(fin, 0, SEEK_END);
	size_t sz = ftell(fin);
	printf("\nPSV File Size: %ld bytes\n", sz);
	fseek(fin, 0, SEEK_SET);
	
	uint8_t *input = (unsigned char*) calloc (1, sz);
	uint32_t *input_ptr = (uint32_t*) input;
	fread(input, sz,1,fin);
	
	if(input_ptr[0] != PSV_MAGIC) {
		perror("Not a PSV file");
		free(input);
		goto error;
	}
	
	generateHash(input, sz, input[TYPE_OFFSET]);
	
	printf("New signature: ");
	for(int i = 0; i < 0x14; i++ ) {
		printf("%02X ", input[HASH_OFFSET + i]);
	}
	printf("\n");

	fout = fopen(src_file, "wb");
	if (!fout) {
		perror("Failed to open output file");
		free(input);
		goto error;
	}
	fwrite(input,  1, sz, fout);
	free(input);
	printf("PSV resigned successfully: %s\n", src_file);


error:
	if (fin)
		fclose(fin);
	if (fout)
		fclose(fout);	

}

int ps1_psv2mcs(const char* psvfile)
{
	char dstName[256];
	uint8_t mcshdr[128];
	size_t sz;
	uint8_t *input;
	FILE *pf;
	ps1_header_t *ps1h;

    pf = fopen(psvfile, "rb");
    if(!pf)
        return 0;

    fseek(pf, 0, SEEK_END);
    sz = ftell(pf);
    fseek(pf, 0, SEEK_SET);
    input = malloc(sz);
    fread(input, 1, sz, pf);
    fclose(pf);

	snprintf(dstName, sizeof(dstName), "%s", psvfile);
	strcpy(strrchr(dstName, '.'), ".mcs");
	pf = fopen(dstName, "wb");
	if (!pf) {
		perror("Failed to open output file");
		free(input);
		return 0;
	}
	
	ps1h = (ps1_header_t*)(input + 0x40);

	memset(mcshdr, 0, sizeof(mcshdr));
	memcpy(mcshdr + 4, &ps1h->saveSize, 4);
	memcpy(mcshdr + 56, &ps1h->saveSize, 4);
	memcpy(mcshdr + 10, ps1h->prodCode, sizeof(ps1h->prodCode));
	mcshdr[0] = 0x51;
	mcshdr[8] = 0xFF;
	mcshdr[9] = 0xFF;

	for (int x=0; x<127; x++)
		mcshdr[127] ^= mcshdr[x];

	fwrite(mcshdr, sizeof(mcshdr), 1, pf);
	fwrite(input + 0x84, sz - 0x84, 1, pf);
	fclose(pf);
	free(input);

	printf("MCS generated successfully: %s\n", dstName);
	return 1;
}

int ps2_psv2psu(const char *save)
{
    u32 dataPos = 0;
    FILE *psuFile, *psvFile;
    int numFiles, next;
    char dstName[256];
    u8 *data;
    ps2_McFsEntry entry;
    ps2_MainDirInfo_t ps2md;
    ps2_FileInfo_t ps2fi;
    
    psvFile = fopen(save, "rb");
    if(!psvFile)
        return 0;

    snprintf(dstName, sizeof(dstName), "%s", save);
    strcpy(strrchr(dstName, '.'), ".psu");
    psuFile = fopen(dstName, "wb");
    
    if(!psuFile)
    {
        fclose(psvFile);
        return 0;
    }

    // Read main directory entry
    fseek(psvFile, 0x68, SEEK_SET);
    fread(&ps2md, sizeof(ps2_MainDirInfo_t), 1, psvFile);
    numFiles = (ps2md.numberOfFilesInDir);

    memset(&entry, 0, sizeof(entry));
    memcpy(&entry.created, &ps2md.create, sizeof(sceMcStDateTime));
    memcpy(&entry.modified, &ps2md.modified, sizeof(sceMcStDateTime));
    memcpy(entry.name, ps2md.filename, sizeof(entry.name));
    entry.mode = ps2md.attribute;
    entry.length = ps2md.numberOfFilesInDir;
    fwrite(&entry, sizeof(entry), 1, psuFile);

    // "."
    memset(entry.name, 0, sizeof(entry.name));
    strcpy(entry.name, ".");
    entry.length = 0;
    fwrite(&entry, sizeof(entry), 1, psuFile);
    numFiles--;

    // ".."
    strcpy(entry.name, "..");
    fwrite(&entry, sizeof(entry), 1, psuFile);
    numFiles--;

    while (numFiles > 0)
    {
        fread(&ps2fi, sizeof(ps2_FileInfo_t), 1, psvFile);
        dataPos = ftell(psvFile);

        memset(&entry, 0, sizeof(entry));
        memcpy(&entry.created, &ps2fi.create, sizeof(sceMcStDateTime));
        memcpy(&entry.modified, &ps2fi.modified, sizeof(sceMcStDateTime));
        memcpy(entry.name, ps2fi.filename, sizeof(entry.name));
        entry.mode = ps2fi.attribute;
        entry.length = ps2fi.filesize;
        fwrite(&entry, sizeof(entry), 1, psuFile);

        ps2fi.positionInFile = (ps2fi.positionInFile);
        ps2fi.filesize = (ps2fi.filesize);
        data = malloc(ps2fi.filesize);

        fseek(psvFile, ps2fi.positionInFile, SEEK_SET);
        fread(data, 1, ps2fi.filesize, psvFile);
        fwrite(data, 1, ps2fi.filesize, psuFile);
        free(data);

        next = 1024 - (ps2fi.filesize % 1024);
        if(next < 1024)
        {
            data = malloc(next);
            memset(data, 0xFF, next);
            fwrite(data, 1, next, psuFile);
            free(data);
        }

        fseek(psvFile, dataPos, SEEK_SET);
        numFiles--;
    }

    fclose(psvFile);
    fclose(psuFile);

    printf("PSU generated successfully: %s\n", dstName);
    return 1;
}

int extractPSV(const char* psvfile)
{
	uint8_t input[0x40];
	FILE *pf;

    pf = fopen(psvfile, "rb");
    if(!pf)
        return 0;

    fread(input, 1, sizeof(input), pf);
    fclose(pf);

	if (*(uint32_t*) input != PSV_MAGIC) {
		printf("Not a .psv file\n");
		return 0;
	}

	printf("Exporting %s file...\n", psvfile);
	
	switch (input[TYPE_OFFSET])
	{
		case 1:
			ps1_psv2mcs(psvfile);
			break;

		case 2:
			ps2_psv2psu(psvfile);
			break;
			
		default:
			printf("Unsupported .psv type\n");
			return 0;
	}

	return 1;
}

int main(int argc, char **argv)
{
	printf("\n PSV Save Converter v1.2.2 - (c) 2020-2025 by Bucanero\n\n");

	if (argc != 2) {
		usage(argv);
		return 1;
	}

	if (endsWith(argv[1], ".max"))
		extractMAX(argv[1]);

	else if (endsWith(argv[1], ".psu"))
		extractPSU(argv[1]);

	else if (endsWith(argv[1], ".mcs"))
		extractMCS(argv[1]);

	else if (endsWith(argv[1], ".psx"))
		extractPSX(argv[1]);

	else if (endsWith(argv[1], ".cbs"))
		extractCBS(argv[1]);

	else if (endsWith(argv[1], ".xps"))
		extractXPS(argv[1]);

	else if (endsWith(argv[1], ".psv"))
		extractPSV(argv[1]);

	else
		usage(argv);

	return 0;
}
