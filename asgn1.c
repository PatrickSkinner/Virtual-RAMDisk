
/**
* File: asgn1.c
* Date: 12/09/2017
* Author: Patrick Skinner
* Version: 0.1
*
* This is a module which serves as a virtual ramdisk which disk size is
* limited by the amount of memory available and serves as the requirement for
* COSC440 assignment 1 in 2012.
*
* Note: multiple devices and concurrent modules are not supported in this
*       version.
*/

/* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version
* 2 of the License, or (at your option) any later version.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/sched.h>

#define MYDEV_NAME "asgn1"
#define MYIOC_TYPE 'k'

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Skinner");
MODULE_DESCRIPTION("COSC440 asgn1");


/**
* The node structure for the memory page linked list.
*/
typedef struct page_node_rec {
	struct list_head list;
	struct page *page;
} page_node;

typedef struct asgn1_dev_t {
	dev_t dev;            /* the device */
	struct cdev *cdev;
	struct list_head mem_list;
	int num_pages;        /* number of memory pages this module currently holds */
	size_t data_size;     /* total data size in this module */
	atomic_t nprocs;      /* number of processes accessing this device */ 
	atomic_t max_nprocs;  /* max number of processes accessing this device */
	struct kmem_cache *cache;      /* cache memory */
	struct class *class;     /* the udev class */
	struct device *device;   /* the udev device node */
} asgn1_dev;

asgn1_dev asgn1_device;
struct prod_dir_entry *asgn1_proc;

int asgn1_major = 0;                      /* major number of module */  
int asgn1_minor = 0;                      /* minor number of module */
int asgn1_dev_count = 1;                  /* number of devices */

/**
* This function frees all memory pages held by the module.
*/
void free_memory_pages(void) {
	page_node *curr; /* pointer to list head object */
	page_node *temp; /* temporary list head for safe deletion */


	/*Loop through the entire page list*/
	list_for_each_entry_safe(curr, temp, &asgn1_device.mem_list, list){

		/* If node has a page, free the page. */
		if(curr->page != NULL){
			__free_page(curr->page);
		}

		/* Remove node from page list, free the node. */
		list_del(&curr->list);
		kfree(curr);
	}

	/* reset device data size, and num_pages */
	asgn1_device.num_pages = 0;
	asgn1_device.data_size = 0;
}


/**
* This function opens the virtual disk, if it is opened in the write-only
* mode, all memory pages will be freed.
*/
int asgn1_open(struct inode *inode, struct file *filp) {

	/* Increment process count, if exceeds max_nprocs, return -EBUSY */
	if(atomic_read(&asgn1_device.nprocs) >= atomic_read(&asgn1_device.max_nprocs)){
		return -EBUSY;
	} else {
		atomic_inc(&asgn1_device.nprocs);
	}

	/* If opened in write-only mode, free all memory pages */
	//if(filp->f_mode == FMODE_WRITE){
	if(filp->f_flags & O_WRONLY){
		printk(KERN_INFO "Write only mode, freeing all pages.\n");
		free_memory_pages();
	}
	printk(KERN_INFO "Device Succesfully Opened\n");
	return 0; /* Success */
}


/**
* This function releases the virtual disk, but nothing needs to be done in this case.
*/
int asgn1_release (struct inode *inode, struct file *filp) {

	/* Decrement process count */
	atomic_dec(&asgn1_device.nprocs);

	printk(KERN_INFO "Successfuly Released, Current NPROCS: %d\n", atomic_read(&asgn1_device.nprocs));
	return 0;
}


