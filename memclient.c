#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "memclient.h"



#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define DEVICE_NAME     "memclient"

#define MEMCLIENT_ATTACH_DMABUF		_IOWR('M', 0x01, memclient_attach_dmabuf_param_t)
#define MEMCLIENT_RELEASE_DMABUF	_IOWR('M', 0x02, int)



typedef struct
{
	struct class *device_class;
	struct device *file_device;
	int version_major;
} memclient_dev_t;

typedef struct
{
	int dmabuf_fd;
	int _padding00;
	struct dma_buf* dmabuf;
	struct dma_buf_attachment* attachment;
	struct sg_table* sgt;
	struct list_head list;
} attach_entry_t;

typedef struct
{
	struct device* dev;
	struct list_head entry_list;
} memclient_private_data_t;



static memclient_dev_t memclient_dev;



static attach_entry_t* memclient_find_entry(struct list_head* entry_list, int dmabuf_fd)
{
	attach_entry_t* entry_ptr = NULL;

	list_for_each_entry(entry_ptr, entry_list, list)
	{
		if (entry_ptr->dmabuf_fd == dmabuf_fd)
		{
			return entry_ptr;
		}
	}

	return NULL;
}

static void memclient_release_entry(attach_entry_t* entry)
{
	printk(KERN_INFO "memclient_release_entry\n");

	if (entry)
	{
		printk(KERN_INFO "memclient_release_entry: freeing dmabuf fd (%d)\n", entry->dmabuf_fd);

		dma_buf_unmap_attachment(entry->attachment, entry->sgt, DMA_BIDIRECTIONAL);
		dma_buf_detach(entry->dmabuf, entry->attachment);
		dma_buf_put(entry->dmabuf);

		kfree(entry);
	}
}


long memclient_ioctl(struct file *file, unsigned int cmd, ulong arg)
{
	unsigned long api;
	memclient_attach_dmabuf_param_t attach_dmabuf_param;
	//struct dma_buf* dmabuf = NULL;
	//struct dma_buf_attachment* attachment = NULL;
	//struct sg_table* sgt = NULL;
	attach_entry_t* entry = NULL;
	memclient_private_data_t* priv = (memclient_private_data_t*)file->private_data;
	int dmabuf_fd = -1;


	printk(KERN_INFO "memclient_ioctl\n");

	switch (cmd)
	{
	case MEMCLIENT_ATTACH_DMABUF:
		{
			printk(KERN_INFO "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF\n");

			// Get the parameters from user space
			api = copy_from_user(&attach_dmabuf_param,
				(void*)arg,
				sizeof(attach_dmabuf_param));
			if (api != 0)
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF copy_from_user failed.\n");
				return -1;	//TODO: error code
			}

			// Check if the entry is already attached
			entry = memclient_find_entry(&priv->entry_list, attach_dmabuf_param.handle);
			if (entry != NULL)
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF an entry already exists for fd (%d).\n",
					attach_dmabuf_param.handle);

				return -1;	//TODO: error code
			}

			// Allocate storage for the entry;
			entry = kmalloc(sizeof(attach_entry_t), GFP_KERNEL);
			if (entry == NULL)
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF kmalloc failed.\n");
				return -ENOMEM;
			}

			INIT_LIST_HEAD(&entry->list);


			// Attach
			entry->dmabuf_fd = attach_dmabuf_param.handle;

			entry->dmabuf = dma_buf_get(entry->dmabuf_fd);
			if (IS_ERR_OR_NULL(entry->dmabuf))
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF dma_buf_get failed (fd=%d).\n",
					entry->dmabuf_fd);
				return -1;	//TODO: error code
			}

		
			entry->attachment = dma_buf_attach(entry->dmabuf, priv->dev);
			if (IS_ERR_OR_NULL(entry->attachment))
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF dma_buf_attach failed.\n");
				goto A_err1;
			}


			entry->sgt = dma_buf_map_attachment(entry->attachment, DMA_BIDIRECTIONAL);
			if (IS_ERR_OR_NULL(entry->sgt))
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF dma_buf_map_attachment failed.\n");
				goto A_err2;
			}

			if (sg_nents(entry->sgt->sgl) != 1)
			{
				// Only one contiguous memory area is supported
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF sg_nents != 1.\n");
				goto A_err3;
			}

			attach_dmabuf_param.physical_address = sg_dma_address(entry->sgt->sgl);
			attach_dmabuf_param.length = sg_dma_len(entry->sgt->sgl);

			printk(KERN_INFO "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF paddr=%p length=%x\n",
				(void*)attach_dmabuf_param.physical_address,
				attach_dmabuf_param.length);


			// Record the entry
			list_add(&entry->list, &priv->entry_list);


			// Return parameters to user
			api = copy_to_user((void*)arg, &attach_dmabuf_param, sizeof(attach_dmabuf_param));
			if (api != 0)
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_ATTACH_DMABUF copy_to_user failed.\n");
				goto A_err4;
			}

			return 0;

		A_err4:
			list_del(&entry->list);

		A_err3:
			dma_buf_unmap_attachment(entry->attachment, entry->sgt, DMA_BIDIRECTIONAL);

		A_err2:
			dma_buf_detach(entry->dmabuf, entry->attachment);

		A_err1:
			dma_buf_put(entry->dmabuf);
			kfree(entry);

			return -1;	// TODO: error code
		}
		break;

	case MEMCLIENT_RELEASE_DMABUF:
		{
			printk(KERN_INFO "memclient_ioctl: MEMCLIENT_RELEASE_DMABUF\n");


			dmabuf_fd = (int)arg;

			if (dmabuf_fd < 0)
			{
				// Invalid file descriptor
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_RELEASE_DMABUF invalid fd (%d).\n", dmabuf_fd);
				return -1;	//TODO: error code 
			}


			entry = memclient_find_entry(&priv->entry_list, dmabuf_fd);
			if (entry == NULL)
			{
				printk(KERN_ALERT "memclient_ioctl: MEMCLIENT_RELEASE_DMABUF an entry does not exist for fd (%d).\n",
					dmabuf_fd);

				return -1;	//TODO: error code
			}

				
			//dma_buf_unmap_attachment(entry->attachment, entry->sgt, DMA_BIDIRECTIONAL);
			//dma_buf_detach(entry->dmabuf, entry->attachment);
			//dma_buf_put(entry->dmabuf);

			//list_del(&entry->list);

			//kfree(entry);

			list_del(&entry->list);
			memclient_release_entry(entry);

			return 0;
		}
		break;

	default:
		break;
	}

	return -1;
}

