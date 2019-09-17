#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>

#define MODULE_NAME "mypipe"
#define BUFFER_SIZE ((size_t)100)


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Xu");
MODULE_DESCRIPTION("Simple pipe implementation");

static struct class *cls;
static dev_t mypipe_devs[2];
static struct cdev mypipe_cdevs[2];
static bool occupied[2];
static struct mutex mutex_occupied;

static char *buffer;
static size_t head, tail;
static struct mutex mutex_buffer;
static struct semaphore sem_empty, sem_full;

static int dev_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO MODULE_NAME":attempting to open %u\n", MINOR(inode->i_cdev->dev));
    mutex_lock_killable(&mutex_occupied);
    if (occupied[MINOR(inode->i_cdev->dev)]) {
        // 设备已被占用
        mutex_unlock(&mutex_occupied);
        return -EMFILE;
    } else {
        occupied[MINOR(inode->i_cdev->dev)] = true;
        if (occupied[0] && occupied[1]) {
            mutex_unlock(&mutex_occupied);
            // 读者、写着都准备好，可以开始
            up(&sem_empty);
        } else {
            mutex_unlock(&mutex_occupied);
            // 等待另一方
            down_killable(&sem_empty);
        }
        printk(KERN_INFO MODULE_NAME":opened %u\n", MINOR(inode->i_cdev->dev));
        return 0;
    }
}

static int dev_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO MODULE_NAME":close %u\n", MINOR(inode->i_cdev->dev));
    mutex_lock_killable(&mutex_occupied);
    occupied[MINOR(inode->i_cdev->dev)] = false;
    if (!occupied[0] && !occupied[1]) {
        // 清空信号量、缓存
        sema_init(&sem_empty, 0);
        sema_init(&sem_full, 0);
        head = tail = 0;
    }
    mutex_unlock(&mutex_occupied);
    return 0;
}

static ssize_t dev_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
    ssize_t res;
    size_t cnt;
    size_t i;
    printk(KERN_INFO MODULE_NAME":attempting to read %zu\n", count);

    mutex_lock_killable(&mutex_buffer);
    while (head == tail) {
        mutex_lock_killable(&mutex_occupied);
        if (!occupied[0]) {
            mutex_unlock(&mutex_occupied);
            // 写者结束，终止读者
            printk(KERN_INFO MODULE_NAME":write closed %zu,%zu\n", head, tail);
            res = -EPIPE;
            goto fail;
        } else {
            mutex_unlock(&mutex_occupied);
            // 缓存空，等待写者
            printk(KERN_INFO MODULE_NAME":read empty %zu,%zu\n", head, tail);
            mutex_unlock(&mutex_buffer);
            up(&sem_full);
            down_killable(&sem_empty);
            mutex_lock_killable(&mutex_buffer);
        }
    }

    cnt = min(count, (BUFFER_SIZE + tail - head) % BUFFER_SIZE); // 计算实际读取字节，防止溢出
    printk(KERN_INFO MODULE_NAME":actually read %zu\n", cnt);
    for (i = 0; i < cnt; ++i) {
        res = put_user(buffer[(head + i) % BUFFER_SIZE], buf + i);
        if (res) {
            goto fail;
        }
    }

    if ((tail + 1) % BUFFER_SIZE == head) {
        // 缓存不满，写者可以继续
        up(&sem_full);
    }
    // 更新缓存头尾
    head = (head + cnt) % BUFFER_SIZE;
    printk(KERN_INFO MODULE_NAME":read %zu,%zu\n", head, tail);
    res = cnt;

    fail:
    mutex_unlock(&mutex_buffer);
    return res;
}

