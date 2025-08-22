#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "ps2mc.h"
#include "lzari.h"

#define  HEADER_MAGIC   "Ps2PowerSave"

typedef struct maxHeader
{
    char    magic[12];
    u32     crc;
    char    dirName[32];
    char    iconSysName[32];
    u32     compressedSize;
    u32     numFiles;

    // This is actually the start of the LZARI stream, but we need it to
    // allocate the buffer.
    u32     decompressedSize;
} maxHeader_t;

typedef struct maxEntry
{
    u32     length;
    char    name[32];
} maxEntry_t;


void psv_resign(const char* src_file);
void get_psv_filename(char* psvName, const char* dirName);

static void printMAXHeader(const maxHeader_t *header)
{
    if(!header)
        return;

    printf("Magic            : %.*s\n", (int)sizeof(header->magic), header->magic);
    printf("CRC              : %08X\n", header->crc);
    printf("dirName          : %.*s\n", (int)sizeof(header->dirName), header->dirName);
    printf("iconSysName      : %.*s\n", (int)sizeof(header->iconSysName), header->iconSysName);
    printf("compressedSize   : %u\n", header->compressedSize);
    printf("numFiles         : %u\n", header->numFiles);
    printf("decompressedSize : %u\n", header->decompressedSize);
}

static int roundUp(int i, int j)
{
    return (i + j - 1) / j * j;
}

int isMAXFile(const char *path)
{
    if(!path)
        return 0;

    FILE *f = fopen(path, "rb");
    if(!f)
        return 0;

    // Verify file size
    fseek(f, 0, SEEK_END);
    int len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(len < (int)sizeof(maxHeader_t))
    {
        fclose(f);
        return 0;
    }

    // Verify header
    maxHeader_t header;
    fread(&header, 1, sizeof(maxHeader_t), f);
    fclose(f);

    printMAXHeader(&header);

    return (header.compressedSize > 0) &&
           (header.decompressedSize > 0) &&
           (header.numFiles > 0) &&
           strncmp(header.magic, HEADER_MAGIC, sizeof(header.magic)) == 0 &&
           strlen(header.dirName) > 0 &&
           strlen(header.iconSysName) > 0;
}

void setMcDateTime(sceMcStDateTime* mc, struct tm *ftm)
{
    mc->Resv2 = 0;
    mc->Sec = ftm->tm_sec;
    mc->Min = ftm->tm_min;
    mc->Hour = ftm->tm_hour;
    mc->Day = ftm->tm_mday;
    mc->Month = ftm->tm_mon + 1;
    mc->Year = ftm->tm_year + 1900;
}

