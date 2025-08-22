#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ps2mc.h"

void psv_resign(const char* src_file);

void get_psv_filename(char* psvName, const char* dirName)
{
	const char *ch = &dirName[12];

	memcpy(psvName, dirName, 12);
	psvName[12] = 0;
#ifndef PSU_EXTRACTOR
	while (*ch)
	{
		char tmp[3];
		snprintf(tmp, sizeof(tmp), "%02X", *ch++);
		strcat(psvName, tmp);
	}
	strcat(psvName, ".PSV");
#endif
}

int extractPSU(const char *save)
{
    u32 dataPos = 0;
    FILE *psuFile, *psvFile;
    int numFiles, next, i;
    char dstName[128];
    u8 *data;
    ps2_McFsEntry entry;
    
    psuFile = fopen(save, "rb");
    if(!psuFile)
        return 0;
    
    // Read main directory entry
    fread(&entry, 1, sizeof(ps2_McFsEntry), psuFile);
    numFiles = entry.length - 2;
    
    get_psv_filename(dstName, entry.name);
#ifndef PSU_EXTRACTOR
    psvFile = fopen(dstName, "wb");
    
    if(!psvFile)
    {
        fclose(psuFile);
        return 0;
    }
#else
#ifdef _WIN32
    mkdir(dstName);
#else
    mkdir(dstName, 0755);
#endif
    strcat(dstName, "/");
#endif

    psv_header_t ph;
    ps2_header_t ps2h;
    ps2_IconSys_t ps2sys;
    ps2_MainDirInfo_t ps2md;
    
    memset(&ph, 0, sizeof(psv_header_t));
    memset(&ps2h, 0, sizeof(ps2_header_t));
    memset(&ps2md, 0, sizeof(ps2_MainDirInfo_t));
    
    ps2h.numberOfFiles = numFiles;

    ps2md.attribute = entry.mode;
    ps2md.numberOfFilesInDir = entry.length;
    memcpy(&ps2md.create, &entry.created, sizeof(sceMcStDateTime));
    memcpy(&ps2md.modified, &entry.modified, sizeof(sceMcStDateTime));
    memcpy(&ps2md.filename, &entry.name, sizeof(ps2md.filename));
    
    ph.headerSize = 0x0000002C;
	ph.saveType = 0x00000002;
    memcpy(&ph.magic, "\0VSP", 4);
    memcpy(&ph.salt, "www.bucanero.com.ar", 20);

#ifndef PSU_EXTRACTOR
	fwrite(&ph, sizeof(psv_header_t), 1, psvFile);
#endif

    // Skip "." and ".."
    fseek(psuFile, sizeof(ps2_McFsEntry)*2, SEEK_CUR);

	// Find the icon.sys (need to know the icons names)
    for(i = 0; i < numFiles; i++)
    {
        fread(&entry, 1, sizeof(ps2_McFsEntry), psuFile);

		if(strcmp(entry.name, "icon.sys") == 0)
			fread(&ps2sys, 1, sizeof(ps2_IconSys_t), psuFile);
		else
			fseek(psuFile, entry.length, SEEK_CUR);

		ps2h.displaySize += entry.length;

	    printf(" %8d bytes  : %s\n", entry.length, entry.name);

        next = 1024 - (entry.length % 1024);
        if(next < 1024)
            fseek(psuFile, next, SEEK_CUR);
	}

    // Skip "." and ".."
    fseek(psuFile, sizeof(ps2_McFsEntry)*3, SEEK_SET);

	// Calculate the start offset for the file's data
	dataPos = sizeof(psv_header_t) + sizeof(ps2_header_t) + sizeof(ps2_MainDirInfo_t) + sizeof(ps2_FileInfo_t)*numFiles;

	ps2_FileInfo_t *ps2fi = malloc(sizeof(ps2_FileInfo_t)*numFiles);

	// Build the PS2 FileInfo entries
    for(i = 0; i < numFiles; i++)
    {
        fread(&entry, 1, sizeof(ps2_McFsEntry), psuFile);

		ps2fi[i].attribute = entry.mode;
		ps2fi[i].positionInFile = dataPos;
		ps2fi[i].filesize = entry.length;
		memcpy(&ps2fi[i].create, &entry.created, sizeof(sceMcStDateTime));
		memcpy(&ps2fi[i].modified, &entry.modified, sizeof(sceMcStDateTime));
		memcpy(&ps2fi[i].filename, &entry.name, sizeof(ps2fi[i].filename));
		
		dataPos += entry.length;
		fseek(psuFile, entry.length, SEEK_CUR);
		
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

        next = 1024 - (entry.length % 1024);
        if(next < 1024)
            fseek(psuFile, next, SEEK_CUR);
	}

#ifndef PSU_EXTRACTOR
	fwrite(&ps2h, sizeof(ps2_header_t), 1, psvFile);
	fwrite(&ps2md, sizeof(ps2_MainDirInfo_t), 1, psvFile);
	fwrite(ps2fi, sizeof(ps2_FileInfo_t), numFiles, psvFile);
#endif

	free(ps2fi);

    printf(" %8d Total bytes\n\n", ps2h.displaySize);

    // Skip "." and ".."
    fseek(psuFile, sizeof(ps2_McFsEntry)*3, SEEK_SET);
    
    // Copy each file entry
    for(i = 0; i < numFiles; i++)
    {
        fread(&entry, 1, sizeof(ps2_McFsEntry), psuFile);
        
        data = malloc(entry.length);
        fread(data, 1, entry.length, psuFile);
        
#ifdef PSU_EXTRACTOR
        strchr(dstName, '/')[1] = 0;
        strcat(dstName, entry.name);
        printf("+ Writing %s...\n", dstName);

        psvFile = fopen(dstName, "wb");
#endif
        fwrite(data, 1, entry.length, psvFile);
#ifndef PSU_EXTRACTOR
        fclose(psvFile);
#endif

        free(data);
        
        next = 1024 - (entry.length % 1024);
        if(next < 1024)
            fseek(psuFile, next, SEEK_CUR);
    }

    fclose(psuFile);
    
#ifndef PSU_EXTRACTOR
    fclose(psvFile);
    psv_resign(dstName);
#endif
    printf("\n+ All files exported!\n");
    
    return 1;
}
