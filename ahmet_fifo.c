#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include  <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/kfifo.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#define BUFFER_SIZE PAGE_SIZE

#define AHMET_IOCTL_MAGIC 'A'

#define AHMET_CLEAR_BUFFER \
        _IO(AHMET_IOCTL_MAGIC, 1)

#define AHMET_GET_BUFFER_SIZE \
        _IOR(AHMET_IOCTL_MAGIC, 2, size_t)

#define AHMET_GET_BUFFER_CAPACITY \
        _IOR(AHMET_IOCTL_MAGIC, 3, size_t)

#define AHMET_FILL_BUFFER \
        _IOW(AHMET_IOCTL_MAGIC, 4, char)

#define AHMET_GET_DEVICE_ID \
        _IOR(AHMET_IOCTL_MAGIC, 5, int)

#define DEVICE_COUNT 4


        

static dev_t dev_num;
static struct class *ahmet_class;
static struct device *ahmet_device[DEVICE_COUNT];
static struct dentry *ahmet_debugfs_root;
static struct proc_dir_entry *ahmet_proc_entry;

struct ahmet_device
{
    struct cdev cdev;
    struct kfifo fifo;
    int device_id;

    struct mutex ahmet_mutex;

    struct completion test_completion;
    struct work_struct completion_work;

    atomic_t produced_events;
    atomic_t read_events;
    atomic_t dropped_events;
    atomic_t timer_callbacks;
    bool timer_enabled;

    struct fasync_struct *async_queue;

    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;

    struct timer_list timer;
    struct work_struct timer_work;
    struct task_struct *thread;
    struct semaphore open_sem;
};

//static struct ahmet_device *ahmet_dev;

static struct ahmet_device ahmet_devices[DEVICE_COUNT];

static ssize_t fifo_len_show(struct device *device,
                            struct device_attribute *attr,
                            char *buf)
{
    struct ahmet_device *dev;
    unsigned int len;

    dev = dev_get_drvdata(device);

    mutex_lock(&dev->ahmet_mutex);

    len = kfifo_len(&dev->fifo);

    mutex_unlock(&dev->ahmet_mutex);

    return sysfs_emit(buf, "%u\n", len);
}

static DEVICE_ATTR_RO(fifo_len);

static ssize_t fifo_capacity_show(struct device *device,
                                  struct device_attribute *attr,
                                  char *buf)
{
    struct ahmet_device *dev;
    unsigned int capacity;

    dev = dev_get_drvdata(device);

    mutex_lock(&dev->ahmet_mutex);
    capacity = kfifo_size(&dev->fifo);
    mutex_unlock(&dev->ahmet_mutex);

    return sysfs_emit(buf, "%u\n", capacity);
}

static DEVICE_ATTR_RO(fifo_capacity);

static ssize_t produced_events_show(struct device *device,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct ahmet_device *dev;

    dev = dev_get_drvdata(device);

    return sysfs_emit(buf, "%d\n",
                      atomic_read(&dev->produced_events));
}

static DEVICE_ATTR_RO(produced_events);

static ssize_t read_events_show(struct device *device,
                                struct device_attribute *attr,
                                char *buf)
{
    struct ahmet_device *dev;

    dev = dev_get_drvdata(device);

    return sysfs_emit(buf, "%d\n",
                      atomic_read(&dev->read_events));
}

static DEVICE_ATTR_RO(read_events);

static ssize_t dropped_events_show(struct device *device,
                                   struct device_attribute *attr,
                                   char *buf)
{
    struct ahmet_device *dev;

    dev = dev_get_drvdata(device);

    return sysfs_emit(buf, "%d\n",
                      atomic_read(&dev->dropped_events));
}

static DEVICE_ATTR_RO(dropped_events);

static ssize_t timer_callbacks_show(struct device *device,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct ahmet_device *dev;

    dev = dev_get_drvdata(device);

    return sysfs_emit(buf, "%d\n",
                      atomic_read(&dev->timer_callbacks));
}

static DEVICE_ATTR_RO(timer_callbacks);

