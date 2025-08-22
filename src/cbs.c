#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps2mc.h"
#include "miniz.h"

#define  CBS_HEADER_MAGIC   "CFU\0"

typedef struct cbsHeader
{
    char magic[4];
    uint32_t unk1;
    uint32_t dataOffset;
    uint32_t decompressedSize;
    uint32_t compressedSize;
    char name[32];
    sceMcStDateTime created;
    sceMcStDateTime modified;
    uint32_t unk2;
    uint32_t mode;
    char unk3[16];
    char title[72];
    char description[132];
} cbsHeader_t;

typedef struct cbsEntry
{
    sceMcStDateTime created;
    sceMcStDateTime modified;
    uint32_t length;
    uint32_t mode;
    char unk1[8];
    char name[32];
} cbsEntry_t;

// This is the initial permutation state ("S") for the RC4 stream cipher
// algorithm used to encrpyt and decrypt Codebreaker saves.
// Source: https://github.com/ps2dev/mymc/blob/master/ps2save.py#L36
const uint8_t cbsKey[256] = {
    0x5f, 0x1f, 0x85, 0x6f, 0x31, 0xaa, 0x3b, 0x18,
    0x21, 0xb9, 0xce, 0x1c, 0x07, 0x4c, 0x9c, 0xb4,
    0x81, 0xb8, 0xef, 0x98, 0x59, 0xae, 0xf9, 0x26,
    0xe3, 0x80, 0xa3, 0x29, 0x2d, 0x73, 0x51, 0x62,
    0x7c, 0x64, 0x46, 0xf4, 0x34, 0x1a, 0xf6, 0xe1,
    0xba, 0x3a, 0x0d, 0x82, 0x79, 0x0a, 0x5c, 0x16,
    0x71, 0x49, 0x8e, 0xac, 0x8c, 0x9f, 0x35, 0x19,
    0x45, 0x94, 0x3f, 0x56, 0x0c, 0x91, 0x00, 0x0b,
    0xd7, 0xb0, 0xdd, 0x39, 0x66, 0xa1, 0x76, 0x52,
    0x13, 0x57, 0xf3, 0xbb, 0x4e, 0xe5, 0xdc, 0xf0,
    0x65, 0x84, 0xb2, 0xd6, 0xdf, 0x15, 0x3c, 0x63,
    0x1d, 0x89, 0x14, 0xbd, 0xd2, 0x36, 0xfe, 0xb1,
    0xca, 0x8b, 0xa4, 0xc6, 0x9e, 0x67, 0x47, 0x37,
    0x42, 0x6d, 0x6a, 0x03, 0x92, 0x70, 0x05, 0x7d,
    0x96, 0x2f, 0x40, 0x90, 0xc4, 0xf1, 0x3e, 0x3d,
    0x01, 0xf7, 0x68, 0x1e, 0xc3, 0xfc, 0x72, 0xb5,
    0x54, 0xcf, 0xe7, 0x41, 0xe4, 0x4d, 0x83, 0x55,
    0x12, 0x22, 0x09, 0x78, 0xfa, 0xde, 0xa7, 0x06,
    0x08, 0x23, 0xbf, 0x0f, 0xcc, 0xc1, 0x97, 0x61,
    0xc5, 0x4a, 0xe6, 0xa0, 0x11, 0xc2, 0xea, 0x74,
    0x02, 0x87, 0xd5, 0xd1, 0x9d, 0xb7, 0x7e, 0x38,
    0x60, 0x53, 0x95, 0x8d, 0x25, 0x77, 0x10, 0x5e,
    0x9b, 0x7f, 0xd8, 0x6e, 0xda, 0xa2, 0x2e, 0x20,
    0x4f, 0xcd, 0x8f, 0xcb, 0xbe, 0x5a, 0xe0, 0xed,
    0x2c, 0x9a, 0xd4, 0xe2, 0xaf, 0xd0, 0xa9, 0xe8,
    0xad, 0x7a, 0xbc, 0xa8, 0xf2, 0xee, 0xeb, 0xf5,
    0xa6, 0x99, 0x28, 0x24, 0x6c, 0x2b, 0x75, 0x5d,
    0xf8, 0xd3, 0x86, 0x17, 0xfb, 0xc0, 0x7b, 0xb3,
    0x58, 0xdb, 0xc7, 0x4b, 0xff, 0x04, 0x50, 0xe9,
    0x88, 0x69, 0xc9, 0x2a, 0xab, 0xfd, 0x5b, 0x1b,
    0x8a, 0xd9, 0xec, 0x27, 0x44, 0x0e, 0x33, 0xc8,
    0x6b, 0x93, 0x32, 0x48, 0xb6, 0x30, 0x43, 0xa5
}; 

void psv_resign(const char* src_file);
void get_psv_filename(char* psvName, const char* dirName);

int isCBSFile(const char *path)
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
    if(len < (int)sizeof(cbsHeader_t))
    {
        fclose(f);
        return 0;
    }

    // Verify header magic
    char magic[4];
    fread(magic, 1, 4, f);
    fclose(f);

    if(memcmp(magic, CBS_HEADER_MAGIC, 4) != 0)
        return 0;

    return 1;
}

static void rc4Crypt(unsigned char *buf, size_t bufLen, const unsigned char *perm)
{
    size_t i;
    unsigned char j = 0;
    unsigned char k = 0;
    unsigned char temp;
    unsigned char s[256];
    
    memcpy(s, perm, 256);
    
    for(i = 0; i < bufLen; i++)
    {
        j += 1;
        k += s[j];
        
        temp = s[j];
        s[j] = s[k];
        s[k] = temp;
        
        buf[i] ^= s[(s[j] + s[k]) & 0xFF];
    }
}

