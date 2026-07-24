/*
 * aesd_ioctl.h
 *
 * Ioctl definitions for the AESD character driver seek support (assignment 9).
 */
#ifndef AESD_IOCTL_H
#define AESD_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
#endif

/**
 * Parameters for the AESDCHAR_IOCSEEKTO command: seek to a byte offset
 * within one of the write commands currently stored in the circular buffer.
 */
struct aesd_seekto
{
    /* Zero referenced index of the write command to seek into, relative to
     * the commands currently stored in the circular buffer. */
    uint32_t write_cmd;
    /* Zero referenced byte offset within the write command identified by
     * write_cmd. */
    uint32_t write_cmd_offset;
};

#define AESD_IOC_MAGIC 0xaeb

#define AESDCHAR_IOCSEEKTO _IOWR(AESD_IOC_MAGIC, 1, struct aesd_seekto)

#define AESDCHAR_IOC_MAXNR 1

#endif /* AESD_IOCTL_H */
