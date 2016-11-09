#ifndef __SYSTEM_HPP__
#define __SYSTEM_HPP__

/* eventfd / signalfd / timerfd not supported by bionic
 * __NR_eventfd2 is not defined also in bionic headers */
#ifdef ANDROID
#include <sys/syscall.h>
#include <linux/fcntl.h>
#include <fcntl.h>

#ifndef __NR_timerfd_create
	#ifdef ARCH_ARM
		/*
		 * syscall number taken from linux kernel header
		 * linux/arch/arm/include/asm/unistd.h
		 */
		#define __NR_timerfd_create (__NR_SYSCALL_BASE+350)
		#define __NR_timerfd_settime (__NR_SYSCALL_BASE+353)
		#define __NR_timerfd_gettime (__NR_SYSCALL_BASE+354)
	#else
		#error __NR_timerfd_create not defined !
	#endif
#endif

/* timer fd defines */
#define TFD_TIMER_ABSTIME (1 << 0)
#define TFD_CLOEXEC O_CLOEXEC
#define TFD_NONBLOCK O_NONBLOCK
static inline int timerfd_create(int clockid, int flags)
{
	return syscall(__NR_timerfd_create, clockid, flags);
}

static inline int timerfd_settime(int fd, int flags,
		const struct itimerspec *new_value,
		struct itimerspec *old_value)
{
	return syscall(__NR_timerfd_settime, fd, flags, new_value, old_value);
}

static inline int timerfd_gettime(int fd, struct itimerspec *curr_value)
{
	return syscall(__NR_timerfd_gettime, fd, curr_value);
}

#ifndef __NR_eventfd2
	#ifdef ARCH_ARM
		/* syscall number taken from linux kernel header
		 * linux/arch/arm/include/asm/unistd.h */
		#define __NR_eventfd2 (__NR_SYSCALL_BASE+356)
	#else
		#error __NR_eventfd2 not defined !
	#endif
#endif

/* flags for eventfd.  */
#define EFD_SEMAPHORE (1 << 0)
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK

/* Return file descriptor for generic event */
static inline int eventfd(int count, int flags)
{
	return syscall(__NR_eventfd2, count, flags);
}

#else

#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>

#ifndef TFD_CLOEXEC
#define TFD_CLOEXEC O_CLOEXEC
#endif

#ifndef TFD_NONBLOCK
#define TFD_NONBLOCK O_NONBLOCK
#endif

#ifndef SFD_CLOEXEC
#define SFD_CLOEXEC O_CLOEXEC
#endif

#ifndef SFD_NONBLOCK
#define SFD_NONBLOCK O_NONBLOCK
#endif

#endif

#endif // !__SYSTEM_HPP__