static ssize_t timer_enabled_show(struct device *device,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct ahmet_device *dev;

    dev = dev_get_drvdata(device);

    return sysfs_emit(buf, "%d\n",
                        READ_ONCE(dev->timer_enabled) ? 1 : 0);
}

static ssize_t timer_enabled_store(struct device *device,
                                    struct device_attribute *attr,
                                    const char *buf,
                                    size_t count)
{
    struct ahmet_device *dev;
    bool value;
    int ret;

    dev = dev_get_drvdata(device);

    ret = kstrtobool(buf, &value);
    if (ret)
        return ret;

    WRITE_ONCE(dev->timer_enabled, value);

    return count;
}

static DEVICE_ATTR_RW(timer_enabled);

static struct attribute *ahmet_attrs[] = {
    &dev_attr_fifo_len.attr,
    &dev_attr_fifo_capacity.attr,
    &dev_attr_produced_events.attr,
    &dev_attr_read_events.attr,
    &dev_attr_dropped_events.attr,
    &dev_attr_timer_callbacks.attr,
    &dev_attr_timer_enabled.attr,
    NULL
};

static const struct attribute_group ahmet_attr_group = {
    .attrs = ahmet_attrs,
};

static void ahmet_timer_work(struct work_struct *work)
{
    struct ahmet_device *dev;
    static const char message[] = "Timer Event\n";
    unsigned int inserted;

    dev = container_of(work,
                       struct ahmet_device,
                       timer_work);

    mutex_lock(&dev->ahmet_mutex);

    inserted = kfifo_in(&dev->fifo,
                        message,
                        sizeof(message) - 1);

    mutex_unlock(&dev->ahmet_mutex);

    if (inserted > 0)
    {
        wake_up_interruptible(&dev->read_queue);

        if (dev->async_queue)
        {
            kill_fasync(&dev->async_queue,
                        SIGIO,
                        POLL_IN);
        }

        pr_info("Timer worker inserted %u bytes into device %d FIFO\n",
                inserted,
                dev->device_id);
    }
}

static void ahmet_timer_callback(struct timer_list *timer)
{
    struct ahmet_device *dev;

    dev = container_of(timer,
                       struct ahmet_device,
                       timer);

    //spin_lock(&dev->stats_lock);

    //dev->timer_callbacks++;

    //spin_unlock(&dev->stats_lock);

    atomic_inc(&dev->timer_callbacks);
    
    if (READ_ONCE(dev->timer_enabled))
        schedule_work(&dev->timer_work);

    mod_timer(&dev->timer,
              jiffies + msecs_to_jiffies(1000));
}

static int ahmet_debugfs_stats_show(struct seq_file *m,
                                    void *v)
{
    struct ahmet_device *dev = m->private;
    unsigned int fifo_len;
    unsigned int fifo_size;
    unsigned int fifo_avail;

    mutex_lock(&dev->ahmet_mutex);

    fifo_len = kfifo_len(&dev->fifo);
    fifo_size = kfifo_size(&dev->fifo);
    fifo_avail = kfifo_avail(&dev->fifo);

    mutex_unlock(&dev->ahmet_mutex);

    seq_printf(m, "device_id:   %d\n",
                dev->device_id);

    seq_printf(m, "fifo_len:         %u\n",
               fifo_len);

    seq_printf(m, "fifo_capacity:    %u\n",
               fifo_size);

    seq_printf(m, "fifo_available:   %u\n",
               fifo_avail);

    seq_printf(m, "produced_events:  %d\n",
               atomic_read(&dev->produced_events));

    seq_printf(m, "read_events:      %d\n",
               atomic_read(&dev->read_events));

    seq_printf(m, "dropped_events:   %d\n",
               atomic_read(&dev->dropped_events));

    seq_printf(m, "timer_callbacks:  %d\n",
               atomic_read(&dev->timer_callbacks));

    seq_printf(m, "timer_enabled:    %d\n",
               READ_ONCE(dev->timer_enabled) ? 1 : 0);

    return 0;
}

static int ahmet_debugfs_stats_open(struct inode *inode,
                                    struct file *file)
{
    return single_open(file,
                       ahmet_debugfs_stats_show,
                       inode->i_private);
}

