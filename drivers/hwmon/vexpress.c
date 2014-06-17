/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#define DRVNAME "vexpress-hwmon"
#define pr_fmt(fmt) DRVNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/vexpress.h>

struct vexpress_hwmon_data {
	struct device *hwmon_dev;
	struct vexpress_config_func *func;
	const char *name;
};

static ssize_t vexpress_hwmon_name_show(struct device *dev,
		struct device_attribute *dev_attr, char *buffer)
{
	struct vexpress_hwmon_data *data = dev_get_drvdata(dev);

	return sprintf(buffer, "%s\n", data->name);
}

static ssize_t vexpress_hwmon_label_show(struct device *dev,
		struct device_attribute *dev_attr, char *buffer)
{
	const char *label = of_get_property(dev->of_node, "label", NULL);

	return snprintf(buffer, PAGE_SIZE, "%s\n", label);
}

static ssize_t vexpress_hwmon_u32_show(struct device *dev,
		struct device_attribute *dev_attr, char *buffer)
{
	struct vexpress_hwmon_data *data = dev_get_drvdata(dev);
	int err;
	u32 value;

	err = vexpress_config_read(data->func, 0, &value);
	if (err)
		return err;

	return snprintf(buffer, PAGE_SIZE, "%u\n", value /
			to_sensor_dev_attr(dev_attr)->index);
}

static ssize_t vexpress_hwmon_u64_show(struct device *dev,
		struct device_attribute *dev_attr, char *buffer)
{
	struct vexpress_hwmon_data *data = dev_get_drvdata(dev);
	int err;
	u32 value_hi, value_lo;

	err = vexpress_config_read(data->func, 0, &value_lo);
	if (err)
		return err;

	err = vexpress_config_read(data->func, 1, &value_hi);
	if (err)
		return err;

	return snprintf(buffer, PAGE_SIZE, "%llu\n",
			div_u64(((u64)value_hi << 32) | value_lo,
			to_sensor_dev_attr(dev_attr)->index));
}

static umode_t vexpress_hwmon_attr_is_visible(struct kobject *kobj,
		struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct device_attribute *dev_attr = container_of(attr,
				struct device_attribute, attr);

	if (dev_attr->show == vexpress_hwmon_label_show &&
			!of_get_property(dev->of_node, "label", NULL))
		return 0;

	return attr->mode;
}

static DEVICE_ATTR(name, S_IRUGO, vexpress_hwmon_name_show, NULL);

#define VEXPRESS_HWMON_ATTRS(_name, _label_attr, _input_attr)	\
struct attribute *vexpress_hwmon_attrs_##_name[] = {		\
	&dev_attr_name.attr,					\
	&dev_attr_##_label_attr.attr,				\
	&sensor_dev_attr_##_input_attr.dev_attr.attr,		\
	NULL							\
}

struct vexpress_hwmon_type {
	const char *name;
	const struct attribute_group **attr_groups;
};

#if !defined(CONFIG_REGULATOR_VEXPRESS)
static DEVICE_ATTR(in1_label, S_IRUGO, vexpress_hwmon_label_show, NULL);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, vexpress_hwmon_u32_show,
		NULL, 1000);
static VEXPRESS_HWMON_ATTRS(volt, in1_label, in1_input);
static struct attribute_group vexpress_hwmon_group_volt = {
	.is_visible = vexpress_hwmon_attr_is_visible,
	.attrs = vexpress_hwmon_attrs_volt,
};
static struct vexpress_hwmon_type vexpress_hwmon_volt = {
	.name = "vexpress_volt",
	.attr_groups = (const struct attribute_group *[]) {
		&vexpress_hwmon_group_volt,
		NULL,
	},
};
#endif

static DEVICE_ATTR(curr1_label, S_IRUGO, vexpress_hwmon_label_show, NULL);
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, vexpress_hwmon_u32_show,
		NULL, 1000);
static VEXPRESS_HWMON_ATTRS(amp, curr1_label, curr1_input);
static struct attribute_group vexpress_hwmon_group_amp = {
	.is_visible = vexpress_hwmon_attr_is_visible,
	.attrs = vexpress_hwmon_attrs_amp,
};
static struct vexpress_hwmon_type vexpress_hwmon_amp = {
	.name = "vexpress_amp",
	.attr_groups = (const struct attribute_group *[]) {
		&vexpress_hwmon_group_amp,
		NULL
	},
};

static DEVICE_ATTR(temp1_label, S_IRUGO, vexpress_hwmon_label_show, NULL);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, vexpress_hwmon_u32_show,
		NULL, 1000);
