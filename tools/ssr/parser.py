#!/usr/bin/python3
# -*- coding: utf-8 -*-

import sys
import struct

class EOFException(Exception):
	pass

_VALUE_TYPE_U8 = 0
_VALUE_TYPE_I8 = 1
_VALUE_TYPE_U16 = 2
_VALUE_TYPE_I16 = 3
_VALUE_TYPE_U32 = 4
_VALUE_TYPE_I32 = 5
_VALUE_TYPE_U64 = 6
_VALUE_TYPE_I64 = 7
_VALUE_TYPE_STR = 8
_VALUE_TYPE_INVALID = 9

valueTypeToStr = {
	_VALUE_TYPE_U8: "U8",
	_VALUE_TYPE_I8: "I8",
	_VALUE_TYPE_U16: "U16",
	_VALUE_TYPE_I16: "I16",
	_VALUE_TYPE_U32: "U32",
	_VALUE_TYPE_I32: "I32",
	_VALUE_TYPE_U64: "U64",
	_VALUE_TYPE_I64: "I64",
	_VALUE_TYPE_STR: "STR",
	_VALUE_TYPE_INVALID: "???"
}

def readU8(f):
	b = f.read(1)
	if len(b) == 0:
		raise EOFException

	(v, ) = struct.unpack('!B', b)
	return v

def readI8(f):
	b = f.read(1)
	if len(b) == 0:
		raise EOFException

	(v, ) = struct.unpack('!b', b)
	return v

def readU16(f):
	b = f.read(2)
	if len(b) < 2:
		raise EOFException

	(v, ) = struct.unpack('!H', b)
	return v

def readI16(f):
	b = f.read(2)
	if len(b) < 2:
		raise EOFException

	(v, ) = struct.unpack('!h', b)
	return v

def readU32(f):
	b = f.read(4)
	if len(b) < 4:
		raise EOFException

	(v, ) = struct.unpack('!I', b)
	return v

def readI32(f):
	b = f.read(4)
	if len(b) < 4:
		raise EOFException

	(v, ) = struct.unpack('!I', b)
	return v

def readU64(f):
	b = f.read(8)
	if len(b) < 8:
		raise EOFException

	(v, ) = struct.unpack('!Q', b)
	return v

def readI64(f):
	if len(b) < 8:
		raise EOFException

	b = f.read(8)
	(v, ) = struct.unpack('!q', b)
	return v

def readString(f):
	l = readU16(f)

	s = f.read(l)
	if len(s) < l:
		raise EOFException

	return s[:-1].decode('ascii')

decodeDict = {
	_VALUE_TYPE_U8: readU8,
	_VALUE_TYPE_I8: readI8,
	_VALUE_TYPE_U16: readU16,
	_VALUE_TYPE_I16: readI16,
	_VALUE_TYPE_U32: readU32,
	_VALUE_TYPE_I32: readI32,
	_VALUE_TYPE_U64: readU64,
	_VALUE_TYPE_I64: readI64,
	_VALUE_TYPE_STR: readString,
	_VALUE_TYPE_INVALID: None
}

_ENTRY_TYPE_RAWVALUE = 0
_ENTRY_TYPE_STRUCT   = 1
_ENTRY_TYPE_LIST     = 2

entryTypeToStr = {
	_ENTRY_TYPE_RAWVALUE: 'raw',
	_ENTRY_TYPE_STRUCT:   'struct',
	_ENTRY_TYPE_LIST:     'list'
}

class EntryDesc:
	def __init__(self, name, _type):
		self.name = name
		self.type = _type

class RawEntryDesc(EntryDesc):
	def __init__(self, name, rawType):
		super().__init__(name, _ENTRY_TYPE_RAWVALUE)
		self.rawType = rawType

	def decode(self, f):
		return decodeDict[self.rawType](f)

class StructDesc:
	def __init__(self, name, _type):
		self.name = name
		self.type = _type
		self.entries = []

	def addEntryDesc(self, desc):
		self.entries.append(desc)

	def decode(self, f):
		v = {}

		for entry in self.entries:
			v[entry.name] = entry.decode(f)

		return v

class Parser:
	def __init__(self,):
		self.f = None
		self.version = None
		self.compressed = None
		self.structDescList = {}

	def parseStructDesc(self):
		structType = readU8(self.f)
		structName = readString(self.f)

		desc = StructDesc(structName, structType)

		entryCount = readU32(self.f)
		for i in range(entryCount):
			entryName = readString(self.f)
			entryType = readU8(self.f)

			if entryType == _ENTRY_TYPE_RAWVALUE:
				rawType = readU8(self.f)

				entryDesc = RawEntryDesc(entryName, rawType)
				desc.addEntryDesc(entryDesc)
			elif entryType == _ENTRY_TYPE_STRUCT:
				raise Exception('Unsupported type struct')
			elif entryType == _ENTRY_TYPE_LIST:
				raise Exception('Unsupported type list')
			else:
				raise Exception('Unknown entry type %d' % entryType)

		return desc

	def parseHeader(self):
		self.version = readU8(self.f)
		self.compressed = readU8(self.f)

	def decodeRecord(self):
		recordType = readU8(self.f)

		try:
			structDesc = self.structDescList[recordType]
			return (structDesc.name, structDesc.decode(self.f))
		except KeyError as e:
			print('Unknown record type %d' % recordType)
			raise e

	def open(self, path):
		self.f = open(path, 'rb')

		self.parseHeader()

		structDescCount = readU8(self.f)
		for i in range(structDescCount):		
			structDesc = self.parseStructDesc()
			self.structDescList[structDesc.type] = structDesc

	def printHeader(self):
		print('File format version : %d' % self.version)
		print('Compressed : %d' % self.compressed)

		print('%d structs defined' % len(self.structDescList))
		for key, desc in self.structDescList.items():
			print('id: %d - name: \'%s\'' % (desc.type, desc.name))

			for entry in desc.entries:

				if entry.type == _ENTRY_TYPE_RAWVALUE:
					rawType = valueTypeToStr[entry.rawType]
					template = '\t{name:16}{type:8}'
					print(template.format(name=entry.name, type=rawType))
				else:
					strType = entryTypeToStr[entry.type]
					raise Exception('Unsupported entry type %s' % strType)

	def parse(self, recordReadCb):
		while True:
			try:
				(name, data) = self.decodeRecord()
				recordReadCb(name, data)
			except EOFException:
				break

if __name__ == '__main__':
	def recordRead(name, data):
		print(name, data)

	if len(sys.argv) < 2:
		print('Usage : %s <logfile>' % (sys.argv[1]))
		sys.exit(1)

	parser = Parser(recordRead)
	parser.parse(sys.argv[1])

