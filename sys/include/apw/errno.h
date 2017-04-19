/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _POSIX_ERRORS
#define _POSIX_ERRORS

typedef enum PosixError
{
	PosixE2BIG	= 2,	/* Argument list too long. */
	PosixEACCES,		/* Permission denied. */
	PosixEADDRINUSE,	/* Address in use. */
	PosixEADDRNOTAVAIL,	/* Address not available. */
	PosixEAFNOSUPPORT,	/* Address family not supported. */
	PosixEAGAIN,		/* Resource unavailable, try again (may be the same value as [EWOULDBLOCK]). */
	PosixEALREADY,		/* Connection already in progress. */
	PosixEBADF,		/* Bad file descriptor. */
	PosixEBADMSG,		/* Bad message. */
	PosixEBUSY,		/* Device or resource busy. */
	PosixECANCELED,		/* Operation canceled. */
	PosixECHILD,		/* No child processes. */
	PosixECONNABORTED,	/* Connection aborted. */
	PosixECONNREFUSED,	/* Connection refused. */
	PosixECONNRESET,	/* Connection reset. */
	PosixEDEADLK,		/* Resource deadlock would occur. */
	PosixEDESTADDRREQ,	/* Destination address required. */
	PosixEDOM,		/* Mathematics argument out of domain of function. */
	PosixEDQUOT,		/* Reserved. */
	PosixEEXIST,		/* File exists. */
	PosixEFAULT,		/* Bad address. */
	PosixEFBIG,		/* File too large. */
	PosixEHOSTUNREACH,	/* Host is unreachable. */
	PosixEIDRM,		/* Identifier removed. */
	PosixEILSEQ,		/* Illegal byte sequence. */
	PosixEINPROGRESS,	/* Operation in progress. */
	PosixEINTR,		/* Interrupted function. */
	PosixEINVAL,		/* Invalid argument. */
	PosixEIO,		/* I/O error. */
	PosixEISCONN,		/* Socket is connected. */
	PosixEISDIR,		/* Idirectory. */
	PosixELOOP,		/* Too many levels of symbolic links. */
	PosixEMFILE,		/* File descriptor value too large. */
	PosixEMLINK,		/* Too many links. */
	PosixEMSGSIZE,		/* Message too large. */
	PosixEMULTIHOP,		/* Reserved. */
	PosixENAMETOOLONG,	/* Filename too long. */
	PosixENETDOWN,		/* Network is down. */
	PosixENETRESET,		/* Connection aborted by network. */
	PosixENETUNREACH,	/* Network unreachable. */
	PosixENFILE,		/* Too many files open in system. */
	PosixENOBUFS,		/* No buffer space available. */
	PosixENODATA,		/* No message is available on the STREAM head read queue. */
	PosixENODEV,		/* No such device. */
	PosixENOENT,		/* No such file or directory. */
	PosixENOEXEC,		/* Executable file format error. */
	PosixENOLCK,		/* No locks available. */
	PosixENOLINK,		/* Reserved. */
	PosixENOMEM,		/* Not enough space. */
	PosixENOMSG,		/* No message of the desired type. */
	PosixENOPROTOOPT,	/* Protocol not available. */
	PosixENOSPC,		/* No space left on device. */
	PosixENOSR,		/* No STREAM resources. */
	PosixENOSTR,		/* Not a STREAM. */
	PosixENOSYS,		/* Functionality not supported. */
	PosixENOTCONN,		/* The socket is not connected. */
	PosixENOTDIR,		/* Not a directory osymbolic link tdirectory. */
	PosixENOTEMPTY,		/* Directory not empty. */
	PosixENOTRECOVERABLE,	/* State not recoverable. */
	PosixENOTSOCK,		/* Not a socket. */
	PosixENOTSUP,		/* Not supported (may be the same value as [EOPNOTSUPP]). */
	PosixENOTTY,		/* Inappropriate I/O control operation. */
	PosixENXIO,		/* No such device or address. */
	PosixEOPNOTSUPP,	/* Operation not supported on socket (may be the same value as [ENOTSUP]). */
	PosixEOVERFLOW,		/* Value too large to be stored in data type. */
	PosixEOWNERDEAD,	/* Previous owner died. */
	PosixEPERM,		/* Operation not permitted. */
	PosixEPIPE,		/* Broken pipe. */
	PosixEPROTO,		/* Protocol error. */
	PosixEPROTONOSUPPORT,	/* Protocol not supported. */
	PosixEPROTOTYPE,	/* Protocol wrong type for socket. */
	PosixERANGE,		/* Result too large. */
	PosixEROFS,		/* Read-only file system. */
	PosixESPIPE,		/* Invalid seek. */
	PosixESRCH,		/* No such process. */
	PosixESTALE,		/* Reserved. */
	PosixETIME,		/* Stream ioctl() timeout. */
	PosixETIMEDOUT,		/* Connection timed out. */
	PosixETXTBSY,		/* Text file busy. */
	PosixEWOULDBLOCK,	/* Operation would block (may be the same value as [EAGAIN]). */
	PosixEXDEV,		/* Cross-device link. */
} PosixError;

