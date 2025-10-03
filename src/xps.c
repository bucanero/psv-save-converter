#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps2mc.h"

#define XPS_HEADER_MAGIC "SharkPortSave\0\0\0"
#define mode_swap(M)     ((M & 0x00FF) << 8) + ((M & 0xFF00) >> 8)

typedef struct __attribute__((__packed__)) xpsEntry
{
    uint16_t entry_sz;
    char name[64];
    uint32_t length;
    uint32_t start;
    uint32_t end;
    uint32_t mode;
    sceMcStDateTime created;
    sceMcStDateTime modified;
    char unk1[4];
    char padding[12];
    char title_ascii[64];
    char title_sjis[64];
    char unk2[8];
} xpsEntry_t;


void psv_resign(const char* src_file);
void get_psv_filename(char* psvName, const char* dirName);

int extractXPS(const char *save)
{
    u32 dataPos = 0;
    FILE *xpsFile, *psvFile;
    int numFiles, i;
    char dstName[128];
    char tmp[100];
    u32 len;
    u8 *data;
    xpsEntry_t entry;
    
    xpsFile = fopen(save, "rb");
    if(!xpsFile)
        return 0;

    fread(&tmp, 1, 0x15, xpsFile);

    if (memcmp(&tmp[4], XPS_HEADER_MAGIC, 16) != 0)
    {
        printf("Not a valid XPS file: %s\n", save);
        fclose(xpsFile);
        return 0;
    }

    // Skip the variable size header
    fread(&len, 1, sizeof(u32), xpsFile);
    fread(&tmp, 1, len, xpsFile);
    fread(&len, 1, sizeof(u32), xpsFile);
    fread(&tmp, 1, len, xpsFile);
    fread(&len, 1, sizeof(u32), xpsFile);
    fread(&len, 1, sizeof(u32), xpsFile);
    
    // Read main directory entry
    fread(&entry, 1, sizeof(xpsEntry_t), xpsFile);
    numFiles = entry.length - 2;

    // Keep the file position (start of file entries)
    len = ftell(xpsFile);
    
    get_psv_filename(dstName, entry.name);
    psvFile = fopen(dstName, "wb");
    
    if(!psvFile)
    {
        printf("Failed to create PSV file: %s\n", dstName);
        fclose(xpsFile);
        return 0;
    }

    psv_header_t ph;
    ps2_header_t ps2h;
    ps2_IconSys_t ps2sys;
    ps2_MainDirInfo_t ps2md;
    
    memset(&ph, 0, sizeof(psv_header_t));
    memset(&ps2h, 0, sizeof(ps2_header_t));
    memset(&ps2md, 0, sizeof(ps2_MainDirInfo_t));
    
    ps2h.numberOfFiles = numFiles;

    ps2md.attribute = mode_swap(entry.mode);
    ps2md.numberOfFilesInDir = entry.length;
    memcpy(&ps2md.create, &entry.created, sizeof(sceMcStDateTime));
    memcpy(&ps2md.modified, &entry.modified, sizeof(sceMcStDateTime));
    memcpy(&ps2md.filename, &entry.name, sizeof(ps2md.filename));
    
    ph.headerSize = 0x0000002C;
	ph.saveType = 0x00000002;
    memcpy(&ph.magic, "\0VSP", 4);
    memcpy(&ph.salt, "www.bucanero.com.ar", 20);

	fwrite(&ph, sizeof(psv_header_t), 1, psvFile);

	// Find the icon.sys (need to know the icons names)
    for(i = 0; i < numFiles; i++)
    {
        fread(&entry, 1, sizeof(xpsEntry_t), xpsFile);

		if(strcmp(entry.name, "icon.sys") == 0)
			fread(&ps2sys, 1, sizeof(ps2_IconSys_t), xpsFile);
		else
			fseek(xpsFile, entry.length, SEEK_CUR);

		ps2h.displaySize += entry.length;

	    printf(" %8d bytes  : %s\n", entry.length, entry.name);
	}

    // Rewind
    fseek(xpsFile, len, SEEK_SET);

	// Calculate the start offset for the file's data
	dataPos = sizeof(psv_header_t) + sizeof(ps2_header_t) + sizeof(ps2_MainDirInfo_t) + sizeof(ps2_FileInfo_t)*numFiles;

	ps2_FileInfo_t *ps2fi = malloc(sizeof(ps2_FileInfo_t)*numFiles);

	// Build the PS2 FileInfo entries
    for(i = 0; i < numFiles; i++)
    {
        fread(&entry, 1, sizeof(xpsEntry_t), xpsFile);

		ps2fi[i].attribute = mode_swap(entry.mode);
		ps2fi[i].positionInFile = dataPos;
		ps2fi[i].filesize = entry.length;
		memcpy(&ps2fi[i].create, &entry.created, sizeof(sceMcStDateTime));
		memcpy(&ps2fi[i].modified, &entry.modified, sizeof(sceMcStDateTime));
		memcpy(&ps2fi[i].filename, &entry.name, sizeof(ps2fi[i].filename));
		
		dataPos += entry.length;
		fseek(xpsFile, entry.length, SEEK_CUR);
		
		if (strcmp(ps2fi[i].filename, ps2sys.IconName) == 0)
		{
			ps2h.icon1Size = ps2fi[i].filesize;
			ps2h.icon1Pos = ps2fi[i].positionInFile;
		}

		if (strcmp(ps2fi[i].filename, ps2sys.copyIconName) == 0)
		{
			ps2h.icon2Size = ps2fi[i].filesize;
			ps2h.icon2Pos = ps2fi[i].positionInFile;
		}

		if (strcmp(ps2fi[i].filename, ps2sys.deleteIconName) == 0)
		{
			ps2h.icon3Size = ps2fi[i].filesize;
			ps2h.icon3Pos = ps2fi[i].positionInFile;
		}

		if(strcmp(ps2fi[i].filename, "icon.sys") == 0)
		{
			ps2h.sysSize = ps2fi[i].filesize;
			ps2h.sysPos = ps2fi[i].positionInFile;
		}
	}

	fwrite(&ps2h, sizeof(ps2_header_t), 1, psvFile);
	fwrite(&ps2md, sizeof(ps2_MainDirInfo_t), 1, psvFile);
	fwrite(ps2fi, sizeof(ps2_FileInfo_t), numFiles, psvFile);

	free(ps2fi);

    printf(" %8d Total bytes\n", ps2h.displaySize);

    // Rewind
    fseek(xpsFile, len, SEEK_SET);
    
    // Copy each file entry
    for(i = 0; i < numFiles; i++)
    {
        fread(&entry, 1, sizeof(xpsEntry_t), xpsFile);
        
        data = malloc(entry.length);
        fread(data, 1, entry.length, xpsFile);
        fwrite(data, 1, entry.length, psvFile);

        free(data);
    }

    fclose(psvFile);
    fclose(xpsFile);
    
    psv_resign(dstName);
    
    return 1;
}
