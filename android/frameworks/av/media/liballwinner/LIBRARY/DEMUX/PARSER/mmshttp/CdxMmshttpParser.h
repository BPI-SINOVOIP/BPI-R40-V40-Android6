#ifndef CDX_MMS_PARSER_H
#define CDX_MMS_PARSER_H

#include <CdxTypes.h>
#include <CdxParser.h>
#include <CdxStream.h>
#include <CdxMemory.h>
#include <pthread.h>

struct CdxMmshttpParser
{
    CdxParserT 		parserinfo; 		//mms parser handle
    CdxStreamT*		stream;
	cdx_int64		size;       		//the Refrence file size
	char*			buffer;				// the Refrence file buffer

	int				status;
	int             mErrno;
    int 			exitFlag; 			// exit flag, for unblocking in prepare
    
    //pthread_t       thread;   			// thread wait for caching probe data
    CdxParserT* 		parserinfoNext; 	//the parser which called by mms parser ( asf parser )
    CdxStreamT*		streamNext;        // stream of next parser (  mmsh stream )
    //int 			findParser;			// if we find the parser in probeThread, we can use the ops of parserinfo_next
  										// it is like the iosatate in stream
	CdxDataSourceT* datasource;

	pthread_mutex_t   mutex;
};


#endif
