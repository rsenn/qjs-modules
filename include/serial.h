/*
 * Copyright 2012 - 2014 Thomas Buck
 * <xythobuz@xythobuz.de>
 */

#ifndef _SERIAL_H_
#define _SERIAL_H_

#define BDEFAULT -1

/*
 * Configuration
 */

/*!
 * \brief Enable XON/XOFF flow control.
 *
 * If you uncomment this definition, the
 * serial port code will stop sending
 * when a XOFF was received, and start
 * again upon receiving XON. However,
 * you need to use the blocking
 * read/write functions!
 */
//#define XONXOFF
#define XON 0x11  //!< XON flow control character
#define XOFF 0x13 //!< XOFF flow control character

/*!
 * \brief Search term to filter the list
 * of available ports.
 *
 * If you define SEARCH, instead of
 * simply returning a list of files in
 * /dev/, serial_ports() will only
 * return items that contain the string
 * defined to SEARCH.
 */
#define SEARCH "tty"

/*!
 * \brief Only list real serial ports.
 *
 * If you uncomment this definition,
 * serial_ports() will try to open
 * every port, only returning the name
 * if it is a real serial port. This
 * could cause a big delay, if eg. your
 * system tries to open non-existing
 * bluetooth devices, waiting for their
 * timeout. Also, if your console tty is
 * probed, it might change it's
 * settings.
 */
//#define TRY_TO_OPEN_PORTS

/*!
 * \brief The timeout in seconds for raw
 * reading/writing.
 *
 * If this amount of time passes without
 * being able to write/read a character,
 * the raw I/O functions will return 0.
 */
#define TIMEOUT 2

/*
 * Setup
 */

/*!
 * \brief open a serial port
 * \param port name of port
 * \param baud baudrate
 * \returns file handle or -1 on error
 */
int serial_open(const char* port, int baud);

/*!
 * \brief close an open serial port
 * \param fd file handle of port to
 * close
 */
void serial_close(int fd);

/*!
 * \brief query available serial ports
 * \returns string array with serial
 * port names. Last element is NULL.
 * Don't forget to free() after using
 * it!
 */
char** serial_ports(void);

/*
 * Raw, non-blocking I/O
 */

/*!
 * \brief read from an open serial port
 * \param fd file handle of port to read
 * from \param data buffer big enough to
 * fit all read data \param length
 * maximum number of bytes to read
 * \returns number of bytes really read
 */
unsigned int serial_read_raw(int fd, char* data, unsigned int length);

/*!
 * \brief write to an open serial port
 * \param fd file handle of port to
 * write to \param data buffer
 * containing data to write \param
 * length number of bytes to write
 * \returns number of bytes really
 * written
 */
unsigned int serial_write_raw(int fd, const char* data, unsigned int length);

/*!
 * \brief wait until data is sent
 * \param fd file handle of port to wait
 * for
 */
void serial_wait_until_sent(int fd);

/*
 * Blocking I/O
 */

/*!
 * \brief check if a character has
 * arrived and can be read \param fd
 * file handle of port to check \returns
 * 1 if a character is available, 0 if
 * not
 */
int serial_has_char(int fd);

/*!
 * \brief read a single character
 * \param fd file handle of port to read
 * from \param c where read character
 * will be stored
 */
void serial_read_char(int fd, char* c);

/*!
 * \brief write a single character
 * \param fd file handle to write to
 * \param c character to write
 */
void serial_write_char(int fd, char c);

/*!
 * \brief write a string
 * \param fd file handle to write to
 * \param s C string to be written
 */
void serial_write_string(int fd, const char* s);

/*!
 * \brief      get (input) baud rate0
 * \param fd file handle to write to
 * \return     baud rate (bps)
 */
int serial_baud_rate(int fd);

#endif
