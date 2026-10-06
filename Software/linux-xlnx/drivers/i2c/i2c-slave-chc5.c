// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 I2C slave register bank
 *
 * Copyright (c) 2025 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/atomic.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define CHC5_BANK_SIZE      256
#define CHC5_RSVD_END       0x400
#define CHC5_CNT_OFFSET     0x400
#define CHC5_FILE_SIZE      (CHC5_CNT_OFFSET + CHC5_BANK_SIZE * 2)
#define CHC5_NUM_SNAPSHOTS  3
#define CHC5_SNAP_IDX       0x3
#define CHC5_SNAP_FRESH     0x4

struct chc5_snap_s {
	u8	data[CHC5_BANK_SIZE];
	u16	cnt[CHC5_BANK_SIZE];
};

struct chc5_slave_data_s {
	struct bin_attribute bin;

	u8		buffer[CHC5_BANK_SIZE];
	u16		wr_cnt[CHC5_BANK_SIZE];
	struct kernfs_node *kn;

	struct chc5_snap_s snapshots[CHC5_NUM_SNAPSHOTS];
	atomic_t	snap_ready;
	int		snap_write_idx;
	int		snap_read_idx;
	struct mutex	read_lock;

	u16		buffer_idx;
	u16		rx_count;
	u8		num_address_bytes;
	u8		idx_write_cnt;
};

static int i2c_slave_chc5_slave_cb(struct i2c_client *client,
				   enum i2c_slave_event event, u8 *val)
{
	struct chc5_slave_data_s *d = i2c_get_clientdata(client);

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		if (d->idx_write_cnt < d->num_address_bytes) {
			if (d->idx_write_cnt == 0)
				d->buffer_idx = 0;

			d->buffer_idx = *val | (d->buffer_idx << 8);
			d->idx_write_cnt++;
		} else {
			if (d->buffer_idx < CHC5_BANK_SIZE) {
				d->buffer[d->buffer_idx] = *val;
				d->wr_cnt[d->buffer_idx++]++;
				d->rx_count++;
			}
		}
		break;

	case I2C_SLAVE_READ_PROCESSED:
		d->buffer_idx++;
		fallthrough;
	case I2C_SLAVE_READ_REQUESTED:
		if (d->buffer_idx < CHC5_BANK_SIZE)
			*val = d->buffer[d->buffer_idx];
		else
			*val = 0xff;
		break;

	case I2C_SLAVE_STOP:
		if (d->rx_count > 0) {
			struct chc5_snap_s *sn = &d->snapshots[d->snap_write_idx];

			memcpy(sn->data, d->buffer, CHC5_BANK_SIZE);
			memcpy(sn->cnt, d->wr_cnt, sizeof(sn->cnt));
			d->snap_write_idx = atomic_xchg(&d->snap_ready,
					d->snap_write_idx | CHC5_SNAP_FRESH) &
					CHC5_SNAP_IDX;
			if (d->kn)
				kernfs_notify(d->kn);
		}
		d->idx_write_cnt = 0;
		d->rx_count = 0;
		break;

	case I2C_SLAVE_WRITE_REQUESTED:
		d->idx_write_cnt = 0;
		d->rx_count = 0;
		break;

	default:
		break;
	}

	return 0;
}

static ssize_t i2c_slave_chc5_bin_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct chc5_slave_data_s *d;
	struct chc5_snap_s *sn;
	size_t done = 0;

	d = dev_get_drvdata(kobj_to_dev(kobj));

	if (off + count > CHC5_FILE_SIZE)
		return -EINVAL;

	mutex_lock(&d->read_lock);
	if (atomic_read(&d->snap_ready) & CHC5_SNAP_FRESH)
		d->snap_read_idx = atomic_xchg(&d->snap_ready,
					       d->snap_read_idx) & CHC5_SNAP_IDX;

	sn = &d->snapshots[d->snap_read_idx];

	while (done < count) {
		loff_t pos = off + done;
		size_t n;

		if (pos < CHC5_BANK_SIZE) {
			n = min_t(size_t, count - done, CHC5_BANK_SIZE - pos);
			memcpy(buf + done, sn->data + pos, n);
		} else if (pos < CHC5_RSVD_END) {
			n = min_t(size_t, count - done, CHC5_RSVD_END - pos);
			memset(buf + done, 0, n);
		} else {
			n = min_t(size_t, count - done, CHC5_FILE_SIZE - pos);
			memcpy(buf + done,
			       (const u8 *)sn->cnt + (pos - CHC5_CNT_OFFSET), n);
		}
		done += n;
	}
	mutex_unlock(&d->read_lock);

	return count;
}

