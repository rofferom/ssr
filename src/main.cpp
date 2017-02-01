#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>

#include <string>

#include <ssr.hpp>

static struct Context {
	bool stop;
	EventLoop loop;
	Timer durationTimer;

	Context()
	{
		stop = false;
	}
} ctx;

struct Params {
	bool help;
	bool verbose;
	std::string output;
	int period;
	int duration;
	int recordThreads;

	Params()
	{
		help = false;
		verbose = false;
		period = 1;
		duration = -1;
		recordThreads = true;
	}
};

struct ProgramParameters {
	char mParams[1024];
};

static int readDecimalParam(int *out_v, const char *name)
{
	char *end;
	long int v;

	errno = 0;
	v = strtol(optarg, &end, 10);
	if (errno != 0) {
		int ret = -errno;
		fprintf(stderr, "Unable to parse '%s' %s : %d(%m)\n",
			name, optarg, errno);
		return ret;
	} else if (*end != '\0') {
		fprintf(stderr, "'%s' arg '%s' is not decimal\n",
			name, optarg);
		return -EINVAL;
	} else if (v <= 0) {
		fprintf(stderr, "'%s' arg '%s' is negative or null\n",
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
		{ "verbose",         optional_argument, 0, 'v' },
		{ "period",          optional_argument, 0, 'p' },
		{ "duration",        optional_argument, 0, 'd' },
		{ "output",          required_argument, 0, 'o' },
		{ "disable-threads", optional_argument, &params->recordThreads, 0 },
		{ 0, 0, 0, 0 }
	};

	while (true) {
		value = getopt_long(argc, argv, "hvo:p:d:", argsOptions, &optionIndex);
		if (value == -1 || value == '?')
			break;

		switch (value) {
		case 'h':
			params->help = true;
			break;

		case 'v':
			params->verbose = true;
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
	printf("  %-20s %s\n", "-v, --verbose", "add extra logs");
	printf("  %-20s %s\n", "-p, --period", "sample acquisition period (seconds). Default : 1");
	printf("  %-20s %s\n", "-d, --duration", "acquisition duration (seconds). Default : infinite");
	printf("  %-20s %s\n", "-o, --output", "output record file");
	printf("  %-20s %s\n", "--disable-threads", "disable threads recording");
}

static void sighandler(int s)
{
	LOGN("stop");
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
		LOGE("record() failed : %d(%s)", -ret, strerror(-ret));
}

static void processStatsCb(
		const SystemMonitor::ProcessStats &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		LOGE("record() failed : %d(%s)", -ret, strerror(-ret));
}

static void threadStatsCb(
		const SystemMonitor::ThreadStats &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		LOGE("record() failed : %d(%s)", -ret, strerror(-ret));
}

static void resultsBeginCb(
		const SystemMonitor::AcquisitionDuration &stats,
		void *userdata)
{
	SystemRecorder *recorder = (SystemRecorder *) userdata;
	int ret;

	ret = recorder->record(stats);
	if (ret < 0)
		LOGE("record() failed : %d(%s)", -ret, strerror(-ret));
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
		LOGE("No output file specified");
		printUsage(argc, argv);
		return 1;
	}

	if (params.verbose)
		logSetLevel(LOG_DEBUG);

	if (optind == argc) {
		LOGI("Record all processes\n");
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
		LOGE("Fail to initialize struct descriptions");
		return 1;
	}

	// Create recorder
	recorder = new SystemRecorder();
	if (!recorder)
		goto error;

	if (!getOutputPath(params.output, &outputPath)) {
		LOGE("Can find a new output file path");
		return 1;
	}

	LOGI("Recording in file %s", outputPath.c_str());
	ret = recorder->open(outputPath.c_str());
	if (ret < 0)
		goto error;

	// Create monitor
	cb.mSystemStats = systemStatsCb;
	cb.mProcessStats = processStatsCb;
	cb.mThreadStats = threadStatsCb;
	cb.mResultsBegin = resultsBeginCb;
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
				LOGE("addProcessFailed() : %d(%s)",
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
		LOGE("readSystemConfig() failed : %d(%s)",
		     -ret, strerror(-ret));
		goto error;
	}

	recorder->record(systemConfig);

	// Create duration timer
	if (params.duration > 0) {
		struct timespec duration;

		auto cb = [] () {
			ctx.stop = true;
		};

		duration.tv_sec = params.duration;
		duration.tv_nsec = 0;

		ret = ctx.durationTimer.set(&ctx.loop, duration, cb);
		if (ret < 0) {
			LOGE("Time.setPeriodic() failed");
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

	ctx.durationTimer.clear();

	return 0;

error:
	delete mon;
	delete recorder;

	ctx.durationTimer.clear();

	return 1;
}
