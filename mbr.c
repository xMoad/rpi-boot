/* Copyright (C) 2013 by John Cronin <jncronin@tysos.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "block.h"
#include "vfs.h"
#include "util.h"

#ifdef DEBUG2
#define MBR_DEBUG
#endif

int fat_init(struct block_device *, struct fs **);
int ext2_init(struct block_device *, struct fs **);

// Code for interpreting an mbr

struct mbr_block_dev {
	struct block_device bd;
	struct block_device *parent;
	int part_no;
	uint32_t start_block;
	uint32_t blocks;
	uint8_t part_id;
};

static char driver_name[] = "mbr";

static int mbr_read(struct block_device *, uint8_t *buf, size_t buf_size, uint32_t starting_block);

int read_mbr(struct block_device *parent, struct block_device ***partitions, int *part_count)
{
	(void)partitions;
	(void)part_count;
	(void)driver_name;
	/* Check the validity of the parent device */
	if(parent == (void*)0)
	{
		printf("MBR: invalid parent device\n");
		return -1;
	}

	/* Read the first 512 bytes */
	uint8_t *block_0 = (uint8_t *)malloc(512);
#ifdef MBR_DEBUG
	printf("MBR: reading block 0 from device %s\n", parent->device_name);
#endif
	
	int ret = block_read(parent, block_0, 512, 0);
	if(ret < 0)
	{
		printf("MBR: block_read failed (%i)\n", ret);
		return ret;
	}
	if(ret != 512)
	{
		printf("MBR: unable to read first 512 bytes of device %s, only %d bytes read\n",
				parent->device_name, ret);
		return -1;
	}

	/* Check the MBR signature */
	if((block_0[0x1fe] != 0x55) || (block_0[0x1ff] != 0xaa))
	{
		printf("MBR: no valid mbr signature on device %s (bytes are %x %x)\n",
				parent->device_name, block_0[0x1fe], block_0[0x1ff]);
		return -1;
	}
	printf("MBR: found valid MBR on device %s\n", parent->device_name);

#ifdef MBR_DEBUG
	/* Dump the first sector */
	printf("MBR: first sector:");
	for(int dump_idx = 0; dump_idx < 512; dump_idx++)
	{
		if((dump_idx & 0xf) == 0)
			printf("\n%03x: ", dump_idx);
		printf("%02x ", block_0[dump_idx]);
	}
	printf("\n");
#endif

	/* Load the partitions */
	struct block_device **parts =
		(struct block_device **)malloc(4 * sizeof(struct block_device *));
	int cur_p = 0;
	for(int i = 0; i < 4; i++)
	{
		int p_offset = 0x1be + (i * 0x10);
		if(block_0[p_offset + 4] != 0x00)
		{
			// Valid partition
			struct mbr_block_dev *d =
				(struct mbr_block_dev *)malloc(sizeof(struct mbr_block_dev));
			memset(d, 0, sizeof(struct mbr_block_dev));
			d->bd.driver_name = driver_name;
			char *dev_name = (char *)malloc(strlen(parent->device_name + 2));
			strcpy(dev_name, parent->device_name);
			dev_name[strlen(parent->device_name)] = '_';
			dev_name[strlen(parent->device_name) + 1] = '0' + i;
			dev_name[strlen(parent->device_name) + 2] = 0;
			d->bd.device_name = dev_name;
			d->bd.device_id = (uint8_t *)malloc(1);
			d->bd.device_id[0] = i;
			d->bd.dev_id_len = 1;
			d->bd.read = mbr_read;
			d->bd.block_size = 512;
			d->part_no = i;
			d->part_id = block_0[p_offset + 4];
			d->start_block = read_word(block_0, p_offset + 8);
			d->blocks = read_word(block_0, p_offset + 12);
			d->parent = parent;
			
			parts[cur_p++] = (struct block_device *)d;
#ifdef MBR_DEBUG
			printf("MBR: partition number %i (%s) of type %x, start sector %u, "
					"sector count %u, p_offset %03x\n", 
					d->part_no, d->bd.device_name, d->part_id,
					d->start_block, d->blocks, p_offset);
#endif

			switch(d->part_id)
			{
				case 1:
				case 4:
				case 6:
				case 0xb:
				case 0xc:
				case 0xe:
				case 0x11:
				case 0x14:
				case 0x1b:
				case 0x1c:
				case 0x1e:
					fat_init((struct block_device *)d, &d->bd.fs);
					break;

				case 0x83:
					ext2_init((struct block_device *)d, &d->bd.fs);
					break;
			}

			if(d->bd.fs)
				vfs_register(d->bd.fs);
		}
	}

	*partitions = parts;
	*part_count = cur_p;
	printf("MBR: found total of %i partition(s)\n", cur_p);

	return 0;
}

int mbr_read(struct block_device *dev, uint8_t *buf, size_t buf_size, uint32_t starting_block)
{
	struct block_device *parent = ((struct mbr_block_dev *)dev)->parent;
	// Check the block size of the partition is equal that of the underlying device
	if(dev->block_size != parent->block_size)
	{
		printf("MBR: read() error - block size differs (%i vs %i)\n",
				dev->block_size,
				parent->block_size);
		return -1;
	}

	return parent->read(parent, buf, buf_size,
			starting_block + ((struct mbr_block_dev *)dev)->start_block);
}

