/*
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 * Copyright (C) 2013 Fredrik Hultin.
 * Copyright (C) 2013 Jakob Bornecrantz.
 * Distributed under the Boost 1.0 licence, see LICENSE for full text.
 * Original implementation by: Yann Vernier.
 */

/* NOLO VR- HID/USB Driver Implementation */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "nolo.h"

#define NOLO_ID					0x0483 //ST microcontroller
#define NOLO_HMD				0x5750

static const int controllerLength = 3 + (3+4)*2 + 2 + 2 + 1;

static drv_priv* drv_priv_get(ohmd_device* device)
{
	return (drv_priv*)device;
}

static int get_feature_report(drv_priv* priv, drv_sensor_feature_cmd cmd, unsigned char* buf)
{
	memset(buf, 0, FEATURE_BUFFER_SIZE);
	buf[0] = (unsigned char)cmd;
	return hid_get_feature_report(priv->handle, buf, FEATURE_BUFFER_SIZE);
}

static int send_feature_report(drv_priv* priv, const unsigned char *data, size_t length)
{
	return hid_send_feature_report(priv->handle, data, length);
}

static void update_device(ohmd_device* device)
{
	drv_priv* priv = drv_priv_get(device);
	unsigned char buffer[FEATURE_BUFFER_SIZE];


	// Read all the messages from the device.
	while(true){
		int size = hid_read(priv->handle, buffer, FEATURE_BUFFER_SIZE);
		if(size < 0){
			LOGE("error reading from device");
			return;
		} else if(size == 0) {
			return; // No more messages, return.
		}

		// currently the only message type the hardware supports
		switch (buffer[0]) {
			case 0xa5:  // Controllers packet
				nolo_decode_controller(0, buffer+1);
				nolo_decode_controller(1, buffer+64-controllerLength);
			break;
			case 0xa6: // HMD packet
				nolo_decode_hmd_marker(priv, buffer+0x15);
				nolo_decode_base_station(priv, buffer+0x36);
			break;
			default:
				LOGE("unknown message type: %u", buffer[0]);
		}
	}


	return;
}

static int getf(ohmd_device* device, ohmd_float_value type, float* out)
{
	drv_priv* priv = drv_priv_get(device);

	switch(type){

	case OHMD_ROTATION_QUAT: {
			*(quatf*)out = priv->base.rotation;
			break;
		}

	case OHMD_POSITION_VECTOR:
		*(vec3f*)out = priv->base.position;
		break;

	default:
		ohmd_set_error(priv->base.ctx, "invalid type given to getf (%ud)", type);
		return -1;
		break;
	}

	return 0;
}

static void close_device(ohmd_device* device)
{
	LOGD("closing device");
	drv_priv* priv = drv_priv_get(device);
	hid_close(priv->handle);
	free(priv);
}

static char* _hid_to_unix_path(char* path)
{
	const int len = 4;
	char bus [4];
	char dev [4];
	char *result = malloc( sizeof(char) * ( 20 + 1 ) );

	sprintf (bus, "%.*s\n", len, path);
	sprintf (dev, "%.*s\n", len, path + 5);

	sprintf (result, "/dev/bus/usb/%03d/%03d",
		(int)strtol(bus, NULL, 16),
		(int)strtol(dev, NULL, 16));
	return result;
}

static ohmd_device* open_device(ohmd_driver* driver, ohmd_device_desc* desc)
{
	drv_priv* priv = ohmd_alloc(driver->ctx, sizeof(drv_priv));
	if(!priv)
		goto cleanup;

	priv->base.ctx = driver->ctx;

	// Open the HID device
	priv->handle = hid_open_path(desc->path);

	if(!priv->handle) {
		char* path = _hid_to_unix_path(desc->path);
		ohmd_set_error(driver->ctx, "Could not open %s. "
		                            "Check your rights.", path);
		free(path);
		goto cleanup;
	}

	if(hid_set_nonblocking(priv->handle, 1) == -1){
		ohmd_set_error(driver->ctx, "failed to set non-blocking on device");
		goto cleanup;
	}

	// Set default device properties
	ohmd_set_default_device_properties(&priv->base.properties);

	// set up device callbacks
	priv->base.update = update_device;
	priv->base.close = close_device;
	priv->base.getf = getf;

	return &priv->base;

cleanup:
	if(priv)
		free(priv);

	return NULL;
}

static void get_device_list(ohmd_driver* driver, ohmd_device_list* list)
{
	struct hid_device_info* devs = hid_enumerate(NOLO_ID, NOLO_HMD);
	struct hid_device_info* cur_dev = devs;

	while (cur_dev) {
		ohmd_device_desc* desc = &list->devices[list->num_devices++];

		strcpy(desc->driver, "OpenHMD NOLO VR CV1 driver");
		strcpy(desc->vendor, "LYRobotix");
		strcpy(desc->product, "NOLO CV1");

		desc->revision = 0;

		strcpy(desc->path, cur_dev->path);

		desc->driver_ptr = driver;

		cur_dev = cur_dev->next;
	}

	hid_free_enumeration(devs);
}

static void destroy_driver(ohmd_driver* drv)
{
	LOGD("shutting down NOLO CV1 driver");
	hid_exit();
	free(drv);
}

ohmd_driver* ohmd_create_nolo_drv(ohmd_context* ctx)
{
	ohmd_driver* drv = ohmd_alloc(ctx, sizeof(ohmd_driver));
	if(drv == NULL)
		return NULL;

	drv->get_device_list = get_device_list;
	drv->open_device = open_device;
	drv->destroy = destroy_driver;
	drv->ctx = ctx;

	return drv;
}

