#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define SIZEOF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

static void setThreadPriority()
{
	pthread_t id;
	struct sched_param param;
	int ret;

	id = pthread_self();

	param.__sched_priority = 99;

	ret = pthread_setschedparam(id, SCHED_FIFO, &param);
	if (ret != 0) {
		fprintf(stderr, "pthread_setschedparam() failed : %d(%s)\n",
			ret, strerror(ret));
	}
}

static int getTimeUs(uint64_t *ns)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		ret = -errno;
		printf("clock_gettime() failed : %d(%m)", errno);
		return ret;
	}

	*ns = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;

	return 0;
}

static int forceCpuLoadSlice(int cpuLoad, int durationUs)
{
	uint64_t workDuration;
	uint64_t begin;
	uint64_t end;
	uint64_t current;
	int i;

	getTimeUs(&begin);

	workDuration = (cpuLoad * durationUs) / 100;
	end = begin + workDuration;

	do {
		for (i = 0; i < 1000; i++);
			sqrt(durationUs);
		getTimeUs(&current);
	} while (current < end);

	usleep(durationUs - workDuration);

	return 0;
}

static int forceCpuLoad(int cpuLoad, int duration)
{
	const int sliceDuration = 1000000;
	int loopCount;
	int i;

	printf("Force CPU load : %d%% during %d seconds\n", cpuLoad, duration);

	loopCount = (duration * 1000000) / sliceDuration;

	for (i = 0; i < loopCount; i++)
		forceCpuLoadSlice(cpuLoad, sliceDuration);

	return 0;
}

int testCase1(void)
{
	struct schedcfg *schedcfg;
	int period = 20;
	int ret;

	forceCpuLoad(5, period);
	forceCpuLoad(10, period);
	forceCpuLoad(15, period);
	forceCpuLoad(20, period);

	forceCpuLoad(25, period);

	forceCpuLoad(20, period);
	forceCpuLoad(15, period);
	forceCpuLoad(10, period);
	forceCpuLoad(5, period);


	return 0;
}

static void *thread_entry(void *arg)
{
	int load = *((int *) arg);

	setThreadPriority();
	forceCpuLoad(load, 120);

	return NULL;
}

int testCase2(void)
{
	const int threadsLoad[] = { 5, 10, 15, 20 };

	const int threadCount = SIZEOF_ARRAY(threadsLoad);
	pthread_t threads[threadCount];
	int i;

	for (i = 0; i < threadCount; i++) {
		pthread_create(&threads[i],
			       NULL,
			       thread_entry,
			       (void *) &threadsLoad[i]);
	}

	for (i = 0; i < threadCount; i++)
		pthread_join(threads[i], NULL);

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Missing args\n");
		return 1;
	}

	int testcase = atoi(argv[1]);
	switch (testcase) {
	case 1:
		testCase1();
		break;

	case 2:
		testCase2();
		break;

	default:
		printf("Unknown test case %d\n", testcase);
		return 1;
	}

	return 0;
}
