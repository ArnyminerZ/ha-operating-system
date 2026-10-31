// SPDX-License-Identifier: Apache-2.0
//
// Minimal initramfs for Android-booted devices: the HAOS disk image lives in
// the "userdata" partition (or the one named by haos.disk= on the kernel
// command line), so attach it as a loop device with a partition scan, mount
// the selected system slot and hand over to systemd.

#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

// Partition numbers of the HAOS GPT layout (genimage/partitions-os-gpt.cfg)
#define SYSTEM_SLOT_A 3
#define SYSTEM_SLOT_B 5

#define WAIT_SECONDS 30

#define DEFAULT_DISK "userdata"

static void die(const char *msg)
{
	fprintf(stderr, "haos-init: %s\n", msg);
	sync();
	sleep(60);
	reboot(RB_AUTOBOOT);
	for (;;)
		pause();
}

// Find the device node of the partition with the given PARTNAME
static int find_partname(const char *name, char *dev, size_t len)
{
	char path[512], line[256], want[128];
	struct dirent *ent;
	DIR *dir;
	FILE *f;
	int found = 0;

	snprintf(want, sizeof(want), "PARTNAME=%s\n", name);

	dir = opendir("/sys/class/block");
	if (!dir)
		return 0;

	while (!found && (ent = readdir(dir))) {
		int match = 0;
		char devname[128] = "";

		snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", ent->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			if (!strcmp(line, want))
				match = 1;
			else if (!strncmp(line, "DEVNAME=", 8))
				snprintf(devname, sizeof(devname), "%s", line + 8);
		}
		fclose(f);

		devname[strcspn(devname, "\n")] = 0;
		if (match && devname[0]) {
			snprintf(dev, len, "/dev/%s", devname);
			found = 1;
		}
	}
	closedir(dir);
	return found;
}

static int wait_for(const char *path)
{
	struct stat st;
	int i;

	for (i = 0; i < WAIT_SECONDS * 10; i++) {
		if (!stat(path, &st))
			return 1;
		usleep(100000);
	}
	return 0;
}

static int attach_loop(const char *backing, char *loopdev, size_t len)
{
	struct loop_config cfg;
	int ctl, num, fd, lfd;

	ctl = open("/dev/loop-control", O_RDWR);
	if (ctl < 0)
		return -1;
	num = ioctl(ctl, LOOP_CTL_GET_FREE);
	close(ctl);
	if (num < 0)
		return -1;

	snprintf(loopdev, len, "/dev/loop%d", num);
	if (!wait_for(loopdev))
		return -1;

	fd = open(backing, O_RDWR);
	lfd = open(loopdev, O_RDWR);
	if (fd < 0 || lfd < 0)
		return -1;

	memset(&cfg, 0, sizeof(cfg));
	cfg.fd = fd;
	cfg.info.lo_flags = LO_FLAGS_PARTSCAN;
	if (ioctl(lfd, LOOP_CONFIGURE, &cfg) < 0)
		return -1;

	close(lfd);
	close(fd);
	return num;
}

static void read_cmdline(char *cmdline, size_t len)
{
	FILE *f = fopen("/proc/cmdline", "r");

	cmdline[0] = 0;
	if (f) {
		if (!fgets(cmdline, len, f))
			cmdline[0] = 0;
		fclose(f);
	}
}

// Copy the value of key=value from the kernel command line, false if missing
static int cmdline_value(const char *cmdline, const char *key, char *value, size_t len)
{
	size_t klen = strlen(key);
	const char *p = cmdline;

	while ((p = strstr(p, key))) {
		if ((p == cmdline || p[-1] == ' ') && p[klen] == '=') {
			size_t n = strcspn(p + klen + 1, " \n");

			if (n >= len)
				n = len - 1;
			memcpy(value, p + klen + 1, n);
			value[n] = 0;
			return 1;
		}
		p += klen;
	}
	return 0;
}

int main(int argc, char **argv)
{
	char cmdline[4096], disk[64], slot[8], backing[64], loopdev[64], root[96];
	int num, system_partition;

	(void)argc;

	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

	read_cmdline(cmdline, sizeof(cmdline));
	if (!cmdline_value(cmdline, "haos.disk", disk, sizeof(disk)))
		snprintf(disk, sizeof(disk), "%s", DEFAULT_DISK);
	if (!cmdline_value(cmdline, "rauc.slot", slot, sizeof(slot)))
		snprintf(slot, sizeof(slot), "A");
	system_partition = strcmp(slot, "B") ? SYSTEM_SLOT_A : SYSTEM_SLOT_B;

	// eMMC probing finishes asynchronously
	for (num = 0; num < WAIT_SECONDS * 10; num++) {
		if (find_partname(disk, backing, sizeof(backing)))
			break;
		usleep(100000);
	}
	if (num == WAIT_SECONDS * 10)
		die("disk partition not found");
	if (!wait_for(backing))
		die("disk device node missing");

	num = attach_loop(backing, loopdev, sizeof(loopdev));
	if (num < 0)
		die("cannot attach loop device");

	snprintf(root, sizeof(root), "%sp%d", loopdev, system_partition);
	if (!wait_for(root))
		die("system partition not found in disk image");

	if (mount(root, "/sysroot", "erofs", MS_RDONLY, NULL) < 0)
		die("cannot mount system partition");

	mount("/dev", "/sysroot/dev", NULL, MS_MOVE, NULL);
	mount("/proc", "/sysroot/proc", NULL, MS_MOVE, NULL);
	mount("/sys", "/sysroot/sys", NULL, MS_MOVE, NULL);

	if (chdir("/sysroot") < 0 || mount(".", "/", NULL, MS_MOVE, NULL) < 0 ||
	    chroot(".") < 0 || chdir("/") < 0)
		die("cannot switch root");

	execv("/sbin/init", argv);
	die("cannot execute /sbin/init");
	return 1;
}
