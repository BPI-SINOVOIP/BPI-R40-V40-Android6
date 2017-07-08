#ifndef DEINTERLACE__H
#define DEINTERLACE__H

#define	DI_IOC_MAGIC		'D'
#define	DI_IOCSTART		_IOWR(DI_IOC_MAGIC, 0, DiRectSizeT)



typedef enum
{
	DI_FORMAT_NV12      =0x00,
	DI_FORMAT_NV21      =0x01,
	DI_FORMAT_MB32_12   =0x02, //UV mapping like NV12
	DI_FORMAT_MB32_21   =0x03, //UV mapping like NV21

}DiPixelformatE;

typedef struct
{
	unsigned int nWidth;
	unsigned int nHeight;
}DiRectSizeT;

typedef struct
{
	unsigned int      addr[2];  // the address of frame buffer
	DiRectSizeT       mRectSize;
	DiPixelformatE    eFormat;
}DiFbT;

typedef struct
{
	DiFbT         mInputFb;	       //current frame fb
	DiFbT         mPreFb;          //previous frame fb
	DiRectSizeT   mSourceRegion;   //current frame and previous frame process region
	DiFbT         mOutputFb;       //output frame fb
	DiRectSizeT   mOutRegion;	   //output frame region
	unsigned int  nField;          //process field <0-top field ; 1-bottom field>
	unsigned int  bTopFieldFirst;  //video infomation <0-is not top_field_first; 1-is top_field_first>
}DiParaT;


#endif
