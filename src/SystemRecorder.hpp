#ifndef __SYSTEM_RECORDER_HPP__
#define __SYSTEM_RECORDER_HPP__

#include "SystemMonitor.hpp"

class SystemRecorder {
public:
	virtual ~SystemRecorder() {}

	virtual int open(const char *path) = 0;
	virtual int close() = 0;

	virtual int flush() = 0;

	virtual int record(const SystemMonitor::SystemStats &stats) = 0;
	virtual int record(const SystemMonitor::ProcessStats &stats) = 0;

	static SystemRecorder *create();
};

#endif // !__SYSTEM_RECORDER_HPP__
