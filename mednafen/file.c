/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "file.h"

struct MDFNFILE *file_open(const char *path)
{
   const char *ld;
   FILE *fp;
   struct MDFNFILE *file = (struct MDFNFILE*)calloc(1, sizeof(*file));

   if (!file)
      return NULL;

   fp = fopen(path, "rb");

   if (!fp)
      goto error;

   fseek(fp, 0, SEEK_SET);
   fseek((FILE *)fp, 0, SEEK_END);
   file->size = ftell((FILE *)fp);
   fseek((FILE *)fp, 0, SEEK_SET);

   if (!(file->data = (uint8_t*)malloc(file->size)))
      goto error;
   fread(file->data, 1, file->size, (FILE *)fp);

   ld = (const char*)strrchr(path, '.');
   file->ext = strdup(ld ? ld + 1 : "");

   if (fp)
      fclose((FILE*)fp);

   return file;

error:
   if (fp)
      fclose((FILE*)fp);
   if (file)
      free(file);
   return NULL;
}

int file_close(struct MDFNFILE *file)
{
   if (!file)
      return 0;

   if (file->ext)
      free(file->ext);
   file->ext = NULL;

   if (file->data)
      free(file->data);
   file->data = NULL;

   free(file);

   return 1;
}

uint64_t file_read(struct MDFNFILE *file, void *ptr,
      size_t element_size, size_t nmemb)
{
   int64_t avail = file->size - file->location;
   if (nmemb > avail)
      nmemb = avail;

   memcpy((uint8_t*)ptr, file->data + file->location, nmemb);
   file->location += nmemb;
   return nmemb;
}

int file_seek(struct MDFNFILE *file, int64_t offset, int whence)
{
   size_t ptr;

   switch(whence)
   {
      case SEEK_SET:
         ptr = offset;
         break;
      case SEEK_CUR:
         ptr = file->location + offset;
         break;
      case SEEK_END:
         ptr = file->size + offset;
         break;
   }    

   if (ptr <= file->size)
   {
      file->location = ptr;
      return 0;
   }

   return -1;
}