static const struct file_operations ahmet_debugfs_fops = {
    .owner   = THIS_MODULE,
    .open    = ahmet_debugfs_stats_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static int ahmet_proc_show(struct seq_file *m, void *v)
{
    int i;

    for (i = 0; i < DEVICE_COUNT; i++)
    {
        struct ahmet_device *dev = &ahmet_devices[i];
        unsigned int fifo_len;

        mutex_lock(&dev->ahmet_mutex);
        fifo_len = kfifo_len(&dev->fifo);
        mutex_unlock(&dev->ahmet_mutex);

        seq_printf(m,
                   "Device %d\n"
                   "  fifo_len: %u\n"
                   "  produced: %d\n"
                   "  read: %d\n"
                   "  dropped: %d\n"
                   "  timer_callbacks: %d\n"
                   "  timer_enabled: %d\n\n",
                   dev->device_id,
                   fifo_len,
                   atomic_read(&dev->produced_events),
                   atomic_read(&dev->read_events),
                   atomic_read(&dev->dropped_events),
                   atomic_read(&dev->timer_callbacks),
                   READ_ONCE(dev->timer_enabled) ? 1 : 0);
    }

    return 0;
}

static int ahmet_proc_open(struct inode *inode,
                           struct file *file)
{
    return single_open(file,
                       ahmet_proc_show,
                       NULL);
}

static const struct proc_ops ahmet_proc_ops = {
    .proc_open    = ahmet_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};


static int ahmet_thread_function(void *data)
{
    struct ahmet_device *dev = data;
    static const char message[] = "Thread Event\n";
    const unsigned int message_len = sizeof(message) - 1;

    unsigned long produced;
    unsigned long read_count;
    unsigned long dropped;
    unsigned long timer_count;

    pr_info("Kthread started for device %d\n",
            dev->device_id);

    reinit_completion(&dev->test_completion);

    schedule_work(&dev->completion_work);

    pr_info("Kthread waiting for completion on device %d\n",
            dev->device_id);

    wait_for_completion(&dev->test_completion);

    pr_info("Kthread completion received on device %d\n",
            dev->device_id);

    while (!kthread_should_stop())
    {
        unsigned int inserted = 0;

        /*
         * Thread yaklaşık bir saniye uyur.
         * kthread_stop() gelirse uyku erken kesilebilir.
         */
        if (msleep_interruptible(1000) &&
            kthread_should_stop())
        {
            break;
        }

        /*
         * Uyku normal bitse bile bu arada durma isteği
         * gelmiş olabilir. FIFO'ya son bir veri yazmamak
         * için tekrar kontrol ediyoruz.
         */
        if (kthread_should_stop())
        {
            break;
        }

        mutex_lock(&dev->ahmet_mutex);

        /*
         * Mesajın tamamı için yer varsa ekliyoruz.
         * Böylece FIFO'ya yarım "Thread Event" girmez.
         */
        if (kfifo_avail(&dev->fifo) >= message_len)
        {
            inserted = kfifo_in(&dev->fifo,
                                message,
                                message_len);
        }

        mutex_unlock(&dev->ahmet_mutex);

        if (inserted == message_len)
        {
            //spin_lock_bh(&dev->stats_lock);

            //dev->produced_events++;

            //spin_unlock_bh(&dev->stats_lock);

            atomic_inc(&dev->produced_events);

            wake_up_interruptible(&dev->read_queue);

            if (dev->async_queue)
            {
                kill_fasync(&dev->async_queue,
                            SIGIO,
                            POLL_IN);
            }

            pr_info("Kthread inserted %u bytes into device %d FIFO\n",
                    inserted,
                    dev->device_id);
        }
        else
        {
            atomic_inc(&dev->dropped_events);

            pr_info("Device %d FIFO full, thread event dropped\n",
                    dev->device_id);
        }
    }

    produced = atomic_read(&dev->produced_events);
    read_count = atomic_read(&dev->read_events);
    dropped = atomic_read(&dev->dropped_events);
    timer_count = atomic_read(&dev->timer_callbacks);



    pr_info("Device %d stats: produced=%lu read=%lu dropped=%lu timer=%lu\n",
        dev->device_id,
        produced,
        read_count,
        dropped,
        timer_count);
    
    pr_info("Kthread stopping for device %d\n",
            dev->device_id);

    return 0;
}

static void ahmet_completion_work(struct work_struct *work)
{
    struct ahmet_device *dev;

    dev = container_of(work,
                        struct ahmet_device,
                        completion_work);

    pr_info("Completion worker running for device %d\n",
            dev->device_id);

    msleep(500);

    pr_info("Completion worker finished for device %d\n",
            dev->device_id);

    complete(&dev->test_completion);
}


static int ahmet_open(struct inode *inode, struct file *file)
{
    struct ahmet_device *dev;

    dev = container_of(inode->i_cdev,
                        struct ahmet_device,
                        cdev);

    if(down_interruptible(&dev->open_sem))
    {
        return -ERESTARTSYS;
    }

    file->private_data = dev;

    pr_info("Device opened\n");
    return 0;

}

static int ahmet_release(struct inode *inode, struct file *file)
{
    struct ahmet_device *dev = file->private_data;

    (void) inode;

    fasync_helper(-1,
              file,
              0,
              &dev->async_queue);
    
    
    up(&dev->open_sem);
    
    pr_info("Device closed\n");
    return 0;
}

static ssize_t ahmet_read(struct file *file,
                          char __user *buf,
                          size_t len,
                          loff_t *offset)
{   
    (void) offset;

    struct ahmet_device *dev = file->private_data;

    unsigned int copied = 0;
    int ret;

    if(len == 0)
        return 0;

    if((file->f_flags & O_NONBLOCK) &&
        kfifo_is_empty(&dev->fifo))
    {
        return -EAGAIN;
    }

    if (wait_event_interruptible(dev->read_queue,
                                !kfifo_is_empty(&dev->fifo)))
    {
        return -ERESTARTSYS;
    }

    mutex_lock(&dev->ahmet_mutex);

    ret = kfifo_to_user(&dev->fifo,
                        buf,
                        len,
                        &copied);

    mutex_unlock(&dev->ahmet_mutex);
    
    if(ret)
        return ret;

    if (copied > 0)
    {
        atomic_inc(&dev->read_events); 
    }

    wake_up_interruptible(&dev->write_queue);

    pr_info("FIFO read: %u bytes\n", copied);

    return copied;
}

static ssize_t ahmet_write(struct file *file,
                           const char __user *buf,
                           size_t len,
                           loff_t *offset)
{
    (void) offset;

    struct ahmet_device *dev = file->private_data;

    unsigned int copied = 0;
    int ret;
    //char *new_buffer;
    if (len> kfifo_size(&dev->fifo))
    {
        return -ENOSPC;
    }

    if ((file->f_flags & O_NONBLOCK) &&
    kfifo_avail(&dev->fifo) < len)
    {
    return -EAGAIN;
    }

    if(wait_event_interruptible(dev->write_queue,
                                kfifo_avail(&dev->fifo) >= len))
    {
        return -ERESTARTSYS;
    }

    mutex_lock(&dev->ahmet_mutex);

    ret = kfifo_from_user(&dev->fifo,
                            buf,
                            len,
                            &copied);
                            

    mutex_unlock(&dev->ahmet_mutex);

    if(ret)
        return ret;

    wake_up_interruptible(&dev->read_queue);

    if (dev->async_queue)
        kill_fasync(&dev->async_queue,
                    SIGIO,
                    POLL_IN);

    pr_info("Device write: %u bytes\n", copied);

    return copied;
}

static __poll_t ahmet_poll(struct file *file,
                            struct poll_table_struct *wait)
{
    struct ahmet_device *dev = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &dev->read_queue, wait);
    poll_wait(file, &dev->write_queue, wait);

    if(!kfifo_is_empty(&dev->fifo))
    mask |= EPOLLIN | EPOLLRDNORM;


    if(!kfifo_is_full(&dev->fifo))
    {
        mask |= EPOLLOUT | EPOLLWRNORM;
    }

    return mask;
}