static ssize_t dev_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
    ssize_t res;
    size_t cnt;
    size_t i;
    printk(KERN_INFO MODULE_NAME":attempting to write %zu\n", count);

    mutex_lock_killable(&mutex_buffer);
    while ((tail + 1) % BUFFER_SIZE == head) {
        if (!occupied[1]) {
            mutex_unlock(&mutex_occupied);
            // 读者结束，终止写者
            printk(KERN_INFO MODULE_NAME":read closed %zu,%zu\n", head, tail);
            res = -EPIPE;
            goto fail;
        } else {
            mutex_unlock(&mutex_occupied);
            // 缓存满，等待读者
            printk(KERN_INFO MODULE_NAME":full %zu,%zu\n", head, tail);
            mutex_unlock(&mutex_buffer);
            up(&sem_empty);
            down_killable(&sem_full);
            mutex_lock_killable(&mutex_buffer);
        }
    }

    cnt = min(count, (BUFFER_SIZE + head - tail - 1) % BUFFER_SIZE); // 计算实际读取字节，防止溢出
    printk(KERN_INFO MODULE_NAME":actually write %zu\n", cnt);
    for (i = 0; i < cnt; ++i) {
        res = get_user(buffer[(tail + i) % BUFFER_SIZE], buf + i);
        if (res) {
            goto fail;
        }
    }

    if (head == tail) {
        // 缓存不空，读者可以继续
        up(&sem_empty);
    }
    // 更新缓存头尾
    tail = (tail + cnt) % BUFFER_SIZE;
    printk(KERN_INFO MODULE_NAME":write %zu,%zu\n", head, tail);
    res = cnt;

    fail:
    mutex_unlock(&mutex_buffer);
    return res;
}

static struct file_operations fops[] = {
        {
                .owner=THIS_MODULE,
                .open=dev_open,
                .release=dev_release,
                .write= dev_write,
        },
        {
                .owner=THIS_MODULE,
                .open=dev_open,
                .release=dev_release,
                .read= dev_read,
        }
};

static char *mypipe_devnode(struct device *dev, umode_t *mode) {
    if (mode) {
        *mode = 0666;
    }
    return kasprintf(GFP_KERNEL, MODULE_NAME"/%s", dev_name(dev));
}

static void mypipe_exit(void) {
    size_t i;
    for (i = 0; i < 2; ++i) {
        device_destroy(cls, mypipe_devs[i]);
        cdev_del(&mypipe_cdevs[i]);
    }

    class_destroy(cls);
    unregister_chrdev_region(mypipe_devs[0], 2);

    kfree(buffer);

    mutex_destroy(&mutex_buffer);
    mutex_destroy(&mutex_occupied);

    printk(KERN_INFO MODULE_NAME ":removed module\n");
}

static int mypipe_init(void) {
    int res;
    struct device *device;
    size_t i;
    char *dev_names[] = {MODULE_NAME"_in", MODULE_NAME"_out"};

    // 获得主设备号
    res = alloc_chrdev_region(&mypipe_devs[0], 0, 2, MODULE_NAME);
    if (res) {
        printk(KERN_ERR MODULE_NAME":alloc_chrdev_region error %d", res);
        goto fail;
    } else {
        printk(KERN_INFO MODULE_NAME":major number %d", MAJOR(mypipe_devs[0]));
    }
    mypipe_devs[1] = MKDEV(MAJOR(mypipe_devs[0]), 1);

    // 创建设备类
    cls = class_create(THIS_MODULE, MODULE_NAME);
    if (IS_ERR(cls)) {
        res = (int) PTR_ERR(cls);
        printk(KERN_ERR MODULE_NAME":class_create error %d", res);
        goto fail;
    }
    cls->devnode = mypipe_devnode;

    // 建立两个字符设备
    for (i = 0; i < 2; ++i) {
        cdev_init(&mypipe_cdevs[i], &fops[i]);
        res = cdev_add(&mypipe_cdevs[i], mypipe_devs[i], 1);
        if (res) {
            printk(KERN_ERR MODULE_NAME":cdev_add %zu error %d", i, res);
            goto fail;
        }
        device = device_create(cls, NULL, mypipe_devs[i], NULL, dev_names[i]);
        if (IS_ERR(device)) {
            res = (int) PTR_ERR(device);
            printk(KERN_ERR MODULE_NAME":device_create %zu error %d", i, res);
            goto fail;
        }
    }

    buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!buffer) {
        printk(KERN_ERR MODULE_NAME":kmalloc error\n");
        res = -ENOMEM;
        goto fail;
    }

    mutex_init(&mutex_buffer);
    mutex_init(&mutex_occupied);

    sema_init(&sem_empty, 0);
    sema_init(&sem_full, 0);

    printk(KERN_INFO MODULE_NAME":inserted module\n");
    return 0;

    fail:
    mypipe_exit();
    return res;
}

module_init(mypipe_init);

module_exit(mypipe_exit);