### The following operations have been implemented and are available to the program user:
- creating a virtual disk,
- copying a file from a Minix system disk to a virtual disk,
- copying a file from a virtual disk to the Minix disk,
- displaying the virtual disk directory,
- deleting a file from a virtual disk,
- deleting the virtual disk,
- displaying a summary of the current virtual disk occupancy map -
  i.e. a list of subsequent areas of the virtual disk with the description: address, type
  area, size, status (e.g. for data blocks: free/busy).

#### There are to different files implementing filesystem:
- filesystem.c : runs on new Unix systems
- minix_fs.c : runs on minix operating system version 2 or newer