static int ahmet_fasync(int fd,
                        struct file *file,
                        int on)
{
    struct ahmet_device *dev = file->private_data;

    return fasync_helper(fd,
                        file,
                        on,
                        &dev->async_queue);
}


static long ahmet_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct ahmet_device *dev = file->private_data;
    size_t fifo_length;
    size_t fifo_capacity;

    pr_info("ioctl received cmd=%u\n", cmd);
    
    switch (cmd)
    {
        case AHMET_CLEAR_BUFFER: 
            mutex_lock(&dev->ahmet_mutex);
            
            kfifo_reset(&dev->fifo);
            
            mutex_unlock(&dev->ahmet_mutex);
            
            wake_up_interruptible(&dev->write_queue);

            pr_info("FIFO cleared\n");
            
            return 0;

        case AHMET_GET_BUFFER_SIZE:
            mutex_lock(&dev->ahmet_mutex);

            fifo_length =kfifo_len(&dev->fifo);

            mutex_unlock(&dev->ahmet_mutex);
            
            if (copy_to_user((
                    void __user *)arg,
                    &fifo_length,
                    sizeof(fifo_length)))
            {
                return  -EFAULT;
            }

            pr_info("Returned FIFO length: %zu\n", fifo_length);

            return 0;


        case AHMET_GET_BUFFER_CAPACITY:
          
        mutex_lock(&dev->ahmet_mutex);

        fifo_capacity =kfifo_size(&dev->fifo);

        mutex_unlock(&dev->ahmet_mutex);

            if (copy_to_user((void __user *)arg,
                            &fifo_capacity,
                            sizeof(fifo_capacity)))
            {
                return -EFAULT;
            }

            pr_info("Returned FIFO capacity\n");
            
            return 0;

        case AHMET_FILL_BUFFER:
        {
            
            char fill_char;
            unsigned int i;
            unsigned int inserted;
            
            if(copy_from_user(&fill_char,
                                (void __user *)arg,
                                sizeof(char)))
            {
                return -EFAULT;
            }

            mutex_lock(&dev->ahmet_mutex);
            
            kfifo_reset(&dev->fifo);
            
            for (i= 0; i < kfifo_size(&dev->fifo); i++)
            {
                inserted = kfifo_in(&dev->fifo,
                                    &fill_char,
                                    1);

                if(inserted != 1)
                    break;
            }

            mutex_unlock(&dev->ahmet_mutex);

            wake_up_interruptible(&dev->read_queue);

            if(dev->async_queue)
            {
                kill_fasync(&dev->async_queue,
                            SIGIO,
                            POLL_IN);
            }

            pr_info("Buffer filled with '%c'\n", fill_char);

            return 0;
        }

        case AHMET_GET_DEVICE_ID:
            if(copy_to_user((void __user *)arg,
                            &dev->device_id,
                            sizeof(int)))
            {
                return -EFAULT;
            }

            pr_info("Returned device id: %d\n",
                    dev->device_id);
        
        return 0;

        default:
            return -EINVAL;
    }

}


