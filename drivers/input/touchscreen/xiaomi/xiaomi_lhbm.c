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
#include <linux/kallsyms.h>
#include <linux/delay.h>

#define NYAKO_CLASS_NAME "nyako"
#define LHBM_DEVICE_NAME "lhbm"
#define HBM_ATTR_NAME "hbm"

static struct class *nyako_class;
static struct device *lhbm_device;
static bool lhbm_enabled = false;
static DEFINE_MUTEX(lhbm_mutex);

// Alternative function names that might be used
static int call_mi_disp_function(u32 fod_btn, bool from_touch)
{
    int (*func)(u32, bool) = NULL;
    int ret = -ENODEV;
    char *possible_symbols[] = {
        "mi_disp_set_fod_queue_work",
        "mdss_mi_disp_set_fod_queue_work",
        "mi_disp_set_fod_work",
        "mdss_mi_disp_set_fod_work",
        NULL
    };
    int i = 0;

    // Try multiple possible symbol names
    while (possible_symbols[i]) {
        func = (void *)kallsyms_lookup_name(possible_symbols[i]);
        if (func) {
            pr_info("nyako_lhbm: Found symbol '%s' at %p\n", possible_symbols[i], func);
            ret = func(fod_btn, from_touch);
            pr_info("nyako_lhbm: Function call returned: %d\n", ret);
            break;
        } else {
            pr_info("nyako_lhbm: Symbol '%s' not found\n", possible_symbols[i]);
        }
        i++;
    }

    if (!func) {
        // Fallback to __symbol_get method
        func = (void *)__symbol_get("mi_disp_set_fod_queue_work");
        if (func) {
            pr_info("nyako_lhbm: Found symbol via __symbol_get\n");
            ret = func(fod_btn, from_touch);
            __symbol_put("mi_disp_set_fod_queue_work");
        } else {
            pr_err("nyako_lhbm: All symbol lookup methods failed\n");
        }
    }
    
    return ret;
}

// Additional function to check display state
static void check_display_state(void)
{
    void *state_func;
    
    pr_info("nyako_lhbm: Checking display state functions...\n");
    
    // Check for common display state functions
    state_func = (void *)kallsyms_lookup_name("mdss_prim_get_power_state");
    if (state_func) {
        pr_info("nyako_lhbm: Found mdss_prim_get_power_state at %p\n", state_func);
    }
    
    state_func = (void *)kallsyms_lookup_name("mdss_fb_get_backlight");
    if (state_func) {
        pr_info("nyako_lhbm: Found mdss_fb_get_backlight at %p\n", state_func);
    }
    
    state_func = (void *)kallsyms_lookup_name("mdss_mdp_get_panel_info");
    if (state_func) {
        pr_info("nyako_lhbm: Found mdss_mdp_get_panel_info at %p\n", state_func);
    }
}