void cbsCrypt(unsigned char *buf, size_t bufLen)
{
    rc4Crypt(buf, bufLen, cbsKey);
}

int extractCBS(const char *save)
{
    FILE *dstFile;
    FILE *cbsFile;
    u8 *cbsData;
    u8 *compressed;
    u8 *decompressed;
    cbsHeader_t *header;
    cbsEntry_t entryHeader;
    unsigned long decompressedSize;
    size_t cbsLen;
    int i, numFiles = 0;
    u32 dataPos = 0, offset = 0;
    char dstName[256];

    if(!isCBSFile(save))
        return 0;

    cbsFile = fopen(save, "rb");
    if(!cbsFile)
        return 0;
    
    fseek(cbsFile, 0, SEEK_END);
    cbsLen = ftell(cbsFile);
    fseek(cbsFile, 0, SEEK_SET);
    cbsData = malloc(cbsLen);
    fread(cbsData, 1, cbsLen, cbsFile);
    fclose(cbsFile);

    header = (cbsHeader_t *)cbsData;
    get_psv_filename(dstName, header->name);
    dstFile = fopen(dstName, "wb");

    if (!dstFile)
        return 0;

    // Get data for file entries
    compressed = cbsData + sizeof(cbsHeader_t);
    // Some tools create .CBS saves with an incorrect compressed size in the header.
    // It can't be trusted!
    cbsCrypt(compressed, cbsLen - sizeof(cbsHeader_t));
    decompressed = tinfl_decompress_mem_to_heap(compressed, cbsLen - sizeof(cbsHeader_t), &decompressedSize, TINFL_FLAG_PARSE_ZLIB_HEADER);
    
    if(!decompressed || (decompressedSize != header->decompressedSize))
    {
        // Compression failed.
        printf("Decompression failed! (Expected Size %d (%ld)\n", header->decompressedSize, decompressedSize);
        free(cbsData);
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

    ps2md.attribute = header->mode;
    memcpy(&ps2md.create, &header->created, sizeof(sceMcStDateTime));
    memcpy(&ps2md.modified, &header->modified, sizeof(sceMcStDateTime));
    memcpy(&ps2md.filename, &header->name, sizeof(ps2md.filename));
    
    ph.headerSize = 0x0000002C;
	ph.saveType = 0x00000002;
    memcpy(&ph.magic, "\0VSP", 4);
    memcpy(&ph.salt, "www.bucanero.com.ar", 20);

	fwrite(&ph, sizeof(psv_header_t), 1, dstFile);

    printf("Save contents:\n");

    // Find the icon.sys (need to know the icons names)
    while(offset < (decompressedSize - sizeof(cbsEntry_t)))
    {
        numFiles++;

        /* Entry header can't be read directly because it might not be 32-bit aligned.
        GCC will likely emit an lw instruction for reading the 32-bit variables in the
        struct which will halt the processor if it tries to load from an address
        that's misaligned. */
        memcpy(&entryHeader, &decompressed[offset], sizeof(cbsEntry_t));
        entryHeader.length = (entryHeader.length);
        
        offset += sizeof(cbsEntry_t);

        if(strcmp(entryHeader.name, "icon.sys") == 0)
            ps2sys = (ps2_IconSys_t*) &decompressed[offset];

        ps2h.displaySize += entryHeader.length;
        offset += entryHeader.length;

        printf(" %8d bytes  : %s\n", entryHeader.length, entryHeader.name);
    }

    printf(" %8d Total bytes\n", ps2h.displaySize);
    ps2h.displaySize = (ps2h.displaySize);
    ps2h.numberOfFiles = (numFiles);
    ps2md.numberOfFilesInDir = (numFiles+2);

    if (!ps2sys)
        return 0;

    // Calculate the start offset for the file's data
    dataPos = sizeof(psv_header_t) + sizeof(ps2_header_t) + sizeof(ps2_MainDirInfo_t) + sizeof(ps2_FileInfo_t)*numFiles;

    ps2_FileInfo_t *ps2fi = malloc(sizeof(ps2_FileInfo_t)*numFiles);

    // Build the PS2 FileInfo entries
    for(i = 0, offset = 0; i < numFiles; i++)
    {
        memcpy(&entryHeader, &decompressed[offset], sizeof(cbsEntry_t));
        offset += sizeof(cbsEntry_t);

        ps2fi[i].attribute = entryHeader.mode;
        ps2fi[i].positionInFile = (dataPos);
        ps2fi[i].filesize = entryHeader.length;
        memcpy(&ps2fi[i].create, &entryHeader.created, sizeof(sceMcStDateTime));
        memcpy(&ps2fi[i].modified, &entryHeader.modified, sizeof(sceMcStDateTime));
        memcpy(&ps2fi[i].filename, &entryHeader.name, sizeof(ps2fi[i].filename));

        entryHeader.length = (entryHeader.length);
        dataPos += entryHeader.length;

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

        offset += entryHeader.length;
    }

    fwrite(&ps2h, sizeof(ps2_header_t), 1, dstFile);
    fwrite(&ps2md, sizeof(ps2_MainDirInfo_t), 1, dstFile);
    fwrite(ps2fi, sizeof(ps2_FileInfo_t), numFiles, dstFile);

    free(ps2fi);

    // Write the file's data
    for(i = 0, offset = 0; i < numFiles; i++)
    {
        memcpy(&entryHeader, &decompressed[offset], sizeof(cbsEntry_t));
        entryHeader.length = (entryHeader.length);
        offset += sizeof(cbsEntry_t);

        fwrite(&decompressed[offset], 1, entryHeader.length, dstFile);
 
        offset += entryHeader.length;
    }

    fclose(dstFile);
    free(decompressed);
    free(cbsData);
    psv_resign(dstName);

    return 1;
}