static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = ahmet_open,
    .release = ahmet_release,
    .read = ahmet_read,
    .write = ahmet_write,
    .unlocked_ioctl = ahmet_ioctl,
    .poll = ahmet_poll,
    .fasync = ahmet_fasync,
};

static int __init ahmet_init(void)
{
    int ret;
    int i;

    for(i=0;i < DEVICE_COUNT;i++)
    {
        ahmet_devices[i].device_id = i;
        mutex_init(&ahmet_devices[i].ahmet_mutex);

        //spin_lock_init(&ahmet_devices[i].stats_lock);

        sema_init(&ahmet_devices[i].open_sem, 1);

        atomic_set(&ahmet_devices[i].produced_events, 0);
        atomic_set(&ahmet_devices[i].read_events, 0);
        atomic_set(&ahmet_devices[i].dropped_events, 0);

        atomic_set(&ahmet_devices[i].timer_callbacks, 0);
        ahmet_devices[i].timer_enabled = true;
        
        init_waitqueue_head(&ahmet_devices[i].read_queue);
        init_waitqueue_head(&ahmet_devices[i].write_queue);

        INIT_WORK(&ahmet_devices[i].timer_work,
          ahmet_timer_work);

        init_completion(&ahmet_devices[i].test_completion);

        INIT_WORK(&ahmet_devices[i].completion_work,
                    ahmet_completion_work);

        timer_setup(&ahmet_devices[i].timer,
            ahmet_timer_callback,
            0);

        ret = kfifo_alloc(&ahmet_devices[i].fifo,
                            BUFFER_SIZE,
                            GFP_KERNEL);

        if(ret)
        {
            pr_err("Failed to allocate FIFO for device %d\n", i);

            mutex_destroy(&ahmet_devices[i].ahmet_mutex);

            goto free_device_buffers;
        }

        ahmet_devices[i].thread =
            kthread_run(ahmet_thread_function,
                &ahmet_devices[i],
                "ahmet_fifo_thread_%d",
                i);

        if (IS_ERR(ahmet_devices[i].thread))
        {
            ret = PTR_ERR(
                ahmet_devices[i].thread);

            ahmet_devices[i].thread = NULL;

            pr_err("Failed to create kthread for device %d: %d\n",
                i,
                ret);

            kfifo_free(
                &ahmet_devices[i].fifo);

            mutex_destroy(
                &ahmet_devices[i].ahmet_mutex);

            goto free_device_buffers;
        }

        mod_timer(&ahmet_devices[i].timer,
          jiffies + msecs_to_jiffies(1000));

    }

    ret = alloc_chrdev_region(&dev_num, 0, DEVICE_COUNT, "ahmet");
    if (ret < 0)
    {
        pr_err("Failed to allocate device number\n");
        goto free_device_buffers;
    }

    pr_info("Major: %d Minor: %d\n",
            MAJOR(dev_num),
            MINOR(dev_num));


    for(i=0; i< DEVICE_COUNT; i++)
    {
        dev_t current_dev_num;

        current_dev_num = MKDEV(MAJOR(dev_num),
                                MINOR(dev_num)+ i);
        
        cdev_init(&ahmet_devices[i].cdev, &fops);

        ret= cdev_add(&ahmet_devices[i].cdev,
                                        current_dev_num,
                                        1);

        if (ret<0)
        {
            pr_err("Failed to add cdev for device %d\n", i);
            goto unregister_cdevs;
        }
    }

    ahmet_class = class_create("ahmet_fifo_class");
    if (IS_ERR(ahmet_class))
    {
        ret = PTR_ERR(ahmet_class);
        goto unregister_cdevs;
    }

    for(i = 0;i < DEVICE_COUNT; i++)
    {
        dev_t current_dev_num;

        current_dev_num = MKDEV(MAJOR(dev_num),
                                MINOR(dev_num) + i);
        
        ahmet_device[i] = device_create(
        ahmet_class,
        NULL,
        current_dev_num,
        &ahmet_devices[i],
        "ahmet_fifo%d",
        i
    );

    if (IS_ERR(ahmet_device[i]))
    {
        ret = PTR_ERR(ahmet_device[i]);
        ahmet_device[i] = NULL;

        pr_err("Failed to create device %d\n", i);
        goto destroy_devices;
    }

    ret = sysfs_create_group(
        &ahmet_device[i]->kobj,
        &ahmet_attr_group
    );

    if (ret)
    {
        pr_err("Failed to create sysfs group for device %d\n",
                i);

        device_destroy(ahmet_class,
                        current_dev_num);

        ahmet_device[i] = NULL;

        goto destroy_devices;
    }

}

    ahmet_debugfs_root =
        debugfs_create_dir("ahmet_fifo", NULL);

        if (IS_ERR(ahmet_debugfs_root))
    {
        ret = PTR_ERR(ahmet_debugfs_root);
        ahmet_debugfs_root = NULL;

        pr_err("Failed to create debugfs directory\n");

        goto destroy_devices;
    }

    for (i = 0; i < DEVICE_COUNT; i++)
    {
        char name[32];

        snprintf(name,
                sizeof(name),
                "device%d_stats",
                i);

        debugfs_create_file(
            name,
            0444,
            ahmet_debugfs_root,
            &ahmet_devices[i],
            &ahmet_debugfs_fops
        );
    }

    ahmet_proc_entry =
    proc_create("ahmet_fifo_stats",
                0444,
                NULL,
                &ahmet_proc_ops);

    if (!ahmet_proc_entry)
    {
        pr_err("Failed to create proc entry\n");

        debugfs_remove_recursive(ahmet_debugfs_root);
        ahmet_debugfs_root = NULL;

        ret = -ENOMEM;

        goto destroy_devices;
    }

    pr_info("Ahmet character device added\n");

    return 0;


    destroy_devices:
        while (--i >=0)
        {
            dev_t current_dev_num;

            current_dev_num = MKDEV(MAJOR(dev_num),
                                    MINOR(dev_num) + i);

            if (ahmet_device[i])
            {
                sysfs_remove_group(
                    &ahmet_device[i]->kobj,
                    &ahmet_attr_group
                );

                device_destroy(ahmet_class,
                            current_dev_num);

                ahmet_device[i] = NULL;
            }
        }

        i = DEVICE_COUNT;



        class_destroy(ahmet_class);


    unregister_cdevs:
        while(--i >=0)
            cdev_del(&ahmet_devices[i].cdev);


        unregister_chrdev_region(dev_num, DEVICE_COUNT);
        i= DEVICE_COUNT;

    free_device_buffers:
    while (--i >= 0)
    {
        timer_shutdown_sync(&ahmet_devices[i].timer);

        cancel_work_sync(&ahmet_devices[i].timer_work);

        if (ahmet_devices[i].thread)
        {
            kthread_stop(ahmet_devices[i].thread);
            ahmet_devices[i].thread = NULL;
        }

        cancel_work_sync(&ahmet_devices[i].completion_work);

        kfifo_free(&ahmet_devices[i].fifo);

        mutex_destroy(&ahmet_devices[i].ahmet_mutex);
    }

    return ret;
}