static VEXPRESS_HWMON_ATTRS(temp, temp1_label, temp1_input);
static struct attribute_group vexpress_hwmon_group_temp = {
	.is_visible = vexpress_hwmon_attr_is_visible,
	.attrs = vexpress_hwmon_attrs_temp,
};
static struct vexpress_hwmon_type vexpress_hwmon_temp = {
	.name = "vexpress_temp",
	.attr_groups = (const struct attribute_group *[]) {
		&vexpress_hwmon_group_temp,
		NULL
	},
};

static DEVICE_ATTR(power1_label, S_IRUGO, vexpress_hwmon_label_show, NULL);
static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, vexpress_hwmon_u32_show,
		NULL, 1);
static VEXPRESS_HWMON_ATTRS(power, power1_label, power1_input);
static struct attribute_group vexpress_hwmon_group_power = {
	.is_visible = vexpress_hwmon_attr_is_visible,
	.attrs = vexpress_hwmon_attrs_power,
};
static struct vexpress_hwmon_type vexpress_hwmon_power = {
	.name = "vexpress_power",
	.attr_groups = (const struct attribute_group *[]) {
		&vexpress_hwmon_group_power,
		NULL
	},
};

static DEVICE_ATTR(energy1_label, S_IRUGO, vexpress_hwmon_label_show, NULL);
static SENSOR_DEVICE_ATTR(energy1_input, S_IRUGO, vexpress_hwmon_u64_show,
		NULL, 1);
static VEXPRESS_HWMON_ATTRS(energy, energy1_label, energy1_input);
static struct attribute_group vexpress_hwmon_group_energy = {
	.is_visible = vexpress_hwmon_attr_is_visible,
	.attrs = vexpress_hwmon_attrs_energy,
};
static struct vexpress_hwmon_type vexpress_hwmon_energy = {
	.name = "vexpress_energy",
	.attr_groups = (const struct attribute_group *[]) {
		&vexpress_hwmon_group_energy,
		NULL
	},
};

static struct of_device_id vexpress_hwmon_of_match[] = {
#if !defined(CONFIG_REGULATOR_VEXPRESS)
	{
		.compatible = "arm,vexpress-volt",
		.data = &vexpress_hwmon_volt,
	},
#endif
	{
		.compatible = "arm,vexpress-amp",
		.data = &vexpress_hwmon_amp,
	}, {
		.compatible = "arm,vexpress-temp",
		.data = &vexpress_hwmon_temp,
	}, {
		.compatible = "arm,vexpress-power",
		.data = &vexpress_hwmon_power,
	}, {
		.compatible = "arm,vexpress-energy",
		.data = &vexpress_hwmon_energy,
	},
	{}
};
MODULE_DEVICE_TABLE(of, vexpress_hwmon_of_match);

static int vexpress_hwmon_probe(struct platform_device *pdev)
{
	int err;
	const struct of_device_id *match;
	struct vexpress_hwmon_data *data;
	const struct vexpress_hwmon_type *type;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);

	match = of_match_device(vexpress_hwmon_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;
	type = match->data;
	data->name = type->name;

	data->func = vexpress_config_func_get_by_dev(&pdev->dev);
	if (!data->func)
		return -ENODEV;

	err = sysfs_create_groups(&pdev->dev.kobj, type->attr_groups);
	if (err)
		goto error;

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error;
	}

	return 0;

error:
	sysfs_remove_group(&pdev->dev.kobj, match->data);
	vexpress_config_func_put(data->func);
	return err;
}

static int vexpress_hwmon_remove(struct platform_device *pdev)
{
	struct vexpress_hwmon_data *data = platform_get_drvdata(pdev);
	const struct of_device_id *match;

	hwmon_device_unregister(data->hwmon_dev);

	match = of_match_device(vexpress_hwmon_of_match, &pdev->dev);
	sysfs_remove_group(&pdev->dev.kobj, match->data);

	vexpress_config_func_put(data->func);

	return 0;
}

static struct platform_driver vexpress_hwmon_driver = {
	.probe = vexpress_hwmon_probe,
	.remove = vexpress_hwmon_remove,
	.driver	= {
		.name = DRVNAME,
		.owner = THIS_MODULE,
		.of_match_table = vexpress_hwmon_of_match,
	},
};

module_platform_driver(vexpress_hwmon_driver);

MODULE_AUTHOR("Pawel Moll <pawel.moll@arm.com>");
MODULE_DESCRIPTION("Versatile Express hwmon sensors driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:vexpress-hwmon");