/**
* This function reads contents of the virtual disk and writes to the user 
*/
ssize_t asgn1_read(struct file *filp, char __user *buf, size_t count,
loff_t *f_pos) {
	size_t size_read = 0;     /* size read from virtual disk in this function */
	size_t begin_offset;      /* the offset from the beginning of a page to start reading */
	int begin_page_no = *f_pos / PAGE_SIZE; /* the first page which contains the requested data */
	int curr_page_no = 0;     /* the current page number */
	size_t curr_size_read;    /* size read from the virtual disk in this round */
	size_t size_to_be_read;   /* size to be read in the current round in while loop */

	//struct list_head *ptr = asgn1_device.mem_list.next;
	page_node *curr;

	/**
	* Traverse the list, once the first requested page is reached,
	*   - use copy_to_user to copy the data to the user-space buf page by page
	*   - you also need to work out the start / end offset within a page
	*   - Also needs to handle the situation where copy_to_user copy less
	*       data than requested, and
	*       copy_to_user should be called again to copy the rest of the
	*       unprocessed data, and the second and subsequent calls still
	*       need to check whether copy_to_user copies all data requested.
	*       This is best done by a while / do-while loop.
	*
	* if end of data area of ramdisk reached before copying the requested
	*   return the size copied to the user space so far
	*/

	printk(KERN_WARNING "Entering Read Function");

	/* check f_pos, if beyond data_size, return 0. */
	if( *f_pos > asgn1_device.data_size ) {
		printk(KERN_WARNING "f_pos beyond data_size");
		return 0;
	}

	/* Traverse the list. */
	list_for_each_entry(curr, &asgn1_device.mem_list, list){
		//printk(KERN_WARNING "Curr: %d, Begin: %d", curr_page_no, begin_page_no);

		/* Reached requested page. */
		if(curr_page_no >= begin_page_no){
			begin_offset = *f_pos % PAGE_SIZE;
			if(count >= asgn1_device.data_size){ count = asgn1_device.data_size; }
			size_to_be_read = min( (int) PAGE_SIZE - begin_offset, (int) count - size_read);
			//printk(KERN_WARNING "First: %d, Second: %d", (int) PAGE_SIZE - begin_offset, (int) count-*f_pos);
			printk(KERN_INFO "Read: %d, Count: %d, To Read: %d", size_read, count, size_to_be_read);

			curr_size_read = 0;

			if( (int) *f_pos + size_to_be_read > asgn1_device.data_size ){
				size_to_be_read = asgn1_device.data_size - (int) *f_pos;
			}

			/* use copy_to_user to copy the data to the user-space buf */
			while(curr_size_read < size_to_be_read){
				curr_size_read = size_to_be_read - copy_to_user(buf + size_read,
				page_address(curr->page) + begin_offset,
				size_to_be_read);

				size_read += curr_size_read;
				size_to_be_read -= curr_size_read;
				begin_offset += curr_size_read;
				*f_pos += curr_size_read;

				printk(KERN_INFO "Size Read: %d, Remaining: %d", size_read, count - size_read);
			}

			if(size_read >= count){
				printk(KERN_INFO "size_read >= count");
				break;
			}

			if( *f_pos > asgn1_device.data_size){
				printk(KERN_INFO "F_POS greater than data_size");
				break;
			};

		}

		curr_page_no++;
	}

	printk(KERN_WARNING "Read %d bytes\n", (int)size_read);
	return size_read;
}

static loff_t asgn1_lseek (struct file *file, loff_t offset, int cmd)
{
	loff_t testpos;
	size_t buffer_size;

	testpos = 0;
	buffer_size = asgn1_device.num_pages * PAGE_SIZE;

	/* set testpos according to the command */
	switch(cmd){
	case SEEK_SET:
		testpos = offset;
		break;
	case SEEK_CUR:
		testpos = file->f_pos + offset;
		break;
	case SEEK_END:
		testpos = buffer_size + offset;
		break;
	}

	/* if testpos larger than buffer_size, set testpos to buffer_size */
	if( testpos > buffer_size ){
		testpos = buffer_size;
	}

	/* if testpos smaller than 0, set testpos to 0 */
	if( testpos < 0 ){
		testpos = 0;
	}

	/* set file->f_pos to testpos */
	file->f_pos = testpos;

	printk (KERN_INFO "Seeking to pos=%ld\n", (long)testpos);
	return testpos;
}


