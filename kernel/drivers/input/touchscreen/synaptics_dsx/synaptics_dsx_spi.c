/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/input/synaptics_dsx.h>
#include "synaptics_dsx_core.h"


#define RMI4_SPI_READ 0x80
#define RMI4_SPI_WRITE 0x00

static int synaptics_rmi4_spi_set_page(struct synaptics_rmi4_data *rmi4_data,
		unsigned int addr)
{
	int retval = 0;
	unsigned char txbuf[PAGE_SELECT_LEN+1];
	unsigned char page;
	unsigned char xfer_index;
	struct spi_message msg;
	struct spi_transfer xfers[PAGE_SELECT_LEN+1];
	struct spi_device *client = to_spi_device(rmi4_data->pdev->dev.parent);
	const struct synaptics_dsx_platform_data *pdata
				= rmi4_data->hw_info->board;

	page = ((addr >> 8) & ~MASK_7BIT);

	if (page != rmi4_data->current_page) {
		txbuf[0] = RMI4_SPI_WRITE;
		txbuf[1] = MASK_8BIT; /* global register 0xFF for page select */
		txbuf[2] = page;
		spi_message_init(&msg);

		for (xfer_index = 0;
			xfer_index < PAGE_SELECT_LEN+1; xfer_index++) {
			memset(&xfers[xfer_index], 0,
					sizeof(struct spi_transfer));

			xfers[xfer_index].len = 1;

			xfers[xfer_index].delay_usecs =
				pdata->spi_delay->byte_delay;

			xfers[xfer_index].cs_change = 0;
			xfers[xfer_index].tx_buf = &txbuf[xfer_index];
			spi_message_add_tail(&xfers[xfer_index], &msg);
		}

		if (pdata->spi_delay->block_delay) {
			xfers[xfer_index-1].delay_usecs =
				pdata->spi_delay->block_delay;

			xfers[xfer_index-1].cs_change = 0;
		}

		if (spi_sync(client, &msg) == 0) {
			rmi4_data->current_page = page;
			retval = PAGE_SELECT_LEN;
		} else{
			retval = 0;
			dev_err(&rmi4_data->pdev->dev,
				"%s: spi read error!!\n", __func__);
		}

	} else {
		retval = PAGE_SELECT_LEN;
	}

	return retval;
}

 /**
 * synaptics_rmi4_spi_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via spi
 * bus.
 */

static int synaptics_rmi4_spi_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char txbuf[PAGE_SELECT_LEN];
	unsigned char *rxbuf;
	unsigned short xfer_index;
	struct spi_message msg;
	struct spi_transfer *xfers;
	struct spi_device *client = to_spi_device(rmi4_data->pdev->dev.parent);
	const struct synaptics_dsx_platform_data *pdata =
					rmi4_data->hw_info->board;
	int xfer_count = PAGE_SELECT_LEN+length;

	txbuf[0] = (addr >> 8) | RMI4_SPI_READ ;
	txbuf[1] = addr & MASK_8BIT;

	spi_message_init(&msg);

	xfers = kcalloc(xfer_count, sizeof(struct spi_transfer), GFP_KERNEL);
	if (!xfers) {
		dev_err(&rmi4_data->pdev->dev,
			"%s: Failed to allocate SPI xfer memory\n", __func__);
		return -ENOMEM;
	}

	rxbuf = kcalloc(length, sizeof(unsigned char), GFP_KERNEL);
	if (!rxbuf) {
		dev_err(&rmi4_data->pdev->dev,
			"%s: Failed to allocate rxbuf memory\n", __func__);
		return -ENOMEM;
	}

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_spi_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	for (xfer_index = 0; xfer_index < xfer_count; xfer_index++) {
		memset(&xfers[xfer_index], 0, sizeof(struct spi_transfer));
		xfers[xfer_index].len = 1;
		xfers[xfer_index].delay_usecs = pdata->spi_delay->byte_delay;
		xfers[xfer_index].cs_change = 0;
		if (xfer_index < PAGE_SELECT_LEN)
			xfers[xfer_index].tx_buf = &txbuf[xfer_index];
		else
			xfers[xfer_index].rx_buf =
				&rxbuf[xfer_index - PAGE_SELECT_LEN];

		spi_message_add_tail(&xfers[xfer_index], &msg);
	}

	if (pdata->spi_delay->block_delay) {
		xfers[xfer_index-1].delay_usecs = pdata->spi_delay->block_delay;
		xfers[xfer_index-1].cs_change = 0;
	}

	if (spi_sync(client, &msg) == 0) {
		memcpy(data, rxbuf, length);
		retval = length;
	} else{
		retval = 0;
		dev_err(&rmi4_data->pdev->dev,
			"%s: spi read error!!\n", __func__);
	}
#ifdef DEBUG
	dev_dbg(&rmi4_data->pdev->dev,
		"%s: dump spi read data: addr = 0x%04x length =%d\n",
		__func__, addr, length);
	int j;
	for (j = 0; j < length; j++)
		dev_dbg(&rmi4_data->pdev->dev, "data[%d]=0x%04x\n", j, data[j]);
