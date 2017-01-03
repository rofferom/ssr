#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "EventLoop.hpp"
#include "System.hpp"
#include "Log.hpp"
#include "Timer.hpp"

Timer::Timer()
{
	mLoop = nullptr;
	mFd = -1;
}

Timer::~Timer()
{
	clear();
}

int Timer::setInternal(EventLoop *loop, const struct itimerspec &ts, TimerCb cb)
{
	int ret;

	if (!loop)
		return -EINVAL;

	if (mFd != -1)
		return -EPERM;

	// Setup timer
	mFd = timerfd_create(CLOCK_MONOTONIC, EFD_CLOEXEC);
	if (mFd == -1) {
		ret = -errno;
		LOG_ERRNO("timerfd_create");
		return ret;
	}

	ret = timerfd_settime(mFd, 0, &ts, NULL);
	if (ret < 0) {
		LOG_ERRNO("timerfd_settime");
		goto clear_timer;
	}

	// Register timer in loop
	ret = loop->addFd(EPOLLIN, mFd,
		[this, cb] (int fd, int evt) {
			uint64_t expirations;
			int ret;

			ret = read(mFd, &expirations, sizeof(expirations));
			if (ret < 0)
				LOG_ERRNO("read");

			cb();
		});
	if (ret < 0) {
		LOGE("EventLoop::addFd() failed : %d(%s)",
		     -ret, strerror(-ret));
		goto clear_timer;
	}

	mLoop = loop;

	return 0;

clear_timer:
	close(mFd);
	mFd = -1;

	return ret;
}

int Timer::set(EventLoop *loop, const struct timespec &ts, TimerCb cb)
{
	struct itimerspec timerConf;

	timerConf.it_value = ts;

	timerConf.it_interval.tv_sec = 0;
	timerConf.it_interval.tv_nsec = 0;

	return setInternal(loop, timerConf, cb);
}

int Timer::setPeriodic(EventLoop *loop, const struct timespec &ts, TimerCb cb)
{
	struct itimerspec timerConf;

	timerConf.it_value = ts;
	timerConf.it_interval = ts;

	return setInternal(loop, timerConf, cb);
}

int Timer::clear()
{
	int ret;

	if (mFd == -1)
		return -EPERM;

	ret = mLoop->delFd(mFd);
	if (ret < 0) {
		LOGE("EventLoop::delFd() failed : %d(%s)",
		     -ret, strerror(-ret));
	}

	close(mFd);
	mFd = -1;

	mLoop = nullptr;

	return 0;
}
