#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>

#include <string>

#include "System.hpp"
#include "SystemRecorder.hpp"
#include "SystemMonitor.hpp"
#include "StructDescRegistry.hpp"
#include "EventLoop.hpp"

#define SIZEOF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

static struct Context {
	bool stop;
	EventLoop loop;
	int durationTimer;

	Context()
	{
		stop = false;
		durationTimer = -1;
	}
} ctx;

struct Params {
	bool help;
	std::string output;
	int period;
	int duration;
	int recordThreads;

	Params()
	{
		help = false;
		period = 1;
		duration = -1;
		recordThreads = true;
	}
};

static int readDecimalParam(int *out_v, const char *name)
{
	char *end;
	long int v;

	errno = 0;
	v = strtol(optarg, &end, 10);
	if (errno != 0) {
		int ret = -errno;
		printf("Unable to parse '%s' %s : %d(%m)\n",
		       name, optarg, errno);
		return ret;
	} else if (*end != '\0') {
		printf("'%s' arg '%s' is not decimal\n",
		        name, optarg);
		return -EINVAL;
	} else if (v <= 0) {
		printf("'%s' arg '%s' is negative or null\n",
		       name, optarg);
		return -EINVAL;
	}

	*out_v = v;

	return 0;
}

int parseArgs(int argc, char *argv[], Params *params)
{
	int optionIndex = 0;
	int value;
	int ret;

	const struct option argsOptions[] = {
		{ "help"  ,          optional_argument, 0, 'h' },
		{ "period",          optional_argument, 0, 'p' },
		{ "duration",        optional_argument, 0, 'd' },
		{ "output",          required_argument, 0, 'o' },
		{ "disable-threads", optional_argument, &params->recordThreads, 0 },
		{ 0, 0, 0, 0 }
	};

	while (true) {
		value = getopt_long(argc, argv, "ho:p:d:", argsOptions, &optionIndex);
		if (value == -1 || value == '?')
			break;

		switch (value) {
		case 'h':
			params->help = true;
			break;

		case 'o':
			params->output = optarg;
			break;

		case 'd':
			ret = readDecimalParam(&params->duration, "duration");
			if (ret < 0)
				return ret;
			break;

		case 'p':
			ret = readDecimalParam(&params->period, "period");
			if (ret < 0)
				return ret;
			break;

		default:
			break;
		}
	}

	return 0;
}

void printUsage(int argc, char *argv[])
{
	printf("Usage  %s [-h] [-p PERIOD] -o OUTPUT [process...]\n",
	       argv[0]);

	printf("\n");

	printf("positional arguments:\n");
	printf("  %-20s %s\n", "process", "Process name to monitor");

	printf("\n");

	printf("optional arguments:\n");
	printf("  %-20s %s\n", "-h, --help", "show this help message and exit");
	printf("  %-20s %s\n", "-p, --period", "sample acquisition period (seconds). Default : 1");
	printf("  %-20s %s\n", "-d, --duration", "acquisition duration (seconds). Default : infinite");
	printf("  %-20s %s\n", "-o, --output", "output record file");
	printf("  %-20s %s\n", "--disable-threads", "disable threads recording");
}

static void sighandler(int s)
{
	printf("stop\n");
	ctx.stop = true;
	ctx.loop.abort();
}

static int initStructDescs()
{
	StructDesc *desc;
	const char *type;
	int ret;

	// ProgramParameters
	type = "programparameters";

	ret = StructDescRegistry::registerType<ProgramParameters>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_STRING(desc, ProgramParameters, mParams, "params");
	RETURN_IF_REGISTER_FAILED(ret);

	// Initialize other available StructDesc
	ret = SystemMonitor::initStructDescs();
	if (ret < 0)
		return 0;

	return 0;
}

static void systemStatsCb(
		const SystemMonitor::SystemStats &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		printf("record() failed : %d(%s)\n", -ret, strerror(-ret));
}

static void processStatsCb(
		const SystemMonitor::ProcessStats &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		printf("record() failed : %d(%s)\n", -ret, strerror(-ret));
}

static void threadStatsCb(
		const SystemMonitor::ThreadStats &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		printf("record() failed : %d(%s)\n", -ret, strerror(-ret));
}

static void acquisitionDurationCb(
		const SystemMonitor::AcquisitionDuration &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		printf("record() failed : %d(%s)\n", -ret, strerror(-ret));
}

static void buildProgParameters(int argc, char *argv[], ProgramParameters *out)
{
	std::string s;

	s = argv[0];

	for (int i = 1; i < argc; i++)
		s += std::string(" ") + std::string(argv[i]);

	snprintf(out->mParams, sizeof(out->mParams), "%s", s.c_str());
}

