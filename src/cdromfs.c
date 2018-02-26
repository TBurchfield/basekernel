/*
Copyright (C) 2016 The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file LICENSE for details.
*/

#include "kmalloc.h"
#include "console.h"
#include "kerneltypes.h"
#include "string.h"
#include "iso9660.h"
#include "ata.h"
#include "memory.h"
#include "fs.h"
#include "fs_ops.h"
#include "cdromfs.h"

struct cdrom_volume {
	int unit;
	int root_sector;
	int root_length;
	int total_sectors;
};

struct cdrom_dirent {
	struct cdrom_volume *volume;
	int sector;
	int length;
	int isdir;
};

static struct fs_volume *cdrom_volume_as_volume(struct cdrom_volume *cdv);
static struct fs_dirent *cdrom_dirent_as_dirent(struct cdrom_dirent *cdd);
char * strdup(const char * s);
void strtoupper(char * name);
void strtolower(char * name);

static struct cdrom_dirent * cdrom_dirent_create( struct cdrom_volume *volume, int sector, int length, int isdir )
{
	struct cdrom_dirent *d = kmalloc(sizeof(*d));
	if(!d) return 0;

	d->volume = volume;
	d->sector = sector;
	d->length = length;
	d->isdir = isdir;
	return d;
}

static char * cdrom_dirent_load( struct fs_dirent *d )
{
	struct cdrom_dirent *cdd = d->private_data;
	int nsectors = cdd->length/ATAPI_BLOCKSIZE + (cdd->length&ATAPI_BLOCKSIZE)?1:0;
	char *data = kmalloc(nsectors*ATAPI_BLOCKSIZE);
	if(!data) return 0;

	atapi_read(cdd->volume->unit,data,nsectors,cdd->sector);
	// XXX check result

	return data;
}

static void fix_filename( char *name, int length )
{
	if(length>2) {
		length -= 2;
		name[length] = 0;
	}

	if(length>1 && name[length-1]=='.') {
		length--;
		name[length] = 0;
	}

}

/*
Read an entire cdrom file into the target address.
*/

static int  cdrom_dirent_read_block( struct fs_dirent *d, char *buffer, uint32_t blocknum )
{
	struct cdrom_dirent *cdd = d->private_data;
	return atapi_read( cdd->volume->unit, buffer, 1, (int) cdd->sector + blocknum );
}

#if 0
/*
Read an entire cdrom file into the target address.
*/

int cdrom_dirent_readfile( struct cdrom_dirent *d, char *data, int length )
{
	int nsectors = d->length/ATAPI_BLOCKSIZE;
	int remainder = d->length%ATAPI_BLOCKSIZE;

	if(length<d->length) length = d->length;

	atapi_read( d->volume->unit, data, nsectors, d->sector);
	// check success here

	if(remainder>0) {
		char *page = memory_alloc_page(0);
		// XXX check success here
		atapi_read(d->volume->unit,page,1,d->sector+nsectors);
		memcpy(data + ATAPI_BLOCKSIZE*nsectors,page,remainder);
		memory_free_page(page);
	}

	return length;
}
#endif

static struct fs_dirent * cdrom_dirent_lookup( struct fs_dirent *dir, const char *name )
{
	struct cdrom_dirent *cddir = dir->private_data;
	char *data = cdrom_dirent_load(dir);
	if(!data) return 0;

	int data_length = cddir->length;

	struct iso_9660_directory_entry *d = (struct iso_9660_directory_entry *) data;
	char *upper_name = strdup(name);
  if (!upper_name) return 0;
  strtoupper(upper_name);

	while(data_length>0 && d->descriptor_length>0 ) {
		fix_filename(d->ident,d->ident_length);

		if(!strcmp(upper_name,d->ident)) {
			struct cdrom_dirent *r = cdrom_dirent_create(
				cddir->volume,
				d->first_sector_little,
				d->length_little,
				d->flags & ISO_9660_EXTENT_FLAG_DIRECTORY );

			kfree(data);
      kfree(upper_name);
			return cdrom_dirent_as_dirent(r);
		}

		d = (struct iso_9660_directory_entry *)((char*)d+d->descriptor_length);
		data_length -= d->descriptor_length;
	}

	kfree(data);
	kfree(upper_name);

	return 0;
}

struct fs_dirent_node *cdrom_dirent_read_dir(struct fs_dirent *dir)
{
	struct cdrom_dirent *cddir = dir->private_data;
	char *data = cdrom_dirent_load(dir);
	if(!data) return 0;

	int data_length = cddir->length;

	struct iso_9660_directory_entry *d = (struct iso_9660_directory_entry *) data;