static int memclient_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	memclient_private_data_t* priv;

	printk(KERN_INFO "memclient_open\n");

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
	{
		return -ENOMEM;
	}


	priv->dev = memclient_dev.file_device;
	INIT_LIST_HEAD(&priv->entry_list);
	
	file->private_data = priv;

	return ret;
}

static int memclient_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	memclient_private_data_t* priv = file->private_data;
	attach_entry_t* entry = NULL;
	attach_entry_t* temp = NULL;


	printk(KERN_INFO "memclient_release\n");

	// Release all buffers
	list_for_each_entry_safe(entry, temp, &priv->entry_list, list)
	{
		list_del(&entry->list);
		memclient_release_entry(entry);
	}


	kfree(priv);

	return ret;
}

struct file_operations memclient_fops = {
	.owner = THIS_MODULE,
	.open = memclient_open,
	.release = memclient_release,
	.unlocked_ioctl = memclient_ioctl,
};



static int memclient_init(void)
{
	int ret;


	printk(KERN_INFO "memclient_init\n");

	
	memset(&memclient_dev, 0, sizeof(memclient_dev));


	ret = register_chrdev(VERSION_MAJOR, DEVICE_NAME, &memclient_fops);
	if (ret < 0)
	{
		printk(KERN_ALERT "memclient: can't register major for device\n");
		return ret;
	}

	memclient_dev.version_major = ret;

	memclient_dev.device_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (!memclient_dev.device_class)
	{
		printk(KERN_ALERT "memclient: failed to create class\n");
		return -EFAULT;
	}

	memclient_dev.file_device = device_create(memclient_dev.device_class,
		NULL,
		MKDEV(memclient_dev.version_major, VERSION_MINOR),
		NULL,
		DEVICE_NAME);
	if (!memclient_dev.file_device)
	{
		printk(KERN_ALERT "failed to create device %s", DEVICE_NAME);
		return -EFAULT;
	}

	return 0;
}
static void memclient_exit(void)
{
	printk(KERN_INFO "memclient_exit\n");

	device_destroy(memclient_dev.device_class,
		MKDEV(memclient_dev.version_major, VERSION_MINOR));

	class_destroy(memclient_dev.device_class);

	unregister_chrdev(memclient_dev.version_major, DEVICE_NAME);
}




MODULE_LICENSE("Dual BSD/GPL");

module_init(memclient_init);
module_exit(memclient_exit);