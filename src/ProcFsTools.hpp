#ifndef __PROCSTATPARSER_HPP__
#define __PROCSTATPARSER_HPP__

#include "SystemMonitor.hpp"

namespace pfstools {

int findProcess(const char *name, int *outPid);

int readProcessStats(int fd, SystemMonitor::ProcessStats *stats);

int readThreadStats(int fd, SystemMonitor::ThreadStats *stats);

} // namespace pfstools

#endif // !__PROCSTATPARSER_HPP__