static ssize_t i2c_slave_chc5_bin_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct chc5_slave_data_s *d;

	d = dev_get_drvdata(kobj_to_dev(kobj));

	if (off + count > CHC5_BANK_SIZE)
		return -EINVAL;

	memcpy(&d->buffer[off], buf, count);

	return count;
}

static ssize_t new_data_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct chc5_slave_data_s *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n",
			  !!(atomic_read(&d->snap_ready) & CHC5_SNAP_FRESH));
}
static DEVICE_ATTR_RO(new_data);

static int i2c_slave_chc5_probe(struct i2c_client *client)
{
	struct chc5_slave_data_s *d;
	int i, ret;

	d = devm_kzalloc(&client->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->num_address_bytes = 1;

	memset(d->buffer, 0xff, CHC5_BANK_SIZE);
	for (i = 0; i < CHC5_NUM_SNAPSHOTS; i++)
		memset(d->snapshots[i].data, 0xff, CHC5_BANK_SIZE);

	d->snap_write_idx = 0;
	atomic_set(&d->snap_ready, 1);
	d->snap_read_idx = 2;
	mutex_init(&d->read_lock);

	i2c_set_clientdata(client, d);

	dev_info(&client->dev, "%s: address 0x%x\n", __func__, client->addr);

	sysfs_bin_attr_init(&d->bin);
	d->bin.attr.name = "slave-chc5";
	d->bin.attr.mode = S_IRUSR | S_IWUSR;
	d->bin.read = i2c_slave_chc5_bin_read;
	d->bin.write = i2c_slave_chc5_bin_write;
	d->bin.size = CHC5_FILE_SIZE;

	ret = sysfs_create_bin_file(&client->dev.kobj, &d->bin);
	if (ret)
		return ret;

	d->kn = sysfs_get_dirent(client->dev.kobj.sd, "slave-chc5");

	ret = device_create_file(&client->dev, &dev_attr_new_data);
	if (ret)
		goto err_bin;

	ret = i2c_slave_register(client, i2c_slave_chc5_slave_cb);
	if (ret)
		goto err_new_data;

	return 0;

err_new_data:
	device_remove_file(&client->dev, &dev_attr_new_data);
err_bin:
	if (d->kn)
		sysfs_put(d->kn);
	sysfs_remove_bin_file(&client->dev.kobj, &d->bin);
	return ret;
}

static void i2c_slave_chc5_remove(struct i2c_client *client)
{
	struct chc5_slave_data_s *d = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	if (d->kn)
		sysfs_put(d->kn);
	device_remove_file(&client->dev, &dev_attr_new_data);
	sysfs_remove_bin_file(&client->dev.kobj, &d->bin);
}

static const struct of_device_id i2c_slave_chc5_of_match[] = {
	{ .compatible = "circuitvalley,slave-chc5" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_slave_chc5_of_match);

static const struct i2c_device_id i2c_slave_chc5_id[] = {
	{ "slave-chc5", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_slave_chc5_id);

static struct i2c_driver i2c_slave_chc5_driver = {
	.driver = {
		.name = "i2c-slave-chc5",
		.of_match_table = i2c_slave_chc5_of_match,
	},
	.probe = i2c_slave_chc5_probe,
	.remove = i2c_slave_chc5_remove,
	.id_table = i2c_slave_chc5_id,
};
module_i2c_driver(i2c_slave_chc5_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 I2C slave register bank");
MODULE_LICENSE("GPL");
