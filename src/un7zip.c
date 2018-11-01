#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LZMA/C/7zip/Archive/7z_C/7zCrc.h"
#include "LZMA/C/7zip/Archive/7z_C/7zIn.h"
#include "LZMA/C/7zip/Archive/7z_C/7zExtract.h"
#include <zlib.h>

typedef struct _CFileInStream
{
  ISzInStream InStream;
  FILE *File;
} CFileInStream;

#ifdef _LZMA_IN_CB

#define kBufferSize (1 << 12)
Byte g_Buffer[kBufferSize];

SZ_RESULT SzFileReadImp(void *object, void **buffer, size_t maxRequiredSize, size_t *processedSize)
{
  CFileInStream *s = (CFileInStream *)object;
  size_t processedSizeLoc;
  if (maxRequiredSize > kBufferSize)
    maxRequiredSize = kBufferSize;
  processedSizeLoc = fread(g_Buffer, 1, maxRequiredSize, s->File);
  *buffer = g_Buffer;
  if (processedSize != 0)
    *processedSize = processedSizeLoc;
  return SZ_OK;
}

#else

SZ_RESULT SzFileReadImp(void *object, void *buffer, size_t size, size_t *processedSize)
{
  CFileInStream *s = (CFileInStream *)object;
  size_t processedSizeLoc = fread(buffer, 1, size, s->File);
  if (processedSize != 0)
    *processedSize = processedSizeLoc;
  return SZ_OK;
}

#endif

SZ_RESULT SzFileSeekImp(void *object, CFileSize pos)
{
  CFileInStream *s = (CFileInStream *)object;
  int res = fseek(s->File, (long)pos, SEEK_SET);
  if (res == 0)
    return SZ_OK;
  return SZE_FAIL;
}

int un7zip_and_gzip(const char *filename, const char *folder, int compressionLevel, char **playlistFilename)
{
  CFileInStream archiveStream;
  CArchiveDatabaseEx db;
  SZ_RESULT res;
  ISzAlloc allocImp;
  ISzAlloc allocTempImp;
	char *destFileName;

  *playlistFilename = NULL;

  archiveStream.File = fopen(filename, "rb");
  if (archiveStream.File == 0)
  {
    return 1;
  }

  archiveStream.InStream.Read = SzFileReadImp;
  archiveStream.InStream.Seek = SzFileSeekImp;

  allocImp.Alloc = SzAlloc;
  allocImp.Free = SzFree;

  allocTempImp.Alloc = SzAllocTemp;
  allocTempImp.Free = SzFreeTemp;

  InitCrcTable();
  SzArDbExInit(&db);
  res = SzArchiveOpen(&archiveStream.InStream, &db, &allocImp, &allocTempImp);
  if (res == SZ_OK)
  {
    UInt32 i;

    /*
    if you need cache, use these 3 variables.
    if you use external function, you can make these variable as static.
    */
    UInt32 blockIndex = 0xFFFFFFFF; /* it can have any value before first call (if outBuffer = 0) */
    Byte *outBuffer = 0; /* it must be 0 before first call for each new archive. */
    size_t outBufferSize = 0;  /* it can have any value before first call (if outBuffer = 0) */

		for (i = 0; i < db.Database.NumFiles; i++)
    {
      size_t offset;
      size_t outSizeProcessed;
      CFileItem *f = db.Database.Files + i;
      UInt32 processedSize;
			int compress = 0;
			char *p;

			if (f->IsDirectory)
      {
        continue;
      }
      res = SzExtract(&archiveStream.InStream, &db, i, 
          &blockIndex, &outBuffer, &outBufferSize, 
          &offset, &outSizeProcessed, 
          &allocImp, &allocTempImp);
      if (res != SZ_OK)
        break;

			// strip out any directory name
			p = strrchr(f->Name,'/');
			if (p == NULL)
				p = f->Name;
			else
				p++;

			// make the output filename
			destFileName = malloc( strlen( folder ) + strlen( p ) + 2 );
			if (!destFileName)
				break;
			strcpy( destFileName, folder );
			strcat( destFileName, "\\" );
			strcat( destFileName, p );

			// do we want to GZip it?
			compress = ( strncmp(outBuffer + offset,"Vgm ",4) == 0 );

			if (compress)
			{
				gzFile *out;
				char params[4] = "wb?";

				if (compressionLevel < 0)
					compressionLevel = 0;
				else if (compressionLevel > 9)
					compressionLevel = 9;

				params[2] = '0' + compressionLevel;

				out = gzopen( destFileName, params );

				if (out == 0)
				{
					res = SZE_FAIL;
					break;
				}

	      processedSize = gzwrite(out,outBuffer + offset, outSizeProcessed);
				if (processedSize != outSizeProcessed)
				{
					res = SZE_FAIL;
					break;
				}
	      if (gzclose(out))
				{
					res = SZE_FAIL;
					break;
				}
			}
			else
			{
				// !compress
				FILE *outputHandle;
	      outputHandle = fopen(destFileName, "wb+");

				if (outputHandle == 0)
				{
					res = SZE_FAIL;
					break;
				}

	      processedSize = fwrite(outBuffer + offset, 1, outSizeProcessed, outputHandle);
				if (processedSize != outSizeProcessed)
				{
					res = SZE_FAIL;
					break;
				}
	      if (fclose(outputHandle))
				{
					res = SZE_FAIL;
					break;
				}

				// record the first playlist we find
				if (*playlistFilename == NULL)
				{
          p = strrchr( destFileName, '.' );
					if (p && strcmp( p, ".m3u" ) == 0 )
						*playlistFilename = strdup( destFileName );
				}
			}
    }
    allocImp.Free(outBuffer);

		free(destFileName);
  }
  SzArDbExFree(&db, allocImp.Free);

  fclose(archiveStream.File);
  if (res == SZ_OK)
  {
    return 0;
  }
  return 1;
}

void un7zip_free_pl_fn( char *playlistFilename )
{
	free(playlistFilename);
}