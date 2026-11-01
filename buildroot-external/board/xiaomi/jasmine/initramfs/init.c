// SPDX-License-Identifier: Apache-2.0
//
// Minimal initramfs for Android-booted devices: the HAOS disk image lives in
// the "userdata" partition (or the one named by haos.disk= on the kernel
// command line), so attach it as a loop device with a partition scan, mount
// the selected system slot and hand over to systemd.
//
// The system slot is chosen here, not by the Android bootloader: the state
// block in the bootstate partition (see jasmine-slot.sh in the rootfs overlay)
// holds the primary slot and how many boots a not yet confirmed slot may still
// try before falling back to the other one.

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
#define BOOTSTATE_PARTITION 6

// Text block at the start of the bootstate partition, written by
// jasmine-slot.sh: "JSLT1 primary=A tries_a=3 good_a=0 tries_b=0 good_b=1"
#define STATE_MAGIC "JSLT1"
#define STATE_SIZE 512

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

struct state {
	int primary;	// 0 = A, 1 = B
	int tries[2];
	int good[2];
};

static int state_number(const char *buf, const char *key, int *value)
{
	const char *p = strstr(buf, key);

	if (!p)
		return 0;
	*value = atoi(p + strlen(key));
	return 1;
}

static int load_state(const char *dev, struct state *st)
{
	char buf[STATE_SIZE + 1] = "";
	const char *p;
	int fd = open(dev, O_RDONLY);
	int ok;

	if (fd < 0)
		return 0;
	ok = read(fd, buf, STATE_SIZE) == STATE_SIZE;
	close(fd);
	if (!ok || strncmp(buf, STATE_MAGIC " ", strlen(STATE_MAGIC) + 1))
		return 0;

	p = strstr(buf, "primary=");
	if (!p || (p[8] != 'A' && p[8] != 'B'))
		return 0;
	st->primary = p[8] == 'B';

	return state_number(buf, " tries_a=", &st->tries[0]) &&
	       state_number(buf, " good_a=", &st->good[0]) &&
	       state_number(buf, " tries_b=", &st->tries[1]) &&
	       state_number(buf, " good_b=", &st->good[1]);
}

static void save_state(const char *dev, const struct state *st)
{
	char buf[STATE_SIZE] = "";
	int fd = open(dev, O_WRONLY);

	snprintf(buf, sizeof(buf), STATE_MAGIC " primary=%c tries_a=%d good_a=%d tries_b=%d good_b=%d\n",
		 st->primary ? 'B' : 'A', st->tries[0], st->good[0], st->tries[1], st->good[1]);
	if (fd < 0)
		return;
	if (pwrite(fd, buf, STATE_SIZE, 0) == STATE_SIZE)
		fsync(fd);
	close(fd);
}

// Pick the slot to boot: the primary one, or the other one when the primary was
// never confirmed and used up its tries. Booting an unconfirmed slot costs a try.
static int choose_slot(struct state *st)
{
	int i, s;

	for (i = 0; i < 2; i++) {
		s = st->primary ^ i;
		if (st->good[s])
			break;
		if (st->tries[s] > 0) {
			st->tries[s]--;
			break;
		}
	}
	if (i == 2)
		return st->primary;	// nothing bootable, try the primary anyway

	st->primary = s;
	return s;
}

// Make /proc/cmdline report the chosen slot, as RAUC and the Supervisor read
// rauc.slot and root from there. The boot image always names slot A.
static void patch_cmdline(const char *cmdline, int slot)
{
	char out[4096], *p;
	size_t n;
	FILE *f;

	snprintf(out, sizeof(out), "%s", cmdline);
	if (slot) {
		if ((p = strstr(out, "rauc.slot=A")))
			p[10] = 'B';
		if ((p = strstr(out, ROOT_PARTUUID_A)))
			memcpy(p, ROOT_PARTUUID_B, strlen(ROOT_PARTUUID_B));
	}

	mkdir("/run", 0755);
	if (mount("tmpfs", "/run", "tmpfs", 0, "size=64k") < 0)
		return;
	f = fopen("/run/cmdline", "w");
	if (!f)
		return;
	n = strlen(out);
	fwrite(out, 1, n, f);
	fclose(f);
	mount("/run/cmdline", "/proc/cmdline", NULL, MS_BIND, NULL);
	umount2("/run", MNT_DETACH);
}

int main(int argc, char **argv)
{
	char cmdline[4096], disk[64], backing[64], loopdev[64], root[96], statedev[96];
	int num, system_partition, slot = 0;
	struct state st = { .primary = 0, .tries = { 0, 0 }, .good = { 1, 0 } };

	(void)argc;

	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

	read_cmdline(cmdline, sizeof(cmdline));
	if (!cmdline_value(cmdline, "haos.disk", disk, sizeof(disk)))
		snprintf(disk, sizeof(disk), "%s", DEFAULT_DISK);
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

	// A missing or damaged state block (fresh install) boots slot A
	snprintf(statedev, sizeof(statedev), "%sp%d", loopdev, BOOTSTATE_PARTITION);
	if (wait_for(statedev) && load_state(statedev, &st)) {
		struct state old = st;

		slot = choose_slot(&st);
		if (memcmp(&old, &st, sizeof(st)))
			save_state(statedev, &st);
	}
	system_partition = slot ? SYSTEM_SLOT_B : SYSTEM_SLOT_A;
	patch_cmdline(cmdline, slot);

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