/**
* This function writes from the user buffer to the virtual disk of this
* module
*/
ssize_t asgn1_write(struct file *filp, const char __user *buf, size_t count,
loff_t *f_pos) {
	size_t orig_f_pos = *f_pos;  /* the original file position */
	size_t size_written = 0;  /* size written to virtual disk in this function */
	size_t begin_offset;      /* the offset from the beginning of a page to
				start writing */
	int begin_page_no = *f_pos / PAGE_SIZE;  /* the first page this finction
						should start writing to */

	int curr_page_no = 0;     /* the current page number */
	size_t curr_size_written; /* size written to virtual disk in this round */
	size_t size_to_be_written;  /* size to be read in the current round in 
				while loop */
	int nPages;


	//struct list_head *ptr = asgn1_device.mem_list.next;
	page_node *curr;

	/**
	* Traverse the list until the first page reached, and add nodes if necessary
	*
	* Then write the data page by page, remember to handle the situation
	*   when copy_from_user() writes less than the amount you requested.
	*   a while loop / do-while loop is recommended to handle this situation. 
	*/

	printk(KERN_INFO "Entered Write Function");

	/* Allocate memory for appropriate number of pages and add them to list */
	nPages = (*f_pos + count + (PAGE_SIZE-1) )/PAGE_SIZE;

	//if(nPages == 0 ) nPages = 1;
	printk(KERN_INFO "nPages = %d,devPages = %d, count = %d", nPages, asgn1_device.num_pages, count);

	while(asgn1_device.num_pages < nPages){
		printk(KERN_INFO "Adding Page");
		curr = kmalloc(sizeof(page_node), GFP_KERNEL);

		if(curr){
			curr->page = alloc_page(GFP_KERNEL);
			if(curr->page == NULL){
				printk(KERN_INFO "Memory Allocation Failed");
				return -ENOMEM;
			}
		} else {
			printk(KERN_INFO "Memory Allocation Failed");
			return -ENOMEM;
		}
		list_add_tail( &(curr->list), &asgn1_device.mem_list);
		asgn1_device.num_pages++;
	}

	printk(KERN_INFO "Memory Allocation Successful");

	printk(KERN_INFO "nPages = %d, devPages = %d, count = %d", nPages, asgn1_device.num_pages, count);

	/* Iterate through list writing to each page */
	list_for_each_entry(curr, &asgn1_device.mem_list, list){

		//printk(KERN_INFO "Iterating List");

		if(curr_page_no >= begin_page_no && size_written < count){
			begin_offset = *f_pos % PAGE_SIZE;
			//size_to_be_written = min( (int) PAGE_SIZE - begin_offset, count - *f_pos);
			size_to_be_written = min( (int) PAGE_SIZE - begin_offset, count-size_written);

			//printk(KERN_INFO "Page Found");
			printk(KERN_INFO "Written: %d, Count: %d, To Write: %d", size_written, count, size_to_be_written);

			//while(size_written < count){}
			curr_size_written = 0;

			while( curr_size_written < size_to_be_written ){
				curr_size_written = size_to_be_written - copy_from_user(page_address(curr->page) + begin_offset,
				buf + size_written,
				size_to_be_written);
				size_written += curr_size_written;
				size_to_be_written -= curr_size_written;
				begin_offset += curr_size_written;
				*f_pos += curr_size_written;

				printk(KERN_INFO "Size Written: %d, Remaining: %d\n", size_written, count-size_written);
			}
		}

		curr_page_no++;
	}

	asgn1_device.data_size = max(asgn1_device.data_size,
	orig_f_pos + size_written);

	printk(KERN_WARNING "Wrote %d bytes\n", (int)size_written);
	return size_written;
}

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
* The ioctl function, which nothing needs to be done in this case.
*/
long asgn1_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {
	int nr;
	int new_nprocs;
	int result;

	printk(KERN_INFO "Entering IOCTL Function");

	/* check whether cmd is for our device, if not for us, return -EINVAL */
	if(MYIOC_TYPE !=  _IOC_TYPE(cmd)){
		printk(KERN_WARNING "Command not for asgn1_device, returning");
		return -EINVAL;
	}

	/* get command, if command is SET_NPROC_OP, then get the data, and set max_nprocs accordingly. */
	nr = _IOC_NR(cmd);
	if( nr == SET_NPROC_OP){

		/*check validity of the value before setting max_nprocs*/
		if( access_ok(VERIFY_READ, arg, sizeof(cmd))){

			result = __get_user(new_nprocs, (int*) arg);
			if( result != 0 ){ /* Bad Access from User Space. */
				printk(KERN_WARNING "Bad Access from User Space\n");
				return -EFAULT;
			}

			if( new_nprocs < 1 ){ /* Max nprocs must be at least one. */
				printk(KERN_WARNING "New max nprocs less than one");
				return -EINVAL;
			}

			atomic_set(&asgn1_device.max_nprocs, new_nprocs); /* Update max_nprocs. */
			printk(KERN_INFO "Max nprocs update successful");
			return 0; /* Success. */

		} else {
			printk(KERN_WARNING "access_ok() failed, unallowed access");
			return -EFAULT; /* Access not allowed. */
		}

	}

	printk(KERN_WARNING "Bad command for driver");
	return -ENOTTY; /* Command not applicable to this driver */

}


static int asgn1_mmap (struct file *filp, struct vm_area_struct *vma)
{
	unsigned long pfn;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long len = vma->vm_end - vma->vm_start;
	unsigned long ramdisk_size = asgn1_device.num_pages * PAGE_SIZE;
	page_node *curr;
	unsigned long index = 0;

	/* Check offset and len */
	if(len > ramdisk_size){
		printk(KERN_WARNING "Length greater than ramdisk size");
		return -EINVAL;
	}
	if(offset > asgn1_device.num_pages){
		printk(KERN_WARNING "Offset greater than number of pages");
		return -EINVAL;
	}

	/* loop through the entire page list */
	list_for_each_entry(curr, &asgn1_device.mem_list, list){
		/* once the first requested page reached, add each page with remap_pfn_range one by one */
		if(index >= offset){
			pfn = page_to_pfn(curr->page);
			remap_pfn_range( vma, vma->vm_start + (index*PAGE_SIZE), pfn, PAGE_SIZE, vma->vm_page_prot);
		}
		index++;
	}
	return 0;
}


