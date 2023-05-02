#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h> // copy_from/to_user
#include <asm/uaccess.h> // ^same
#include <linux/sched.h> // for timers
#include <linux/jiffies.h> // for jiffies global variable
#include <linux/string.h> // for string manipulation functions
#include <linux/ctype.h> // for isdigit
#include <linux/delay.h> //for msleep()
#include <linux/input.h> //for input_register_handler()


MODULE_LICENSE("GPL");
MODULE_AUTHOR("jimmy95&gylin");


//graphics colors
#define CYG_FB_DEFAULT_PALETTE_BLUE         0x01
#define CYG_FB_DEFAULT_PALETTE_RED          0x04
#define CYG_FB_DEFAULT_PALETTE_WHITE        0x0F
#define CYG_FB_DEFAULT_PALETTE_LIGHTBLUE    0x09
#define CYG_FB_DEFAULT_PALETTE_BLACK        0x00


// CHARACTER DEVICE VARIABLES
static int myscreensaver_major = 61;


struct fb_info *info;
struct fb_fillrect *blank;
struct fb_fillrect *Rblank;
struct fb_fillrect *bulogo[9];

static int input_flag = 1;
void screensaver_handler(void);
void build_arrays(void);


// TIMER 
static struct timer_list Timer;
int del_timer_sync(struct timer_list *Timer);
void mytimer_callback(struct timer_list *Timer)
{
	input_flag = 1;
	screensaver_handler();
}

static int myscreensaver_open(struct inode *inode, struct file *filp);
static int myscreensaver_release(struct inode *inode, struct file *filp);
struct file_operations myscreensaver_fops = {
	open: myscreensaver_open,
	release: myscreensaver_release,
};

static struct input_device_id screenev_ids[2];
static int screenev_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id);
static void screenev_disconnect(struct input_handle *handle);
static void screenev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value);


static int screenev_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
  		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "kbd";

	error = input_register_handle(handle);
	if (error)
  		goto err_free_handle;

	error = input_open_device(handle);
	if (error)
  		goto err_unregister_handle;

	return 0;

err_unregister_handle:
	input_unregister_handle(handle);
err_free_handle:
	kfree(handle);
	return error;
}


static void screenev_disconnect(struct input_handle *handle)
{
  	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

MODULE_DEVICE_TABLE(input, screenev_ids);


static struct input_device_id screenev_ids[2] = {
	{ .driver_info = 1 },	// Matches all devices
	{ },			// Terminating zero entry
};

static void screenev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	// Runs when an event occurs
	input_flag = 0;
	mod_timer(&Timer,  jiffies + 15 * HZ);
}

static struct input_handler screenev_handler = {
	.event		= screenev_event,
	.connect	= screenev_connect,
	.disconnect	= screenev_disconnect,
	.legacy_minors	= true,
	.name		= "screenev",
	.id_table	= screenev_ids,
};


/* Helper function borrowed from drivers/video/fbdev/core/fbmem.c */
static struct fb_info *get_fb_info(unsigned int idx)
{
	struct fb_info *fb_info;

	if (idx >= FB_MAX)
        	return ERR_PTR(-ENODEV);

	fb_info = registered_fb[idx];
	if (fb_info)
        	atomic_inc(&fb_info->count);

	return fb_info;
}


static int __init myscreensaver_init(void)
{
	int result, ret;

	printk(KERN_INFO "Hello framebuffer!\n");

	// Register device
	result = register_chrdev(myscreensaver_major, "myscreensaver", &myscreensaver_fops);
	if (result < 0)
	{
        	printk(KERN_ALERT "Cannot obtain major number");
		return result;
    	}


	// Register input handler
	ret = input_register_handler(&screenev_handler);
	if (ret < 0)
	{
		printk(KERN_ALERT "Unable to register input handler");
		return ret;
	}


	// Initialize all graphics arrays
	build_arrays();
	blank = kmalloc(sizeof(struct fb_fillrect), GFP_KERNEL);
	blank->dx = 0;
	blank->dy = 0;
    	blank->width = 480;
    	blank->height = 272;
    	blank->color = CYG_FB_DEFAULT_PALETTE_BLACK;
    	blank->rop = ROP_COPY;
	info = get_fb_info(0);
        lock_fb_info(info);
	sys_fillrect(info, blank);
	unlock_fb_info(info);
	// Start 15 second timer
	timer_setup(&Timer, mytimer_callback, 0);
        mod_timer(&Timer, jiffies + 15 * HZ);

	printk(KERN_INFO "Module init complete\n");

    	return 0;
}

