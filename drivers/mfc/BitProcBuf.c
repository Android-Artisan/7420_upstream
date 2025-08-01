/*
 * Project Name MFC DRIVER 
 * Copyright  2007 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "Mfc.h"
#include "MfcMemory.h"
#include "LogMsg.h"
#include "BitProcBuf.h"
#include "MfcConfig.h"
#include "Prism_S.h"

static volatile unsigned char     *vir_pBITPROC_BUF   = NULL;

static unsigned int                phyBITPROC_BUF  = 0;


BOOL MfcBitProcBufMemMapping()
{
	BOOL	ret = FALSE;

	// FIRWARE/WORKING/PARAMETER BUFFER  <-- virtual bitprocessor buffer address mapping
	vir_pBITPROC_BUF = (volatile unsigned char *)Phy2Vir_AddrMapping(S3C6400_BASEADDR_MFC_BITPROC_BUF, MFC_BITPROC_BUF_SIZE);
	if (vir_pBITPROC_BUF == NULL)
	{
		LOG_MSG(LOG_ERROR, "MfcBitProcBufMapping", "For BITPROC_BUF: VirtualAlloc failed!\r\n");
		return ret;
	}

	// Physical register address mapping
	phyBITPROC_BUF	= S3C6400_BASEADDR_MFC_BITPROC_BUF;


	ret = TRUE;

	return ret;
}

volatile unsigned char *GetBitProcBufVirAddr()
{
	volatile unsigned char	*pBitProcBuf;

	pBitProcBuf	= vir_pBITPROC_BUF;

	return pBitProcBuf;
}

unsigned char *GetParamBufVirAddr()
{
	unsigned char	*pParamBuf;

	pParamBuf = (unsigned char *)(vir_pBITPROC_BUF + MFC_CODE_BUF_SIZE + MFC_WORK_BUF_SIZE);

	return pParamBuf;
}

void MfcFirmwareIntoCodeBuf()
{
	unsigned int  i, j;
	unsigned int  data;

	unsigned int *uAddrFirmwareCode;

	uAddrFirmwareCode = (unsigned int *)vir_pBITPROC_BUF;

	/////////////////////////////////////////////////
	// Putting the Boot & Firmware code into SDRAM //
	/////////////////////////////////////////////////
	// Boot code(1KB) + Codec Firmware (79KB)
	for (i=j=0 ; i<sizeof(bit_code)/sizeof(bit_code[0]); i+=2, j++)
	{
		data = (bit_code[i] << 16) | bit_code[i+1];
	
		*(uAddrFirmwareCode + j) = data;
	}
}