static bool getOutputPath(const std::string &basePath, std::string *outPath)
{
	struct stat st;
	char path[128];
	int ret;
	bool found = false;

	for (int i = 0; i < 100; i++) {
		snprintf(path, sizeof(path), "%s-%02d.log",
			 basePath.c_str(), i);

		ret = stat(path, &st);
		if (ret == -1 && errno == ENOENT) {
			found = true;
			*outPath = path;
			break;
		}
	}

	return found;
}

int main(int argc, char *argv[])
{
	struct itimerspec timer;
	Params params;
	std::string outputPath;
	ProgramParameters progParameters;
	SystemMonitor::SystemConfig systemConfig;
	SystemMonitor::Callbacks cb;
	SystemMonitor *mon = nullptr;
	SystemMonitor::Config monConfig;
	SystemRecorder *recorder = nullptr;
	bool recordAllProcesses;
	int ret;

	signal(SIGINT, sighandler);

	// Parse parameters
	if (argc == 1) {
		printUsage(argc, argv);
		return 0;
	}

	ret = parseArgs(argc, argv, &params);
	if (ret < 0) {
		printUsage(argc, argv);
		return 1;
	} else if (params.output.empty()) {
		printf("No output file specified\n\n");
		printUsage(argc, argv);
		return 1;
	}

	if (optind == argc) {
		printf("Record all processes\n");
		recordAllProcesses = true;
	} else {
		recordAllProcesses = false;
	}

	// Init loop
	ret = ctx.loop.init();
	if (ret < 0)
		return 1;

	// Init StructDesc
	ret = initStructDescs();
	if (ret < 0) {
		printf("Fail to initialize struct descriptions\n");
		return 1;
	}

	// Create recorder
	recorder = new SystemRecorder();
	if (!recorder)
		goto error;

	if (!getOutputPath(params.output, &outputPath)) {
		printf("Can find a new output file path\n");
		return 1;
	}

	printf("Recording in file %s\n", outputPath.c_str());
	ret = recorder->open(outputPath.c_str());
	if (ret < 0)
		goto error;

	// Create monitor
	cb.mSystemStats = systemStatsCb;
	cb.mProcessStats = processStatsCb;
	cb.mThreadStats = threadStatsCb;
	cb.mAcquisitionDuration = acquisitionDurationCb;
	cb.mUserdata = recorder;

	monConfig.mRecordThreads = params.recordThreads;
	monConfig.mAcqPeriod = params.period;

	ret = SystemMonitor::create(&ctx.loop, monConfig, cb, &mon);
	if (ret < 0)
		goto error;

	if (!recordAllProcesses) {
		for (int i = optind; i < argc; i++) {
			ret = mon->addProcess(argv[i]);
			if (ret < 0) {
				printf("addProcessFailed() : %d(%s)\n",
				       -ret, strerror(-ret));
				goto error;
			}

		}
	}

	mon->loadProcesses();

	// Write program parameters
	buildProgParameters(argc, argv, &progParameters);
	recorder->record(progParameters);

	// Write system config
	ret = mon->readSystemConfig(&systemConfig);
	if (ret < 0) {
		printf("readSystemConfig() failed : %d(%m)\n", errno);
		goto error;
	}

	recorder->record(systemConfig);

	// Create duration timer
	if (params.duration > 0) {
		ret = timerfd_create(CLOCK_MONOTONIC, EFD_CLOEXEC);
		if (ret < 0) {
			printf("timerfd_create() failed : %d(%m)\n", errno);
			goto error;
		}

		ctx.durationTimer = ret;

		// Start timer
		timer.it_value.tv_sec = params.duration;
		timer.it_value.tv_nsec = 0;

		timer.it_interval.tv_sec = 0;
		timer.it_interval.tv_nsec = 0;

		ret = timerfd_settime(ctx.durationTimer, 0, &timer, NULL);
		if (ret < 0) {
			printf("timerfd_settime() failed : %d(%m)\n", errno);
			goto error;
		}

		ret = ctx.loop.addFd(EPOLLIN, ctx.durationTimer,
			[] (int fd, int evt) {
				ctx.stop = true;
			}
		);
		if (ret < 0) {
			printf("addFd() failed\n");
			goto error;
		}
	}

	// Start poll
	while (!ctx.stop) {
		ret = ctx.loop.wait(-1);
		if (ret < 0)
			break;
	}

	recorder->close();

	delete mon;
	delete recorder;


	if (ctx.durationTimer != -1)
		close(ctx.durationTimer);

	return 0;

error:
	delete mon;
	delete recorder;


	if (ctx.durationTimer != -1)
		close(ctx.durationTimer);

	return 1;
}
