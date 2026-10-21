/*
 * smartcar_hw.c —— 车载系统外设内核驱动（DHT11 / HC-SR04 / 蜂鸣器）
 *
 * 用户态 GPIO 翻转受调度影响，DHT11 这种 us 级单总线时序容易偶发失败。
 * 这个模块在内核里做位操作，稳定性更好，并且仍然用 /dev 节点与应用程序对接：
 *
 *   /dev/mydht11   read() 返回 4 字节：[湿度整数, 湿度小数, 温度整数, 温度小数]
 *   /dev/mysr04    read() 返回 4 字节：int 回波高电平时间(us)，距离(cm) = us / 58
 *   /dev/mybuzzer  write() 2 字节，第 2 个字节非 0 表示响（按 buzzer_active_low 换算电平）
 *
 * 编译（使用 SDK 的内核源码）：
 *   make -C ~/atk_dlrk3506_linux6.1_release_v1.3.1_20260326/kernel \
 *        M=$PWD ARCH=arm \
 *        CROSS_COMPILE=~/atk_dlrk3506_linux6.1_release_v1.3.1_20260326/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf- \
 *        modules
 *
 * 加载：
 *   insmod smartcar_hw.ko dht11_pin=51 sr04_trig_pin=54 sr04_echo_pin=55 buzzer_pin=56
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/version.h>

#define DEV_NAME "smartcar_hw"

static int dht11_pin = 52;
static int sr04_trig_pin = 54;
static int sr04_echo_pin = 55;
static int buzzer_pin = 56;
static int buzzer_active_low = 1;
module_param(dht11_pin, int, 0444);
module_param(sr04_trig_pin, int, 0444);
module_param(sr04_echo_pin, int, 0444);
module_param(buzzer_pin, int, 0444);
module_param(buzzer_active_low, int, 0444);
MODULE_PARM_DESC(dht11_pin, "DHT11 data GPIO (default 52 = GPIO1_C4; 51 was UART4_RX)");
MODULE_PARM_DESC(buzzer_active_low, "1 = 低电平触发蜂鸣器(默认), 0 = 高电平触发");

static dev_t devno;
static struct class *cls;
static struct cdev dht11_cdev, sr04_cdev, buzzer_cdev;
static struct device *dht11_dev, *sr04_dev, *buzzer_dev;

static struct gpio_desc *dht11_gpio;
static struct gpio_desc *trig_gpio;
static struct gpio_desc *echo_gpio;
static struct gpio_desc *buzzer_gpio;

static DEFINE_MUTEX(dht11_lock);

/* ---------------------------------------------------------------- DHT11 */

static inline int dht_level(void)
{
    return gpiod_get_value(dht11_gpio) ? 1 : 0;
}

/* 等待电平变化，返回耗时(ns)，超时返回 -1 */
static long long dht_wait(int level, long long timeout_ns)
{
    ktime_t start = ktime_get();
    while (ktime_to_ns(ktime_sub(ktime_get(), start)) < timeout_ns) {
        if (dht_level() == level)
            return ktime_to_ns(ktime_sub(ktime_get(), start));
    }
    return -1;
}

static int dht11_do_read(unsigned char *out)
{
    unsigned char data[5] = {0};
    int i, bit;
    long long t;

    /* 起始信号：拉低 20ms */
    gpiod_direction_output(dht11_gpio, 0);
    msleep(20);
    gpiod_direction_input(dht11_gpio);

    /* 从机响应：80us 低 + 80us 高 */
    if (dht_wait(0, 1000 * 1000) < 0)
        return -1;
    if (dht_wait(1, 1000 * 1000) < 0)
        return -2;
    if (dht_wait(0, 1000 * 1000) < 0)
        return -3;

    for (i = 0; i < 40; i++) {
        long long high;
        if (dht_wait(1, 1000 * 1000) < 0)
            return -4;
        high = dht_wait(0, 1000 * 1000);
        if (high < 0)
            return -5;
        bit = (high > 45000) ? 1 : 0; /* >45us 为 1 */
        data[i / 8] = (data[i / 8] << 1) | bit;
    }
    t = data[0] + data[1] + data[2] + data[3];
    if ((unsigned char)t != data[4])
        return -6;

    out[0] = data[0];
    out[1] = data[1];
    out[2] = data[2];
    out[3] = data[3];
    return 0;
}

static ssize_t dht11_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    unsigned char data[4];
    int ret;

    if (len < 4)
        return -EINVAL;
    if (mutex_lock_interruptible(&dht11_lock))
        return -ERESTARTSYS;

    ret = dht11_do_read(data);
    if (ret == 0) {
        if (copy_to_user(buf, data, 4))
            ret = -EFAULT;
        else
            ret = 4;
    } else {
        ret = -EIO;
    }
    mutex_unlock(&dht11_lock);
    return ret;
}

static const struct file_operations dht11_fops = {
    .owner = THIS_MODULE,
    .read = dht11_read,
};

/* ---------------------------------------------------------------- SR04 */