#endif

exit:
	kfree(rxbuf);
	kfree(xfers);
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;

}

 /**
 * synaptics_rmi4_spi_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via spi bus.
 */
static int synaptics_rmi4_spi_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char txbuf[PAGE_SELECT_LEN+length];
	unsigned short xfer_index;
	struct spi_message msg;
	struct spi_transfer xfers[PAGE_SELECT_LEN+length];
	struct spi_device *client = to_spi_device(rmi4_data->pdev->dev.parent);
	const struct synaptics_dsx_platform_data *pdata =
					rmi4_data->hw_info->board;
	int xfer_count = PAGE_SELECT_LEN+length;

#ifdef DEBUG
	dev_dbg(&rmi4_data->pdev->dev,
		"%s: dump spi write data: addr = 0x%04x length =%d\n",
		__func__, addr, length);
	int j;
	for (j = 0; j < length; j++)
		dev_dbg(&rmi4_data->pdev->dev, "data[%d]=0x%02x\n", j, data[j]);
#endif

	txbuf[0] = (addr >> 8) & ~RMI4_SPI_READ ;
	txbuf[1] = addr & MASK_8BIT;
	memcpy(&txbuf[PAGE_SELECT_LEN], data, length);  /* copy to txbuf[2] */
	spi_message_init(&msg);

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_spi_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	for (xfer_index = 0; xfer_index < xfer_count; xfer_index++) {
		memset(&xfers[xfer_index], 0, sizeof(struct spi_transfer));
		xfers[xfer_index].len = 1;
		xfers[xfer_index].delay_usecs = pdata->spi_delay->byte_delay;
		xfers[xfer_index].cs_change = 0;
		xfers[xfer_index].tx_buf = &txbuf[xfer_index];
		spi_message_add_tail(&xfers[xfer_index], &msg);
	}

	if (pdata->spi_delay->block_delay) {
		xfers[xfer_index-1].delay_usecs = pdata->spi_delay->block_delay;
		xfers[xfer_index-1].cs_change = 0;
	}

	if (spi_sync(client, &msg) == 0) {
		retval = length;
	} else{
		retval = 0;
		dev_err(&rmi4_data->pdev->dev,
			"%s: spi read error!!\n", __func__);
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

static struct synaptics_dsx_bus_data rmi4_spi_bus_data = {
	.type		= BUS_SPI,
	.read		= synaptics_rmi4_spi_read,
	.write		= synaptics_rmi4_spi_write,
};

static int synaptics_rmi4_spi_probe(struct spi_device *spi)
{
	int retval;
	struct platform_device *pdev;
	struct synaptics_dsx_hw_info hw_info = {
		.board = spi->dev.platform_data,
		.bus = &rmi4_spi_bus_data,
	};

	if (spi->master->flags & SPI_MASTER_HALF_DUPLEX)
		return -EINVAL;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3;

	retval = spi_setup(spi);
	if (retval < 0) {
		dev_err(&spi->dev, "spi_setup failed!\n");
		return retval;
	}

	pdev = platform_device_alloc(DRIVER_NAME, -1);
	if (!pdev) {
		dev_err(&spi->dev,
			"%s: Failed to allocate platform device\n", __func__);
		return -ENOMEM;
	}

	pdev->dev.parent = &spi->dev;

	retval = platform_device_add_data(pdev, &hw_info, sizeof(hw_info));
	if (retval) {
		dev_err(&spi->dev,
			"%s: Failed to add platform data\n", __func__);
		goto err_device_put;
	}

	retval = platform_device_add(pdev);
	if (retval) {
		dev_err(&spi->dev,
			"%s: Failed to add platform device\n", __func__);
		goto err_device_put;
	}

	if (!pdev->dev.driver) {
		retval = -ENODEV;
		goto err_device_put;
	}

	dev_info(&spi->dev, "%s: success\n", __func__);

	return 0;

err_device_put:
	platform_device_put(pdev);
	return retval;

}

static int synaptics_rmi4_spi_remove(struct spi_device *spi)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(&spi->dev);

	platform_device_unregister(rmi4_data->pdev);

	return 0;
}

static struct spi_driver synaptics_rmi4_spi_driver = {
	.driver = {
		.name = "synaptics_dsx_spi",
		.owner = THIS_MODULE,
	},
	.probe = synaptics_rmi4_spi_probe,
	.remove = __devexit_p(synaptics_rmi4_spi_remove),
};


int synaptics_rmi4_bus_init(void)
{
	return spi_register_driver(&synaptics_rmi4_spi_driver);
}
EXPORT_SYMBOL(synaptics_rmi4_bus_init);

void synaptics_rmi4_bus_exit(void)
{
	return spi_unregister_driver(&synaptics_rmi4_spi_driver);
}
EXPORT_SYMBOL(synaptics_rmi4_bus_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX SPI Touch Driver");
MODULE_LICENSE("GPL v2");
