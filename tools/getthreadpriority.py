#!/usr/bin/python3
# -*- coding: utf-8 -*-

import argparse
from ssr.parser import Parser

_POLICY_TO_STR = {
	0: "SCHED_OTHER",
	1: "SCHED_FIFO",
	2: "SCHED_RR",
	3: "SCHED_BATCH",
	5: "SCHED_IDLE"
}
class ParserEvtHandler:
	def __init__(self):
		self.handlers = {
			'processstats': self._processStatsCb,
			'threadstats': self._threadStatsCb
		}

		self.processSet = {}
		self.threadSet = {}

	def __call__(self, name, data):
		try:
			self.handlers[name](data)
		except KeyError:
			pass

	def _handleStats(self, data, keyname, target):
		key = data[keyname]
		if key in target:
			return

		target[key] = data		

	def _processStatsCb(self, data):
		self._handleStats(data, "pid", self.processSet)

	def _threadStatsCb(self, data):
		self._handleStats(data, "tid", self.threadSet)

	def getProcessList(self):
		return self.processSet

	def getThreadList(self):
		return self.threadSet

def displayResults(stats, keyname):
	print("%s\t%20s\tpolicy\tpriority\tnice\trtpriority" % (keyname, "name"))

	for key in sorted(stats.keys()):
		entry = stats[key]

		policyStr = _POLICY_TO_STR[entry["policy"]]
		print("%d\t%20s\t%s\t%d\t%d\t%d" % \
			(entry[keyname], entry["name"], policyStr,
			 entry["priority"], entry["nice"], entry["rtpriority"]))


def parseArgs():
	parser = argparse.ArgumentParser(description='Display priority contained in a sysstats log file.')
	parser.add_argument('input', help='File to proces')
	return (parser, parser.parse_args())

if __name__ == '__main__':
	(argParser, args) = parseArgs()

	parser = Parser()
	parser.open(args.input)

	# Parse input file
	evtHandler = ParserEvtHandler()
	parser.parse(evtHandler)

	# Display stats
	displayResults(evtHandler.getProcessList(), "pid")
	displayResults(evtHandler.getThreadList(), "tid")