static void __exit myscreensaver_exit(void) {
	//will run when call rmmod myscreensaver
	//may cause page domain fault or kernel oops if screensaver is currently drawing - needs to be off in order to not cause error and remove successfully
	// Cleanup framebuffer graphics
	int i;
	kfree(blank);
	kfree(Rblank);
        for(i=0; i<9; i++)
        {
                kfree(bulogo[i]);
        }
	

	// Unregister evdev handler
	input_unregister_handler(&screenev_handler);

	// Delete remaining timers
	del_timer_sync(&Timer);

	// Freeing the major number
        unregister_chrdev(myscreensaver_major, "myscreensaver");

    	printk(KERN_INFO "Goodbye framebuffer!\n");
    	printk(KERN_INFO "Module exiting\n");
}

module_init(myscreensaver_init);
module_exit(myscreensaver_exit);


static int myscreensaver_open(struct inode *inode, struct file *filp)
{
        /* Success */
        return 0;
}

static int myscreensaver_release(struct inode *inode, struct file *filp)
{
        /* Success */
        return 0;
}

void build_arrays()
{
	int i;
    	// Set up RED bg
    	Rblank = kmalloc(sizeof(struct fb_fillrect), GFP_KERNEL);
	Rblank->dx = 0;
	Rblank->dy = 0;
    	Rblank->width = 480;
    	Rblank->height = 272;
    	Rblank->color = CYG_FB_DEFAULT_PALETTE_RED;
    	Rblank->rop = ROP_COPY;

    	// Set up bulogo
    	for(i=0; i<9; i++)
    	{
        	bulogo[i] = kmalloc(sizeof(struct fb_fillrect), GFP_KERNEL);
        	bulogo[i]->color = CYG_FB_DEFAULT_PALETTE_WHITE;
        	bulogo[i]->rop = ROP_COPY;
    	}

             //the letter "B"
    	bulogo[0]->dx = 120;
    	bulogo[0]->dy = 31;
    	bulogo[0]->width = 20;
    	bulogo[0]->height = 210;

	bulogo[1]->dx = 120;
        bulogo[1]->dy = 31;
        bulogo[1]->width = 80;
        bulogo[1]->height = 20;

    	bulogo[2]->dx = 200;
        bulogo[2]->dy = 51;
        bulogo[2]->width = 20;
        bulogo[2]->height = 75;

         
    	bulogo[3]->dx = 120;
        bulogo[3]->dy = 126;
        bulogo[3]->width = 80;
        bulogo[3]->height = 20;

    	bulogo[4]->dx = 120;
        bulogo[4]->dy = 221;
        bulogo[4]->width = 80;
        bulogo[4]->height = 20;

        bulogo[5]->dx = 200;
        bulogo[5]->dy = 146;
        bulogo[5]->width = 20;
        bulogo[5]->height = 75;

             //the letter "U"
    	bulogo[6]->dx = 280;
        bulogo[6]->dy = 31;
        bulogo[6]->width = 20;
        bulogo[6]->height = 190;

        bulogo[7]->dx = 300;
        bulogo[7]->dy = 221;
        bulogo[7]->width = 40;
        bulogo[7]->height = 20;

        bulogo[8]->dx = 340;
        bulogo[8]->dy = 31;
        bulogo[8]->width = 20;
        bulogo[8]->height = 190;
}
    	

void screensaver_handler(void)
{
    int i;
    int xpos = 0; // starting position of the logo
    int xdir = 1; // direction of movement (1 = right, -1 = left)
    int speed = 10; // number of pixels to move per iteration
    int screen_width = 480;

    info = get_fb_info(0);
    lock_fb_info(info);

    // Initial Setup
    while (input_flag == 1)
    {
        sys_fillrect(info, Rblank);

        // Move logo horizontally
        xpos += speed * xdir;
        for (i = 0; i < 9; i++)
        {
            bulogo[i]->dx = xpos + bulogo[i]->dx - 120;
            sys_fillrect(info, bulogo[i]);
        }

        // Check if logo reaches edge of screen
        if (xpos <= 0 || xpos + 220 >= screen_width)
        {
            xdir *= -1; // change direction
            xpos += speed * xdir; // move back in opposite direction
        }

        if (input_flag == 0)
            break;

        msleep(100);
    }

    blank->color = CYG_FB_DEFAULT_PALETTE_BLACK;
    sys_fillrect(info, blank);
    unlock_fb_info(info);
}

