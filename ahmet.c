#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define BUFFER_SIZE 1024

static dev_t dev_num;
static struct cdev ahmet_cdev;
static struct class *ahmet_class;
static struct device *ahmet_device;

//static char buffer[BUFFER_SIZE];


static char *buffer;
static size_t buffer_size = 0;
static size_t buffer_capacity = 0;

static struct mutex ahmet_mutex;

static int ahmet_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened\n");
    return 0;
}

static int ahmet_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}

static ssize_t ahmet_read(struct file *file,
                          char __user *buf,
                          size_t len,
                          loff_t *offset)
{

    size_t bytes_to_read;
    mutex_lock(&ahmet_mutex);

    if (*offset >= buffer_size)
    {
        mutex_unlock(&ahmet_mutex);
        return 0;
    }

    bytes_to_read = min(len, buffer_size - *offset);

    if (copy_to_user(buf,
                     buffer + *offset,
                     bytes_to_read))
    {
        mutex_unlock(&ahmet_mutex);
        return -EFAULT;
    }

    *offset += bytes_to_read;

    pr_info("Device read: %zu bytes\n", bytes_to_read);

    mutex_unlock(&ahmet_mutex);

    return bytes_to_read;
}

static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
mutex_lock(&ahmet_mutex);

    if (len > buffer_capacity)
        len = buffer_capacity;

    if (copy_from_user(buffer, buf, len))
    {
        mutex_unlock(&ahmet_mutex);
        return -EFAULT;
    }

    buffer_size = len;

    mutex_unlock(&ahmet_mutex);

    pr_info("Device write: %zu bytes\n", len);

    return len;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = ahmet_open,
    .release = ahmet_release,
    .read = ahmet_read,
    .write = ahmet_write,
};

static int __init ahmet_init(void)
{
    int ret;

    mutex_init(&ahmet_mutex);

    buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);

    if(!buffer)
    {
        pr_err("Failed to allocate buffer\n");
        
        return -ENOMEM;
    }

    memset(buffer, 0, buffer_capacity);

    ret = alloc_chrdev_region(&dev_num, 0, 1, "ahmet");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        goto free_buffer;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));

    cdev_init(&ahmet_cdev, &fops);

    ret = cdev_add(&ahmet_cdev, dev_num, 1);
    if (ret < 0)
    {
        pr_err("Failed to add cdev\n");
        goto unregister_chrdev;
    }

    ahmet_class = class_create("ahmet_class");
    if (IS_ERR(ahmet_class))
    {
        ret = PTR_ERR(ahmet_class);
        goto unregister_cdev;
        //cdev_del(&ahmet_cdev);
        //unregister_chrdev_region(dev_num, 1);
        //return PTR_ERR(ahmet_class);
    }

    ahmet_device = device_create(
        ahmet_class,
        NULL,
        dev_num,
        NULL,
        "ahmet"
    );

    if(IS_ERR(ahmet_device))
    {
        ret = PTR_ERR(ahmet_device);
        goto unregister_class;
        //class_destroy(ahmet_class);
        //cdev_del(&ahmet_cdev);
        //unregister_chrdev_region(dev_num, 1);
        //return PTR_ERR(ahmet_device);
    }

    pr_info("Ahmet character device added\n");

    return 0;

    unregister_class:

        class_destroy(ahmet_class);

    unregister_cdev:

        cdev_del(&ahmet_cdev);

    unregister_chrdev:
        unregister_chrdev_region(dev_num, 1);

    free_buffer:
        kfree(buffer);
        buffer = NULL;

        return ret;
}

static void ahmet_cleanup(void)
{
    if(ahmet_device)
        device_destroy(ahmet_class, dev_num);


    if(ahmet_class)
        class_destroy(ahmet_class);

    cdev_del(&ahmet_cdev);

    unregister_chrdev_region(dev_num, 1);

    mutex_destroy(&ahmet_mutex);

    if(buffer)
    {
        kfree(buffer);
        buffer = NULL;
    }
}


static void __exit ahmet_exit(void)
{
    ahmet_cleanup();

    pr_info("Ahmet module unloaded!\n");
}

module_init(ahmet_init);
module_exit(ahmet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmet Hasan Celik");
MODULE_DESCRIPTION("My first Linux character driver");