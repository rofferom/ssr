#ifndef __SSR_HPP__
#define __SSR_HPP__

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include <list>
#include <map>
#include <functional>

#include <ssr/Log.hpp>

#include <ssr/EventLoop.hpp>
#include <ssr/Timer.hpp>

#include <ssr/FileSink.hpp>
#include <ssr/StructDescTypes.hpp>
#include <ssr/StructDesc.hpp>
#include <ssr/StructDescRegistry.hpp>

#include <ssr/SystemMonitor.hpp>
#include <ssr/SystemRecorder.hpp>

#define SIZEOF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

#endif // !__SSR_HPP__
