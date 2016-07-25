#!/usr/bin/python3
# -*- coding: utf-8 -*-

import os
import sys
import argparse
import jinja2
from ssr.parser import Parser

DEFAULT_STRUCTNAME = 'processstats'
DEFAULT_SAMPLENAME = 'cpuload'

class Helpers:
	@staticmethod
	def computeLoad(ticks, duration, sysconfig):
		cpuload = float(ticks) / float(sysconfig.clkTck)
		cpuload /= float(duration) / 1000000000.0
		cpuload *= 100

		return cpuload

	@staticmethod
	def computeTicks(prevSample, curSample, keyList):
		ticks = 0
		for key in keyList:
			ticks += curSample[key]

		for key in keyList:
			ticks -= prevSample[key]

		return ticks

	@staticmethod
	def computeCpuLoad(prevSample, curSample, sysconfig, keyList):
		ticks = Helpers.computeTicks(prevSample, curSample, keyList)
		duration = curSample['ts'] - prevSample['ts']

		return Helpers.computeLoad(ticks, duration, sysconfig)

class ProgParamsHandler:
	def handleSample(self, data):
		print('File recorded with params %s' % data['params'])

class SystemConfigHandler:
	def __init__(self):
		self.clkTck   = None
		self.pagesize = None

	def handleSample(self, data):
		self.clkTck   = data['clktck']
		self.pagesize = data['pagesize']

		print('Got system config : clktck=%d, pagesize=%d' % (self.clkTck, self.pagesize))

class AcqDurationHandler:
	def __init__(self):
		self.totalAcqTime = 0
		self.sampleCount = 0

	def handleSample(self, sample):
		acqTime = (sample['end'] - sample['start']) / 1000
#		print('ts %u - Acquisition took %6u us' % (sample['start'], acqTime))

		self.totalAcqTime += acqTime
		self.sampleCount += 1

	def printStats(self):
		average = self.totalAcqTime / self.sampleCount
		print('Average acquisition time : %d us' % average)

class SystemStatsHandler:
	SAMPLENAME = 'systemstats'
	def __init__(self, args, sysconfig, samples):

		self.args = args
		self.sysconfig = sysconfig
		self.samples = samples

	def handleSample(self, sample):
		ts = sample['ts']

		lastSample = self.samples.getLastSample(self.SAMPLENAME)
		if not lastSample:
			# At least two samples are required. Save the first one without
			# any extra processing
			self.samples.saveSample(self.SAMPLENAME, sample)
			return

		# Record samples for display
		totalKeyList = ['utime', 'nice', 'stime', 'irq', 'softirq', 'idle', 'iowait']
		totalTicks = Helpers.computeTicks(lastSample, sample, totalKeyList)

		loadKeyList = ['utime', 'nice', 'stime', 'irq', 'softirq']
		loadTicks = Helpers.computeTicks(lastSample, sample, loadKeyList)

		idleKeyList = ['idle', 'iowait']
		idleTicks = Helpers.computeTicks(lastSample, sample, idleKeyList)

		idle = (float(idleTicks) / float(totalTicks)) * 100
		self.samples.addSample('idle', ts, idle)

		load = (float(loadTicks) / float(totalTicks)) * 100
		self.samples.addSample('load', ts, load)

		# Save sample to get it at next sample
		self.samples.saveSample(self.SAMPLENAME, sample)

class ProcStatsHandler:
	def __init__(self, args, sysconfig, samples):
		self.args = args
		self.sysconfig = sysconfig
		self.samples = samples

		self.handlers = {
			'cpuload': self.handleCpuload,
			'vsize':   self.handleVSize,
			'rss':     self.handleRss,
		}

	def handleCpuload(self, ts, sampleName, lastSample, sample):
		keyList = ['utime', 'stime']
		cpuload = Helpers.computeCpuLoad(lastSample, sample, self.sysconfig, keyList)
		self.samples.addSample(sampleName, ts, cpuload)

	def handleVSize(self, ts, sampleName, lastSample, sample):
		vsize = sample['vsize'] / 1024
		self.samples.addSample(sampleName, ts, vsize)

	def handleRss(self, ts, sampleName, lastSample, sample):
		rss = sample['rss'] * self.sysconfig.pagesize / 1024
		self.samples.addSample(sampleName, ts, rss)

	def handleSample(self, sample):
		ts = sample['ts']
		sampleName = '%d-%s' % (sample['pid'], sample['name'])

		lastSample = self.samples.getLastSample(sampleName)
		if not lastSample:
			# At least two samples are required. Save the first one without
			# any extra processing
			self.samples.saveSample(sampleName, sample)
			return

		# Record sample requested by user
		try:
			handler = self.handlers[self.args.sample]
		except KeyError:
			print('Handled sample %s' % self.args.sample)

		handler(ts, sampleName, lastSample, sample)

		# Save sample to get it at next sample
		self.samples.saveSample(sampleName, sample)

