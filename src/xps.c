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

/*
 * The four bytes after the body are a checksum of it. Not every writer appends
 * them - a file that stops at the last byte of data is accepted - but when
 * they are there they have to agree, because nothing else in an .xps can catch
 * a corrupted byte: the container is uncompressed and the entry sizes are all
 * self-consistent, so a flipped bit inside a file just becomes a flipped bit
 * on the card. A stored zero means the writer left the field alone.
 */
static int xpsChecksumOk(FILE *f, long bodyStart, u32 bodySize)
{
    u8 buf[4096];
    u32 sum = 0, stored;
    long fileLen, end;
    size_t left = bodySize, n, i;

    if(fseek(f, 0, SEEK_END) != 0)
        return 0;

    fileLen = ftell(f);
    if(fileLen < 0 || bodyStart < 0)
        return 0;               /* cannot tell where anything is */

    end = bodyStart + (long) bodySize;

    if(end < bodyStart || end > fileLen)
        return 0;               /* the body overflows, or runs past the file */

    /* Not every writer appends the checksum, so a file that stops at the last
     * byte of data is accepted. Anything other than exactly four trailing
     * bytes is not a trailer this format defines, and there is nothing to
     * check it against - the same call ps2vmc-tool's xps_trailer_ok() makes. */
    if(fileLen - end != 4)
        return 1;

    if(fseek(f, bodyStart, SEEK_SET) != 0)
        return 0;

    while(left > 0)
    {
        n = fread(buf, 1, (left < sizeof(buf)) ? left : sizeof(buf), f);
        if(n == 0)
            return 0;

        for(i = 0; i < n; i++)
            sum += (u32) buf[i] << (sum % 24);

        left -= n;
    }

    if(fread(&stored, 1, sizeof(stored), f) != sizeof(stored))
        return 0;

    return (stored == 0) || (stored == sum);
}

int extractXPS(const char *save)
{
    u32 dataPos = 0;
    FILE *xpsFile, *psvFile;
    int numFiles, i;
    char dstName[128];
    char tmp[100];
    u32 len;
    long bodyStart;
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

    // Skip the variable size header: three length-prefixed strings (title,
    // date and the writing tool's comment), then the size of the rest of the
    // file. The comment is empty in most saves, which makes its length look
    // like a spare zero word - but PS2SaveConverter fills it in, and reading
    // only two strings then skipping 8 bytes lands in the middle of it.
    // Seek past them instead of reading: these are longer than tmp[].
    for (i = 0; i < 3; i++)
    {
        if (fread(&len, 1, sizeof(u32), xpsFile) != sizeof(u32) ||
            fseek(xpsFile, len, SEEK_CUR) != 0)
        {
            printf("Not a valid XPS file: %s\n", save);
            fclose(xpsFile);
            return 0;
        }
    }
    if(fread(&len, 1, sizeof(u32), xpsFile) != sizeof(u32))
    {
        printf("Not a valid XPS file: %s\n", save);
        fclose(xpsFile);
        return 0;
    }

    bodyStart = ftell(xpsFile);
    if(bodyStart < 0)
    {
        printf("Not a valid XPS file: %s\n", save);
        fclose(xpsFile);
        return 0;
    }

    if(!xpsChecksumOk(xpsFile, bodyStart, len))
    {
        printf("ERROR! Damaged save: the file's checksum does not match its "
               "contents. Refusing to convert %s\n", save);
        fclose(xpsFile);
        return 0;
    }
    fseek(xpsFile, bodyStart, SEEK_SET);

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
    // Only filled in if the save has an icon.sys; the icon name comparisons
    // below read it either way.
    memset(&ps2sys, 0, sizeof(ps2_IconSys_t));
    
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
		{
			// Real saves carry icon.sys files that are not exactly
			// sizeof(ps2_IconSys_t): read what fits, then seek past the rest
			// so the stream stays aligned with the entry.
			u32 want = (entry.length < sizeof(ps2_IconSys_t)) ? entry.length : sizeof(ps2_IconSys_t);

			fread(&ps2sys, 1, want, xpsFile);
			fseek(xpsFile, entry.length - want, SEEK_CUR);
		}
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
