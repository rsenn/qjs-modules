#ifndef XPARSER_H
#define XPARSER_H

#include <quickjs.h>
#include "xml/xml.h"
#include "xml/xopts.h"
#include "location.h"

/**
 * \addtogroup xml
 * @{
 */

/* XmlParser: the pull parser of the JS class, an XMLParser with the Reader it reads, the Location
 * the JS side reads and the selfClosingTags option it owns. */
typedef struct xml_parser_obj {
  Reader reader;
  XMLParser xp; /* xp.reader is &reader, xp.loc is loc */
  Location* loc;
  char** self_closing_owned;
} XmlParser;

/* xparser_new: the parser of new XMLParser(input, options).
 *
 *   input    a string, buffer, read function, object with read() or fd, none leaves the parser without input
 *   options  a file name, or { filename, tolerant, flat, selfClosingTags }
 *
 * returns the parser (xparser_free() it), or NULL with an exception pending. */
XmlParser* xparser_new(JSContext*, int argc, JSValueConst argv[]);

/* xparser_free: releases p and what it owns. */
void xparser_free(XmlParser* p, JSRuntime* rt);

/* xparser_pump: runs the parser into a sink, in C. XMLParser.pump() is this.
 *
 *   JSValueConst  sink  an XmlEventSink object (handshake) or any object with the sink methods
 *
 * returns the XML_PULL_* status as a number (0 when the input ended, 1 when the reader has no data now),
 * or JS_EXCEPTION: the SyntaxError of a mismatched closing tag, what the sink throws, Error with a code
 * when a native sink refuses an event. */
JSValue xparser_pump(JSContext*, XmlParser* p, JSValueConst sink);

/* xparser_next: the next event as { type, name?, value?, loc }, XMLParser.next() without the iterator result.
 * *done is set when there is no event (the document is complete or the reader has no data now).
 * returns the event, undefined at the end, or JS_EXCEPTION for the SyntaxError of a mismatched closing tag. */
JSValue xparser_next(JSContext*, XmlParser* p, BOOL* done);

/** @} */
#endif /* XPARSER_H */
