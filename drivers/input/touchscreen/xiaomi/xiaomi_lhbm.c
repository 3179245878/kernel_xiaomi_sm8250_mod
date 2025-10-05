// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi LHBM Control Driver
 * 
 * Copyright (c) 2024 Xiaomi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define NYAKO_CLASS_NAME "nyako"
#define LHBM_DEVICE_NAME "lhbm"
#define HBM_ATTR_NAME "hbm"

static struct class *nyako_class;
static struct device *lhbm_device;
static bool lhbm_enabled = false;
static DEFINE_MUTEX(lhbm_mutex);

static int call_mi_disp_function(u32 fod_btn, bool from_touch)
{
    int (*func)(u32, bool) = NULL;
    int ret = -ENODEV;

    func = (void *)__symbol_get("mi_disp_set_fod_queue_work");
    if (func) {
        pr_info("nyako_lhbm: Successfully got mi_disp_set_fod_queue_work symbol\n");
        ret = func(fod_btn, from_touch);
        __symbol_put("mi_disp_set_fod_queue_work");
    } else {
        pr_err("nyako_lhbm: Failed to get mi_disp_set_fod_queue_work symbol\n");
    }
    
    return ret;
}

static ssize_t hbm_show(struct device *dev,
                       struct device_attribute *attr,
                       char *buf)
{
    ssize_t ret;
    
    mutex_lock(&lhbm_mutex);
    ret = snprintf(buf, PAGE_SIZE, "%d\n", lhbm_enabled ? 1 : 0);
    mutex_unlock(&lhbm_mutex);
    
    pr_info("nyako_lhbm: hbm_show called, returning: %d\n", lhbm_enabled ? 1 : 0);
    
    return ret;
}

static ssize_t hbm_store(struct device *dev,
                        struct device_attribute *attr,
                        const char *buf, size_t count)
{
    unsigned int input;
    int ret;
    
    pr_info("nyako_lhbm: hbm_store called with data: %.*s\n", (int)count, buf);
    
    ret = kstrtouint(buf, 10, &input);
    if (ret < 0) {
        pr_err("nyako_lhbm: Invalid input format\n");
        return ret;
    }
    
    mutex_lock(&lhbm_mutex);
    
    if (input == 1) {
        if (!lhbm_enabled) {
            pr_info("nyako_lhbm: Enabling LHBM\n");
            ret = call_mi_disp_function(1, false);
            pr_info("nyako_lhbm: call_mi_disp_function returned: %d\n", ret);
            
            if (ret == 0) {
                lhbm_enabled = true;
                pr_info("nyako_lhbm: LHBM enabled successfully\n");
            } else {
                pr_err("nyako_lhbm: Failed to enable LHBM, error: %d\n", ret);
            }
        } else {
            pr_info("nyako_lhbm: LHBM is already enabled\n");
        }
    } else if (input == 0) {
        if (lhbm_enabled) {
            pr_info("nyako_lhbm: Disabling LHBM\n");

            ret = call_mi_disp_function(0, false);
            pr_info("nyako_lhbm: call_mi_disp_function returned: %d\n", ret);
            
            if (ret == 0) {
                lhbm_enabled = false;
                pr_info("nyako_lhbm: LHBM disabled successfully\n");
            } else {
                pr_err("nyako_lhbm: Failed to disable LHBM, error: %d\n", ret);
            }
        } else {
            pr_info("nyako_lhbm: LHBM is already disabled\n");
        }
    } else {
        pr_warn("nyako_lhbm: Invalid input %u, only 0 or 1 allowed\n", input);
        mutex_unlock(&lhbm_mutex);
        return -EINVAL;
    }
    
    mutex_unlock(&lhbm_mutex);
    
    return count;
}

static DEVICE_ATTR_RW(hbm);

static int __init nyako_lhbm_init(void)
{
    int ret;
    
    pr_info("nyako_lhbm: Initializing driver\n");

    pr_info("nyako_lhbm: Testing symbol acquisition...\n");
    if (__symbol_get("mi_disp_set_fod_queue_work")) {
        pr_info("nyako_lhbm: mi_disp_set_fod_queue_work symbol is available\n");
        __symbol_put("mi_disp_set_fod_queue_work");
    } else {
        pr_err("nyako_lhbm: mi_disp_set_fod_queue_work symbol is NOT available\n");
    }

    nyako_class = class_create(THIS_MODULE, NYAKO_CLASS_NAME);
    if (IS_ERR(nyako_class)) {
        ret = PTR_ERR(nyako_class);
        pr_err("nyako_lhbm: Failed to create class: %d\n", ret);
        return ret;
    }

    lhbm_device = device_create(nyako_class, NULL, 0, NULL, LHBM_DEVICE_NAME);
    if (IS_ERR(lhbm_device)) {
        ret = PTR_ERR(lhbm_device);
        pr_err("nyako_lhbm: Failed to create device: %d\n", ret);
        goto device_err;
    }

    ret = device_create_file(lhbm_device, &dev_attr_hbm);
    if (ret) {
        pr_err("nyako_lhbm: Failed to create attribute: %d\n", ret);
        goto attr_err;
    }
    
    pr_info("nyako_lhbm: Driver initialized successfully\n");
    pr_info("nyako_lhbm: Control node at /sys/class/nyako/lhbm/hbm\n");
    
    return 0;

attr_err:
    device_destroy(nyako_class, 0);
device_err:
    class_destroy(nyako_class);
    return ret;
}

static void __exit nyako_lhbm_exit(void)
{
    if (lhbm_device) {
        device_remove_file(lhbm_device, &dev_attr_hbm);
        device_destroy(nyako_class, 0);
    }
    if (nyako_class) {
        class_destroy(nyako_class);
    }
    
    pr_info("nyako_lhbm: Driver unloaded\n");
}

module_init(nyako_lhbm_init);
module_exit(nyako_lhbm_exit);

MODULE_DESCRIPTION("Nyako LHBM Control Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nyako");

