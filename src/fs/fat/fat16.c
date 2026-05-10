#include "fat16.h"
#include "string/string.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "status.h"
#include "kernel.h"
#include <stdint.h>

#define PEACHOS_FAT16_SIGNATURE      0x29
#define PEACHOS_FAT16_FAT_ENTRY_SIZE 0x02
#define PEACHOS_FAT16_BAD_SECTOR     0xFF7
#define PEACHOS_FAT16_UNUSED         0x00
#define FAT16_EOC                    0xFFFF  /* value written to mark end-of-chain */
#define FAT16_EOC_MIN                0xFFF8  /* values >= this are EOC when reading */

typedef unsigned int FAT_ITEM_TYPE;
#define FAT_ITEM_TYPE_DIRECTORY 0
#define FAT_ITEM_TYPE_FILE      1

#define FAT_FILE_READ_ONLY    0x01
#define FAT_FILE_HIDDEN       0x02
#define FAT_FILE_SYSTEM       0x04
#define FAT_FILE_VOLUME_LABEL 0x08
#define FAT_FILE_SUBDIRECTORY 0x10
#define FAT_FILE_ARCHIVED     0x20
#define FAT_FILE_DEVICE       0x40
#define FAT_FILE_RESERVED     0x80

struct fat_header_extended
{
    uint8_t  drive_number;
    uint8_t  win_nt_bit;
    uint8_t  signature;
    uint32_t volume_id;
    uint8_t  volume_id_string[11];
    uint8_t  system_id_string[8];
} __attribute__((packed));

struct fat_header
{
    uint8_t  short_jmp_ins[3];
    uint8_t  oem_identifier[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_copies;
    uint16_t root_dir_entries;
    uint16_t number_of_sectors;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_setors;
    uint32_t sectors_big;
} __attribute__((packed));

struct fat_h
{
    struct fat_header primary_header;
    union fat_h_e {
        struct fat_header_extended extended_header;
    } shared;
};

struct fat_directory_item
{
    uint8_t  filename[8];
    uint8_t  ext[3];
    uint8_t  attribute;
    uint8_t  reserved;
    uint8_t  creation_time_tenths_of_a_sec;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access;
    uint16_t high_16_bits_first_cluster;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t low_16_bits_first_cluster;
    uint32_t filesize;
} __attribute__((packed));

struct fat_directory
{
    struct fat_directory_item *item;
    int total;
    int sector_pos;
    int ending_sector_pos;
};

struct fat_item
{
    union {
        struct fat_directory_item *item;
        struct fat_directory      *directory;
    };
    FAT_ITEM_TYPE type;
};

struct fat_file_descriptor
{
    struct fat_item *item;
    uint32_t         pos;
    /* >= 0: index into root_directory.item[] for write-opened files.
       -1:   read-only open (no directory flush needed). */
    int              root_dir_slot;
};

struct fat_private
{
    struct fat_h         header;
    struct fat_directory root_directory;