  struct fs_dirent_node * head = 0;
	while(data_length>0 && d->descriptor_length>0 ) {
		fix_filename(d->ident,d->ident_length);

    struct fs_dirent_node *node = kmalloc(sizeof(struct fs_dirent_node));
    node->next = head;
    node->data = dir;
    node->data = cdrom_dirent_lookup(dir, d->ident);
    head = node;
    if (!node->data) {
      printf("issue:\n");
      if (d->ident[0] == 0) {
        printf("issue with '.' .\n");
        return 0;
      }
      else if (d->ident[0] == 1) {
        printf("issue with '..' .\n");
        return 0;
      }
      else {
        printf("issue with %s.\n",d->ident);
        return 0;
      }
    }
    if (d->ident[0] == 0) {
      printf("processing with '.' .\n");
    }
    else if (d->ident[0] == 1) {
      printf("processing with '..' .\n");
    }
    else {
      printf("processing with %s.\n",d->ident);
    }
		d = (struct iso_9660_directory_entry *)((char*)d+d->descriptor_length);
		data_length -= d->descriptor_length;
	}
  printf("end of loop\n");

	kfree(data);

	return head;
}

char * strdup(const char * s) {
  char * new = kmalloc(strlen(s) + 1);
  if (new)
    strcpy(new, s);
  return new;
}

void strtoupper(char * name) {
  while (*name) {
    if (*name >= 'a' && *name <= 'z') {
      *name -= 'a' - 'A';
    }
    name++;
  }
}

void strtolower(char * name) {
  while (*name) {
    if (*name >= 'A' && *name <= 'Z') {
      *name += 'a' - 'A';
    }
    name++;
  }
}

static int cdrom_dirent_close( struct fs_dirent *d )
{
	struct cdrom_dirent *cdd = d->private_data;
	kfree(cdd);
	kfree(d);
	return 0;
}

static struct fs_dirent * cdrom_volume_root( struct fs_volume *v )
{
	struct cdrom_volume *cdv = v->private_data;
	struct cdrom_dirent *cdd = cdrom_dirent_create(cdv,cdv->root_sector,cdv->root_length,1);
	return cdrom_dirent_as_dirent(cdd);
}

static struct fs_volume * cdrom_volume_open( uint32_t unit )
{
	struct cdrom_volume *cdv = kmalloc(sizeof(*cdv));
	if(!cdv) return 0;

	struct iso_9660_volume_descriptor *d = memory_alloc_page(0);
	if(!d) {
		kfree(cdv);
		return 0;
	}

	printf("cdromfs: scanning atapi unit %d...\n",unit);

	int j;
	for(j=0;j<16;j++) {
		printf("cdromfs: checking volume %d\n",j);

		atapi_read(unit,d,1,j+16);
		// XXX check reuslt

		if(strncmp(d->magic,"CD001",5)) continue;

		if(d->type==ISO_9660_VOLUME_TYPE_PRIMARY) {
			cdv->root_sector = d->root.first_sector_little;
			cdv->root_length = d->root.length_little;
			cdv->total_sectors = d->nsectors_little;
			cdv->unit = unit;

			printf("cdromfs: mounted filesystem on unit %d\n",cdv->unit);

			memory_free_page(d);

			return cdrom_volume_as_volume(cdv);

		} else if(d->type==ISO_9660_VOLUME_TYPE_TERMINATOR) {
			break;
		} else {
			continue;
		}
	}

	console_printf("cdromfs: no filesystem found\n");
	return 0;		
}

static int cdrom_volume_close( struct fs_volume *v )
{
	struct cdrom_volume *cdv = v->private_data;
	console_printf("cdromfs: umounted filesystem from unit %d\n",cdv->unit);
	kfree(v);
	return 0;
}

int cdrom_init() {
	char cdrom_name[] = "cdrom";
	char *cdrom_name_cpy = kmalloc(6);
	struct fs f;
	strcpy(cdrom_name_cpy, cdrom_name);
	f.mount = cdrom_volume_open;
	f.name = cdrom_name_cpy;
	fs_register(&f);
	return 0;
}

const struct fs_volume_ops cdrom_volume_ops = {
	.root = cdrom_volume_root,
	.umount = cdrom_volume_close,
};

const struct fs_dirent_ops cdrom_dirent_ops = {
	.close = cdrom_dirent_close,
	.readdir = cdrom_dirent_read_dir,
	.lookup = cdrom_dirent_lookup,
	.mkdir = 0,
	.rmdir = 0,
	.link = 0,
	.unlink = 0,
	.read_block = cdrom_dirent_read_block,
	.write_block = 0,
};

static struct fs_volume *cdrom_volume_as_volume(struct cdrom_volume *cdv) {
	struct fs_volume *v = kmalloc(sizeof(struct fs_volume));
	memset(v, 0, sizeof(struct fs_volume));
	v->private_data = cdv;
	v->ops = &cdrom_volume_ops;
	v->block_size = ATAPI_BLOCKSIZE;
	return v;
}

static struct fs_dirent *cdrom_dirent_as_dirent(struct cdrom_dirent *cdd) {
	struct fs_dirent *d = kmalloc(sizeof(struct fs_dirent));
	memset(d, 0, sizeof(struct fs_volume));
	d->private_data = cdd;
	d->sz = cdd->length;
	d->ops = &cdrom_dirent_ops;
	return d;
}