#define ERRNO_FIRST	PosixE2BIG	/* Value of the first error constant */
#define ERRNO_LAST	PosixEXDEV	/* Value of the last error constant */

#endif

#ifndef _ERRNO_H
#define _ERRNO_H

extern int __errno();
#define errno __errno()

/* See The Open Group Base Specifications Issue 7
 * IEEE Std 1003.1-2008, 2016 Edition got from
 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html#tag_13_10
 *
 * NOTES:
 * None of these errno will ever be set by the Jehanne kernel.
 * It's possible however that a file server designed to mimic a Unix
 * behaviour use them.
 *
 * The value 1 is reserved since -1 is the default return value of
 * failing syscalls.
 *
 * The constant values are continuous, thus ERRNO_FIRST and ERRNO_LAST
 * can be used to create a map from errors to descriptions.
 */

#define	E2BIG		PosixE2BIG
#define	EACCES		PosixEACCES
#define	EADDRINUSE	PosixEADDRINUSE
#define	EADDRNOTAVAIL	PosixEADDRNOTAVAIL
#define	EAFNOSUPPORT	PosixEAFNOSUPPORT
#define	EAGAIN		PosixEAGAIN
#define	EALREADY	PosixEALREADY
#define	EBADF		PosixEBADF
#define	EBADMSG		PosixEBADMSG
#define	EBUSY		PosixEBUSY
#define	ECANCELED	PosixECANCELED
#define	ECHILD		PosixECHILD
#define	ECONNABORTED	PosixECONNABORTED
#define	ECONNREFUSED	PosixECONNREFUSED
#define	ECONNRESET	PosixECONNRESET
#define	EDEADLK		PosixEDEADLK
#define	EDESTADDRREQ	PosixEDESTADDRREQ
#define	EDOM		PosixEDOM
#define	EDQUOT		PosixEDQUOT
#define	EEXIST		PosixEEXIST
#define	EFAULT		PosixEFAULT
#define	EFBIG		PosixEFBIG
#define	EHOSTUNREACH	PosixEHOSTUNREACH
#define	EIDRM		PosixEIDRM
#define	EILSEQ		PosixEILSEQ
#define	EINPROGRESS	PosixEINPROGRESS
#define	EINTR		PosixEINTR
#define	EINVAL		PosixEINVAL
#define	EIO		PosixEIO
#define	EISCONN		PosixEISCONN
#define	EISDIR		PosixEISDIR
#define	ELOOP		PosixELOOP
#define	EMFILE		PosixEMFILE
#define	EMLINK		PosixEMLINK
#define	EMSGSIZE	PosixEMSGSIZE
#define	EMULTIHOP	PosixEMULTIHOP
#define	ENAMETOOLONG	PosixENAMETOOLONG
#define	ENETDOWN	PosixENETDOWN
#define	ENETRESET	PosixENETRESET
#define	ENETUNREACH	PosixENETUNREACH
#define	ENFILE		PosixENFILE
#define	ENOBUFS		PosixENOBUFS
#define	ENODATA		PosixENODATA
#define	ENODEV		PosixENODEV
#define	ENOENT		PosixENOENT
#define	ENOEXEC		PosixENOEXEC
#define	ENOLCK		PosixENOLCK
#define	ENOLINK		PosixENOLINK
#define	ENOMEM		PosixENOMEM
#define	ENOMSG		PosixENOMSG
#define	ENOPROTOOPT	PosixENOPROTOOPT
#define	ENOSPC		PosixENOSPC
#define	ENOSR		PosixENOSR
#define	ENOSTR		PosixENOSTR
#define	ENOSYS		PosixENOSYS
#define	ENOTCONN	PosixENOTCONN
#define	ENOTDIR		PosixENOTDIR
#define	ENOTEMPTY	PosixENOTEMPTY
#define	ENOTRECOVERABLE	PosixENOTRECOVERABLE
#define	ENOTSOCK	PosixENOTSOCK
#define	ENOTSUP		PosixENOTSUP
#define	ENOTTY		PosixENOTTY
#define	ENXIO		PosixENXIO
#define	EOPNOTSUPP	PosixEOPNOTSUPP
#define	EOVERFLOW	PosixEOVERFLOW
#define	EOWNERDEAD	PosixEOWNERDEAD
#define	EPERM		PosixEPERM
#define	EPIPE		PosixEPIPE
#define	EPROTO		PosixEPROTO
#define	EPROTONOSUPPORT	PosixEPROTONOSUPPORT
#define	EPROTOTYPE	PosixEPROTOTYPE
#define	ERANGE		PosixERANGE
#define	EROFS		PosixEROFS
#define	ESPIPE		PosixESPIPE
#define	ESRCH		PosixESRCH
#define	ESTALE		PosixESTALE
#define	ETIME		PosixETIME
#define	ETIMEDOUT	PosixETIMEDOUT
#define	ETXTBSY		PosixETXTBSY
#define	EWOULDBLOCK	PosixEWOULDBLOCK
#define	EXDEV		PosixEXDEV

#endif
