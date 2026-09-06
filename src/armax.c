#include <string.h>
#include <stdio.h>
#include <stddef.h>
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

/* zlib's CRC-32 (reflected, polynomial 0xEDB88320), computed a bit at a time.
 * The project links no zlib and this is its only caller, so the table-less
 * form is worth the few extra cycles for how little code it is. */
static u32 crc32_update(u32 crc, const u8 *buf, size_t len)
{
    size_t i;
    int j;

    crc = ~crc;
    for(i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for(j = 0; j < 8; j++)
        {
            u32 mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

/*
 * A .max carries a CRC-32 of the whole file, computed with its own four header
 * bytes taken as zero. Nothing else in the container can catch a corrupted
 * byte: the LZARI stream has no integrity check of its own, so a flipped bit
 * silently becomes a flipped bit in the save. A stored zero means the writer
 * left the field alone, which the original Action Replay software does.
 */
static int maxChecksumOk(FILE *f, u32 stored)
{
    const long at = (long) offsetof(maxHeader_t, crc);
    u8 buf[4096];
    u32 crc = 0;
    long pos = 0;
    size_t n, i;

    if(stored == 0)
        return 1;

    if(fseek(f, 0, SEEK_SET) != 0)
        return 0;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        for(i = 0; i < n; i++)
            if(pos + (long) i >= at && pos + (long) i < at + 4)
                buf[i] = 0;

        crc = crc32_update(crc, buf, n);
        pos += (long) n;
    }

    return crc == stored;
}

static int roundUp(int i, int j)
{
    return (i + j - 1) / j * j;
}

/*
 * Walk the entry chain and check every header and every file's data lies
 * inside what unlzari() actually produced.
 *
 * unlzari() stops when its input runs out and reports how much it wrote, so a
 * truncated or corrupt stream still "succeeds" - it just returns a short
 * buffer. The loops below index the buffer using each entry's declared length
 * and never look at that figure, so without this check a short decode is
 * written out as a save whose last file ends in zeros, and a malformed one
 * reads off the end of the allocation entirely. Refuse both.
 */
static int maxEntriesFit(const u8 *buf, u32 len, u32 numFiles)
{
    const maxEntry_t *e;
    u32 offset = 0, i;

    for(i = 0; i < numFiles; i++)
    {
        if(offset > len || len - offset < sizeof(maxEntry_t))
            return 0;

        e = (const maxEntry_t*) &buf[offset];
        offset += sizeof(maxEntry_t);

        if(e->length > len - offset)
            return 0;

        offset = roundUp(offset + e->length + 8, 16) - 8;
    }
    return 1;
}

static int isMAXFile(const char *path)
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
           strlen(header.dirName) > 0;
}

static void setMcDateTime(sceMcStDateTime* mc, struct tm *ftm)
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
    struct stat st;
    struct tm *ftm;
    sceMcStDateTime fctime;
    sceMcStDateTime fmtime;
    maxHeader_t header;
    char dirName[sizeof(header.dirName) + 1];
    char psvName[128];
    FILE *f, *psv;

    if (!isMAXFile(save))
    {
        printf("ERROR! Not a valid AR Max save: %s\n", save);
        return 0;
    }

    f = fopen(save, "rb");
    if(!f)
        return 0;

    fstat(fileno(f), &st);
    ftm = gmtime(&st.st_ctime);
    setMcDateTime(&fctime, ftm);
    
    ftm = gmtime(&st.st_mtime);
    setMcDateTime(&fmtime, ftm);

    fread(&header, 1, sizeof(maxHeader_t), f);

    if(!maxChecksumOk(f, header.crc))
    {
        printf("ERROR! Damaged save: the file's CRC does not match the one in "
               "its header. Refusing to convert %s\n", save);
        fclose(f);
        return 0;
    }

    memcpy(dirName, header.dirName, sizeof(header.dirName));
    dirName[32] = '\0';
	get_psv_filename(psvName, dirName);

    // Get compressed file entries. compressedSize cannot be trusted either:
    // real saves under-report it (BASLUS-20963FF1200.max claims 17529 for a
    // 17533 byte stream), and handing unlzari only what the header allows
    // starves the decoder - it stops early, returns a short buffer, and the
    // last file in the save is silently truncated. Read everything from the
    // start of the stream to the end of the file instead; the stream carries
    // its own length in its first word, so the extra bytes are harmless.
    long streamStart = sizeof(maxHeader_t) - 4;
    u32 avail;

    fseek(f, 0, SEEK_END);
    avail = (u32)(ftell(f) - streamStart);

    u8 *compressed = malloc(avail);
    if(!compressed)
    {
        fclose(f);
        return 0;
    }

    fseek(f, streamStart, SEEK_SET); // Seek to beginning of LZARI stream.
    u32 ret = fread(compressed, 1, avail, f);
    if(ret != avail)
    {
        printf("WARNING! Compressed size: actual=%d, expected=%d\n", ret, avail);
        avail = ret;
    }

    fclose(f);
    // calloc, not malloc: a short stream leaves the tail untouched, and it
    // must read as zeros rather than as whatever was on the heap.
    u8 *decompressed = calloc(1, header.decompressedSize);
    if(!decompressed)
    {
        free(compressed);
        return 0;
    }

    ret = unlzari(compressed, avail, decompressed, header.decompressedSize);
    free(compressed);
    // As with other save formats, decompressedSize isn't acccurate.
    if(ret == 0)
    {
        printf("Decompression failed.\n");
        free(decompressed);
        return 0;
    }

    if(!maxEntriesFit(decompressed, (u32)ret, header.numFiles))
    {
        printf("ERROR! Truncated or corrupt save: the %u decompressed bytes do "
               "not cover the %u files the header declares.\n",
               (u32)ret, header.numFiles);
        free(decompressed);
        return 0;
    }

    int i;
    u32 offset = 0;
    u32 dataPos = 0;
    maxEntry_t *entry;
    
    psv = fopen(psvName, "wb");
    if (!psv)
    {
        printf("Failed to create PSV file: %s\n", psvName);
        free(decompressed);
        return 0;
    }
    
    psv_header_t ph;
    ps2_header_t ps2h;
    ps2_IconSys_t *ps2sys = NULL;
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
		
		// ps2sys is only set if the save carries an icon.sys; without this
		// guard a save that has none dereferences NULL here.
		if (ps2sys)
		{
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
