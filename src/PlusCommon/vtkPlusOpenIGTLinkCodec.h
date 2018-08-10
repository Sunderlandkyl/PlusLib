/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusOpenIGTLinkCodec_h
#define __vtkPlusOpenIGTLinkCodec_h

// VTK includes
#include <vtkImageMapToColors.h>
#include <vtkObject.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>

#include <igtlCodecCommonClasses.h>
#include <vtkGenericCodec.h>

class vtkImageData;

#include "vtkPlusCommonExport.h"
class vtkPlusCommonExport vtkPlusOpenIGTLinkCodec : public vtkGenericCodec
{
public:
  vtkTypeMacro(vtkPlusOpenIGTLinkCodec, vtkGenericCodec);
  static vtkPlusOpenIGTLinkCodec* New();
  virtual void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;
protected:
  vtkPlusOpenIGTLinkCodec(const vtkPlusOpenIGTLinkCodec&); //purposely not implemented
  void operator=(const vtkPlusOpenIGTLinkCodec&); //purposely not implemented
  vtkPlusOpenIGTLinkCodec();
  virtual ~vtkPlusOpenIGTLinkCodec();

public:
  virtual vtkSmartPointer<vtkGenericCodec> CreateCodecInstance();

  virtual vtkSmartPointer<vtkUnsignedCharArray> EncodeFrame(vtkImageData* inputFrame, unsigned long &size, int &frameType);
  virtual bool DecodeFrame(unsigned char*, unsigned long size, vtkImageData* outputFrame);

public:
  igtl::GenericEncoder::Pointer Encoder;
  igtl::GenericDecoder::Pointer Decoder;

protected:
  bool EncoderInitialized;
  bool DecoderInitialized;

  vtkSmartPointer<vtkImageMapToColors> GreyScaleToRGBFilter;
};

#endif
