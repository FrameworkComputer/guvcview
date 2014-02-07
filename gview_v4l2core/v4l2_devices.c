/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "gviewv4l2core.h"

extern int verbosity;

/*
 * enumerate available v4l2 devices
 * and creates list in vd->list_devices
 * args:
 *   vd - pointer to video device data
 *
 * asserts:
 *   vd is not null
 *   vd->videodevice is not null
 *   vd->udev is not null
 *   vd->list_devices is null
 *
 * returns: error code
 */
int enum_v4l2_devices(v4l2_dev_t *vd)
{
    /*assertions*/
    assert(vd != NULL);
    assert(vd->videodevice != NULL);
    assert(vd->udev != NULL);
    assert(vd->list_devices == NULL);

    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices;
    struct udev_list_entry *dev_list_entry;
    struct udev_device *dev;

    int num_dev = 0;
    int fd = 0;
    struct v4l2_capability v4l2_cap;

    vd->list_devices = calloc(1, sizeof(v4l2_dev_sys_data_t));

    /* Create a list of the devices in the 'v4l2' subsystem. */
    enumerate = udev_enumerate_new(vd->udev);
    udev_enumerate_add_match_subsystem(enumerate, "video4linux");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);
    /*
     * For each item enumerated, print out its information.
     * udev_list_entry_foreach is a macro which expands to
     * a loop. The loop will be executed for each member in
     * devices, setting dev_list_entry to a list entry
     * which contains the device's path in /sys.
     */
    udev_list_entry_foreach(dev_list_entry, devices)
    {
        const char *path;

        /*
         * Get the filename of the /sys entry for the device
         * and create a udev_device object (dev) representing it
         */
        path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(vd->udev, path);

        /* usb_device_get_devnode() returns the path to the device node
            itself in /dev. */
        const char *v4l2_device = udev_device_get_devnode(dev);
        if (verbosity > 0)
            printf("V4L2_CORE: Device Node Path: %s\n", v4l2_device);

        /* open the device and query the capabilities */
        if ((fd = v4l2_open(v4l2_device, O_RDWR | O_NONBLOCK, 0)) < 0)
        {
            fprintf(stderr, "V4L2_CORE: ERROR opening V4L2 interface for %s\n", v4l2_device);
            v4l2_close(fd);
            continue; /*next dir entry*/
        }

        if (xioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0)
        {
            fprintf(stderr, "V4L2_CORE: VIDIOC_QUERYCAP error: %s\n", strerror(errno));
            fprintf(stderr, "V4L2_CORE: couldn't query device %s\n", v4l2_device);
            v4l2_close(fd);
            continue; /*next dir entry*/
        }
        v4l2_close(fd);

        num_dev++;
        /* Update the device list*/
        vd->list_devices = realloc(vd->list_devices, num_dev * sizeof(v4l2_dev_sys_data_t));
        vd->list_devices[num_dev-1].device = strdup(v4l2_device);
        vd->list_devices[num_dev-1].name = strdup((char *) v4l2_cap.card);
        vd->list_devices[num_dev-1].driver = strdup((char *) v4l2_cap.driver);
        vd->list_devices[num_dev-1].location = strdup((char *) v4l2_cap.bus_info);
        vd->list_devices[num_dev-1].valid = 1;

        if(strcmp(vd->videodevice, vd->list_devices[num_dev-1].device)==0)
        {
            vd->list_devices[num_dev-1].current = 1;
            vd->this_device = num_dev-1;
        }
        else
            vd->list_devices[num_dev-1].current = 0;

        /* The device pointed to by dev contains information about
            the v4l2 device. In order to get information about the
            USB device, get the parent device with the
            subsystem/devtype pair of "usb"/"usb_device". This will
            be several levels up the tree, but the function will find
            it.*/
        dev = udev_device_get_parent_with_subsystem_devtype(
                dev,
                "usb",
                "usb_device");
        if (!dev)
        {
            fprintf(stderr, "V4L2_CORE: Unable to find parent usb device.");
            continue;
        }

        /* From here, we can call get_sysattr_value() for each file
            in the device's /sys entry. The strings passed into these
            functions (idProduct, idVendor, serial, etc.) correspond
            directly to the files in the directory which represents
            the USB device. Note that USB strings are Unicode, UCS2
            encoded, but the strings returned from
            udev_device_get_sysattr_value() are UTF-8 encoded. */
        if (verbosity > 0)
        {
            printf("  VID/PID: %s %s\n",
                udev_device_get_sysattr_value(dev,"idVendor"),
                udev_device_get_sysattr_value(dev, "idProduct"));
            printf("  %s\n  %s\n",
                udev_device_get_sysattr_value(dev,"manufacturer"),
                udev_device_get_sysattr_value(dev,"product"));
            printf("  serial: %s\n",
                udev_device_get_sysattr_value(dev, "serial"));
            printf("  busnum: %s\n",
                udev_device_get_sysattr_value(dev, "busnum"));
            printf("  devnum: %s\n",
                udev_device_get_sysattr_value(dev, "devnum"));
        }

        vd->list_devices[num_dev-1].vendor = strtoull(udev_device_get_sysattr_value(dev,"idVendor"), NULL, 16);
        vd->list_devices[num_dev-1].product = strtoull(udev_device_get_sysattr_value(dev, "idProduct"), NULL, 16);
        vd->list_devices[num_dev-1].busnum = strtoull(udev_device_get_sysattr_value(dev, "busnum"), NULL, 16);
		vd->list_devices[num_dev-1].devnum = strtoull(udev_device_get_sysattr_value(dev, "devnum"), NULL, 16);

        udev_device_unref(dev);
    }
    /* Free the enumerator object */
    udev_enumerate_unref(enumerate);

    vd->num_devices = num_dev;

    return(E_OK);
}

/*
 * free v4l2 devices list
 * args:
 *   vd - pointer to video device data
 *
 * asserts:
 *   vd is not null
 *   vd->list_devices is not null
 *
 * returns: void
 */
void free_v4l2_devices_list(v4l2_dev_t *vd)
{
	/*assertions*/
	assert(vd != NULL);
	assert(vd->list_devices != NULL);

	int i=0;
	for(i=0;i<(vd->num_devices);i++)
	{
		free(vd->list_devices[i].device);
		free(vd->list_devices[i].name);
		free(vd->list_devices[i].driver);
		free(vd->list_devices[i].location);
	}
	free(vd->list_devices);
	vd->list_devices = NULL;
}