// Try alternative approach with multiple calls
static int enhanced_lhbm_control(u32 fod_btn, bool from_touch)
{
    int ret;
    int i;
    
    pr_info("nyako_lhbm: Enhanced LHBM control called with fod_btn=%d, from_touch=%d\n", 
            fod_btn, from_touch);
    
    // Try multiple times with small delays
    for (i = 0; i < 3; i++) {
        ret = call_mi_disp_function(fod_btn, from_touch);
        pr_info("nyako_lhbm: Attempt %d, returned: %d\n", i + 1, ret);
        
        if (ret == 0) {
            pr_info("nyako_lhbm: Success on attempt %d\n", i + 1);
            break;
        }
        
        // Small delay between attempts
        if (i < 2) {
            msleep(10);
        }
    }
    
    // If enabling, also try with from_touch=true
    if (fod_btn == 1 && ret != 0) {
        pr_info("nyako_lhbm: Trying alternative with from_touch=true\n");
        ret = call_mi_disp_function(fod_btn, true);
        pr_info("nyako_lhbm: Alternative call returned: %d\n", ret);
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
            
            // Check display state first
            check_display_state();
            
            // Use enhanced control function
            ret = enhanced_lhbm_control(1, false);
            pr_info("nyako_lhbm: LHBM enable sequence completed, final result: %d\n", ret);
            
            if (ret == 0) {
                lhbm_enabled = true;
                pr_info("nyako_lhbm: LHBM enabled successfully\n");
            } else {
                pr_err("nyako_lhbm: Failed to enable LHBM, error: %d\n", ret);
                
                // Try one more time with different parameters
                pr_info("nyako_lhbm: Retrying with different parameters...\n");
                ret = enhanced_lhbm_control(1, true);
                if (ret == 0) {
                    lhbm_enabled = true;
                    pr_info("nyako_lhbm: LHBM enabled successfully on retry\n");
                }
            }
        } else {
            pr_info("nyako_lhbm: LHBM is already enabled\n");
        }
    } else if (input == 0) {
        if (lhbm_enabled) {
            pr_info("nyako_lhbm: Disabling LHBM\n");

            ret = enhanced_lhbm_control(0, false);
            pr_info("nyako_lhbm: LHBM disable sequence completed, result: %d\n", ret);
            
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

// Debug file to test symbol lookup
static ssize_t debug_show(struct device *dev,
                         struct device_attribute *attr,
                         char *buf)
{
    ssize_t count = 0;
    char *possible_symbols[] = {
        "mi_disp_set_fod_queue_work",
        "mdss_mi_disp_set_fod_queue_work",
        "mi_disp_set_fod_work",
        "mdss_mi_disp_set_fod_work",
        NULL
    };
    int i = 0;
    void *func;
    
    count += snprintf(buf + count, PAGE_SIZE - count, "Symbol lookup debug:\n");
    
    while (possible_symbols[i]) {
        func = (void *)kallsyms_lookup_name(possible_symbols[i]);
        if (func) {
            count += snprintf(buf + count, PAGE_SIZE - count, 
                            "✓ Found: %s at %p\n", possible_symbols[i], func);
        } else {
            count += snprintf(buf + count, PAGE_SIZE - count, 
                            "✗ Missing: %s\n", possible_symbols[i]);
        }
        i++;
    }
    
    return count;
}

static DEVICE_ATTR_RO(debug);

static int __init nyako_lhbm_init(void)
{
    int ret;
    void *symbol_addr;
    
    pr_info("nyako_lhbm: Initializing driver\n");

    // Test symbol acquisition with multiple names
    pr_info("nyako_lhbm: Testing symbol acquisition...\n");
    
    symbol_addr = (void *)kallsyms_lookup_name("mi_disp_set_fod_queue_work");
    pr_info("nyako_lhbm: mi_disp_set_fod_queue_work: %p\n", symbol_addr);
    
    if (!symbol_addr) {
        symbol_addr = (void *)kallsyms_lookup_name("mdss_mi_disp_set_fod_queue_work");
        pr_info("nyako_lhbm: mdss_mi_disp_set_fod_queue_work: %p\n", symbol_addr);
    }
    
    if (!symbol_addr) {
        symbol_addr = __symbol_get("mi_disp_set_fod_queue_work");
        pr_info("nyako_lhbm: __symbol_get result: %p\n", symbol_addr);
        if (symbol_addr) {
            __symbol_put("mi_disp_set_fod_queue_work");
        }
    }

    // Check display-related symbols
    check_display_state();

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
        pr_err("nyako_lhbm: Failed to create hbm attribute: %d\n", ret);
        goto attr_err;
    }

    ret = device_create_file(lhbm_device, &dev_attr_debug);
    if (ret) {
        pr_err("nyako_lhbm: Failed to create debug attribute: %d\n", ret);
        goto debug_attr_err;
    }
    
    pr_info("nyako_lhbm: Driver initialized successfully\n");
    pr_info("nyako_lhbm: Control node at /sys/class/nyako/lhbm/hbm\n");
    pr_info("nyako_lhbm: Debug node at /sys/class/nyako/lhbm/debug\n");
    
    return 0;

debug_attr_err:
    device_remove_file(lhbm_device, &dev_attr_hbm);
attr_err:
    device_destroy(nyako_class, 0);
device_err:
    class_destroy(nyako_class);
    return ret;
}

static void __exit nyako_lhbm_exit(void)
{
    if (lhbm_enabled) {
        pr_info("nyako_lhbm: Disabling LHBM during module exit\n");
        enhanced_lhbm_control(0, false);
    }
    
    if (lhbm_device) {
        device_remove_file(lhbm_device, &dev_attr_debug);
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