    struct disk_stream *cluster_read_stream;
    struct disk_stream *fat_read_stream;
    struct disk_stream *directory_stream;
    struct disk_stream *cluster_write_stream;
    struct disk_stream *fat_write_stream;
};

/* Forward declarations */
int   fat16_resolve(struct disk *disk);
void *fat16_open(struct disk *disk, struct path_part *path, FILE_MODE mode);
int   fat16_read(struct disk *disk, void *descriptor, uint32_t size, uint32_t nmemb, char *out_ptr);
int   fat16_write(struct disk *disk, void *descriptor, uint32_t size, uint32_t nmemb, const char *buf);
int   fat16_seek(void *private, uint32_t offset, FILE_SEEK_MODE seek_mode);
int   fat16_stat(struct disk *disk, void *private, struct file_stat *stat);
int   fat16_close(void *private);
int   fat16_delete(struct disk *disk, struct path_part *path);
int   fat16_readdir(struct disk *disk, int index, char *buf, int buf_size);

struct filesystem fat16_fs = {
    .resolve = fat16_resolve,
    .open    = fat16_open,
    .read    = fat16_read,
    .write   = fat16_write,
    .seek    = fat16_seek,
    .stat    = fat16_stat,
    .close   = fat16_close,
    .delete  = fat16_delete,
    .readdir = fat16_readdir,
};

struct filesystem *fat16_init()
{
    strcpy(fat16_fs.name, "FAT16");
    return &fat16_fs;
}

static void fat16_init_private(struct disk *disk, struct fat_private *private)
{
    memset(private, 0, sizeof(struct fat_private));
    private->cluster_read_stream  = diskstreamer_new(disk->id);
    private->fat_read_stream      = diskstreamer_new(disk->id);
    private->directory_stream     = diskstreamer_new(disk->id);
    private->cluster_write_stream = diskstreamer_new(disk->id);
    private->fat_write_stream     = diskstreamer_new(disk->id);
}

int fat16_sector_to_absolute(struct disk *disk, int sector)
{
    return sector * disk->sector_size;
}

int fat16_get_total_items_for_directory(struct disk *disk, uint32_t directory_start_sector)
{
    struct fat_directory_item item;
    struct fat_private *fat_private = disk->fs_private;

    int res = 0;
    int i   = 0;
    int directory_start_pos = directory_start_sector * disk->sector_size;
    struct disk_stream *stream = fat_private->directory_stream;
    if (diskstreamer_seek(stream, directory_start_pos) != PEACHOS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

    while (1)
    {
        if (diskstreamer_read(stream, &item, sizeof(item)) != PEACHOS_ALL_OK)
        {
            res = -EIO;
            goto out;
        }
        if (item.filename[0] == 0x00)
            break;
        if (item.filename[0] == 0xE5)
            continue;
        i++;
    }

    res = i;
out:
    return res;
}

int fat16_get_root_directory(struct disk *disk, struct fat_private *fat_private,
                             struct fat_directory *directory)
{
    int res = 0;
    struct fat_header *primary_header = &fat_private->header.primary_header;
    int root_dir_sector_pos = (primary_header->fat_copies * primary_header->sectors_per_fat)
                              + primary_header->reserved_sectors;
    int root_dir_entries = fat_private->header.primary_header.root_dir_entries;
    int root_dir_size    = root_dir_entries * (int)sizeof(struct fat_directory_item);
    int total_sectors    = root_dir_size / disk->sector_size;
    if (root_dir_size % disk->sector_size)
        total_sectors += 1;

    int total_items = fat16_get_total_items_for_directory(disk, root_dir_sector_pos);

    struct fat_directory_item *dir = kzalloc(root_dir_size);
    if (!dir)
    {
        res = -ENOMEM;
        goto out;
    }

    struct disk_stream *stream = fat_private->directory_stream;
    if (diskstreamer_seek(stream, fat16_sector_to_absolute(disk, root_dir_sector_pos)) != PEACHOS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }
    if (diskstreamer_read(stream, dir, root_dir_size) != PEACHOS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

    directory->item              = dir;
    directory->total             = total_items;
    directory->sector_pos        = root_dir_sector_pos;
    directory->ending_sector_pos = root_dir_sector_pos + (root_dir_size / disk->sector_size);
out:
    return res;
}

int fat16_resolve(struct disk *disk)
{
    int res = 0;
    struct fat_private *fat_private = kzalloc(sizeof(struct fat_private));
    fat16_init_private(disk, fat_private);

    disk->fs_private = fat_private;
    disk->filesystem = &fat16_fs;

    struct disk_stream *stream = diskstreamer_new(disk->id);
    if (!stream)
    {
        res = -ENOMEM;
        goto out;
    }

    if (diskstreamer_read(stream, &fat_private->header, sizeof(fat_private->header)) != PEACHOS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

    if (fat_private->header.shared.extended_header.signature != 0x29)
    {
        res = -EFSNOTUS;
        goto out;
    }

    if (fat16_get_root_directory(disk, fat_private, &fat_private->root_directory) != PEACHOS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

out:
    if (stream)
        diskstreamer_close(stream);

    if (res < 0)
    {
        kfree(fat_private);
        disk->fs_private = 0;
    }
    return res;
}

void fat16_to_proper_string(char **out, const char *in)
{
    while (*in != 0x00 && *in != 0x20)
    {
        **out  = *in;
        *out  += 1;
        in    += 1;
    }
    if (*in == 0x20)
        **out = 0x00;
}

void fat16_get_full_relative_filename(struct fat_directory_item *item, char *out, int max_len)
{
    memset(out, 0x00, max_len);
    char *out_tmp = out;
    /* Copy fields to null-terminated buffers so fat16_to_proper_string cannot
       read past the 8-byte name or 3-byte ext field into adjacent struct bytes. */
    char fname_buf[9];
    char ext_buf[4];
    memcpy(fname_buf, item->filename, 8);
    fname_buf[8] = 0x00;
    memcpy(ext_buf, item->ext, 3);
    ext_buf[3] = 0x00;
    fat16_to_proper_string(&out_tmp, fname_buf);
    if (ext_buf[0] != 0x00 && ext_buf[0] != 0x20)
    {
        *out_tmp++ = '.';
        fat16_to_proper_string(&out_tmp, ext_buf);
    }
}

struct fat_directory_item *fat16_clone_directory_item(struct fat_directory_item *item, int size)
{
    if (size < (int)sizeof(struct fat_directory_item))
        return 0;

    struct fat_directory_item *item_copy = kzalloc(size);
    if (!item_copy)
        return 0;

    memcpy(item_copy, item, size);
    return item_copy;
}

static uint32_t fat16_get_first_cluster(struct fat_directory_item *item)
{
    return ((uint32_t)item->high_16_bits_first_cluster << 16)
           | item->low_16_bits_first_cluster;
}

static int fat16_cluster_to_sector(struct fat_private *private, int cluster)
{
    return private->root_directory.ending_sector_pos
           + ((cluster - 2) * private->header.primary_header.sectors_per_cluster);
}

static uint32_t fat16_get_first_fat_sector(struct fat_private *private)
{
    return private->header.primary_header.reserved_sectors;
}

static int fat16_get_fat_entry(struct disk *disk, int cluster)
{
    int res = -1;
    struct fat_private *private = disk->fs_private;
    struct disk_stream *stream  = private->fat_read_stream;
    if (!stream)
        goto out;

    uint32_t fat_table_position = fat16_get_first_fat_sector(private) * disk->sector_size;
    /* NOTE: original code had a bug here — it used * instead of + */
    res = diskstreamer_seek(stream,
                            fat_table_position + (cluster * PEACHOS_FAT16_FAT_ENTRY_SIZE));
    if (res < 0)
        goto out;

    uint16_t result = 0;
    res = diskstreamer_read(stream, &result, sizeof(result));
    if (res < 0)
        goto out;

    res = result;
out:
    return res;
}

static int fat16_get_cluster_for_offset(struct disk *disk, int starting_cluster, int offset)
{
    int res = 0;
    struct fat_private *private   = disk->fs_private;
    int size_of_cluster_bytes     = private->header.primary_header.sectors_per_cluster
                                    * disk->sector_size;
    int cluster_to_use            = starting_cluster;
    int clusters_ahead            = offset / size_of_cluster_bytes;

    for (int i = 0; i < clusters_ahead; i++)
    {
        int entry = fat16_get_fat_entry(disk, cluster_to_use);
        if (entry == 0xFF8 || entry == 0xFFF)
        {
            res = -EIO;
            goto out;
        }
        if (entry == PEACHOS_FAT16_BAD_SECTOR
            || entry == 0xFF0 || entry == 0xFF6
            || entry == 0x00)
        {
            res = -EIO;
            goto out;
        }
        cluster_to_use = entry;
    }

    res = cluster_to_use;
out:
    return res;
}

static int fat16_read_internal_from_stream(struct disk *disk, struct disk_stream *stream,
                                           int cluster, int offset, int total, void *out)
{
    int res = 0;
    struct fat_private *private   = disk->fs_private;
    int size_of_cluster_bytes     = private->header.primary_header.sectors_per_cluster
                                    * disk->sector_size;
    int cluster_to_use            = fat16_get_cluster_for_offset(disk, cluster, offset);
    if (cluster_to_use < 0)
    {
        res = cluster_to_use;
        goto out;
    }

    int offset_from_cluster = offset % size_of_cluster_bytes;
    int starting_sector     = fat16_cluster_to_sector(private, cluster_to_use);
    int starting_pos        = (starting_sector * disk->sector_size) + offset_from_cluster;
    int total_to_read       = total > size_of_cluster_bytes ? size_of_cluster_bytes : total;

    res = diskstreamer_seek(stream, starting_pos);
    if (res != PEACHOS_ALL_OK)
        goto out;

    res = diskstreamer_read(stream, out, total_to_read);
    if (res != PEACHOS_ALL_OK)
        goto out;

    total -= total_to_read;
    if (total > 0)
        res = fat16_read_internal_from_stream(disk, stream, cluster,
                                              offset + total_to_read, total,
                                              (char*)out + total_to_read);
out:
    return res;
}

static int fat16_read_internal(struct disk *disk, int starting_cluster,
                               int offset, int total, void *out)
{
    struct fat_private *fs_private = disk->fs_private;
    return fat16_read_internal_from_stream(disk, fs_private->cluster_read_stream,
                                           starting_cluster, offset, total, out);
}

void fat16_free_directory(struct fat_directory *directory)
{
    if (!directory)
        return;
    if (directory->item)
        kfree(directory->item);
    kfree(directory);
}

void fat16_fat_item_free(struct fat_item *item)
{
    if (item->type == FAT_ITEM_TYPE_DIRECTORY)
        fat16_free_directory(item->directory);
    else if (item->type == FAT_ITEM_TYPE_FILE)
        kfree(item->item);
    kfree(item);
}

struct fat_directory *fat16_load_fat_directory(struct disk *disk, struct fat_directory_item *item)
{
    int res = 0;
    struct fat_directory *directory = 0;
    struct fat_private   *fat_private = disk->fs_private;

    if (!(item->attribute & FAT_FILE_SUBDIRECTORY))
    {
        res = -EINVARG;
        goto out;
    }

    directory = kzalloc(sizeof(struct fat_directory));
    if (!directory)
    {
        res = -ENOMEM;
        goto out;
    }

    int cluster        = fat16_get_first_cluster(item);
    int cluster_sector = fat16_cluster_to_sector(fat_private, cluster);
    int total_items    = fat16_get_total_items_for_directory(disk, cluster_sector);
    directory->total   = total_items;
    int directory_size = directory->total * (int)sizeof(struct fat_directory_item);
    directory->item    = kzalloc(directory_size);
    if (!directory->item)
    {
        res = -ENOMEM;
        goto out;
    }

    res = fat16_read_internal(disk, cluster, 0x00, directory_size, directory->item);
    if (res != PEACHOS_ALL_OK)
        goto out;

out:
    if (res != PEACHOS_ALL_OK)
        fat16_free_directory(directory);
    return directory;
}

struct fat_item *fat16_new_fat_item_for_directory_item(struct disk *disk,
                                                       struct fat_directory_item *item)
{
    struct fat_item *f_item = kzalloc(sizeof(struct fat_item));
    if (!f_item)
        return 0;

    if (item->attribute & FAT_FILE_SUBDIRECTORY)
    {
        f_item->directory = fat16_load_fat_directory(disk, item);
        f_item->type      = FAT_ITEM_TYPE_DIRECTORY;
    }

    f_item->type = FAT_ITEM_TYPE_FILE;
    f_item->item = fat16_clone_directory_item(item, sizeof(struct fat_directory_item));
    return f_item;
}

struct fat_item *fat16_find_item_in_directory(struct disk *disk, struct fat_directory *directory,
                                              const char *name)
{
    struct fat_item *f_item = 0;
    char tmp_filename[PEACHOS_MAX_PATH];

    for (int i = 0; i < directory->total; i++)
    {
        fat16_get_full_relative_filename(&directory->item[i], tmp_filename, sizeof(tmp_filename));
        if (istrncmp(tmp_filename, name, sizeof(tmp_filename)) == 0)
            f_item = fat16_new_fat_item_for_directory_item(disk, &directory->item[i]);
    }
    return f_item;
}

struct fat_item *fat16_get_directory_entry(struct disk *disk, struct path_part *path)
{
    struct fat_private *fat_private = disk->fs_private;
    struct fat_item    *current_item =
        fat16_find_item_in_directory(disk, &fat_private->root_directory, path->part);
    if (!current_item)
        goto out;

    struct path_part *next_part = path->next;
    while (next_part != 0)
    {
        if (current_item->type != FAT_ITEM_TYPE_DIRECTORY)
        {
            current_item = 0;
            break;
        }
        struct fat_item *tmp_item =
            fat16_find_item_in_directory(disk, current_item->directory, next_part->part);
        fat16_fat_item_free(current_item);
        current_item = tmp_item;
        next_part    = next_part->next;
    }
out:
    return current_item;
}

/* ── Write helpers ─────────────────────────────────────────────────────── */

/* Write value to both FAT copies at cluster's entry offset. */
static int fat16_set_fat_entry(struct disk *disk, int cluster, uint16_t value)
{
    struct fat_private *private = disk->fs_private;
    struct disk_stream *stream  = private->fat_write_stream;

    uint32_t fat_base  = fat16_get_first_fat_sector(private) * disk->sector_size;
    uint32_t entry_off = fat_base + (uint32_t)(cluster * PEACHOS_FAT16_FAT_ENTRY_SIZE);
    uint32_t fat2_off  = entry_off
                         + ((uint32_t)private->header.primary_header.sectors_per_fat
                            * disk->sector_size);

    int res = diskstreamer_seek(stream, entry_off);
    if (res < 0) goto out;
    res = diskstreamer_write(stream, &value, sizeof(value));
    if (res < 0) goto out;

    res = diskstreamer_seek(stream, fat2_off);
    if (res < 0) goto out;
    res = diskstreamer_write(stream, &value, sizeof(value));
out:
    return res;
}

/* Scan FAT from cluster 2 upward for a free (0x0000) entry.
   Returns the cluster number, or -ENOMEM if the volume is full. */
static int fat16_find_free_cluster(struct disk *disk)
{
    struct fat_private *private = disk->fs_private;
    int total_sectors =
        (private->header.primary_header.number_of_sectors != 0)
            ? (int)private->header.primary_header.number_of_sectors
            : (int)private->header.primary_header.sectors_big;

    int first_data_sector = private->root_directory.ending_sector_pos;
    int data_sectors      = total_sectors - first_data_sector;
    int count_of_clusters = data_sectors
                            / private->header.primary_header.sectors_per_cluster;

    for (int i = 2; i <= count_of_clusters + 1; i++)
    {
        int entry = fat16_get_fat_entry(disk, i);
        if (entry == PEACHOS_FAT16_UNUSED)
            return i;
    }
    return -ENOMEM;
}

/* Allocate a free cluster, mark it 0xFFFF (EOC).
   If prev_cluster >= 2, link prev_cluster -> new cluster in FAT. */
static int fat16_alloc_cluster(struct disk *disk, int prev_cluster)
{
    int cluster = fat16_find_free_cluster(disk);
    if (cluster < 0)
        return cluster;

    int res = fat16_set_fat_entry(disk, cluster, FAT16_EOC);
    if (res < 0)
        return res;

    if (prev_cluster >= 2)
    {
        res = fat16_set_fat_entry(disk, prev_cluster, (uint16_t)cluster);
        if (res < 0)
            return res;
    }
    return cluster;
}

/* Zero all FAT entries in the chain starting at first_cluster. */
static int fat16_free_cluster_chain(struct disk *disk, uint32_t first_cluster)
{
    int cluster = (int)first_cluster;
    while (cluster >= 2 && cluster < (int)FAT16_EOC_MIN)
    {
        int next = fat16_get_fat_entry(disk, cluster);
        fat16_set_fat_entry(disk, cluster, 0x0000);
        if (next < 2 || next >= (int)FAT16_EOC_MIN)
            break;
        cluster = next;
    }
    return PEACHOS_ALL_OK;
}

/* Write len bytes from buf to cluster starting at byte offset_in_cluster. */
static int fat16_write_cluster_data(struct disk *disk, int cluster,
                                    uint32_t offset_in_cluster, void *buf, uint32_t len)
{
    struct fat_private *private = disk->fs_private;
    int sector   = fat16_cluster_to_sector(private, cluster);
    int byte_pos = (sector * disk->sector_size) + (int)offset_in_cluster;

    int res = diskstreamer_seek(private->cluster_write_stream, byte_pos);
    if (res < 0)
        return res;

    return diskstreamer_write(private->cluster_write_stream, buf, (int)len);
}

/* Write total bytes from data into the file at file_offset, following and
   extending the cluster chain as needed.  *first_cluster is updated when the
   file had no clusters before this call.
   Returns bytes written, or a negative error code. */
static int fat16_write_internal(struct disk *disk, uint32_t *first_cluster,
                                uint32_t file_offset, void *data, uint32_t total)
{
    struct fat_private *private = disk->fs_private;
    uint32_t cluster_size = (uint32_t)private->header.primary_header.sectors_per_cluster
                            * disk->sector_size;
    int current_cluster;

    if (*first_cluster == 0)
    {
        /* Brand-new file — allocate the first cluster */
        int new_cluster = fat16_alloc_cluster(disk, -1);
        if (new_cluster < 0)
            return new_cluster;
        *first_cluster  = (uint32_t)new_cluster;
        current_cluster = new_cluster;
    }
    else
    {
        /* Walk existing chain to the cluster containing file_offset */
        current_cluster          = (int)*first_cluster;
        uint32_t clusters_ahead  = file_offset / cluster_size;

        for (uint32_t i = 0; i < clusters_ahead; i++)
        {
            int next = fat16_get_fat_entry(disk, current_cluster);
            if (next < 2 || next >= (int)FAT16_EOC_MIN)
            {
                /* Chain too short — extend it */
                int new_cluster = fat16_alloc_cluster(disk, current_cluster);
                if (new_cluster < 0)
                    return new_cluster;
                current_cluster = new_cluster;
            }
            else
            {
                current_cluster = next;
            }
        }
    }

    uint32_t offset_in_cluster = file_offset % cluster_size;
    uint32_t written = 0;
    char    *src     = (char *)data;

    while (written < total)
    {
        uint32_t space    = cluster_size - offset_in_cluster;
        uint32_t to_write = (total - written) < space ? (total - written) : space;

        int res = fat16_write_cluster_data(disk, current_cluster,
                                           offset_in_cluster, src + written, to_write);
        if (res < 0)
            return res;

        written           += to_write;
        offset_in_cluster  = 0;

        if (written < total)
        {
            int next = fat16_get_fat_entry(disk, current_cluster);
            if (next < 2 || next >= (int)FAT16_EOC_MIN)
            {
                int new_cluster = fat16_alloc_cluster(disk, current_cluster);
                if (new_cluster < 0)
                    return new_cluster;
                current_cluster = new_cluster;
            }
            else
            {
                current_cluster = next;
            }
        }
    }

    return (int)written;
}

/* Return the root-directory slot index whose name matches, or -1. */
static int fat16_find_root_dir_slot_by_name(struct disk *disk, const char *name)
{
    struct fat_private *private  = disk->fs_private;
    int total_slots = private->header.primary_header.root_dir_entries;
    char tmp[PEACHOS_MAX_PATH];

    for (int i = 0; i < total_slots; i++)
    {
        struct fat_directory_item *entry = &private->root_directory.item[i];
        if (entry->filename[0] == 0x00)
            break;
        if (entry->filename[0] == 0xE5)
            continue;
        if (entry->attribute == 0x0F)
            continue;
        fat16_get_full_relative_filename(entry, tmp, sizeof(tmp));
        if (istrncmp(tmp, name, sizeof(tmp)) == 0)
            return i;
    }
    return -1;
}

/* Return the index of the first free (0x00 or 0xE5 first byte) root dir slot. */
static int fat16_find_free_root_dir_slot(struct disk *disk)
{
    struct fat_private *private = disk->fs_private;
    int total_slots = private->header.primary_header.root_dir_entries;

    for (int i = 0; i < total_slots; i++)
    {
        uint8_t first = private->root_directory.item[i].filename[0];
        if (first == 0x00 || first == 0xE5)
            return i;
    }
    return -ENOMEM;
}

/* Convert "file.txt" to the 8-byte name + 3-byte ext fields used in a dir entry.
   Both fields are space-padded (0x20) and uppercased. */
static void fat16_name_to_83(const char *name, uint8_t *out_fname, uint8_t *out_ext)
{
    memset(out_fname, 0x20, 8);
    memset(out_ext,   0x20, 3);

    int i = 0;
    while (*name && *name != '.' && i < 8)
    {
        char c = *name++;
        out_fname[i++] = (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    if (*name == '.')
        name++;

    int j = 0;
    while (*name && j < 3)
    {
        char c = *name++;
        out_ext[j++] = (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
}

/* Flush root_directory.item[slot_index] to the correct byte offset on disk. */
static int fat16_write_root_dir_entry(struct disk *disk, int slot_index)
{
    struct fat_private *private = disk->fs_private;
    int byte_offset = (private->root_directory.sector_pos * disk->sector_size)
                      + (slot_index * (int)sizeof(struct fat_directory_item));

    int res = diskstreamer_seek(private->fat_write_stream, byte_offset);
    if (res < 0)
        return res;

    return diskstreamer_write(private->fat_write_stream,
                              &private->root_directory.item[slot_index],
                              sizeof(struct fat_directory_item));
}

/* ── Public filesystem functions ─────────────────────────────────────── */

void *fat16_open(struct disk *disk, struct path_part *path, FILE_MODE mode)
{
    struct fat_private         *private    = disk->fs_private;
    struct fat_file_descriptor *descriptor = 0;

    if (mode == FILE_MODE_READ)
    {
        descriptor = kzalloc(sizeof(struct fat_file_descriptor));
        if (!descriptor)
            return ERROR(-ENOMEM);

        descriptor->item = fat16_get_directory_entry(disk, path);
        if (!descriptor->item)
        {
            kfree(descriptor);
            return ERROR(-EIO);
        }

        descriptor->pos           = 0;
        descriptor->root_dir_slot = -1;
        return descriptor;
    }

    if (mode == FILE_MODE_WRITE)
    {
        int slot = fat16_find_root_dir_slot_by_name(disk, path->part);

        if (slot >= 0)
        {
            /* File exists — truncate: free cluster chain and reset metadata */
            struct fat_directory_item *existing = &private->root_directory.item[slot];
            uint32_t first_cluster = fat16_get_first_cluster(existing);
            if (first_cluster >= 2)
                fat16_free_cluster_chain(disk, first_cluster);

            existing->filesize                 = 0;
            existing->low_16_bits_first_cluster  = 0;
            existing->high_16_bits_first_cluster = 0;
            fat16_write_root_dir_entry(disk, slot);
        }
        else
        {
            /* File does not exist — allocate a new root directory slot */
            slot = fat16_find_free_root_dir_slot(disk);
            if (slot < 0)
                return ERROR(-ENOMEM);

            struct fat_directory_item *new_entry = &private->root_directory.item[slot];
            memset(new_entry, 0, sizeof(*new_entry));
            fat16_name_to_83(path->part, new_entry->filename, new_entry->ext);
            new_entry->attribute = FAT_FILE_ARCHIVED;
            fat16_write_root_dir_entry(disk, slot);

            /* Extend in-memory total so future searches reach this new slot. */
            if (slot >= private->root_directory.total)
                private->root_directory.total = slot + 1;
        }

        descriptor = kzalloc(sizeof(struct fat_file_descriptor));
        if (!descriptor)
            return ERROR(-ENOMEM);

        descriptor->item = kzalloc(sizeof(struct fat_item));
        if (!descriptor->item)
        {
            kfree(descriptor);
            return ERROR(-ENOMEM);
        }

        descriptor->item->type = FAT_ITEM_TYPE_FILE;
        descriptor->item->item = fat16_clone_directory_item(
            &private->root_directory.item[slot],
            sizeof(struct fat_directory_item));
        if (!descriptor->item->item)
        {
            kfree(descriptor->item);
            kfree(descriptor);
            return ERROR(-ENOMEM);
        }

        descriptor->pos           = 0;
        descriptor->root_dir_slot = slot;
        return descriptor;
    }

    return ERROR(-EINVARG);
}

static void fat16_free_file_descriptor(struct fat_file_descriptor *desc)
{
    fat16_fat_item_free(desc->item);
    kfree(desc);
}

int fat16_close(void *private)
{
    fat16_free_file_descriptor((struct fat_file_descriptor *)private);
    return 0;
}

int fat16_stat(struct disk *disk, void *private, struct file_stat *stat)
{
    int res = 0;
    struct fat_file_descriptor *descriptor = (struct fat_file_descriptor *)private;
    struct fat_item            *desc_item  = descriptor->item;

    if (desc_item->type != FAT_ITEM_TYPE_FILE)
    {
        res = -EINVARG;
        goto out;
    }

    struct fat_directory_item *ritem = desc_item->item;
    stat->filesize = ritem->filesize;
    stat->flags    = 0x00;
    if (ritem->attribute & FAT_FILE_READ_ONLY)
        stat->flags |= FILE_STAT_READ_ONLY;
out:
    return res;
}

int fat16_read(struct disk *disk, void *descriptor, uint32_t size, uint32_t nmemb, char *out_ptr)
{
    int res = 0;
    struct fat_file_descriptor *fat_desc = descriptor;
    struct fat_directory_item  *item     = fat_desc->item->item;
    int offset = fat_desc->pos;

    for (uint32_t i = 0; i < nmemb; i++)
    {
        res = fat16_read_internal(disk, fat16_get_first_cluster(item),
                                  offset, size, out_ptr);
        if (ISERR(res))
            goto out;
        out_ptr += size;
        offset  += size;
    }

    res = nmemb;
out:
    return res;
}

int fat16_write(struct disk *disk, void *descriptor, uint32_t size, uint32_t nmemb,
                const char *buf)
{
    int res = 0;
    struct fat_file_descriptor *fat_desc = descriptor;
    struct fat_directory_item  *item     = fat_desc->item->item;
    struct fat_private         *private  = disk->fs_private;

    uint32_t total         = size * nmemb;
    uint32_t first_cluster = fat16_get_first_cluster(item);

    int bytes_written = fat16_write_internal(disk, &first_cluster,
                                             fat_desc->pos, (void *)buf, total);
    if (bytes_written < 0)
    {
        res = bytes_written;
        goto out;
    }

    /* Persist newly allocated first cluster back into the directory item */
    if (item->low_16_bits_first_cluster == 0 && first_cluster != 0)
    {
        item->low_16_bits_first_cluster  = (uint16_t)first_cluster;
        item->high_16_bits_first_cluster = (uint16_t)(first_cluster >> 16);
    }

    /* Extend filesize if the write grew the file */
    uint32_t new_end = fat_desc->pos + (uint32_t)bytes_written;
    if (new_end > item->filesize)
        item->filesize = new_end;

    fat_desc->pos += (uint32_t)bytes_written;

    /* Sync in-memory root dir entry and flush to disk */
    if (fat_desc->root_dir_slot >= 0)
    {
        memcpy(&private->root_directory.item[fat_desc->root_dir_slot],
               item, sizeof(struct fat_directory_item));
        fat16_write_root_dir_entry(disk, fat_desc->root_dir_slot);
    }

    res = (int)nmemb;
out:
    return res;
}

int fat16_delete(struct disk *disk, struct path_part *path)
{
    int res = 0;
    struct fat_private *private = disk->fs_private;

    int slot = fat16_find_root_dir_slot_by_name(disk, path->part);
    if (slot < 0)
    {
        res = -EIO;
        goto out;
    }

    struct fat_directory_item *item = &private->root_directory.item[slot];

    /* Free the cluster chain */
    uint32_t first_cluster = fat16_get_first_cluster(item);
    if (first_cluster >= 2)
        fat16_free_cluster_chain(disk, first_cluster);

    /* Mark directory entry as deleted (0xE5) and zero metadata */
    item->filename[0]                = 0xE5;
    item->filesize                   = 0;
    item->low_16_bits_first_cluster  = 0;
    item->high_16_bits_first_cluster = 0;

    fat16_write_root_dir_entry(disk, slot);
out:
    return res;
}

/* Return the index-th valid file entry name in the root directory into buf.
   Returns 1 if a name was written, 0 if no more entries, negative on error.
   Skips deleted (0xE5), end-of-dir (0x00), volume labels, and subdirs. */
int fat16_readdir(struct disk *disk, int index, char *buf, int buf_size)
{
    struct fat_private *private    = disk->fs_private;
    int                 total_slots = private->header.primary_header.root_dir_entries;
    int                 count      = 0;

    for (int i = 0; i < total_slots; i++)
    {
        struct fat_directory_item *entry = &private->root_directory.item[i];
        if (entry->filename[0] == 0x00)
            break;
        if (entry->filename[0] == 0xE5)
            continue;
        if (entry->attribute & (FAT_FILE_VOLUME_LABEL | FAT_FILE_SUBDIRECTORY))
            continue;
        if (count == index)
        {
            fat16_get_full_relative_filename(entry, buf, buf_size);
            return 1;
        }
        count++;
    }
    return 0;
}

int fat16_seek(void *private, uint32_t offset, FILE_SEEK_MODE seek_mode)
{
    int res = 0;
    struct fat_file_descriptor *desc      = private;
    struct fat_item            *desc_item = desc->item;

    if (desc_item->type != FAT_ITEM_TYPE_FILE)
    {
        res = -EINVARG;
        goto out;
    }

    struct fat_directory_item *ritem = desc_item->item;
    if (offset >= ritem->filesize)
    {
        res = -EIO;
        goto out;
    }

    switch (seek_mode)
    {
    case SEEK_SET:
        desc->pos = offset;
        break;
    case SEEK_END:
        res = -EUNIMP;
        break;
    case SEEK_CUR:
        desc->pos += offset;
        break;
    default:
        res = -EINVARG;
        break;
    }
out:
    return res;
}
