#!/usr/bin/python3
# -*- coding: utf-8 -*-

import os
import sys
import argparse
import jinja2
from sysstatsrec.parser import Parser

DEFAULT_STRUCTNAME = 'processstats'
DEFAULT_SAMPLENAME = 'cpuload'

class SysstatsReader:
	def __init__(self, processList, sampleName, structName):
		if len(processList) == 0:
			self.allProcess = True
			self.processList = []
		else:
			self.allProcess = False
			self.processList = processList

		self.sampleName = sampleName
		self.structName = structName
		self.samples = {}

	def __call__(self, name, data):
		if name != self.structName:
			return
		elif not self.allProcess and data['name'] not in self.processList:
			return

		if self.allProcess and data['name'] not in self.processList:
			self.processList.append(data['name'])

		ts = data['ts']
		try:
			sample = self.samples[ts]
		except KeyError:
			sample = { 'ts': ts }
			self.samples[ts] = sample

		sample[data['name']] = data[self.sampleName]

	def getProcessList(self):
		return self.processList

	def getSamples(self):
		ret = []

		for ts in sorted(self.samples):
			row = [ts]

			sample = self.samples[ts]
			for name in self.processList:
				try:
					row.append(sample[name])
				except:
					row.append(0)

			ret.append(row)

		return ret

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
	(argParser, args) = parseArgs()

	parser = Parser()
	parser.open(args.input)

	if args.header:
		parser.printHeader()
	else:
		if not args.output:
			argParser.print_help()
			sys.exit(1)

		# Parse record file
		reader = SysstatsReader(args.process, args.sample, args.struct)
		parser.parse(reader)

		# Generate html
		sourcePath = os.path.dirname(os.path.abspath(__file__))
		templateFile = open('%s/template.html' % sourcePath, 'r')
		template = jinja2.Template(templateFile.read())

		columns = ['timestamp'] + reader.getProcessList()
		html = template.render(sampleName=args.sample, columns=columns, samples=reader.getSamples())

		# Write output file
		out = open(args.output, 'w')
		out.write(html)

