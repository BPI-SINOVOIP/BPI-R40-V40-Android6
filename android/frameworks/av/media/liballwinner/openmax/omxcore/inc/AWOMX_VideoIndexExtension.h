

#ifndef _AWOMX_VIDEO_INDEX_EXTENSION_H_
#define _AWOMX_VIDEO_INDEX_EXTENSION_H_

/*========================================================================

                     INCLUDE FILES FOR MODULE

========================================================================== */
#include <OMX_Core.h>

/*========================================================================

                      DEFINITIONS AND DECLARATIONS

========================================================================== */

#if defined( __cplusplus )
extern "C"
{
#endif /* end of macro __cplusplus */

#define OMX_VIDEO_PARAMS_EXTENDED_FLAGS_SCALING 1; 
#define OMX_VIDEO_PARAMS_EXTENDED_FLAGS_CROPPING 2;

typedef struct _OMX_VIDEO_PARAMS_EXTENDED {
     OMX_U32         ui32Flags;
     OMX_BOOL        bEnableScaling; // Resolution Scaling
     OMX_U16         ui16ScaledWidth;
     OMX_U16         ui16ScaledHeight;
     OMX_BOOL        bEnableCropping; // Resolution Cropping
     OMX_U16         ui16CropLeft;//Number of columns to be cropped from lefthand-side edge
     OMX_U16         ui16CropRight;//Number of columns to be cropped from righthand-side edge
     OMX_U16         ui16CropTop;//Number of rows to be cropped from the top edge
     OMX_U16         ui16CropBottom;// Number of rows to be cropped from the bottom edge
} OMX_VIDEO_PARAMS_EXTENDED;


typedef struct _OMX_VIDEO_SUPER_FRAME {
     OMX_BOOL        bEnable;
     OMX_U32         uReserved0;
	 OMX_U32         uReserved1;
	 OMX_U32         uReserved3;
} OMX_VIDEO_PARAMS_SUPER_FRAME;


/**
 * Enumeration used to define Allwinner's vendor extensions for
 * video. The video extensions occupy a range of
 * 0x7F100000-0x7F1FFFFF, inclusive.
 */

typedef enum AW_VIDEO_EXTENSIONS_INDEXTYPE
{
	AWOMX_IndexParamVideoEnableAndroidNativeBuffers    = 0x7F100000,	/* OMX.google.android.index.enableAndroidNativeBuffers */
	AWOMX_IndexParamVideoGetAndroidNativeBufferUsage   = 0x7F100001,    /* OMX.google.android.index.getAndroidNativeBufferUsage */
	AWOMX_IndexParamVideoUseAndroidNativeBuffer2       = 0x7F100002,    /* OMX.google.android.index.useAndroidNativeBuffer2 */
    AWOMX_IndexParamVideoUseStoreMetaDataInBuffer      = 0x7F100003,
    AWOMX_IndexParamVideoUsePrepareForAdaptivePlayback = 0x7F100004,
    AWOMX_IndexParamVideoUnused                        = 0x7F2FFFFF
} AW_VIDEO_EXTENSIONS_INDEXTYPE;

#define VIDDEC_CUSTOMPARAM_ENABLE_ANDROID_NATIVE_BUFFER "OMX.google.android.index.enableAndroidNativeBuffers"
#define VIDDEC_CUSTOMPARAM_GET_ANDROID_NATIVE_BUFFER_USAGE "OMX.google.android.index.getAndroidNativeBufferUsage"
#define VIDDEC_CUSTOMPARAM_USE_ANDROID_NATIVE_BUFFER2 "OMX.google.android.index.useAndroidNativeBuffer2"
#define VIDDEC_CUSTOMPARAM_STORE_META_DATA_IN_BUFFER  "OMX.google.android.index.storeMetaDataInBuffers"
#define VIDDEC_CUSTOMPARAM_PREPARE_FOR_ADAPTIVE_PLAYBACK  "OMX.google.android.index.prepareForAdaptivePlayback"
#if defined( __cplusplus )
}
#endif /* end of macro __cplusplus */

#endif /* end of macro _AWOMX_VIDEO_INDEX_EXTENSION_H_ */
