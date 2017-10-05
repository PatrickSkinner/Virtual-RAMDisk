A device driver that implements a virtual RAMdisk. One the module is loaded users can read and write /dev/asgn1 to interact 
with the device. A list of pages is maintained by the device, when writing to the device new pages are automatically allocated as required.

Users can use IOCTL to set the maximum number of processes that can access the device. Debug information can be output by reading from /proc/asgn1.
All pages can be freed when opening the device in write only mode.
