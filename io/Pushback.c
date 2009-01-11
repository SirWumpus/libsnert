/*
 * Data.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/io/Pushback.h>

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

static void
PushbackClose(Pushback self)
{
	if (self->_sourceFp != NULL) {
		(void) fclose(self->_sourceFp);
		self->_sourceFp = NULL;
	} else if (0 <= self->_sourceFd) {
		(void) close(self->_sourceFd);
		self->_sourceFd = -1;
	}
}

void
PushbackDestroy(/*@only@*//*@null@*/ void *selfless)
{
	Pushback self = selfless;
	
	if (self == NULL)
		return;
		
	PushbackClose(self);
	
	free(self->_holdBase);
	free(self);	
}

long
PushbackAvailable(Pushback self)
{
	return self->_holdLength - self->_holdIndex;
}

/***********************************************************************
 *** Byte Buffer instance methods
 ***********************************************************************/

long
PushbackBytesSkip(Pushback self, long n)
{
	long available = self->available(self);
	
	if (n < available) {
		self->_holdIndex += n;
		return n;
	}
	
	/* Skip remainder of hold buffer. */
	self->_holdIndex += available;
	
	if (n - available < self->_sourceLength - self->_sourceIndex) {
		self->_sourceIndex += n - available;
		return n;
	}
	
	/* Skip remainder of source buffer. */
	self->_sourceIndex = self->_sourceLength;
		
	return n - available - (self->_sourceLength - self->_sourceIndex);
}

int
PushbackBytesGet(Pushback self)
{
	if (self->_holdBase != NULL && self->_holdIndex < self->_holdLength)
		return self->_holdBase[self->_holdIndex++];

	if (self->_sourceIndex < self->_sourceLength)		
		return self->_sourceBase[self->_sourceIndex++];	
		
	return -1;
}

int
PushbackBytesUnget(Pushback self, int byte)
{
}

long
PushbackBytesRead(Pushback self, unsigned char *buffer, long length)
{
}

int
PushbackBytesUnread(Pushback self, unsigned char *bytes, long length)
{
}

void 
PushbackSetSourceBytes(Pushback self, unsigned char *bytes, long length)
{
	PushbackClose(self);

	self->_sourceBase = bytes;
	self->_sourceLength = length;
	self->_sourceIndex = 0;
	
	self->skip = PushbackBytesSkip;
	self->get = PushbackBytesGet;
	self->unget = PushbackBytesUnget;
	self->read = PushbackBytesRead;
	self->unread = PushbackBytesUnread;
}

/***********************************************************************
 *** FILE * instance methods
 ***********************************************************************/

long
PushbackFileSkip(Pushback self, long n)
{
}

int
PushbackFileGet(Pushback self)
{
}

int
PushbackFileUnget(Pushback self, int byte)
{
}

long
PushbackFileRead(Pushback self, unsigned char *buffer, long length)
{
}

int
PushbackFileUnread(Pushback self, unsigned char *File, long length)
{
}

void 
PushbackSetSourceFile(Pushback self, FILE *fp)
{
	PushbackClose(self);

	self->_sourceFp = fp;

	self->skip = PushbackFileSkip;
	self->get = PushbackFileGet;
	self->unget = PushbackFileUnget;
	self->read = PushbackFileRead;
	self->unread = PushbackFileUnread;
}

/***********************************************************************
 *** File Descriptor instance methods
 ***********************************************************************/

long
PushbackFdSkip(Pushback self, long n)
{
}

int
PushbackFdGet(Pushback self)
{
}

int
PushbackFdUnget(Pushback self, int byte)
{
}

long
PushbackFdRead(Pushback self, unsigned char *buffer, long length)
{
}

int
PushbackFdUnread(Pushback self, unsigned char *Fd, long length)
{
}

void 
PushbackSetSourceFd(Pushback self, int fd)
{
	PushbackClose(self);
		
	self->_sourceFd = fd;

	self->skip = PushbackFdSkip;
	self->get = PushbackFdGet;
	self->unget = PushbackFdUnget;
	self->read = PushbackFdRead;
	self->unread = PushbackFdUnread;
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

static void 
PushbackInit(Pushback self)
{
	static struct pushback model;

	if (model.objectName == NULL) {
		ObjectInit(&model);

		/* Overrides */
		model.objectSize = sizeof (struct pushback);			
		model.objectName = "Pushback";
		model.destroy = PushbackDestroy;
		model.clone = DataClone;
		model.equals = DataEquals;
		model.compare = DataCompare;
		model.hashcode = DataHashcode;

		/* Methods */
		model.available = PushbackAvailable;
		model.setSourceBytes = PushbackSetSourceBytes;
		model.setSourceFile = PushbackSetSourceFile;
		model.setSourceFd = PushbackSetSourceFd;
		model.objectMethodCount += 4;					

		model._sourceFd = -1;
	}

	*self = model;
}

static Pushback
PushbackCreate(void)
{
	Pushback self;
	
	if ((self = calloc(1, sizeof (*self))) == NULL)
		goto error0;
				
	PushbackInit(self);
error0:
	return self;
}

Pushback 
PushbackCreateFromBytes(unsigned char *bytes, long length)
{
	Pushback self;
	
	if (bytes == NULL)
		return NULL;
	
	if ((self = PushbackCreate()) == NULL)
		return NULL;

	PushbackSetSourceBytes(self, bytes, length);

	return self;
}

Pushback 
PushbackCreateFromFile(FILE *fp)
{
	Pushback self;
	
	if (fp == NULL)
		return NULL;
	
	if ((self = PushbackCreate()) == NULL)
		return NULL;

	PushbackSetSourceFile(self, fp);
	
	return self;
}

Pushback 
PushbackCreateFromFd(int fd)
{
	Pushback self;
	
	if (fd < 0)
		return NULL;
	
	if ((self = PushbackCreate()) == NULL)
		return NULL;

	PushbackSetSourceFd(self, fd);
	
	return self;
}



