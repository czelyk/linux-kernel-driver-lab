#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define BUFFER_SIZE 1024

static dev_t dev_num;
static struct cdev ahmet_cdev;
static struct class *ahmet_class;
static struct device *ahmet_device;

static char buffer[BUFFER_SIZE];
static size_t buffer_size = 0;

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

    if (*offset >= buffer_size)
        return 0;

    bytes_to_read = min(len, buffer_size - *offset);

    if (copy_to_user(buf,
                     buffer + *offset,
                     bytes_to_read))
    {
        return -EFAULT;
    }

    *offset += bytes_to_read;

    pr_info("Device read: %zu bytes\n", bytes_to_read);

    return bytes_to_read;
}

static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
    if (len > BUFFER_SIZE)
        len = BUFFER_SIZE;

    if (copy_from_user(buffer, buf, len))
    {
        return -EFAULT;
    }

    buffer_size = len;

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

    ret = alloc_chrdev_region(&dev_num, 0, 1, "ahmet");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        return ret;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));

    cdev_init(&ahmet_cdev, &fops);

    ret = cdev_add(&ahmet_cdev, dev_num, 1);
    if (ret < 0)
    {
        pr_err("Failed to add cdev\n");
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    ahmet_class = class_create("ahmet_class");
    if (IS_ERR(ahmet_class))
    {
        cdev_del(&ahmet_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(ahmet_class);
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
        class_destroy(ahmet_class);
        cdev_del(&ahmet_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(ahmet_device);
    }

    pr_info("Ahmet character device added\n");

    return 0;
}

static void __exit ahmet_exit(void)
{
    device_destroy(ahmet_class, dev_num);
    class_destroy(ahmet_class);
    
    cdev_del(&ahmet_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("Ahmet module unloaded!\n");
}

module_init(ahmet_init);
module_exit(ahmet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmet Hasan Celik");
MODULE_DESCRIPTION("My first Linux character driver");