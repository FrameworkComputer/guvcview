/*******************************************************************************#
#           guvcview              http://guvcview.berlios.de                    #
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

#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "v4l2_devices.h"

/* enumerates system video devices
 * by checking /sys/class/video4linux
 * args: 
 * videodevice: current device string (default "/dev/video0")
 * 
 * returns: pointer to LDevices struct containing the video devices list */
LDevices *enum_devices( gchar *videodevice )
{
	int ret=0;
	int fd=0;
	LDevices *listDevices = NULL;
	listDevices = g_new0( LDevices, 1);
	listDevices->listVidDevices = NULL;
	struct v4l2_capability v4l2_cap;
	GDir *v4l2_dir=NULL;
	GDir *id_dir=NULL;
	GError *error=NULL;
	v4l2_dir = g_dir_open("/sys/class/video4linux",0,&error);
	if(v4l2_dir == NULL)
	{
		g_printerr ("opening '/sys/class/video4linux' failed: %s\n", 
			 error->message);
		g_error_free ( error );
		error=NULL;
		return NULL;
	}
	const gchar *v4l2_device;
	int num_dev = 0;
	
	while((v4l2_device = g_dir_read_name(v4l2_dir)) != NULL)
	{
		if(!(g_str_has_prefix(v4l2_device, "video")))
			continue;
		gchar *device = NULL;
		device = g_strjoin("/","/dev",v4l2_device,NULL);
		
		if ((fd = open(device, O_RDWR )) == -1) 
		{
			g_printerr("ERROR opening V4L interface for %s\n",
				device);
			close(fd);
			continue;
		} 
		else
		{
			ret = ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap);
			if (ret < 0) 
			{
				perror("VIDIOC_QUERYCAP error");
				g_printerr("   couldn't query device %s\n",
					device);
				close(fd);
				continue;
			}
			else
			{
				num_dev++;
				g_printf("%s - device %d\n", device, num_dev);
				listDevices->listVidDevices = g_renew(VidDevice, 
					listDevices->listVidDevices, 
					num_dev);
				listDevices->listVidDevices[num_dev-1].device = g_strdup(device);
				listDevices->listVidDevices[num_dev-1].name = g_strdup((gchar *) v4l2_cap.card);
				listDevices->listVidDevices[num_dev-1].driver = g_strdup((gchar *) v4l2_cap.driver);
				listDevices->listVidDevices[num_dev-1].location = g_strdup((gchar *) v4l2_cap.bus_info);
				listDevices->listVidDevices[num_dev-1].valid = 1;
				if(g_strcmp0(videodevice,listDevices->listVidDevices[num_dev-1].device)==0) 
				{
					listDevices->listVidDevices[num_dev-1].current = 1;
					listDevices->current_device = num_dev-1;
				}
				else
					listDevices->listVidDevices[num_dev-1].current = 0;
			}
		}
		g_free(device);
		
		close(fd);
		listDevices->listVidDevices[num_dev-1].vendor = NULL;
		listDevices->listVidDevices[num_dev-1].product = NULL;
		listDevices->listVidDevices[num_dev-1].version = NULL;
		/*get vid, pid and version from:                         */
		/* /sys/class/video4linux/videoN/device/input/inputX/id/ */
		
		/* get input number - inputX */
		gchar *dev_dir= g_strjoin("/",
			"/sys/class/video4linux",
			v4l2_device,
			"device/input",
			NULL);
		id_dir = g_dir_open(dev_dir,0,&error);
		if(id_dir == NULL)
		{
			g_printerr ("opening '%s' failed: %s\n",
			dev_dir,
			error->message);
			g_error_free ( error );
			error=NULL;
		}
		else
		{
			/* get data */
			const gchar *inpXdir;
			if((inpXdir = g_dir_read_name(id_dir)) != NULL)
			{
				char code[5];
				/* vid */
				gchar *vid_str=g_strjoin("/",
					dev_dir,
					inpXdir,
					"id/vendor",
					NULL);
				FILE *vid_fp = g_fopen(vid_str,"r");
				if(vid_fp != NULL)
				{
					if(fgets(code, sizeof(code), vid_fp) != NULL)
						listDevices->listVidDevices[num_dev-1].vendor = g_ascii_strdown(code,-1);
					fclose (vid_fp);
				}
				g_free(vid_str);
				/* pid */
				gchar *pid_str=g_strjoin("/",
					dev_dir,
					inpXdir,
					"id/product",
					NULL);
				
				FILE *pid_fp = g_fopen(pid_str,"r");
				if(pid_fp != NULL)
				{
					if(fgets(code, sizeof(code), pid_fp) != NULL)
						listDevices->listVidDevices[num_dev-1].product = g_ascii_strdown(code,-1);
					fclose (pid_fp);
				}
				g_free(pid_str);
				/* version */
				gchar *ver_str=g_strjoin("/",
					dev_dir,
					inpXdir,
					"id/version",
					NULL);
				
				FILE *ver_fp = g_fopen(ver_str,"r");
				if(pid_fp != NULL)
				{
					if(fgets(code, sizeof(code), ver_fp) != NULL)
						listDevices->listVidDevices[num_dev-1].version = g_ascii_strdown(code,-1);
					fclose (ver_fp);
				}
				g_free(ver_str);
			}
		}
		g_free(dev_dir);
		if(id_dir != NULL) g_dir_close(id_dir);
	}
	
	if(v4l2_dir != NULL) g_dir_close(v4l2_dir);
	
	listDevices->num_devices = num_dev;
	return(listDevices);
}

/*clean video devices list
 * args: listVidDevices: array of VidDevice (list of video devices)
 * numb_devices: number of existing supported video devices
 *
 * returns: void                                                       */
void freeDevices(LDevices *listDevices)
{
	int i=0;
	
	for(i=0;i<(listDevices->num_devices);i++)
	{
		g_free(listDevices->listVidDevices[i].device);
		g_free(listDevices->listVidDevices[i].name);
		g_free(listDevices->listVidDevices[i].driver);
		g_free(listDevices->listVidDevices[i].location);
		g_free(listDevices->listVidDevices[i].vendor);
		g_free(listDevices->listVidDevices[i].product);
		g_free(listDevices->listVidDevices[i].version);
	}
	g_free(listDevices->listVidDevices);
	g_free(listDevices);
}