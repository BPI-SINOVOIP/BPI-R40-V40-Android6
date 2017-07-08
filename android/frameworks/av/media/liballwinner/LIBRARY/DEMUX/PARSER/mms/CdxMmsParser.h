#ifndef CDX_MMS_PARSER_H
#define CDX_MMS_PARSER_H

#include <CdxTypes.h>
#include <CdxParser.h>
#include <CdxStream.h>
#include <CdxMemory.h>
#include <pthread.h>

struct CdxMmsParser
{
    CdxParserT 		parserinfo; 		//mms parser handle
    CdxStreamT*		stream;

    int             mErrno;
	int				mStatus;
    int 			exitFlag; 			// exit flag, for unblocking in prepare
    
    //pthread_t       thread;   			// thread wait for caching probe data
    CdxParserT* 		asfParser; 	//the parser which called by mms parser ( asf parser )
    //CdxStreamT*		streamNext;        // stream of next parser (  mmsh stream )
    //int 			findParser;			// if we find the parser in probeThread, we can use the ops of parserinfo_next
  										// it is like the iosatate in stream
	//CdxDataSourceT* datasource;

	int             videoCodecFormat;
	int             audioCodecFormat;
};


#endif
