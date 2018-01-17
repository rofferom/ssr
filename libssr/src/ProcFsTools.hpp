#ifndef __PROCSTATPARSER_HPP__
#define __PROCSTATPARSER_HPP__

#define STAT_SIZE 4096*2

namespace pfstools {

struct RawStats {
	int mFd;

	bool mPending;
	uint64_t mTs;
	uint64_t mAcqEnd;
	char mContent[STAT_SIZE];

	RawStats();

	int open(const char *path);
	void close();
};

int findProcess(const char *name, int *outPid);

int findAllProcesses(std::list<int> *outPid);

int readRawStats(RawStats *stats);

int readSystemStats(char *s, SystemMonitor::SystemStats *stats);

int readMeminfoStats(char *s, SystemMonitor::SystemStats *stats);

int readProcessStats(const char *s, SystemMonitor::ProcessStats *stats);

int readThreadStats(const char *s, SystemMonitor::ThreadStats *stats);

} // namespace pfstools

#endif // !__PROCSTATPARSER_HPP__
