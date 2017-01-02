#ifndef __EVENTLOOP_HPP__
#define __EVENTLOOP_HPP__

#include <sys/epoll.h>
#include <list>
#include <functional>

typedef std::function<void(int fd, int evt)> EventLoopCb;

class EventLoop {
private:
	struct InternalFd {
		int mFd;
		EventLoopCb mCb;
	};

private:
	int mEpollFd;
	int mStopFd;
	std::list<InternalFd> mFdList;

private:
	int findInternalFd(int fd, std::list<InternalFd>::iterator *internalFd);
	void readStopFd();

public:
	EventLoop();
	virtual ~EventLoop();

	int init();

	int wait(int timeout);
	int abort();

	int addFd(int op, int fd, EventLoopCb cb);
	int modFd(int op, int fd, EventLoopCb cb);
	int delFd(int fd);
};

#endif // !__LOOP_HPP__
