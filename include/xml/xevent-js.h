#ifndef XEVENT_JS_H
#define XEVENT_JS_H

#include <quickjs.h>
#include "xml/xevent.h"
#include "xml/xml.h"
#include "xml/xopts.h"
#include "location.h"

/**
 * \addtogroup xevent
 * @{
 */

/* js_xmlevent_new: an XmlEvent as a plain object, what XMLParser.next() returns.
 *
 *   { type: XmlEventType, name?, value?, loc: { line, column } }
 *
 *   element_start, element_end   name
 *   attribute                    name, value (left out for a bare attribute)
 *   text, comment, declaration,
 *   instruction, cdata           value
 *
 * loc is the 1-based line and column of the tag the event belongs to. returns the object
 * (refcount: a new reference), or JS_EXCEPTION. */
JSValue js_xmlevent_new(JSContext*, const XmlEvent* ev);

/* js_xmlwrite_throw: the exception for a failed xwrite event (-r).
 *
 *   XWRITE_E_MISMATCH, _UNMATCHED, _NOT_IN_TAG   TypeError with code mismatched-end-tag,
 *                                                unmatched-end-tag, attribute-outside-tag
 *   XWRITE_E_WRITE                               Error with code write-failed
 *   XWRITE_E_NOMEM                               out of memory
 *
 * returns JS_EXCEPTION. */
JSValue js_xmlwrite_throw(JSContext*, ssize_t r);

/* js_xmlevent_error: the SyntaxError for the XE_ERROR that xp just returned, at the start of the
 * offending tag, base supplies the input name.
 *
 * returns the error object (refcount: a new reference), or JS_EXCEPTION. */
JSValue js_xmlevent_error(JSContext*, const XMLParser* xp, const Location* base);

/* xml_read_value: the element tree or flat list of buf, built by an XmlBuilder.
 *
 *   tree  [{tagName, attributes, children}], children only on elements that stay open
 *   flat  [{tagName, attributes}, 'text', {tagName: '/name'}, ...]
 *
 * returns the array (or [array, WeakMap] with opts.location), or JS_EXCEPTION with a SyntaxError
 * pending (mismatched closing tag). */
JSValue xml_read_value(JSContext*, const uint8_t* buf, size_t len, const char* input_name, XmlOptions opts);

/* xml_tree_list: the list of nodes of a tree or flat list, a single node is wrapped.
 *
 * *flat is set when the last node has no children array, else it is a tree.
 * returns the array (refcount: a new reference). */
JSValue xml_tree_list(JSContext*, JSValueConst tree, BOOL* flat);

/* xml_write_value: the XML text of a tree or flat list, indented by 2 spaces.
 *
 *   JSValueConst  obj        an element, an array of them, or a flat list
 *   int32_t       max_depth  elements deeper than this are cut, INT32_MAX for none
 *
 * a single element is wrapped, a list whose last node has no children array is flat.
 * returns the string, or JS_EXCEPTION with an exception pending (bad tree, see js_xmlwrite_throw()). */
JSValue xml_write_value(JSContext*, JSValueConst obj, int32_t max_depth);

/** @} */
#endif