int extractMAX(const char *save)
{
    if (!isMAXFile(save))
    	return 0;
    
    FILE *f = fopen(save, "rb");
    if(!f)
        return 0;

	struct stat st;
	struct tm *ftm;
	sceMcStDateTime fctime;
	sceMcStDateTime fmtime;

	fstat(fileno(f), &st);

	ftm = gmtime(&st.st_ctime);
	setMcDateTime(&fctime, ftm);
	
	ftm = gmtime(&st.st_mtime);
	setMcDateTime(&fmtime, ftm);


    maxHeader_t header;
    fread(&header, 1, sizeof(maxHeader_t), f);

    char dirName[sizeof(header.dirName) + 1];
    char psvName[128];

    memcpy(dirName, header.dirName, sizeof(header.dirName));
    dirName[32] = '\0';
	get_psv_filename(psvName, dirName);

    // Get compressed file entries
    u8 *compressed = malloc(header.compressedSize);

    fseek(f, sizeof(maxHeader_t) - 4, SEEK_SET); // Seek to beginning of LZARI stream.
    u32 ret = fread(compressed, 1, header.compressedSize, f);
    if(ret != header.compressedSize)
    {
        printf("Compressed size: actual=%d, expected=%d\n", ret, header.compressedSize);
        free(compressed);
        return 0;
    }

    fclose(f);
    u8 *decompressed = malloc(header.decompressedSize);

    ret = unlzari(compressed, header.compressedSize, decompressed, header.decompressedSize);
    // As with other save formats, decompressedSize isn't acccurate.
    if(ret == 0)
    {
        printf("Decompression failed.\n");
        free(decompressed);
        free(compressed);
        return 0;
    }

    free(compressed);

    int i;
    u32 offset = 0;
    u32 dataPos = 0;
    maxEntry_t *entry;
    FILE* psv;
    
    psv = fopen(psvName, "wb");
    
    psv_header_t ph;
    ps2_header_t ps2h;
    ps2_IconSys_t *ps2sys;
    ps2_MainDirInfo_t ps2md;
    
    memset(&ph, 0, sizeof(psv_header_t));
    memset(&ps2h, 0, sizeof(ps2_header_t));
    memset(&ps2md, 0, sizeof(ps2_MainDirInfo_t));
    
    ps2h.numberOfFiles = header.numFiles;

    ps2md.attribute = 0x00008427;
    ps2md.numberOfFilesInDir = header.numFiles+2;
    memcpy(&ps2md.create, &fctime, sizeof(sceMcStDateTime));
    memcpy(&ps2md.modified, &fmtime, sizeof(sceMcStDateTime));
    memcpy(&ps2md.filename, &dirName, sizeof(ps2md.filename));
    
    ph.headerSize = 0x0000002C;
	ph.saveType = 0x00000002;
    memcpy(&ph.magic, "\0VSP", 4);
    memcpy(&ph.salt, "www.bucanero.com.ar", 20);

	fwrite(&ph, sizeof(psv_header_t), 1, psv);

	printf("\nSave contents:\n");

	// Find the icon.sys (need to know the icons names)
    for(i = 0, offset = 0; i < (int)header.numFiles; i++)
    {
        entry = (maxEntry_t*) &decompressed[offset];
        offset += sizeof(maxEntry_t);

		if(strcmp(entry->name, "icon.sys") == 0)
			ps2sys = (ps2_IconSys_t*) &decompressed[offset];

        offset = roundUp(offset + entry->length + 8, 16) - 8;
		ps2h.displaySize += entry->length;

	    printf(" %8d bytes  : %s\n", entry->length, entry->name);
	}

	// Calculate the start offset for the file's data
	dataPos = sizeof(psv_header_t) + sizeof(ps2_header_t) + sizeof(ps2_MainDirInfo_t) + sizeof(ps2_FileInfo_t)*header.numFiles;

	ps2_FileInfo_t *ps2fi = malloc(sizeof(ps2_FileInfo_t)*header.numFiles);

	// Build the PS2 FileInfo entries
    for(i = 0, offset = 0; i < (int)header.numFiles; i++)
    {
        entry = (maxEntry_t*) &decompressed[offset];
        offset += sizeof(maxEntry_t);

		ps2fi[i].attribute = 0x00008497;
		ps2fi[i].positionInFile = dataPos;
		ps2fi[i].filesize = entry->length;
    	memcpy(&ps2fi[i].create, &fctime, sizeof(sceMcStDateTime));
    	memcpy(&ps2fi[i].modified, &fmtime, sizeof(sceMcStDateTime));
		memcpy(&ps2fi[i].filename, &entry->name, sizeof(ps2fi[i].filename));
		
		dataPos += entry->length;
		
		if (strcmp(ps2fi[i].filename, ps2sys->IconName) == 0)
		{
			ps2h.icon1Size = ps2fi[i].filesize;
			ps2h.icon1Pos = ps2fi[i].positionInFile;
		}

		if (strcmp(ps2fi[i].filename, ps2sys->copyIconName) == 0)
		{
			ps2h.icon2Size = ps2fi[i].filesize;
			ps2h.icon2Pos = ps2fi[i].positionInFile;
		}

		if (strcmp(ps2fi[i].filename, ps2sys->deleteIconName) == 0)
		{
			ps2h.icon3Size = ps2fi[i].filesize;
			ps2h.icon3Pos = ps2fi[i].positionInFile;
		}

		if(strcmp(ps2fi[i].filename, "icon.sys") == 0)
		{
			ps2h.sysSize = ps2fi[i].filesize;
			ps2h.sysPos = ps2fi[i].positionInFile;
		}

        offset = roundUp(offset + entry->length + 8, 16) - 8;
	}

	fwrite(&ps2h, sizeof(ps2_header_t), 1, psv);
	fwrite(&ps2md, sizeof(ps2_MainDirInfo_t), 1, psv);
	fwrite(ps2fi, sizeof(ps2_FileInfo_t), header.numFiles, psv);

	free(ps2fi);

    printf(" %8d Total bytes\n", ps2h.displaySize);
    
	// Write the file's data
    for(i = 0, offset = 0; i < (int)header.numFiles; i++)
    {
        entry = (maxEntry_t*) &decompressed[offset];
        offset += sizeof(maxEntry_t);

        fwrite(&decompressed[offset], 1, entry->length, psv);
 
        offset = roundUp(offset + entry->length + 8, 16) - 8;
    }

	fclose(psv);
    free(decompressed);

	psv_resign(psvName);
    
    return 1;
}
