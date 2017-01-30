#ifndef __TIMER_HPP__
#define __TIMER_HPP__

class EventLoop;

typedef std::function<void()> TimerCb;

class Timer {
private:
	EventLoop *mLoop;
	int mFd;

private:
	int setInternal(EventLoop *loop, const struct itimerspec &ts, TimerCb cb);

public:
	Timer();
	virtual ~Timer();

	int set(EventLoop *loop, const struct timespec &ts, TimerCb cb);
	int setPeriodic(EventLoop *loop, const struct timespec &ts, TimerCb cb);
	int clear();
};

#endif // !__TIMER_HPP__