class ParserEvtHandler:
	def __init__(self, samples):
		self.samples = samples
		self.handlers = {}

	def __call__(self, name, data):
		try:
			handler = self.handlers[name]
		except KeyError:
			# No handler, ignore sample
			return

		handler.handleSample(data)

	def registerSectionHandler(self, name, handler):
		try:
			h = self.handlers[name]
			raise Exception('Handler for section \'%s\' already exists' % name)
		except KeyError:
			self.handlers[name] = handler

class SampleCollection:
	def __init__(self):
		self.samples = {}
		self.lastSamples = {}
		self.columns = ['ts']

	def addSample(self, name, ts, value):
		if not name in self.columns:
			self.columns.append(name)

		try:
			tsEntry = self.samples[ts]
		except KeyError:
			tsEntry = {}
			self.samples[ts] = tsEntry

		tsEntry[name] = value

	def saveSample(self, name, rawValue):
		self.lastSamples[name] = rawValue

	def getLastSample(self, name):
		try:
			return self.lastSamples[name]
		except KeyError:
			return None

	def getColumns(self):
		return self.columns

	def getColumnsToDisplay(self):
		# Build columns to display.
		# Timestamp column is removed to force its position at the very
		# beginning of the list.
		columnsToDisplay = list(self.columns)
		columnsToDisplay.remove('ts')

		return columnsToDisplay

	def getSamples(self):
		ret = []

		for ts in sorted(self.samples):
			row = [ts]

			tsEntry = self.samples[ts]
			for name in self.getColumnsToDisplay():
				try:
					row.append(tsEntry[name])
				except KeyError:
					row.append(None)

			ret.append(row)

		return ret

	def printStats(self):
		columns = self.getColumnsToDisplay()

		print('%d columns' % len(columns))
		for name in columns:
			print('\t%s' % name)

def parseArgs():
	parser = argparse.ArgumentParser(description='Parse sysstats log file.')
	parser.add_argument('-i', '--input', required=True, help='File to parse')
	parser.add_argument('-o', '--output', help='File to generate')
	parser.add_argument('-S', '--struct', default=DEFAULT_STRUCTNAME, help='Struct name to use')
	parser.add_argument('-s', '--sample', default=DEFAULT_SAMPLENAME, help='Sample name to use')
	parser.add_argument('-H', '--header', action='store_true', help='Display input header')
	parser.add_argument('process', nargs='*', help='Process list to use')
	return (parser, parser.parse_args())

if __name__ == '__main__':
	handlers = {
		'systemstats':  lambda args, sysconfigHandler, outSamples: SystemStatsHandler(args, sysconfigHandler, outSamples),
		'processstats': lambda args, sysconfigHandler, outSamples: ProcStatsHandler(args, sysconfigHandler, outSamples),
		'threadstats':  lambda args, sysconfigHandler, outSamples: ProcStatsHandler(args, sysconfigHandler, outSamples),
	}

	(argParser, args) = parseArgs()

	parser = Parser()
	parser.open(args.input)

	if args.header:
		parser.printHeader()
		sys.exit(1)
	elif not args.output:
		argParser.print_help()
		sys.exit(1)

	# Create generic data
	samples = SampleCollection()
	evtHandler = ParserEvtHandler(samples)

	# Create generic section handlers
	progParamsHandler = ProgParamsHandler()
	evtHandler.registerSectionHandler('programparameters', progParamsHandler)

	sysconfigHandler = SystemConfigHandler()
	evtHandler.registerSectionHandler('systemconfig', sysconfigHandler)

	acqDurationHandler = AcqDurationHandler()
	evtHandler.registerSectionHandler('acqduration', acqDurationHandler)

	# Create user-required handler
	try:
		handlerCreateCb = handlers[args.struct]
	except KeyError:
		print('Unhandled struct %s' % args.struct)
		argParser.print_help()
		sys.exit(1)

	evtHandler.registerSectionHandler(args.struct, handlerCreateCb(args, sysconfigHandler, samples))

	# Parse input file
	parser.parse(evtHandler)

	# Create output file
	sourcePath = os.path.dirname(os.path.abspath(__file__))
	templateFile = open('%s/template.html' % sourcePath, 'r')
	template = jinja2.Template(templateFile.read())

	html = template.render(sampleName=args.sample, columns=samples.getColumns(), samples=samples.getSamples())

	# Write output file
	out = open(args.output, 'w')
	out.write(html)

	# Display general stats
	acqDurationHandler.printStats()
	samples.printStats()