static void ahmet_cleanup(void)
{
    int i;

        if (ahmet_proc_entry)
    {
        proc_remove(ahmet_proc_entry);
        ahmet_proc_entry = NULL;
    }

    debugfs_remove_recursive(ahmet_debugfs_root);
    ahmet_debugfs_root = NULL;

    for (i = 0; i < DEVICE_COUNT; i++)
    {
        dev_t current_dev_num;

        current_dev_num =
            MKDEV(MAJOR(dev_num),
                  MINOR(dev_num) + i);

        if (ahmet_device[i])
        {
            sysfs_remove_group(
                &ahmet_device[i]->kobj,
                &ahmet_attr_group
            );

            device_destroy(
                ahmet_class,
                current_dev_num
            );

            ahmet_device[i] = NULL;
        }
    }
    

    if (ahmet_class)
    {
        class_destroy(ahmet_class);
        ahmet_class = NULL;
    }

    /*
     * Character device kayıtlarını kaldır.
     */
    for (i = 0; i < DEVICE_COUNT; i++)
    {
        cdev_del(&ahmet_devices[i].cdev);
    }

    unregister_chrdev_region(dev_num,
                             DEVICE_COUNT);

    /*
     * Aktif kernel işlerini durdur,
     * ardından belleği yalnızca bir kez temizle.
     */
    for (i = 0; i < DEVICE_COUNT; i++)
    {
        timer_shutdown_sync(
            &ahmet_devices[i].timer);

        cancel_work_sync(
            &ahmet_devices[i].timer_work);

        if (ahmet_devices[i].thread)
        {
            kthread_stop(
                ahmet_devices[i].thread);

            ahmet_devices[i].thread = NULL;
        }

        cancel_work_sync(
            &ahmet_devices[i].completion_work);

        kfifo_free(
            &ahmet_devices[i].fifo);

        mutex_destroy(
            &ahmet_devices[i].ahmet_mutex);
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