static ssize_t sr04_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    ktime_t start;
    long long us;
    int val = 0;

    if (len < sizeof(int))
        return -EINVAL;

    gpiod_set_value(trig_gpio, 0);
    udelay(3);
    gpiod_set_value(trig_gpio, 1);
    udelay(12);
    gpiod_set_value(trig_gpio, 0);

    start = ktime_get();
    while (gpiod_get_value(echo_gpio) == 0) {
        if (ktime_to_us(ktime_sub(ktime_get(), start)) > 60000)
            return -EIO;
    }
    start = ktime_get();
    while (gpiod_get_value(echo_gpio) == 1) {
        if (ktime_to_us(ktime_sub(ktime_get(), start)) > 60000)
            return -EIO;
    }
    us = ktime_to_us(ktime_sub(ktime_get(), start));
    if (us <= 0)
        return -EIO;
    val = (int)us;

    if (copy_to_user(buf, &val, sizeof(int)))
        return -EFAULT;
    return sizeof(int);
}

static const struct file_operations sr04_fops = {
    .owner = THIS_MODULE,
    .read = sr04_read,
};

/* ---------------------------------------------------------------- 蜂鸣器 */

static ssize_t buzzer_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    unsigned char tmp[2] = {0, 0};
    if (len < 2)
        return -EINVAL;
    if (copy_from_user(tmp, buf, 2))
        return -EFAULT;
    /*
     * 应用语义：第 2 个字节 = 1 表示“报警/响”，0 表示静音。
     * 引脚实际电平按触发性别换算。
     */
    const int on = tmp[1] ? 1 : 0;
    const int level = buzzer_active_low ? (on ? 0 : 1) : (on ? 1 : 0);
    gpiod_set_value(buzzer_gpio, level);
    return len;
}

static const struct file_operations buzzer_fops = {
    .owner = THIS_MODULE,
    .write = buzzer_write,
};

/* ---------------------------------------------------------------- 初始化 */

static struct gpio_desc *get_gpio(int pin, const char *name)
{
    struct gpio_desc *d = gpio_to_desc(pin);
    if (!d)
        pr_err(DEV_NAME ": gpio%d (%s) 不存在\n", pin, name);
    return d;
}

static int create_node(const char *name, const struct file_operations *fops,
                       struct cdev *cdev, struct device **dev, int minor)
{
    int ret;
    cdev_init(cdev, fops);
    cdev->owner = THIS_MODULE;
    ret = cdev_add(cdev, MKDEV(MAJOR(devno), minor), 1);
    if (ret)
        return ret;
    *dev = device_create(cls, NULL, MKDEV(MAJOR(devno), minor), NULL, name);
    if (IS_ERR(*dev)) {
        cdev_del(cdev);
        return PTR_ERR(*dev);
    }
    return 0;
}

static int __init smartcar_hw_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&devno, 0, 3, DEV_NAME);
    if (ret)
        return ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    cls = class_create(DEV_NAME);
#else
    cls = class_create(THIS_MODULE, DEV_NAME);
#endif
    if (IS_ERR(cls)) {
        unregister_chrdev_region(devno, 3);
        return PTR_ERR(cls);
    }

    dht11_gpio = get_gpio(dht11_pin, "dht11");
    if (!dht11_gpio) {
        ret = -ENODEV;
        goto err;
    }
    trig_gpio = get_gpio(sr04_trig_pin, "sr04-trig");
    echo_gpio = get_gpio(sr04_echo_pin, "sr04-echo");
    buzzer_gpio = get_gpio(buzzer_pin, "buzzer");
    if (!trig_gpio || !echo_gpio || !buzzer_gpio) {
        ret = -ENODEV;
        goto err;
    }
    gpiod_direction_output(trig_gpio, 0);
    gpiod_direction_input(echo_gpio);
    /*
     * 蜂鸣器上电静音：常见有源蜂鸣器模块是“低电平触发”，
     * 所以默认把引脚驱动为高电平（不响），避免 insmod 一加载就叫。
     * 若你的模块是高电平触发，insmod 时加 buzzer_active_low=0。
     */
    gpiod_direction_output(buzzer_gpio, buzzer_active_low ? 1 : 0);

    /* DHT11 数据线：输入（外部需 4.7k~10k 上拉到 3.3V） */
    gpiod_direction_input(dht11_gpio);

    ret = create_node("mydht11", &dht11_fops, &dht11_cdev, &dht11_dev, 0);
    if (ret)
        goto err;
    ret = create_node("mysr04", &sr04_fops, &sr04_cdev, &sr04_dev, 1);
    if (ret)
        goto err;
    ret = create_node("mybuzzer", &buzzer_fops, &buzzer_cdev, &buzzer_dev, 2);
    if (ret)
        goto err;

    pr_info(DEV_NAME ": 已加载 dht11=gpio%d sr04=%d/%d buzzer=gpio%d\n",
            dht11_pin, sr04_trig_pin, sr04_echo_pin, buzzer_pin);
    return 0;

err:
    pr_err(DEV_NAME ": 初始化失败 %d\n", ret);
    if (cls && !IS_ERR(cls))
        class_destroy(cls);
    unregister_chrdev_region(devno, 3);
    return ret;
}

static void __exit smartcar_hw_exit(void)
{
    device_destroy(cls, MKDEV(MAJOR(devno), 0));
    device_destroy(cls, MKDEV(MAJOR(devno), 1));
    device_destroy(cls, MKDEV(MAJOR(devno), 2));
    cdev_del(&dht11_cdev);
    cdev_del(&sr04_cdev);
    cdev_del(&buzzer_cdev);
    class_destroy(cls);
    unregister_chrdev_region(devno, 3);
    pr_info(DEV_NAME ": 已卸载\n");
}

module_init(smartcar_hw_init);
module_exit(smartcar_hw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Smart_car port");
MODULE_DESCRIPTION("ATK-DLRK3506B smart car peripherals (DHT11 / HC-SR04 / buzzer)");
