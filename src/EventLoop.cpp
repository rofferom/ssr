#include <unistd.h>
#include "System.hpp"
#include "EventLoop.hpp"

#define SIZEOF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

EventLoop::EventLoop()
{
	mEpollFd = -1;
	mStopFd = -1;
}

EventLoop::~EventLoop()
{
	if (mEpollFd != -1)
		close(mEpollFd);

	if (mStopFd != -1)
		close(mStopFd);
}

int EventLoop::init()
{
	struct epoll_event evt;
	int ret;

	// Init epoll fd
	mEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (mEpollFd == -1) {
		ret = -errno;
		printf("epoll_create1() failed : %d(%m)", errno);
		return ret;
	}

	// Init stop fd
	mStopFd = eventfd(0, EFD_CLOEXEC);
	if (mStopFd < 0) {
		ret = -errno;
		printf("eventfd() failed : %d(%m)\n", errno);
		goto clear_epollfd;
	}

	memset(&evt, 0, sizeof(evt));
	evt.events = EPOLLIN;
	evt.data.fd = mStopFd;

	ret = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mStopFd, &evt);
	if (ret < 0) {
		ret = -errno;
		printf("epoll_ctl() failed : %d(%m)\n", errno);
		goto clear_stopfd;
	}

	return 0;

clear_stopfd:
	close(mStopFd);
	mStopFd = -1;
clear_epollfd:
	close(mEpollFd);
	mEpollFd = -1;

	return ret;
}

int EventLoop::wait(int timeout)
{
	std::list<InternalFd>::iterator internalFd;
	struct epoll_event events[8];
	int events_count;
	int ret;

	do {
		ret = epoll_wait(mEpollFd,
				 events, SIZEOF_ARRAY(events),
				 timeout);
	} while (ret == -1 && errno == EINTR);

	if (ret == -1) {
		ret = -errno;
		printf("epoll_wait() failed : %d(%m)\n", ret);
		return ret;
	}

	events_count = ret;
	for (int i = 0; i < events_count; i++) {
		if (events[i].data.fd == mStopFd) {
			readStopFd();
			continue;
		}

		ret = findInternalFd(events[i].data.fd, &internalFd);
		if (ret < 0) {
			printf("fd %d doesn't exist\n", events[i].data.fd);
			continue;
		}

		internalFd->mCb(events[i].data.fd, events[i].events);
	}

	return 0;
}

int EventLoop::abort()
{
	int64_t stop = 1;
	int ret;

	ret = write(mStopFd, &stop, sizeof(stop));
	if (ret < 0)
		printf("write() failed : %d(%m)\n", errno);

	return 0;
}

void EventLoop::readStopFd()
{
	int64_t stop;
	int ret;

	do {
		ret = read(mStopFd, &stop, sizeof(stop));
	} while (ret == -1 && errno == EINTR);

	if (ret < 0)
		printf("read() failed : %d(%m)\n", errno);
}

int EventLoop::findInternalFd(int fd,
		std::list<InternalFd>::iterator *internalFd)
{
	for (auto i = mFdList.begin(); i != mFdList.end(); i++) {
		if (i->mFd == fd) {
			*internalFd = i;

			return 0;
		}
	}

	return -ENOENT;
}

int EventLoop::addFd(int op, int fd, EventLoopCb cb)
{
	std::list<InternalFd>::iterator internalFd;
	struct epoll_event evt;
	int ret;

	if (!cb)
		return -EINVAL;

	// Check fd isn't already registered
	ret = findInternalFd(fd, &internalFd);
	if (ret == 0) {
		printf("fd %d already exists\n", fd);
		return -EPERM;
	}

	// Register in epoll fd
	memset(&evt, 0, sizeof(evt));
	evt.events = op;
	evt.data.fd = fd;

	ret = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &evt);
	if (ret == -1) {
		ret = -errno;
		printf("epoll_ctl() failed : %d(%m)\n", errno);
		return ret;
	}

	// Register fd
	mFdList.push_back( { fd, cb } );

	return 0;
}

int EventLoop::modFd(int op, int fd, EventLoopCb cb)
{
	std::list<InternalFd>::iterator internalFd;
	struct epoll_event evt;
	int ret;

	if (!cb)
		return -EINVAL;

	// Check fd isn't already registered
	ret = findInternalFd(fd, &internalFd);
	if (ret < 0) {
		printf("fd %d doesn't exists\n", fd);
		return -EPERM;
	}

	// Update in epoll fd
	memset(&evt, 0, sizeof(evt));
	evt.events = op;
	evt.data.fd = fd;

	ret = epoll_ctl(mEpollFd, EPOLL_CTL_MOD, fd, &evt);
	if (ret == -1) {
		ret = -errno;
		printf("epoll_ctl() failed : %d(%m)\n", errno);
		return ret;
	}

	return 0;
}

int EventLoop::delFd(int fd)
{
	std::list<InternalFd>::iterator internalFd;
	int ret;

	// Check fd isn't already registered
	ret = findInternalFd(fd, &internalFd);
	if (ret < 0) {
		printf("fd %d doesn't exists\n", fd);
		return -EPERM;
	}

	// Remove from epoll fd
	ret = epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, NULL);
	if (ret == -1) {
		ret = -errno;
		printf("epoll_ctl() failed : %d(%m)\n", errno);
		return ret;
	}

	// Register fd
	mFdList.erase(internalFd);

	return 0;
}