struct file_operations asgn1_fops = {
	.owner = THIS_MODULE,
	.read = asgn1_read,
	.write = asgn1_write,
	.unlocked_ioctl = asgn1_ioctl,
	.open = asgn1_open,
	.mmap = asgn1_mmap,
	.release = asgn1_release,
	.llseek = asgn1_lseek
};


static void *my_seq_start(struct seq_file *s, loff_t *pos)
{
	if(*pos >= 1) return NULL;
	else return &asgn1_dev_count + *pos;
}

static void *my_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if(*pos >= 1) return NULL;
	else return &asgn1_dev_count + *pos;
}

static void my_seq_stop(struct seq_file *s, void *v)
{
	/* There's nothing to do here! */
}

int my_seq_show(struct seq_file *s, void *v) {
	/**
* use seq_printf to print some info to s
*/
	seq_printf(s,"Major Number: %d\n Minor Number: %d\n Num Pages: %d\n Data Size: %zu\n Nprocs: %d\n Max-Nprocs: %d\n",
	asgn1_major, asgn1_minor, asgn1_device.num_pages, asgn1_device.data_size,
	atomic_read(&asgn1_device.nprocs), atomic_read(&asgn1_device.max_nprocs));
	return 0;


}


static struct seq_operations my_seq_ops = {
	.start = my_seq_start,
	.next = my_seq_next,
	.stop = my_seq_stop,
	.show = my_seq_show
};

static int my_proc_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &my_seq_ops);
}

struct file_operations asgn1_proc_ops = {
	.owner = THIS_MODULE,
	.open = my_proc_open,
	.llseek = seq_lseek,
	.read = seq_read,
	.release = seq_release,
};



/**
* Initialise the module and create the master device
*/
int __init asgn1_init_module(void){
	int result;
	
	/* set nprocs and max_nprocs of the device */
	atomic_set(&asgn1_device.nprocs, 0);
	atomic_set(&asgn1_device.max_nprocs, 1);

	/* Allocate Major/Minor number */
	asgn1_device.dev = MKDEV(asgn1_major, asgn1_minor);
	result = alloc_chrdev_region(&asgn1_device.dev, asgn1_minor, asgn1_dev_count, MYDEV_NAME);
	if(result != 0){
		printk(KERN_WARNING "Major/Minor number allocation failed");
		goto fail_device;
	}

	/*Allocate cdev and set ops and owner field */
	asgn1_device.cdev = cdev_alloc();
	cdev_init(asgn1_device.cdev, &asgn1_fops);
	asgn1_device.cdev->owner = THIS_MODULE; 
	result = cdev_add(asgn1_device.cdev, asgn1_device.dev, asgn1_dev_count);
	if(result != 0){
		printk(KERN_WARNING "CDEV Initialisation Failed");
		goto fail_device;
	}

	/* Initialise page list */
	INIT_LIST_HEAD(&asgn1_device.mem_list);

	/* Create proc entries */
	asgn1_proc = proc_create(MYDEV_NAME, 0, NULL, &asgn1_proc_ops);
	
	asgn1_device.class = class_create(THIS_MODULE, MYDEV_NAME);
	if (IS_ERR(asgn1_device.class)) {
	}

	asgn1_device.device = device_create(asgn1_device.class, NULL, 
	asgn1_device.dev, "%s", MYDEV_NAME);
	if (IS_ERR(asgn1_device.device)) {
		printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
		result = -ENOMEM;
		goto fail_device;
	}

	printk(KERN_WARNING "set up udev entry\n");
	printk(KERN_WARNING "Hello world from %s\n", MYDEV_NAME);
	return 0;

	/* cleanup code called when any of the initialization steps fail */
	fail_device:
	class_destroy(asgn1_device.class);
	
	if(asgn1_proc) remove_proc_entry(MYDEV_NAME, NULL);
	cdev_del(asgn1_device.cdev);
	unregister_chrdev_region(asgn1_device.dev, asgn1_dev_count);

	return result;
}


/**
* Finalise the module
*/
void __exit asgn1_exit_module(void){
	device_destroy(asgn1_device.class, asgn1_device.dev);
	class_destroy(asgn1_device.class);
	printk(KERN_WARNING "cleaned up udev entry\n");

	/**
	* free all pages in the page list
	* cleanup in reverse order
	*/
	free_memory_pages();

	remove_proc_entry(MYDEV_NAME, NULL);
	cdev_del(asgn1_device.cdev);
	unregister_chrdev_region(asgn1_device.dev, asgn1_dev_count);
	printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}


module_init(asgn1_init_module);
module_exit(asgn1_exit_module);


