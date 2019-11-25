/*
 * driver/block/block.c
 *
 * Copyright(c) 2007-2019 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <block/block.h>

struct sub_block_pdata_t
{
	u64_t from;
	struct block_t * pblk;
};

static ssize_t block_read_size(struct kobj_t * kobj, void * buf, size_t size)
{
	struct block_t * blk = (struct block_t *)kobj->priv;
	return sprintf(buf, "%lld", block_size(blk));
}

static ssize_t block_read_count(struct kobj_t * kobj, void * buf, size_t size)
{
	struct block_t * blk = (struct block_t *)kobj->priv;
	return sprintf(buf, "%lld", block_count(blk));
}

static ssize_t block_read_capacity(struct kobj_t * kobj, void * buf, size_t size)
{
	struct block_t * blk = (struct block_t *)kobj->priv;
	return sprintf(buf, "%lld", block_capacity(blk));
}

static u64_t sub_block_read(struct block_t * blk, u8_t * buf, u64_t blkno, u64_t blkcnt)
{
	struct sub_block_pdata_t * pdat = (struct sub_block_pdata_t *)(blk->priv);
	struct block_t * pblk = pdat->pblk;
	return (pblk->read(pblk, buf, blkno + pdat->from, blkcnt));
}

static u64_t sub_block_write(struct block_t * blk, u8_t * buf, u64_t blkno, u64_t blkcnt)
{
	struct sub_block_pdata_t * pdat = (struct sub_block_pdata_t *)(blk->priv);
	struct block_t * pblk = pdat->pblk;
	return (pblk->write(pblk, buf, blkno + pdat->from, blkcnt));
}

static void sub_block_sync(struct block_t * blk)
{
	struct sub_block_pdata_t * pdat = (struct sub_block_pdata_t *)(blk->priv);
	struct block_t * pblk = pdat->pblk;
	pblk->sync(pblk);
}

struct block_t * search_block(const char * name)
{
	struct device_t * dev;

	dev = search_device(name, DEVICE_TYPE_BLOCK);
	if(!dev)
		return NULL;
	return (struct block_t *)dev->priv;
}

struct device_t * register_block(struct block_t * blk, struct driver_t * drv)
{
	struct device_t * dev;

	if(!blk || !blk->name)
		return NULL;

	if(!blk->read || !blk->write || !blk->sync)
		return NULL;

	dev = malloc(sizeof(struct device_t));
	if(!dev)
		return NULL;

	dev->name = strdup(blk->name);
	dev->type = DEVICE_TYPE_BLOCK;
	dev->driver = drv;
	dev->priv = blk;
	dev->kobj = kobj_alloc_directory(dev->name);
	kobj_add_regular(dev->kobj, "size", block_read_size, NULL, blk);
	kobj_add_regular(dev->kobj, "count", block_read_count, NULL, blk);
	kobj_add_regular(dev->kobj, "capacity", block_read_capacity, NULL, blk);

	if(!register_device(dev))
	{
		kobj_remove_self(dev->kobj);
		free(dev->name);
		free(dev);
		return NULL;
	}
	return dev;
}

void unregister_block(struct block_t * blk)
{
	struct device_t * dev;

	if(blk && blk->name)
	{
		dev = search_device(blk->name, DEVICE_TYPE_BLOCK);
		if(dev && unregister_device(dev))
		{
			kobj_remove_self(dev->kobj);
			free(dev->name);
			free(dev);
		}
	}
}

struct device_t * register_sub_block(struct block_t * pblk, u64_t offset, u64_t length, const char * name, struct driver_t * drv)
{
	struct device_t * dev;
	struct block_t * blk;
	struct sub_block_pdata_t * pdat;
	u64_t blksz, blkno, blkcnt, tmp;
	char buffer[256];

	if(!name)
		return NULL;

	if(!pblk || !pblk->name)
		return NULL;

	blksz = block_size(pblk);
	blkno = offset / blksz;
	tmp = offset % blksz;
	if(tmp > 0)
		blkno++;
	tmp = length / blksz;
	blkcnt = block_available_count(pblk, blkno, tmp);
	if(blkcnt <= 0)
		return NULL;

	blk = malloc(sizeof(struct block_t));
	pdat = malloc(sizeof(struct sub_block_pdata_t));
	if(!blk || !pdat)
	{
		free(blk);
		free(pdat);
		return NULL;
	}

	snprintf(buffer, sizeof(buffer), "%s.%s", pblk->name, name);
	pdat->from = blkno;
	pdat->pblk = pblk;

	blk->name = strdup(buffer);
	blk->blksz = blksz;
	blk->blkcnt = blkcnt;
	blk->read = sub_block_read;
	blk->write = sub_block_write;
	blk->sync = sub_block_sync;
	blk->priv = pdat;

	if(!(dev = register_block(blk, drv)))
	{
		free(blk->priv);
		free(blk->name);
		free(blk);
		return NULL;
	}
	return dev;
}

void unregister_sub_block(struct block_t * pblk)
{
	struct device_t * pos, * n;
	struct block_t * blk;
	int len;

	if(pblk && pblk->name && search_block(pblk->name))
	{
		len = strlen(pblk->name);
		list_for_each_entry_safe(pos, n, &__device_head[DEVICE_TYPE_BLOCK], head)
		{
			if((strncmp(pos->name, pblk->name, len) == 0) && (strlen(pos->name) > len))
			{
				blk = (struct block_t *)pos->priv;
				if(blk)
				{
					unregister_block(blk);
					free(blk->priv);
					free(blk->name);
					free(blk);
				}
			}
		}
	}
}

u64_t block_read(struct block_t * blk, u8_t * buf, u64_t offset, u64_t count)
{
	u64_t blkno, blksz, blkcnt, capacity;
	u64_t len, tmp;
	u64_t ret = 0;
	u8_t * p;

	if(!blk || !buf || !count)
		return 0;

	blksz = block_size(blk);
	blkcnt = block_count(blk);
	if(!blksz || !blkcnt)
		return 0;

	capacity = block_capacity(blk);
	if(offset >= capacity)
		return 0;

	tmp = capacity - offset;
	if(count > tmp)
		count = tmp;

	p = malloc(blksz);
	if(!p)
		return 0;

	blkno = offset / blksz;
	tmp = offset % blksz;
	if(tmp > 0)
	{
		len = blksz - tmp;
		if(count < len)
			len = count;

		if(blk->read(blk, p, blkno, 1) != 1)
		{
			free(p);
			return ret;
		}

		memcpy((void *)buf, (const void *)(&p[tmp]), len);
		buf += len;
		count -= len;
		ret += len;
		blkno += 1;
	}

	tmp = count / blksz;
	if(tmp > 0)
	{
		len = tmp * blksz;

		if(blk->read(blk, buf, blkno, tmp) != tmp)
		{
			free(p);
			return ret;
		}

		buf += len;
		count -= len;
		ret += len;
		blkno += tmp;
	}

	if(count > 0)
	{
		len = count;

		if(blk->read(blk, p, blkno, 1) != 1)
		{
			free(p);
			return ret;
		}

		memcpy((void *)buf, (const void *)(&p[0]), len);
		ret += len;
	}

	free(p);
	return ret;
}

u64_t block_write(struct block_t * blk, u8_t * buf, u64_t offset, u64_t count)
{
	u64_t blkno, blksz, blkcnt, capacity;
	u64_t len, tmp;
	u64_t ret = 0;
	u8_t * p;

	if(!blk || !buf || !count)
		return 0;

	blksz = block_size(blk);
	blkcnt = block_count(blk);
	if(!blksz || !blkcnt)
		return 0;

	capacity = block_capacity(blk);
	if(offset >= capacity)
		return 0;

	tmp = capacity - offset;
	if(count > tmp)
		count = tmp;

	p = malloc(blksz);
	if(!p)
		return 0;

	blkno = offset / blksz;
	tmp = offset % blksz;
	if(tmp > 0)
	{
		len = blksz - tmp;
		if(count < len)
			len = count;

		if(blk->read(blk, p, blkno, 1) != 1)
		{
			free(p);
			return ret;
		}

		memcpy((void *)(&p[tmp]), (const void *)buf, len);

		if(blk->write(blk, p, blkno, 1) != 1)
		{
			free(p);
			return ret;
		}

		buf += len;
		count -= len;
		ret += len;
		blkno += 1;
	}

	tmp = count / blksz;
	if(tmp > 0)
	{
		len = tmp * blksz;

		if(blk->write(blk, buf, blkno, tmp) != tmp)
		{
			free(p);
			return ret;
		}

		buf += len;
		count -= len;
		ret += len;
		blkno += tmp;
	}

	if(count > 0)
	{
		len = count;

		if(blk->read(blk, p, blkno, 1) != 1)
		{
			free(p);
			return ret;
		}

		memcpy((void *)(&p[0]), (const void *)buf, len);

		if(blk->write(blk, p, blkno, 1) != 1)
		{
			free(p);
			return ret;
		}

		ret += len;
	}

	free(p);
	return ret;
}

void block_sync(struct block_t * blk)
{
	if(blk && blk->sync)
		blk->sync(blk);
}
